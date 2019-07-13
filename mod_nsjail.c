/*
   mod_nsjail 0.10.0
   Copyright (C) 2019 Hax LLC

   Author: Andrew Pietila

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

   Based on:
   - mod_suid - http://bluecoara.net/servers/apache/mod_suid2_en.phtml
     Copyright 2004 by Hideo NAKAMITSU. All rights reserved
   - mod_ruid - http://websupport.sk/~stanojr/projects/mod_ruid/
     Copyright 2004 by Pavel Stano. All rights reserved
   - mod_ruid2 - https://github.com/mind04/mod-ruid2/
     Copyright 2009-2013 by Monshouwer Internet Diensten. All rights reserved.

   Instalation:
   - /usr/apache/bin/apxs -a -i -l cap -c mod_nsjail.c

   Issues:
   - https://github.com/mind04/mod-ruid2/issues
*/

/* define CORE_PRIVATE for apache < 2.4 */
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER < 4
#define CORE_PRIVATE
#endif

#include <unixd.h>
#include <http_core.h>
#include <http_log.h>
#include <http_protocol.h>
#include <http_request.h>
#include <mpm_common.h>

#include <unistd.h>
#include <sys/prctl.h>
#include <sys/capability.h>
#include "nsjail_config.h"

#define MODULE_NAME		"mod_nsjail"
#define MODULE_VERSION		"0.10.0"

#define NSJAIL_CAP_MODE_DROP	0
#define NSJAIL_CAP_MODE_KEEP	1

#define UNUSED(x) (void)(x)

/* added for apache 2.0 and 2.2 compatibility */
#if !AP_MODULE_MAGIC_AT_LEAST(20081201,0)
#define ap_unixd_config unixd_config
#endif

static int cap_mode		= NSJAIL_CAP_MODE_KEEP;

static int coredump, root_handle;
static const char *old_root;

static gid_t startup_groups[NSJAIL_MAXGROUPS];
static int startup_groupsnr;


/* configure options in httpd.conf */
static const command_rec nsjail_cmds[] = {
	AP_INIT_FLAG("NsJailEnableSetUidGid", set_enablesetuidgid, NULL, RSRC_CONF | ACCESS_CONF, "Define whether to enable setting UID/GID in location."),
	AP_INIT_TAKE2 ("RUidGid", set_uidgid, NULL, RSRC_CONF | ACCESS_CONF, "Minimal uid or gid file/dir, else set[ug]id to default (User,Group)"),
	AP_INIT_ITERATE ("RGroups", set_groups, NULL, RSRC_CONF | ACCESS_CONF, "Set additional groups"),
	AP_INIT_TAKE2 ("RDefaultUidGid", set_defuidgid, NULL, RSRC_CONF, "If uid or gid is < than RMinUidGid set[ug]id to this uid gid"),
	AP_INIT_TAKE2 ("RMinUidGid", set_minuidgid, NULL, RSRC_CONF, "Minimal uid or gid file/dir, else set[ug]id to default (RDefaultUidGid)"),
	AP_INIT_TAKE2 ("RDocumentChRoot", set_documentchroot, NULL, RSRC_CONF, "Set chroot directory and the document root inside"),
	{NULL, {NULL}, NULL, 0, NO_ARGS, NULL}
};


/* run in post config hook ( we are parent process and we are uid 0) */
static int nsjail_init (apr_pool_t *p, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *s)
{
	UNUSED(p);
	UNUSED(plog);
	UNUSED(ptemp);

	void *data;
	const char *userdata_key = "nsjail_init";

	/* keep capabilities after setuid */
	prctl(PR_SET_KEEPCAPS,1);

	/* initialize_module() will be called twice, and if it's a DSO
	 * then all static data from the first call will be lost. Only
	 * set up our static data on the second call. */
	apr_pool_userdata_get(&data, userdata_key, s->process->pool);
	if (!data) {
		apr_pool_userdata_set((const void *)1, userdata_key, apr_pool_cleanup_null, s->process->pool);
	} else {
		ap_log_error(APLOG_MARK, APLOG_NOTICE, 0, NULL, MODULE_NAME "/" MODULE_VERSION " enabled");

		/* MaxRequestsPerChild MUST be 1 to enable mod_nsjail's functionality. */
		if (ap_max_requests_per_child == 1) {
			ap_log_error(APLOG_MARK, APLOG_NOTICE, 0, NULL, MODULE_NAME " enabled.");
			cap_mode = NSJAIL_CAP_MODE_DROP;
		}
	}

	return OK;
}


/* child cleanup function */
static apr_status_t nsjail_child_exit(void *data)
{
	int fd = (int)((long)data);

	if (close(fd) < 0) {
		ap_log_error (APLOG_MARK, APLOG_ERR, 0, NULL, "%s CRITICAL ERROR closing root file descriptor (%d) failed", MODULE_NAME, fd);
		return APR_EGENERAL;
	}

	return APR_SUCCESS;
}


/* run after child init we are uid User and gid Group */
static void nsjail_child_init (apr_pool_t *p, server_rec *s)
{
	/* MaxRequestsPerChild MUST be 1 to enable mod_nsjail's functionality. */
	if ( cap_mode == NSJAIL_CAP_MODE_KEEP ) {
		return;
	}
	
	UNUSED(s);

	int ncap;
	cap_t cap;
	cap_value_t capval[4];

	/* detect default supplementary group IDs */
	if ((startup_groupsnr = getgroups(NSJAIL_MAXGROUPS, startup_groups)) == -1) {
		startup_groupsnr = 0;
		ap_log_error (APLOG_MARK, APLOG_ERR, 0, NULL, "%s ERROR getgroups() failed on child init, ignoring supplementary group IDs", MODULE_NAME);
	}

	/* setup chroot jailbreak */
	root_handle = (is_chroot_used() == NSJAIL_CHROOT_USED ? NONE : UNSET);

	/* init cap with all zeros */
	cap = cap_init();

	capval[0] = CAP_SETUID;
	capval[1] = CAP_SETGID;
	ncap = 2;
	if (root_handle != UNSET) {
		capval[ncap++] = CAP_SYS_CHROOT;
	}
	cap_set_flag(cap, CAP_PERMITTED, ncap, capval, CAP_SET);
	if (cap_set_proc(cap) != 0) {
		ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL, "%s CRITICAL ERROR %s:cap_set_proc failed", MODULE_NAME, __func__);
	}
	cap_free(cap);

	/* check if process is dumpable */
	coredump = prctl(PR_GET_DUMPABLE);
}


static int nsjail_set_perm (request_rec *r, const char *from_func)
{
	/* MaxRequestsPerChild MUST be 1 to enable mod_nsjail's functionality. */
	if ( cap_mode == NSJAIL_CAP_MODE_KEEP ) {
		return DECLINED;
	}

	nsjail_config_t *conf = ap_get_module_config(r->server->module_config, &nsjail_module);
	nsjail_dir_config_t *dconf = ap_get_module_config(r->per_dir_config, &nsjail_module);

	int retval = DECLINED;
	gid_t gid;
	uid_t uid;
	gid_t groups[NSJAIL_MAXGROUPS];
	int groupsnr;

	cap_t cap;
	cap_value_t capval[3];

	/* TODO: De-magic-number this. NSJAIL_SETUIDGID_DISABLED/NSJAIL_SETUIDGID_ENABLED. */
	if ( dconf->enable_setuidgid == 1 ) {

		/* Ensure we have the capabilities CAP_SETUID and CAP_SETGID, and that they are effective. */
		cap=cap_get_proc();
		capval[0]=CAP_SETUID;
		capval[1]=CAP_SETGID;
		cap_set_flag(cap,CAP_EFFECTIVE,2,capval,CAP_SET);
		if (cap_set_proc(cap)!=0) {
			ap_log_error (APLOG_MARK, APLOG_ERR, 0, NULL, "%s CRITICAL ERROR %s>%s:cap_set_proc failed before setuid", MODULE_NAME, from_func, __func__);
		}
		cap_free(cap);

		gid=(dconf->nsjail_gid == UNSET) ? ap_unixd_config.group_id : dconf->nsjail_gid;
		uid=(dconf->nsjail_uid == UNSET) ? ap_unixd_config.user_id : dconf->nsjail_uid;
		

		/* if uid of filename is less than conf->min_uid then set to conf->default_uid */
		if (uid < conf->min_uid) {
			uid=conf->default_uid;
		}
		if (gid < conf->min_gid) {
			gid=conf->default_gid;
		}

		/* set supplementary groups */
		if ((dconf->groupsnr == UNSET) && (startup_groupsnr > 0)) {
			memcpy(groups, startup_groups, sizeof(groups));
			groupsnr = startup_groupsnr;
		} else if (dconf->groupsnr > 0) {
			for (groupsnr = 0; groupsnr < dconf->groupsnr; groupsnr++) {
				if (dconf->groups[groupsnr] >= conf->min_gid) {
					groups[groupsnr] = dconf->groups[groupsnr];
				} else {
					groups[groupsnr] = conf->default_gid;
				}
			}
		} else {
			groupsnr = 0;
		}
		setgroups(groupsnr, groups);

		/* final set[ug]id */
		if (setgid(gid) != 0)
		{
			ap_log_error (APLOG_MARK, APLOG_ERR, 0, NULL, "%s %s %s %s>%s:setgid(%d) failed. getgid=%d getuid=%d", MODULE_NAME, ap_get_server_name(r), r->the_request, from_func, __func__, dconf->nsjail_gid, getgid(), getuid());
			retval = HTTP_FORBIDDEN;
		} else {
			if (setuid(uid) != 0)
			{
				ap_log_error (APLOG_MARK, APLOG_ERR, 0, NULL, "%s %s %s %s>%s:setuid(%d) failed. getuid=%d", MODULE_NAME, ap_get_server_name(r), r->the_request, from_func, __func__, dconf->nsjail_uid, getuid());
				retval = HTTP_FORBIDDEN;
			}
		}

		/* set httpd process dumpable after setuid */
		if (coredump) {
			prctl(PR_SET_DUMPABLE,1);
		}

		/* clear capabilties from effective set */
		cap=cap_get_proc();
		capval[0]=CAP_SETUID;
		capval[1]=CAP_SETGID;
		capval[2]=CAP_DAC_READ_SEARCH;
		cap_set_flag(cap,CAP_EFFECTIVE,3,capval,CAP_CLEAR);

		if (cap_set_proc(cap)!=0) {
			ap_log_error (APLOG_MARK, APLOG_ERR, 0, NULL, "%s CRITICAL ERROR %s>%s:cap_set_proc failed after setuid", MODULE_NAME, from_func, __func__);
			retval = HTTP_FORBIDDEN;
		}
		cap_free(cap);
	}

	return retval;
}


/* run in post_read_request hook */
static int nsjail_setup (request_rec *r)
{
	/* We decline when we are in a subrequest. The nsjail_setup function was
	 * already executed in the main request. */
	if (!ap_is_initial_req(r)) {
		return DECLINED;
	}

	/* MaxRequestsPerChild MUST be 1 to enable mod_nsjail's functionality. */
	if ( cap_mode == NSJAIL_CAP_MODE_KEEP ) {
		return DECLINED;
	}

	nsjail_config_t *conf = ap_get_module_config (r->server->module_config,  &nsjail_module);
	nsjail_dir_config_t *dconf = ap_get_module_config(r->per_dir_config, &nsjail_module);
	core_server_config *core = (core_server_config *) ap_get_module_config(r->server->module_config, &core_module);

	int ncap=0;
	cap_t cap;
	cap_value_t capval[2];

	if (root_handle != UNSET) capval[ncap++] = CAP_SYS_CHROOT;
	if (ncap) {
		cap=cap_get_proc();
		cap_set_flag(cap, CAP_EFFECTIVE, ncap, capval, CAP_SET);
		if (cap_set_proc(cap)!=0) {
			ap_log_error (APLOG_MARK, APLOG_ERR, 0, NULL, "%s CRITICAL ERROR %s:cap_set_proc failed", MODULE_NAME, __func__);
		}
		cap_free(cap);
	}

	/* do chroot trick only if chrootdir is defined */
	if (conf->chroot_dir)
	{
		old_root = ap_document_root(r);
		core->ap_document_root = conf->document_root;
		if (chdir(conf->chroot_dir) != 0)
		{
			ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL,"%s %s %s chdir to %s failed", MODULE_NAME, ap_get_server_name (r), r->the_request, conf->chroot_dir);
			return HTTP_FORBIDDEN;
		}
		if (chroot(conf->chroot_dir) != 0)
		{
			ap_log_error (APLOG_MARK, APLOG_ERR, 0, NULL,"%s %s %s chroot to %s failed", MODULE_NAME, ap_get_server_name (r), r->the_request, conf->chroot_dir);
			return HTTP_FORBIDDEN;
		}

		cap = cap_get_proc();
		capval[0] = CAP_SYS_CHROOT;
		cap_set_flag(cap, CAP_EFFECTIVE, 1, capval, CAP_CLEAR);
		if (cap_set_proc(cap) != 0 )
		{
			ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL, "%s CRITICAL ERROR %s:cap_set_proc failed", MODULE_NAME, __func__);
		}
		cap_free(cap);
	}

	return nsjail_set_perm(r, __func__);
}


/* run in map_to_storage hook */
static int nsjail_uiiii (request_rec *r)
{
	if (!ap_is_initial_req(r)) {
		return DECLINED;
	}

	int retval = nsjail_set_perm(r, __func__);

	int ncap;
	cap_t cap;
	cap_value_t capval[4];

	/* clear capabilities from permitted set (permanent) */
	if (cap_mode == NSJAIL_CAP_MODE_DROP) {
		cap=cap_get_proc();
		capval[0]=CAP_SETUID;
		capval[1]=CAP_SETGID;
		capval[2]=CAP_DAC_READ_SEARCH;
		ncap = 2;
		if (root_handle == UNSET) capval[ncap++] = CAP_SYS_CHROOT;
		cap_set_flag(cap,CAP_PERMITTED,ncap,capval,CAP_CLEAR);

		if (cap_set_proc(cap)!=0) {
			ap_log_error (APLOG_MARK, APLOG_ERR, 0, NULL, "%s CRITICAL ERROR %s:cap_set_proc failed after setuid", MODULE_NAME, __func__);
			retval = HTTP_FORBIDDEN;
		}
		cap_free(cap);
	}

	return retval;
}


static void register_hooks (apr_pool_t *p)
{
	UNUSED(p);

	ap_hook_post_config (nsjail_init, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_child_init (nsjail_child_init, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_post_read_request(nsjail_setup, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_header_parser(nsjail_uiiii, NULL, NULL, APR_HOOK_FIRST);
}


module AP_MODULE_DECLARE_DATA nsjail_module = {
	STANDARD20_MODULE_STUFF,
	create_dir_config,		/* dir config creater */
	merge_dir_config,		/* dir merger --- default is to override */
	create_config,			/* server config */
	NULL,				/* merge server config */
	nsjail_cmds,			/* command apr_table_t */
	register_hooks			/* register hooks */
};
