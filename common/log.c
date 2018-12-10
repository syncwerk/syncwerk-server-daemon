/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "common.h"

#include <stdio.h>
#include <glib/gstdio.h>

#ifndef WIN32
#ifdef SYNCWERK_SERVER
#include <sys/syslog.h>
#endif
#endif

#include "log.h"
#include "utils.h"

/* message with greater log levels will be ignored */
static int ccnet_log_level;
static int syncwerk_log_level;
static char *logfile;
static FILE *logfp;

#ifndef WIN32
#ifdef SYNCWERK_SERVER
static gboolean enable_syslog;
#endif
#endif

#ifndef WIN32
#ifdef SYNCWERK_SERVER
static int
get_syslog_level (GLogLevelFlags level)
{
    switch (level) {
        case G_LOG_LEVEL_DEBUG:
            return LOG_DEBUG;
        case G_LOG_LEVEL_INFO:
            return LOG_INFO;
        case G_LOG_LEVEL_WARNING:
            return LOG_WARNING;
        case G_LOG_LEVEL_ERROR:
            return LOG_ERR;
        default:
            return LOG_DEBUG;
    }
}
#endif
#endif

static void 
syncwerk_log (const gchar *log_domain, GLogLevelFlags log_level,
             const gchar *message,    gpointer user_data)
{
    time_t t;
    struct tm *tm;
    char buf[1024];
    int len;

    if (log_level > syncwerk_log_level)
        return;

    t = time(NULL);
    tm = localtime(&t);
    len = strftime (buf, 1024, "[%x %X] ", tm);
    g_return_if_fail (len < 1024);
    if (logfp) {    
        fputs (buf, logfp);
        fputs (message, logfp);
        fflush (logfp);
    }

#ifndef WIN32
#ifdef SYNCWERK_SERVER
    if (enable_syslog)
        syslog (get_syslog_level (log_level), "%s", message);
#endif
#endif
}

static void 
ccnet_log (const gchar *log_domain, GLogLevelFlags log_level,
             const gchar *message,    gpointer user_data)
{
    time_t t;
    struct tm *tm;
    char buf[1024];
    int len;

    if (log_level > ccnet_log_level)
        return;

    t = time(NULL);
    tm = localtime(&t);
    len = strftime (buf, 1024, "[%x %X] ", tm);
    g_return_if_fail (len < 1024);
    if (logfp) {
        fputs (buf, logfp);
        fputs (message, logfp);
        fflush (logfp);
    }

#ifndef WIN32
#ifdef SYNCWERK_SERVER
    if (enable_syslog)
        syslog (get_syslog_level (log_level), "%s", message);
#endif
#endif
}

static int
get_debug_level(const char *str, int default_level)
{
    if (strcmp(str, "debug") == 0)
        return G_LOG_LEVEL_DEBUG;
    if (strcmp(str, "info") == 0)
        return G_LOG_LEVEL_INFO;
    if (strcmp(str, "warning") == 0)
        return G_LOG_LEVEL_WARNING;
    return default_level;
}

int
syncwerk_log_init (const char *_logfile, const char *ccnet_debug_level_str,
                  const char *syncwerk_debug_level_str)
{
    g_log_set_handler (NULL, G_LOG_LEVEL_MASK | G_LOG_FLAG_FATAL
                       | G_LOG_FLAG_RECURSION, syncwerk_log, NULL);
    g_log_set_handler ("Ccnet", G_LOG_LEVEL_MASK | G_LOG_FLAG_FATAL
                       | G_LOG_FLAG_RECURSION, ccnet_log, NULL);

    /* record all log message */
    ccnet_log_level = get_debug_level(ccnet_debug_level_str, G_LOG_LEVEL_INFO);
    syncwerk_log_level = get_debug_level(syncwerk_debug_level_str, G_LOG_LEVEL_DEBUG);

    if (strcmp(_logfile, "-") == 0) {
        logfp = stdout;
        logfile = g_strdup (_logfile);
    }
    else {
        logfile = ccnet_expand_path(_logfile);
        if ((logfp = g_fopen (logfile, "a+")) == NULL) {
            syncw_message ("Failed to open file %s\n", logfile);
            return -1;
        }
    }

    return 0;
}

int
syncwerk_log_reopen ()
{
    FILE *fp, *oldfp;

    if (strcmp(logfile, "-") == 0)
        return 0;

    if ((fp = g_fopen (logfile, "a+")) == NULL) {
        syncw_message ("Failed to open file %s\n", logfile);
        return -1;
    }

    //TODO: check file's health

    oldfp = logfp;
    logfp = fp;
    if (fclose(oldfp) < 0) {
        syncw_message ("Failed to close file %s\n", logfile);
        return -1;
    }

    return 0;
}

static SyncwerkDebugFlags debug_flags = 0;

static GDebugKey debug_keys[] = {
  { "Transfer", SYNCWERK_DEBUG_TRANSFER },
  { "Sync", SYNCWERK_DEBUG_SYNC },
  { "Watch", SYNCWERK_DEBUG_WATCH },
  { "Http", SYNCWERK_DEBUG_HTTP },
  { "Merge", SYNCWERK_DEBUG_MERGE },
  { "Other", SYNCWERK_DEBUG_OTHER },
};

gboolean
syncwerk_debug_flag_is_set (SyncwerkDebugFlags flag)
{
    return (debug_flags & flag) != 0;
}

void
syncwerk_debug_set_flags (SyncwerkDebugFlags flags)
{
    g_message ("Set debug flags %#x\n", flags);
    debug_flags |= flags;
}

void
syncwerk_debug_set_flags_string (const gchar *flags_string)
{
    guint nkeys = G_N_ELEMENTS (debug_keys);

    if (flags_string)
        syncwerk_debug_set_flags (
            g_parse_debug_string (flags_string, debug_keys, nkeys));
}

void
syncwerk_debug_impl (SyncwerkDebugFlags flag, const gchar *format, ...)
{
    if (flag & debug_flags) {
        va_list args;
        va_start (args, format);
        g_logv (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, format, args);
        va_end (args);
    }
}

#ifndef WIN32
#ifdef SYNCWERK_SERVER
void
set_syslog_config (GKeyFile *config)
{
    enable_syslog = g_key_file_get_boolean (config,
                                            "general", "enable_syslog",
                                            NULL);
    if (enable_syslog)
        openlog (NULL, LOG_NDELAY | LOG_PID, LOG_USER);
}
#endif
#endif
