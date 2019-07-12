#ifndef _nsjail_config_h_
#define _nsjail_config_h_
#include <ap_release.h>
#include <apr_strings.h>
#include <apr_md5.h>
#include <apr_file_info.h>
#include <sys/types.h>
#include <unixd.h>

// TODO: Override capability.
#define NSJAIL_MAXGROUPS 8

// TODO: It is not our place to be making this decision for the user.
#define NSJAIL_MIN_UID 100
#define NSJAIL_MIN_GID 100

#define NONE -2
#define UNSET -1
#define SET 1

#define NSJAIL_CHROOT_NOT_USED 0
#define NSJAIL_CHROOT_USED 1

// TODO: I don't know. Figure it out, you're the smart one.
module AP_MODULE_DECLARE_DATA nsjail_module;

typedef struct
{
    uid_t nsjail_uid;
    gid_t nsjail_gid;
    gid_t groups[NSJAIL_MAXGROUPS];
    int groupsnr;
} nsjail_dir_config_t;

typedef struct
{
    uid_t default_uid;
    gid_t default_gid;
    uid_t min_uid;
    gid_t min_gid;
    const char *chroot_dir;
    const char *document_root;
} nsjail_config_t;

static void *create_dir_config(apr_pool_t*, char*);
static void *merge_dir_config(apr_pool_t*, void*, void*);
static void *create_config(apr_pool_t*, server_rec*);
static const char *set_uidgid(cmd_parms*, void*, const char*, const char*);
static const char *set_groups(cmd_parms*, void*, const char*);
static const char *set_defuidgid(cmd_parms*, void*, const char*, const char*);
static const char *set_minuidgid(cmd_parms*, void*, const char*, const char*);
static const char *set_documentchroot(cmd_parms*, void*, const char*, const char*);

static int is_chroot_used();
#endif