#include "nsjail_config.h"

int chroot_used = NSJAIL_CHROOT_NOT_USED;

void *create_dir_config(apr_pool_t * p, char *d)
{
    char *dname = d;
    nsjail_dir_config_t *dconf = apr_pcalloc(p, sizeof(*dconf));

    /* TODO: De-magic-number this. NSJAIL_SETUIDGID_DISABLED/NSJAIL_SETUIDGID_ENABLED. */
    dconf->enable_setuidgid = 1;
    dconf->nsjail_uid = UNSET;
    dconf->nsjail_gid = UNSET;
    dconf->groupsnr = UNSET;

    return dconf;
}

void *merge_dir_config(apr_pool_t *p, void *base, void *overrides)
{
    nsjail_dir_config_t *parent = base;
    nsjail_dir_config_t *child = overrides;
    nsjail_dir_config_t *conf = apr_pcalloc(p, sizeof(nsjail_dir_config_t));

    conf->nsjail_uid = (child->nsjail_uid == UNSET) ? parent->nsjail_uid : child->nsjail_uid;
    conf->nsjail_gid = (child->nsjail_gid == UNSET) ? parent->nsjail_gid : child->nsjail_gid;
    if (child->groupsnr == NONE)
    {
        conf->groupsnr = NONE;
    }
    else if (child->groupsnr > 0)
    {
        memcpy(conf->groups, child->groups, sizeof(child->groups));
        conf->groupsnr = child->groupsnr;
    }
    else if (parent->groupsnr > 0)
    {
        memcpy(conf->groups, parent->groups, sizeof(parent->groups));
        conf->groupsnr = parent->groupsnr;
    }
    else
    {
        conf->groupsnr = (child->groupsnr == UNSET) ? parent->groupsnr : child->groupsnr;
    }

    return conf;
}


void *create_config(apr_pool_t *p, server_rec *s)
{
    UNUSED(s);

    nsjail_config_t *conf = apr_palloc(p, sizeof(*conf));

    conf->default_uid = ap_unixd_config.user_id;
    conf->default_gid = ap_unixd_config.group_id;
    conf->min_uid = NSJAIL_MIN_UID;
    conf->min_gid = NSJAIL_MIN_GID;
    conf->chroot_dir = NULL;
    conf->document_root = NULL;

    return conf;
}


/*
 * Configuration option.
 * RUidGid <uid> <gid>
 * uid: Username.
 * gid: Group name.
 */
const char *set_uidgid(cmd_parms *cmd, void *mconfig, const char *uid, const char *gid)
{
    nsjail_dir_config_t *dconf = (nsjail_dir_config_t *)mconfig;
    const char *err = ap_check_cmd_context(cmd, NOT_IN_FILES | NOT_IN_LIMIT);

    if (err != NULL)
    {
        return err;
    }

    dconf->nsjail_uid = ap_uname2id(uid);
    dconf->nsjail_gid = ap_gname2id(gid);

    return NULL;
}


/*
 * Configuration option.
 * RGroups <gid>
 * gid: Group name.
 */
const char *set_groups(cmd_parms *cmd, void *mconfig, const char *arg)
{
    nsjail_dir_config_t *dconf = (nsjail_dir_config_t *)mconfig;
    const char *err = ap_check_cmd_context(cmd, NOT_IN_FILES | NOT_IN_LIMIT);

    if (err != NULL)
    {
        return err;
    }

    if (strcasecmp(arg, "@none") == 0)
    {
        dconf->groupsnr = NONE;
    }

    if (dconf->groupsnr == UNSET)
    {
        dconf->groupsnr = 0;
    }
    if ((dconf->groupsnr < NSJAIL_MAXGROUPS) && (dconf->groupsnr >= 0))
    {
        dconf->groups[dconf->groupsnr++] = ap_gname2id(arg);
    }

    return NULL;
}

/*
 * Configuration option
 * RDefaultUidGid <uid> <gid>
 * uid: Username.
 * gid: Group name.
 */
const char *set_defuidgid(cmd_parms *cmd, void *mconfig, const char *uid, const char *gid)
{
    UNUSED(mconfig);

    nsjail_config_t *conf = ap_get_module_config(cmd->server->module_config, &nsjail_module);
    const char *err = ap_check_cmd_context(cmd, NOT_IN_DIR_LOC_FILE | NOT_IN_LIMIT);

    if (err != NULL)
    {
        return err;
    }

    conf->default_uid = ap_uname2id(uid);
    conf->default_gid = ap_gname2id(gid);

    return NULL;
}

/*
 * Configuration option.
 * RMinUidGid <uid> <gid>
 * uid: Username.
 * gid: Group name.
 */
const char *set_minuidgid(cmd_parms *cmd, void *mconfig, const char *uid, const char *gid)
{
    UNUSED(mconfig);

    nsjail_config_t *conf = ap_get_module_config(cmd->server->module_config, &nsjail_module);
    const char *err = ap_check_cmd_context(cmd, NOT_IN_DIR_LOC_FILE | NOT_IN_LIMIT);

    if (err != NULL)
    {
        return err;
    }

    conf->min_uid = ap_uname2id(uid);
    conf->min_gid = ap_gname2id(gid);

    return NULL;
}

/*
 * Configuration option.
 * RDocumentChRoot <basedir> <docroot>
 * basedir: Base directory to chroot to.
 * docroot: Document root within chroot.
 */
const char *set_documentchroot(cmd_parms *cmd, void *mconfig, const char *chroot_dir, const char *document_root)
{
    UNUSED(mconfig);

    nsjail_config_t *conf = ap_get_module_config(cmd->server->module_config, &nsjail_module);
    const char *err = ap_check_cmd_context(cmd, NOT_IN_DIR_LOC_FILE | NOT_IN_LIMIT);

    if (err != NULL)
    {
        return err;
    }

    conf->chroot_dir = chroot_dir;
    conf->document_root = document_root;
    chroot_used |= NSJAIL_CHROOT_USED;

    return NULL;
}

/*
 * Configuration option.
 * NsJailEnableSetUidGid <On|Off>
 * Enable or disable setting UID/GID for location.
 */
const char *set_enablesetuidgid(cmd_parms *cmd, void *mconfig, int value) {
    nsjail_dir_config_t *dconf = (nsjail_dir_config_t *)mconfig;
    const char *err = ap_check_cmd_context(cmd, NOT_IN_FILES | NOT_IN_LIMIT);
    
    if (err != NULL)
    {
        return err;
    }

    dconf->enable_setuidgid = value;
    return NULL;
}

/*
 * Configuration option.
 * NsJailEnableUtsNamespace <On|Off>
 * Enable or disable UTS namespaces.
 */
const char *set_enableutsnamespace(cmd_parms *cmd, void *mconfig, int value) {
    nsjail_dir_config_t *dconf = (nsjail_dir_config_t *)mconfig;
    const char *err = ap_check_cmd_context(cmd, NOT_IN_FILES|NOT_IN_LIMIT);
    if ( err != NULL ) {
        return err;
    }

    dconf->enable_utsnamespace = value;
    return NULL;
}

/*
 * Configuration option.
 * NsJailUtsHostname <hostname>
 * hostname: Hostname to set within UTS namespace.
 */
const char *set_utshostname(cmd_parms *cmd, void *mconfig, const char *host_name) {
    nsjail_dir_config_t *dconf = (nsjail_dir_config_t *)mconfig;
    const char *err = ap_check_cmd_context(cmd, NOT_IN_FILES | NOT_IN_LIMIT);
    if (err != NULL)
    {
        return err;
    }

    dconf->uts_hostname = host_name;
    return NULL;
}

/*
 * Configuration option.
 * NsJailUtsDomainName <domain name>
 * domain name: Domain name to set within UTS namespace.
 */
const char *set_utsdomainname(cmd_parms *cmd, void *mconfig, const char *domain_name) {
    nsjail_dir_config_t *dconf = (nsjail_dir_config_t *)mconfig;
    const char *err = ap_check_cmd_context(cmd, NOT_IN_FILES | NOT_IN_LIMIT);
    if (err != NULL)
    {
        return err;
    }

    dconf->uts_domainname = domain_name;
    return NULL;
}

/*
 * Configuration option.
 * NsJailUtsCachePath <path>
 * Path to bind UTS namespace filehandle to.
 */
const char *set_utscachepath(cmd_parms *cmd, void *mconfig, const char *cache_path) {
    nsjail_dir_config_t *dconf = (nsjail_dir_config_t *)mconfig;
    const char *err = ap_check_cmd_context(cmd, NOT_IN_FILES | NOT_IN_LIMIT);
    if (err != NULL)
    {
        return err;
    }

    dconf->uts_cachepath = cache_path;
    return NULL;
}

int is_chroot_used() {
    return chroot_used;
}
