/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "common.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>

#include <glib.h>
#include <ccnet.h>

#include "utils.h"
#include "log.h"
#include "syncwerk-controller.h"

#define CHECK_PROCESS_INTERVAL 10        /* every 10 seconds */

SyncwerkController *ctl;

static char *controller_pidfile = NULL;

char *bin_dir = NULL;
char *installpath = NULL;
char *topdir = NULL;

char *syncwerk_ld_library_path = NULL;

static const char *short_opts = "hvftc:d:l:g:G:P:F:";
static const struct option long_opts[] = {
    { "help", no_argument, NULL, 'h', },
    { "version", no_argument, NULL, 'v', },
    { "foreground", no_argument, NULL, 'f', },
    { "test", no_argument, NULL, 't', },
    { "config-dir", required_argument, NULL, 'c', },
    { "syncwerk-dir", required_argument, NULL, 'd', },
    { "central-config-dir", required_argument, NULL, 'F' },
    { "logdir", required_argument, NULL, 'l', },
    { "ccnet-debug-level", required_argument, NULL, 'g' },
    { "syncwerk-debug-level", required_argument, NULL, 'G' },
    { "pidfile", required_argument, NULL, 'P' },
};

static void controller_exit (int code) __attribute__((noreturn));

static int read_syncwdav_config();

static void
controller_exit (int code)
{
    if (code != 0) {
        syncw_warning ("syncw-controller exited with code %d\n", code);
    }
    exit(code);
}

//
// Utility functions Start
//

/* returns the pid of the newly created process */
static int
spawn_process (char *argv[])
{
    char **ptr = argv;
    GString *buf = g_string_new(argv[0]);
    while (*(++ptr)) {
        g_string_append_printf (buf, " %s", *ptr);
    }
    syncw_message ("spawn_process: %s\n", buf->str);
    g_string_free (buf, TRUE);

    pid_t pid = fork();

    if (pid == 0) {
        /* child process */
        execvp (argv[0], argv);
        syncw_warning ("failed to execvp %s\n", argv[0]);
        exit(-1);
    } else {
        /* controller */
        if (pid == -1)
            syncw_warning ("error when fork %s: %s\n", argv[0], strerror(errno));
        else
            syncw_message ("spawned %s, pid %d\n", argv[0], pid);

        return (int)pid;
    }
}

#define PID_ERROR_ENOENT 0
#define PID_ERROR_OTHER  -1

/**
 * @return
 * - pid if successfully opened and read the file
 * - PID_ERROR_ENOENT if file not exists,
 * - PID_ERROR_OTHER if other errors
 */
static int
read_pid_from_pidfile (const char *pidfile)
{
    FILE *pf = g_fopen (pidfile, "r");
    if (!pf) {
        if (errno == ENOENT) {
            return PID_ERROR_ENOENT;
        } else {
            return PID_ERROR_OTHER;
        }
    }

    int pid = PID_ERROR_OTHER;
    if (fscanf (pf, "%d", &pid) < 0) {
        syncw_warning ("bad pidfile format: %s\n", pidfile);
        fclose(pf);
        return PID_ERROR_OTHER;
    }

    fclose(pf);

    return pid;
}

static void
try_kill_process(int which)
{
    if (which < 0 || which >= N_PID)
        return;

    char *pidfile = ctl->pidfile[which];
    int pid = read_pid_from_pidfile(pidfile);
    if (pid > 0) {
        // if SIGTERM send success, then remove related pid file
        if (kill ((pid_t)pid, SIGTERM) == 0) {
            g_unlink (pidfile);
        }
    }
}

static void
kill_by_force (int which)
{
    if (which < 0 || which >= N_PID)
        return;

    char *pidfile = ctl->pidfile[which];
    int pid = read_pid_from_pidfile(pidfile);
    if (pid > 0) {
        // if SIGKILL send success, then remove related pid file
        if (kill ((pid_t)pid, SIGKILL) == 0) {
            g_unlink (pidfile);
        }
    }
}

//
// Utility functions End
//

static int
start_syncwerk_server_ccnet ()
{
    if (!ctl->config_dir)
        return -1;

    syncw_message ("starting syncwerk-server-ccnet ...\n");


    static char *logfile = NULL;
    if (logfile == NULL) {
        logfile = g_build_filename (ctl->logdir, "ccnet.log", NULL);
    }

    char *argv[] = {
        "syncwerk-server-ccnet",
        "-F", ctl->central_config_dir,
        "-c", ctl->config_dir,
        "-f", logfile,
        "-d",
        "-P", ctl->pidfile[PID_CCNET],
        NULL};

    int pid = spawn_process (argv);
    if (pid <= 0) {
        syncw_warning ("Failed to spawn syncwerk-server-ccnet\n");
        return -1;
    }

    return 0;
}

static int
start_syncwerk_server_daemon ()
{
    if (!ctl->config_dir || !ctl->syncwerk_dir)
        return -1;

    syncw_message ("starting syncwerk-server-daemon ...\n");
    static char *logfile = NULL;
    if (logfile == NULL) {
        logfile = g_build_filename (ctl->logdir, "syncwerk.log", NULL);
    }

    char *argv[] = {
        "syncwerk-server-daemon",
        "-F", ctl->central_config_dir,
        "-c", ctl->config_dir,
        "-d", ctl->syncwerk_dir,
        "-l", logfile,
        "-P", ctl->pidfile[PID_SERVER],
        NULL};

    int pid = spawn_process (argv);
    if (pid <= 0) {
        syncw_warning ("Failed to spawn syncwerk-server-daemon\n");
        return -1;
    }

    return 0;
}

static const char *
get_python_executable() {
    static const char *python = NULL;
    if (python != NULL) {
        return python;
    }

    static const char *try_list[] = {
        "python2.7",
        "python27",
        "python2.6",
        "python26",
    };

    int i;
    for (i = 0; i < G_N_ELEMENTS(try_list); i++) {
        char *binary = g_find_program_in_path (try_list[i]);
        if (binary != NULL) {
            python = binary;
            break;
        }
    }

    if (python == NULL) {
        python = g_getenv ("PYTHON");
        if (python == NULL) {
            python = "python";
        }
    }

    return python;
}

static void
init_syncwerk_path ()
{
    GError *error = NULL;
    char *binary = g_file_read_link ("/proc/self/exe", &error);
    char *tmp = NULL;
    if (error != NULL) {
        syncw_warning ("failed to readlink: %s\n", error->message);
        return;
    }

    bin_dir = g_path_get_dirname (binary);

    tmp = g_path_get_dirname (bin_dir);
    installpath = g_path_get_dirname (tmp);

    topdir = g_path_get_dirname (installpath);

    g_free (binary);
    g_free (tmp);
}

static void
setup_python_path()
{
    static GList *path_list = NULL;
    if (path_list != NULL) {
        /* Only setup once */
        return;
    }

    path_list = g_list_prepend (path_list,
        g_build_filename (installpath, "restapi", NULL));

    path_list = g_list_prepend (path_list,
        g_build_filename (installpath, "restapi/thirdpart", NULL));

    path_list = g_list_prepend (path_list,
        g_build_filename (installpath, "restapi/restapi-extra", NULL));

    path_list = g_list_prepend (path_list,
        g_build_filename (installpath, "restapi/restapi-extra/thirdparts", NULL));

    path_list = g_list_prepend (path_list,
        g_build_filename (installpath, "syncwerk/lib/python2.6/site-packages", NULL));

    path_list = g_list_prepend (path_list,
        g_build_filename (installpath, "syncwerk/lib64/python2.6/site-packages", NULL));

    path_list = g_list_prepend (path_list,
        g_build_filename (installpath, "syncwerk/lib/python2.7/site-packages", NULL));

    path_list = g_list_prepend (path_list,
        g_build_filename (installpath, "syncwerk/lib64/python2.7/site-packages", NULL));

    path_list = g_list_reverse (path_list);

    GList *ptr;
    GString *new_pypath = g_string_new (g_getenv("PYTHONPATH"));

    for (ptr = path_list; ptr != NULL; ptr = ptr->next) {
        const char *path = (char *)ptr->data;

        g_string_append_c (new_pypath, ':');
        g_string_append (new_pypath, path);
    }

    g_setenv ("PYTHONPATH", g_string_free (new_pypath, FALSE), TRUE);

    /* syncw_message ("PYTHONPATH is:\n\n%s\n", g_getenv ("PYTHONPATH")); */
}

static void
setup_env ()
{
    g_setenv ("CCNET_CONF_DIR", ctl->config_dir, TRUE);
    g_setenv ("SYNCWERK_CONF_DIR", ctl->syncwerk_dir, TRUE);
    g_setenv ("SYNCWERK_CENTRAL_CONF_DIR", ctl->central_config_dir, TRUE);

    char *restapi_dir = g_build_filename (installpath, "restapi", NULL);
    char *syncwdav_conf = g_build_filename (ctl->central_config_dir, "syncwdav.conf", NULL);
    g_setenv ("RESTAPI_DIR", restapi_dir, TRUE);
    g_setenv ("SYNCWDAV_CONF", syncwdav_conf, TRUE);

    setup_python_path();
}

static int
start_syncwdav() {
    static char *syncwdav_log_file = NULL;
    if (syncwdav_log_file == NULL)
        syncwdav_log_file = g_build_filename (ctl->logdir,
                                             "syncwdav.log",
                                             NULL);

    SyncwDavConfig conf = ctl->syncwdav_config;
    char port[16];
    snprintf (port, sizeof(port), "%d", conf.port);

    char *argv[] = {
        (char *)get_python_executable(),
        "-m", "wsgidav.server.run_server",
        "--log-file", syncwdav_log_file,
        "--pid", ctl->pidfile[PID_SYNCWDAV],
        "--port", port,
        "--host", conf.host,
        NULL
    };

    char *argv_fastcgi[] = {
        (char *)get_python_executable(),
        "-m", "wsgidav.server.run_server",
        "runfcgi",
        "--log-file", syncwdav_log_file,
        "--pid", ctl->pidfile[PID_SYNCWDAV],
        "--port", port,
        "--host", conf.host,
        NULL
    };

    char **args;
    if (ctl->syncwdav_config.fastcgi) {
        args = argv_fastcgi;
    } else {
        args = argv;
    }

    int pid = spawn_process (args);

    if (pid <= 0) {
        syncw_warning ("Failed to spawn syncwdav\n");
        return -1;
    }

    return 0;
}

static void
run_controller_loop ()
{
    GMainLoop *mainloop = g_main_loop_new (NULL, FALSE);

    g_main_loop_run (mainloop);
}

static gboolean
need_restart (int which)
{
    if (which < 0 || which >= N_PID)
        return FALSE;

    int pid = read_pid_from_pidfile (ctl->pidfile[which]);
    if (pid == PID_ERROR_ENOENT) {
        syncw_warning ("pid file %s does not exist\n", ctl->pidfile[which]);
        return TRUE;
    } else if (pid == PID_ERROR_OTHER) {
        syncw_warning ("failed to read pidfile %s: %s\n", ctl->pidfile[which], strerror(errno));
        return FALSE;
    } else {
        char buf[256];
        snprintf (buf, sizeof(buf), "/proc/%d", pid);
        if (g_file_test (buf, G_FILE_TEST_IS_DIR)) {
            return FALSE;
        } else {
            syncw_warning ("path /proc/%d doesn't exist, restart progress [%d]\n", pid, which);
            return TRUE;
        }
    }
}

static gboolean
check_process (void *data)
{
    if (need_restart(PID_SERVER)) {
        syncw_message ("syncwerk-server-daemon need restart...\n");
        kill_by_force (PID_CCNET);
    }

    if (ctl->syncwdav_config.enabled) {
        if (need_restart(PID_SYNCWDAV)) {
            syncw_message ("syncwdav need restart...\n");
            start_syncwdav ();
        }
    }

    return TRUE;
}

static void
start_process_monitor ()
{
    ctl->check_process_timer = g_timeout_add (
        CHECK_PROCESS_INTERVAL * 1000, check_process, NULL);
}

static void
stop_process_monitor ()
{
    if (ctl->check_process_timer != 0) {
        g_source_remove (ctl->check_process_timer);
        ctl->check_process_timer = 0;
    }
}

static void
disconnect_clients ()
{
    CcnetClient *client, *sync_client;
    client = ctl->client;
    sync_client = ctl->sync_client;

    if (client->connected) {
        ccnet_client_disconnect_daemon (client);
    }

    if (sync_client->connected) {
        ccnet_client_disconnect_daemon (sync_client);
    }
}

static void rm_client_fd_from_mainloop ();
static int syncw_controller_start ();

static void
on_ccnet_daemon_down ()
{
    stop_process_monitor ();
    disconnect_clients ();
    rm_client_fd_from_mainloop ();

    syncw_message ("restarting ccnet server ...\n");

    /* restart ccnet */
    if (syncw_controller_start () < 0) {
        syncw_warning ("Failed to restart ccnet server.\n");
        controller_exit (1);
    }
}

static gboolean
client_io_cb (GIOChannel *source, GIOCondition condition, gpointer data)
{
    if (condition & G_IO_IN) {
        if (ccnet_client_read_input (ctl->client) <= 0) {
            on_ccnet_daemon_down ();
            return FALSE;
        }
        return TRUE;
    } else {
        on_ccnet_daemon_down ();
        return FALSE;
    }
}

static void
add_client_fd_to_mainloop ()
{
    GIOChannel *channel;

    channel = g_io_channel_unix_new (ctl->client->connfd);
    ctl->client_io_id = g_io_add_watch (channel,
                                        G_IO_IN | G_IO_HUP | G_IO_ERR,
                                        client_io_cb, NULL);
}

static void
rm_client_fd_from_mainloop ()
{
    if (ctl->client_io_id != 0) {
        g_source_remove (ctl->client_io_id);
        ctl->client_io_id = 0;
    }
}

static void
on_ccnet_connected ()
{
    if (start_syncwerk_server_daemon () < 0)
        controller_exit(1);

    if (ctl->syncwdav_config.enabled) {
        if (need_restart(PID_SYNCWDAV)) {
            if (start_syncwdav() < 0)
                controller_exit(1);
        }
    } else {
        syncw_message ("syncwdav not enabled.\n");
    }

    add_client_fd_to_mainloop ();

    start_process_monitor ();
}

static gboolean
do_connect_ccnet ()
{
    CcnetClient *client, *sync_client;
    client = ctl->client;
    sync_client = ctl->sync_client;

    if (!client->connected) {
        if (ccnet_client_connect_daemon (client, CCNET_CLIENT_ASYNC) < 0) {
            return TRUE;
        }
    }

    if (!sync_client->connected) {
        if (ccnet_client_connect_daemon (sync_client, CCNET_CLIENT_SYNC) < 0) {
            return TRUE;
        }
    }

    syncw_message ("ccnet daemon connected.\n");

    on_ccnet_connected ();

    return FALSE;
}

/* This would also stop syncwerk-server-daemon & other components */
static void
stop_syncwerk_server_ccnet ()
{
    syncw_message ("shutting down syncwerk-server-ccnet ...\n");
    GError *error = NULL;
    ccnet_client_send_cmd (ctl->sync_client, "shutdown", &error);

    kill_by_force(PID_CCNET);
    kill_by_force(PID_SERVER);
    kill_by_force(PID_SYNCWDAV);
}

static void
init_pidfile_path (SyncwerkController *ctl)
{
    char *pid_dir = g_build_filename (topdir, "pids", NULL);
    if (!g_file_test(pid_dir, G_FILE_TEST_EXISTS)) {
        if (g_mkdir(pid_dir, 0777) < 0) {
            syncw_warning("failed to create pid dir %s: %s", pid_dir, strerror(errno));
            controller_exit(1);
        }
    }

    ctl->pidfile[PID_CCNET] = g_build_filename (pid_dir, "ccnet.pid", NULL);
    ctl->pidfile[PID_SERVER] = g_build_filename (pid_dir, "syncwerk-server-daemon.pid", NULL);
    ctl->pidfile[PID_SYNCWDAV] = g_build_filename (pid_dir, "syncwdav.pid", NULL);
}

static int
syncw_controller_init (SyncwerkController *ctl,
                      char *central_config_dir,
                      char *config_dir,
                      char *syncwerk_dir,
                      char *logdir)
{
    init_syncwerk_path ();
    if (!g_file_test (config_dir, G_FILE_TEST_IS_DIR)) {
        syncw_warning ("invalid config_dir: %s\n", config_dir);
        return -1;
    }

    if (!g_file_test (syncwerk_dir, G_FILE_TEST_IS_DIR)) {
        syncw_warning ("invalid syncwerk_dir: %s\n", syncwerk_dir);
        return -1;
    }

    ctl->client = ccnet_client_new ();
    ctl->sync_client = ccnet_client_new ();

    if (ccnet_client_load_confdir (ctl->client, central_config_dir, config_dir) < 0) {
        syncw_warning ("Failed to load ccnet confdir\n");
        return -1;
    }

    if (ccnet_client_load_confdir (ctl->sync_client, central_config_dir, config_dir) < 0) {
        syncw_warning ("Failed to load ccnet confdir\n");
        return -1;
    }

    if (logdir == NULL) {
        char *topdir = g_path_get_dirname(config_dir);
        logdir = g_build_filename (topdir, "logs", NULL);
        if (checkdir_with_mkdir(logdir) < 0) {
            fprintf (stderr, "failed to create log folder \"%s\": %s\n",
                     logdir, strerror(errno));
            return -1;
        }
        g_free (topdir);
    }

    ctl->central_config_dir = central_config_dir;
    ctl->config_dir = config_dir;
    ctl->syncwerk_dir = syncwerk_dir;
    ctl->logdir = logdir;

    if (read_syncwdav_config() < 0) {
        return -1;
    }

    init_pidfile_path (ctl);
    setup_env ();

    return 0;
}

static int
syncw_controller_start ()
{
    if (start_syncwerk_server_ccnet () < 0) {
        syncw_warning ("Failed to start ccnet server\n");
        return -1;
    }

    g_timeout_add (1000 * 1, do_connect_ccnet, NULL);

    return 0;
}

static int
write_controller_pidfile ()
{
    if (!controller_pidfile)
        return -1;

    pid_t pid = getpid();

    FILE *pidfile = g_fopen(controller_pidfile, "w");
    if (!pidfile) {
        syncw_warning ("Failed to fopen() pidfile %s: %s\n",
                      controller_pidfile, strerror(errno));
        return -1;
    }

    char buf[32];
    snprintf (buf, sizeof(buf), "%d\n", pid);
    if (fputs(buf, pidfile) < 0) {
        syncw_warning ("Failed to write pidfile %s: %s\n",
                      controller_pidfile, strerror(errno));
        fclose (pidfile);
        return -1;
    }

    fflush (pidfile);
    fclose (pidfile);
    return 0;
}

static void
remove_controller_pidfile ()
{
    if (controller_pidfile) {
        g_unlink (controller_pidfile);
    }
}

static void
sigint_handler (int signo)
{
    stop_syncwerk_server_ccnet ();

    remove_controller_pidfile();

    signal (signo, SIG_DFL);
    raise (signo);
}

static void
sigchld_handler (int signo)
{
    waitpid (-1, NULL, WNOHANG);
}

static void
sigusr1_handler (int signo)
{
    syncwerk_log_reopen();
}

static void
set_signal_handlers ()
{
    signal (SIGINT, sigint_handler);
    signal (SIGTERM, sigint_handler);
    signal (SIGCHLD, sigchld_handler);
    signal (SIGUSR1, sigusr1_handler);
    signal (SIGPIPE, SIG_IGN);
}

static void
usage ()
{
    fprintf (stderr, "Usage: syncwerk-controller OPTIONS\n"
             "OPTIONS:\n"
             "  -b, --bin-dir           insert a directory in front of the PATH env\n"
             "  -c, --config-dir        ccnet config dir\n"
             "  -d, --syncwerk-dir       syncwerk dir\n"
        );
}

/* syncwerk-controller -t is used to test whether config file is valid */
static void
test_config (const char *central_config_dir,
             const char *ccnet_dir,
             const char *syncwerk_dir)
{
    char buf[1024];
    GError *error = NULL;
    int retcode = 0;
    char *child_stdout = NULL;
    char *child_stderr = NULL;

    snprintf(buf,
             sizeof(buf),
             "syncwerk-server-ccnet -F \"%s\" -c \"%s\" -t",
             central_config_dir,
             ccnet_dir);

    g_spawn_command_line_sync (buf,
                               &child_stdout, /* stdout */
                               &child_stderr, /* stderror */
                               &retcode,
                               &error);

    if (error != NULL) {
        fprintf (stderr,
                 "failed to run \"syncwerk-server-ccnet -t\": %s\n",
                 error->message);
        exit (1);
    }

    if (child_stdout) {
        fputs (child_stdout, stdout);
    }

    if (child_stderr) {
        fputs (child_stderr, stdout);
    }

    if (retcode != 0) {
        fprintf (stderr,
                 "failed to run \"syncwerk-server-ccnet -t\"\n");
        exit (1);
    }

    exit(0);
}

static int
read_syncwdav_config()
{
    int ret = 0;
    char *syncwdav_conf = NULL;
    GKeyFile *key_file = NULL;
    GError *error = NULL;

    syncwdav_conf = g_build_filename(ctl->central_config_dir, "syncwdav.conf", NULL);
    if (!g_file_test(syncwdav_conf, G_FILE_TEST_EXISTS)) {
        goto out;
    }

    key_file = g_key_file_new ();
    if (!g_key_file_load_from_file (key_file, syncwdav_conf,
                                    G_KEY_FILE_KEEP_COMMENTS, NULL)) {
        syncw_warning("Failed to load syncwdav.conf\n");
        ret = -1;
        goto out;
    }

    /* enabled */
    ctl->syncwdav_config.enabled = g_key_file_get_boolean(key_file, "WEBDAV", "enabled", &error);
    if (error != NULL) {
        if (error->code != G_KEY_FILE_ERROR_KEY_NOT_FOUND) {
            syncw_message ("Error when reading WEBDAV.enabled, use default value 'false'\n");
        }
        ctl->syncwdav_config.enabled = FALSE;
        g_clear_error (&error);
        goto out;
    }

    if (!ctl->syncwdav_config.enabled) {
        goto out;
    }

    /* fastcgi */
    ctl->syncwdav_config.fastcgi = g_key_file_get_boolean(key_file, "WEBDAV", "fastcgi", &error);
    if (error != NULL) {
        if (error->code != G_KEY_FILE_ERROR_KEY_NOT_FOUND) {
            syncw_message ("Error when reading WEBDAV.fastcgi, use default value 'false'\n");
        }
        ctl->syncwdav_config.fastcgi = FALSE;
        g_clear_error (&error);
    }

    /* host */
    char *host = syncw_key_file_get_string (key_file, "WEBDAV", "host", &error);
    if (error != NULL) {
        g_clear_error(&error);
        ctl->syncwdav_config.host = g_strdup(ctl->syncwdav_config.fastcgi ? "localhost" : "0.0.0.0");
    } else {
        ctl->syncwdav_config.host = host;
    }

    /* port */
    ctl->syncwdav_config.port = g_key_file_get_integer(key_file, "WEBDAV", "port", &error);
    if (error != NULL) {
        if (error->code != G_KEY_FILE_ERROR_KEY_NOT_FOUND) {
            syncw_message ("Error when reading WEBDAV.port, use deafult value 8080\n");
        }
        ctl->syncwdav_config.port = 8080;
        g_clear_error (&error);
    }

    if (ctl->syncwdav_config.port <= 0 || ctl->syncwdav_config.port > 65535) {
        syncw_warning("Failed to load syncwdav config: invalid port %d\n", ctl->syncwdav_config.port);
        ret = -1;
        goto out;
    }

out:
    if (key_file) {
        g_key_file_free (key_file);
    }
    g_free (syncwdav_conf);

    return ret;
}

static int
init_syslog_config ()
{
    char *syncwerk_conf = g_build_filename (ctl->central_config_dir, "server.conf", NULL);
    GKeyFile *key_file = g_key_file_new ();
    int ret = 0;

    if (!g_key_file_load_from_file (key_file, syncwerk_conf,
                                    G_KEY_FILE_KEEP_COMMENTS, NULL)) {
        syncw_warning("Failed to load server.conf.\n");
        ret = -1;
        goto out;
    }

    set_syslog_config (key_file);

out:
    g_key_file_free (key_file);
    g_free (syncwerk_conf);

    return ret;
}

int main (int argc, char **argv)
{
    if (argc <= 1) {
        usage ();
        exit (1);
    }

    char *config_dir = DEFAULT_CONFIG_DIR;
    char *central_config_dir = NULL;
    char *syncwerk_dir = NULL;
    char *logdir = NULL;
    char *ccnet_debug_level_str = "info";
    char *syncwerk_debug_level_str = "debug";
    int daemon_mode = 1;
    gboolean test_conf = FALSE;

    int c;
    while ((c = getopt_long (argc, argv, short_opts,
                             long_opts, NULL)) != EOF)
    {
        switch (c) {
        case 'h':
            usage ();
            exit(1);
            break;
        case 'v':
            fprintf (stderr, "syncwerk-controller version 1.0\n");
            break;
        case 't':
            test_conf = TRUE;
            break;
        case 'c':
            config_dir = optarg;
            break;
        case 'F':
            central_config_dir = g_strdup(optarg);
            break;
        case 'd':
            syncwerk_dir = g_strdup(optarg);
            break;
        case 'f':
            daemon_mode = 0;
            break;
        case 'L':
            logdir = g_strdup(optarg);
            break;
        case 'g':
            ccnet_debug_level_str = optarg;
            break;
        case 'G':
            syncwerk_debug_level_str = optarg;
            break;
        case 'P':
            controller_pidfile = optarg;
            break;
        default:
            usage ();
            exit (1);
        }
    }

#if !GLIB_CHECK_VERSION(2, 35, 0)
    g_type_init();
#endif
#if !GLIB_CHECK_VERSION(2,32,0)
    g_thread_init (NULL);
#endif

    if (!syncwerk_dir) {
        fprintf (stderr, "<syncwerk_dir> must be specified with --syncwerk-dir\n");
        exit(1);
    }

    if (!central_config_dir) {
        fprintf (stderr, "<central_config_dir> must be specified with --central-config-dir\n");
        exit(1);
    }

    central_config_dir = ccnet_expand_path (central_config_dir);
    config_dir = ccnet_expand_path (config_dir);
    syncwerk_dir = ccnet_expand_path (syncwerk_dir);

    if (test_conf) {
        test_config (central_config_dir, config_dir, syncwerk_dir);
    }

    ctl = g_new0 (SyncwerkController, 1);
    if (syncw_controller_init (ctl, central_config_dir, config_dir, syncwerk_dir, logdir) < 0) {
        controller_exit(1);
    }

    char *logfile = g_build_filename (ctl->logdir, "controller.log", NULL);
    if (syncwerk_log_init (logfile, ccnet_debug_level_str,
                          syncwerk_debug_level_str) < 0) {
        syncw_warning ("Failed to init log.\n");
        controller_exit (1);
    }

    if (init_syslog_config () < 0) {
        controller_exit (1);
    }

    set_signal_handlers ();

    if (syncw_controller_start () < 0)
        controller_exit (1);

#ifndef WIN32
    if (daemon_mode) {
#ifndef __APPLE__
        daemon (1, 0);
#else   /* __APPLE */
        /* daemon is deprecated under APPLE
         * use fork() instead
         * */
        switch (fork ()) {
          case -1:
              syncw_warning ("Failed to daemonize");
              exit (-1);
              break;
          case 0:
              /* all good*/
              break;
          default:
              /* kill origin process */
              exit (0);
        }
#endif  /* __APPLE */
    }
#endif /* !WIN32 */

    if (controller_pidfile == NULL) {
        controller_pidfile = g_strdup(g_getenv ("SYNCWERK_PIDFILE"));
    }

    if (controller_pidfile != NULL) {
        if (write_controller_pidfile () < 0) {
            syncw_warning ("Failed to write pidfile %s\n", controller_pidfile);
            return -1;
        }
    }

    run_controller_loop ();

    return 0;
}

