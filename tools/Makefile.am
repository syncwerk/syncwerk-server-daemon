
#AM_CPPFLAGS = @GLIB2_CFLAGS@

bin_PROGRAMS = syncwerk-server-daemon-init

syncwerk_server_daemon_init_SOURCES = syncwerk-server-daemon-init.c ../common/syncwerk-server-db.c

syncwerk_server_daemon_init_LDADD = @GLIB2_LIBS@ $(top_builddir)/common/db-wrapper/libdbwrapper.la -lpthread @MYSQL_LIBS@ @PGSQL_LIBS@

syncwerk_server_daemon_init_CPPFLAGS = @GLIB2_CFLAGS@ @MSVC_CFLAGS@

EXTRA_DIST = syncwerk-admin

bin_SCRIPTS = syncwerk-admin
