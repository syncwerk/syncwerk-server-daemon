#include "common.h"
#include "log.h"

#include <getopt.h>

#include <ccnet.h>

#include "syncwerk-session.h"
#include "gc-core.h"
#include "verify.h"

#include "utils.h"

static char *config_dir = NULL;
static char *syncwerk_dir = NULL;
static char *central_config_dir = NULL;

CcnetClient *ccnet_client;
SyncwerkSession *syncw;

static const char *short_opts = "hvc:d:VDrF:";
static const struct option long_opts[] = {
    { "help", no_argument, NULL, 'h', },
    { "version", no_argument, NULL, 'v', },
    { "config-file", required_argument, NULL, 'c', },
    { "central-config-dir", required_argument, NULL, 'F' },
    { "syncwdir", required_argument, NULL, 'd', },
    { "verbose", no_argument, NULL, 'V' },
    { "dry-run", no_argument, NULL, 'D' },
    { "rm-deleted", no_argument, NULL, 'r' },
    { 0, 0, 0, 0 },
};

static void usage ()
{
    fprintf (stderr,
             "usage: syncwerk-server-gc [-c config_dir] [-d syncwerk_dir] "
             "[repo_id_1 [repo_id_2 ...]]\n"
             "Additional options:\n"
             "-r, --rm-deleted: remove garbaged repos\n"
             "-D, --dry-run: report blocks that can be remove, but not remove them\n"
             "-V, --verbose: verbose output messages\n");
}

#ifdef WIN32
/* Get the commandline arguments in unicode, then convert them to utf8  */
static char **
get_argv_utf8 (int *argc)
{
    int i = 0;
    char **argv = NULL;
    const wchar_t *cmdline = NULL;
    wchar_t **argv_w = NULL;

    cmdline = GetCommandLineW();
    argv_w = CommandLineToArgvW (cmdline, argc);
    if (!argv_w) {
        printf("failed to CommandLineToArgvW(), GLE=%lu\n", GetLastError());
        return NULL;
    }

    argv = (char **)malloc (sizeof(char*) * (*argc));
    for (i = 0; i < *argc; i++) {
        argv[i] = wchar_to_utf8 (argv_w[i]);
    }

    return argv;
}
#endif

int
main(int argc, char *argv[])
{
    int c;
    int verbose = 0;
    int dry_run = 0;
    int rm_garbage = 0;

#ifdef WIN32
    argv = get_argv_utf8 (&argc);
#endif

    config_dir = DEFAULT_CONFIG_DIR;

    while ((c = getopt_long(argc, argv,
                short_opts, long_opts, NULL)) != EOF) {
        switch (c) {
        case 'h':
            usage();
            exit(0);
        case 'v':
            exit(-1);
            break;
        case 'c':
            config_dir = strdup(optarg);
            break;
        case 'd':
            syncwerk_dir = strdup(optarg);
            break;
        case 'F':
            central_config_dir = strdup(optarg);
            break;
        case 'V':
            verbose = 1;
            break;
        case 'D':
            dry_run = 1;
            break;
        case 'r':
            rm_garbage = 1;
            break;
        default:
            usage();
            exit(-1);
        }
    }

#if !GLIB_CHECK_VERSION(2, 35, 0)
    g_type_init();
#endif

    if (syncwerk_log_init ("-", "info", "debug") < 0) {
        syncw_warning ("Failed to init log.\n");
        exit (1);
    }

    ccnet_client = ccnet_client_new();
    if ((ccnet_client_load_confdir(ccnet_client, central_config_dir, config_dir)) < 0) {
        syncw_warning ("Read config dir error\n");
        return -1;
    }

    if (syncwerk_dir == NULL)
        syncwerk_dir = g_build_filename (config_dir, "syncwerk-data", NULL);
    
    syncw = syncwerk_session_new(central_config_dir, syncwerk_dir, ccnet_client, TRUE);
    if (!syncw) {
        syncw_warning ("Failed to create syncwerk session.\n");
        exit (1);
    }

    if (rm_garbage) {
        delete_garbaged_repos (dry_run);
        return 0;
    }

    GList *repo_id_list = NULL;
    int i;
    for (i = optind; i < argc; i++)
        repo_id_list = g_list_append (repo_id_list, g_strdup(argv[i]));

    gc_core_run (repo_id_list, dry_run, verbose);

    return 0;
}
