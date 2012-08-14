plugindir = @plugindir@

NULL =
LIBS =

INCLUDES = \
	-I$(top_srcdir)/src \
	-DPREFIX=\""$(prefix)"\" \
	-DLIBDIR=\""$(libdir)"\" \
	-DG_DISABLE_DEPRECATED \
	$(FSTER_CFLAGS)

plugin_LTLIBRARIES = dumpmetadata_plugin.la real_plugin.la

real_plugin_la_SOURCES = \
	contents-real-plugin.c \
	contents-real-plugin.h

real_plugin_la_LDFLAGS = -avoid-version -module -shared
real_plugin_la_LIBADD = $(FSTER_LIBS)
real_plugin_la_CFLAGS = $(INCLUDES)

dumpmetadata_plugin_la_SOURCES = \
	contents-dumpmetadata-plugin.c \
	contents-dumpmetadata-plugin.h

dumpmetadata_plugin_la_LDFLAGS = -avoid-version -module -shared
dumpmetadata_plugin_la_LIBADD = $(FSTER_LIBS)
dumpmetadata_plugin_la_CFLAGS = $(INCLUDES)

