#include "nsjail_config.h"

int chroot_used = NSJAIL_CHROOT_NOT_USED;

static void *create_dir_config(apr_pool_t * p, char *d)
{
    char *dname = d;
    nsjail_dir_config_t *dconf = apr_pcalloc(p, sizeof(*dconf));

    dconf->nsjail_uid = UNSET;
    dconf->nsjail_gid = UNSET;
    dconf->groupsnr = UNSET;

    return dconf;
}

static void *merge_dir_config(apr_pool_t *p, void *base, void *overrides)
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


static void *create_config(apr_pool_t *p, server_rec *s)
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
static const char *set_uidgid(cmd_parms *cmd, void *mconfig, const char *uid, const char *gid)
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
static const char *set_groups(cmd_parms *cmd, void *mconfig, const char *arg)
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
static const char *set_defuidgid(cmd_parms *cmd, void *mconfig, const char *uid, const char *gid)
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
static const char *set_minuidgid(cmd_parms *cmd, void *mconfig, const char *uid, const char *gid)
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
static const char *set_documentchroot(cmd_parms *cmd, void *mconfig, const char *chroot_dir, const char *document_root)
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


static int is_chroot_used() {
    return chroot_used;
}