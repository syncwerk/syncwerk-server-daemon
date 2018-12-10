AM_CFLAGS = -DPKGDATADIR=\"$(pkgdatadir)\" \
	-DPACKAGE_DATA_DIR=\""$(pkgdatadir)"\" \
	-DSYNCWERK_SERVER \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib \
	-I$(top_builddir)/lib \
	-I$(top_srcdir)/common \
	@CCNET_CFLAGS@ \
	@RPCSYNCWERK_CFLAGS@ \
	@GLIB2_CFLAGS@ \
	@FUSE_CFLAGS@ \
	-Wall

bin_PROGRAMS = syncwerk-server-fuse

noinst_HEADERS = syncwerk-server-fuse.h syncwerk-session.h repo-mgr.h

syncwerk_server_fuse_SOURCES = syncwerk-server-fuse.c \
                    syncwerk-session.c \
		    file.c \
		    getattr.c \
                    readdir.c \
                    repo-mgr.c \
                    ../common/block-mgr.c \
                    ../common/block-backend.c \
                    ../common/block-backend-fs.c \
                    ../common/branch-mgr.c \
                    ../common/commit-mgr.c \
                    ../common/fs-mgr.c \
                    ../common/log.c \
                    ../common/syncwerk-server-db.c \
                    ../common/syncwerk-server-utils.c \
                    ../common/obj-store.c \
                    ../common/obj-backend-fs.c \
                    ../common/obj-backend-riak.c \
                    ../common/syncwerk-crypt.c

syncwerk_server_fuse_LDADD = @CCNET_LIBS@ \
		  @GLIB2_LIBS@ @GOBJECT_LIBS@ @SSL_LIBS@ @LIB_RT@ @LIB_UUID@ \
                  -lsqlite3 @LIBEVENT_LIBS@ \
		  $(top_builddir)/common/cdc/libcdc.la \
		  $(top_builddir)/common/db-wrapper/libdbwrapper.la \
		  @RPCSYNCWERK_LIBS@ @JANSSON_LIBS@ @FUSE_LIBS@ @ZLIB_LIBS@ \
		  @MYSQL_LIBS@ @PGSQL_LIBS@

