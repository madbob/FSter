find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(LIBXML REQUIRED libxml-2.0)
pkg_check_modules(TRACKER REQUIRED tracker-client-0.7)

ADD_DEFINITIONS(
    -Wall
    )

set(real_plugin_SRCS contents-real-plugin.c)
set(real_plugin_HEADERS contents-real-plugin.h)
add_library(real_plugin MODULE ${real_plugin_SRCS})

set(dumpmetadata_plugin_SRCS contents-dumpmetadata-plugin.c)
set(dumpmetadata_plugin_HEADERS contents-dumpmetadata-plugin.h)
add_library(dumpmetadata_plugin MODULE ${dumpmetadata_plugin_SRCS})

link_directories(
    ${GLIB_LIBRARY_DIRS}
    ${LIBXML_LIBRARY_DIRS}
    ${TRACKER_CLIENT_LIBRARY_DIRS}
    ${TRACKER_LIBRARY_DIRS}
    )

include_directories(
    ${GLIB_INCLUDE_DIRS}
    ${LIBXML_INCLUDE_DIRS}
    ${TRACKER_CLIENT_INCLUDE_DIRS}
    ${TRACKER_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/
    )

install(TARGETS real_plugin DESTINATION lib/fster/plugins)
install(TARGETS dumpmetadata_plugin DESTINATION lib/fster/plugins)
