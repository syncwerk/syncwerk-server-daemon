#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <glib.h>

struct _SyncwerkSession;

struct _HttpServer;

struct _HttpServerStruct {
    struct _SyncwerkSession *syncw_session;

    struct _HttpServer *priv;

    char *bind_addr;
    int bind_port;
    char *http_temp_dir;        /* temp dir for file upload */
    char *windows_encoding;
    gint64 fixed_block_size;
    int web_token_expire_time;
    int max_indexing_threads;
    int worker_threads;
    int max_index_processing_threads;
};

typedef struct _HttpServerStruct HttpServerStruct;

HttpServerStruct *
syncw_http_server_new (struct _SyncwerkSession *session);

int
syncw_http_server_start (HttpServerStruct *htp_server);

int
syncw_http_server_invalidate_tokens (HttpServerStruct *htp_server,
                                    const GList *tokens);

void
send_statistic_msg (const char *repo_id, char *user, char *operation, guint64 bytes);

#endif
