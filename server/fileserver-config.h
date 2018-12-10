#ifndef SYNCWERK_FILESERVER_CONFIG_H
#define SYNCWERK_FILESERVER_CONFIG_H

struct GKeyFile;

int
fileserver_config_get_integer(GKeyFile *config, char *key, GError **error);

char *
fileserver_config_get_string(GKeyFile *config, char *key, GError **error);

gboolean
fileserver_config_get_boolean(GKeyFile *config, char *key, GError **error);

#endif // SYNCWERK_FILESERVER_CONFIG_H
