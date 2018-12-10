#ifndef SYNCW_UTILS_H
#define SYNCW_UTILS_H

struct _SyncwerkSession;


char *
syncwerk_session_get_tmp_file_path (struct _SyncwerkSession *session,
                                   const char *basename,
                                   char path[]);

int
load_database_config (struct _SyncwerkSession *session);

#endif
