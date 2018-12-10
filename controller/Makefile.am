bin_PROGRAMS = syncwerk-controller

AM_CFLAGS = \
	-DSYNCWERK_SERVER \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib \
	-I$(top_builddir)/lib \
	-I$(top_srcdir)/common \
	@CCNET_CFLAGS@ \
	@RPCSYNCWERK_CFLAGS@ \
	@GLIB2_CFLAGS@ \
	-Wall

noinst_HEADERS = syncwerk-controller.h ../common/log.h

syncwerk_controller_SOURCES = syncwerk-controller.c ../common/log.c

syncwerk_controller_LDADD = @CCNET_LIBS@ \
	$(top_builddir)/lib/libsyncwerk_common.la \
	@GLIB2_LIBS@  @GOBJECT_LIBS@ @SSL_LIBS@ @LIB_RT@ @LIB_UUID@ @LIBEVENT_LIBS@ \
	@RPCSYNCWERK_LIBS@ @JANSSON_LIBS@ @ZLIB_LIBS@
