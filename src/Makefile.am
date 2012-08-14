NULL =

INCLUDES = \
	-DPREFIX=\""$(prefix)"\"				\
	-DLIBDIR=\""$(libdir)"\"				\
	-DDATADIR=\""$(datadir)"\"				\
	$(NULL)

common_cflags = \
	-rdynamic \
	-I$(top_srcdir) \
	$(FSTER_CFLAGS) \
	$(NULL)

common_ldadd = \
	$(FSTER_LIBS) \
	$(NULL)

bin_PROGRAMS = fster

fster_SOURCES = \
	common.h \
	contents-plugin.c \
	contents-plugin.h \
	core.h \
	fuse.c \
	gfuse-loop.c \
	gfuse-loop.h \
	hierarchy.c \
	hierarchy.h \
	hierarchy-node.c \
	hierarchy-node.h \
	item-handler.c \
	item-handler.h \
	nodes-cache.c \
	nodes-cache.h \
	property.c \
	property.h \
	property-handler.c \
	property-handler.h \
	utils.c \
	utils.h

fster_CFLAGS = $(common_cflags)
fster_LDADD = $(common_ldadd)

