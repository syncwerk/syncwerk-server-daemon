#ifndef LOG_H
#define LOG_H

#define SYNCWERK_DOMAIN g_quark_from_string("syncwerk")

#ifndef syncw_warning
#define syncw_warning(fmt, ...) g_warning("%s(%d): " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#endif

#ifndef syncw_message
#define syncw_message(fmt, ...) g_message("%s(%d): " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#endif


int syncwerk_log_init (const char *logfile, const char *ccnet_debug_level_str,
                      const char *syncwerk_debug_level_str);
int syncwerk_log_reopen ();

#ifndef WIN32
#ifdef SYNCWERK_SERVER
void
set_syslog_config (GKeyFile *config);
#endif
#endif

void
syncwerk_debug_set_flags_string (const gchar *flags_string);

typedef enum
{
    SYNCWERK_DEBUG_TRANSFER = 1 << 1,
    SYNCWERK_DEBUG_SYNC = 1 << 2,
    SYNCWERK_DEBUG_WATCH = 1 << 3, /* wt-monitor */
    SYNCWERK_DEBUG_HTTP = 1 << 4,  /* http server */
    SYNCWERK_DEBUG_MERGE = 1 << 5,
    SYNCWERK_DEBUG_OTHER = 1 << 6,
} SyncwerkDebugFlags;

void syncwerk_debug_impl (SyncwerkDebugFlags flag, const gchar *format, ...);

#ifdef DEBUG_FLAG

#undef syncw_debug
#define syncw_debug(fmt, ...)  \
    syncwerk_debug_impl (DEBUG_FLAG, "%.10s(%d): " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

#endif  /* DEBUG_FLAG */

#endif
