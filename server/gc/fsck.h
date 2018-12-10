#ifndef SYNCW_FSCK_H
#define SYNCW_FSCK_H

int
syncwerk_server_fsck (GList *repo_id_list, gboolean repair);

void export_file (GList *repo_id_list, const char *syncwerk_dir, char *export_path);

#endif
