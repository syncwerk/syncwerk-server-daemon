pcfiles = libsyncwerk.pc
pkgconfig_DATA = $(pcfiles)
pkgconfigdir = $(libdir)/pkgconfig

AM_CPPFLAGS = @GLIB2_CFLAGS@ -I$(top_srcdir)/include \
	-I$(top_srcdir)/lib \
	-I$(top_srcdir)/common \
	@CCNET_CFLAGS@ \
	@RPCSYNCWERK_CFLAGS@ \
	@MSVC_CFLAGS@ \
	-Wall

BUILT_SOURCES = gensource

## source file rules
syncwerk_object_define = repo.vala commit.vala dirent.vala dir.vala \
	task.vala branch.vala crypt.vala webaccess.vala copy-task.vala

syncwerk_object_gen = $(syncwerk_object_define:.vala=.c)

valac_gen = ${syncwerk_object_gen} syncwerk-object.h

EXTRA_DIST = ${syncwerk_object_define} rpc_table.py $(pcfiles) vala.stamp

utils_headers = net.h bloom-filter.h utils.h db.h

utils_srcs = $(utils_headers:.h=.c)

noinst_HEADERS = ${utils_headers} include.h

syncwerkdir = $(includedir)/syncwerk
syncwerk_HEADERS = syncwerk-object.h

syncwerk-object.h: ${syncwerk_object_define}
	rm -f $@
	valac --pkg posix ${syncwerk_object_define} -C -H syncwerk-object.h

DISTCLEANFILES = ${rpcsyncwerk_gen}

## library rules

noinst_LTLIBRARIES = libsyncwerk_common.la

libsyncwerk_common_la_SOURCES = ${syncwerk_object_gen} ${utils_srcs}
libsyncwerk_common_la_LDFLAGS = -no-undefined
libsyncwerk_common_la_LIBADD = @GLIB2_LIBS@  @GOBJECT_LIBS@ @SSL_LIBS@ -lcrypto @LIB_GDI32@ \
				     @LIB_UUID@ @LIB_WS32@ @LIB_PSAPI@ -lsqlite3 \
					 @LIBEVENT_LIBS@ @RPCSYNCWERK_LIBS@ @LIB_SHELL32@ \
	@ZLIB_LIBS@

rpcsyncwerk_gen = rpcsyncwerk-signature.h rpcsyncwerk-marshal.h

gensource: ${rpcsyncwerk_gen} ${valac_gen}

rpc_table.stamp: ${top_srcdir}/lib/rpc_table.py
	@rm -f rpc_table.tmp
	@touch rpc_table.tmp
	@echo "[librpcsyncwerk]: generating rpc header files"
	@PYTHON@ `which rpcsyncwerk-codegen.py` ${top_srcdir}/lib/rpc_table.py
	@echo "[librpcsyncwerk]: done"
	@mv -f rpc_table.tmp $@

${rpcsyncwerk_gen}: rpc_table.stamp

vala.stamp: ${syncwerk_object_define}
	rm -f ${syncwerk_object_gen}
	@rm -f vala.tmp
	@touch vala.tmp
	valac -C --pkg posix $^
	@mv -f vala.tmp $@

${syncwerk_object_gen}: vala.stamp

clean-local:
	rm -f ${rpcsyncwerk_gen}
	rm -f rpc_table.pyc
	rm -f rpc_table.stamp
	rm -f rpc_table.tmp
	rm -f vala.tmp vala.stamp ${valac_gen}

install-data-local:
if MACOS
	sed -i '' -e "s|(DESTDIR)|${DESTDIR}|g" $(pcfiles)
else
	${SED} -i "s|(DESTDIR)|${DESTDIR}|g" $(pcfiles)
endif
