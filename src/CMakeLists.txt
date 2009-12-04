set(CMAKE_MODULE_PATH
    ${FSter_SOURCE_DIR}/cmake/Modules
    )


find_package(Tracker REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE REQUIRED fuse)
pkg_check_modules(GIO REQUIRED gio-2.0)
pkg_check_modules(GTHREAD2 REQUIRED gthread-2.0)
pkg_check_modules(DBUSGLIB REQUIRED dbus-glib-1)
pkg_check_modules(LIBXML REQUIRED libxml-2.0)
pkg_check_modules(TRACKER_CLIENT REQUIRED tracker-client-0.7)

ADD_DEFINITIONS(
    -D_FILE_OFFSET_BITS=64
    -Wall
    )

CONFIGURE_FILE(
    ${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/config.h
)

set(fster_SRCS
    fuse.c
    utils.c
    hierarchy.c
    property-handler.c
    item-handler.c
    hierarchy-node.c
    nodes-cache.c
    contents-plugin.c
    )

set(fster_HEADERS
    core.h
    common.h
    utils.h
    hierarchy.h
    property-handler.h
    item-handler.h
    hierarchy-node.h
    nodes-cache.h
    contents-plugin.h
    )

link_directories(
    ${FUSE_LIBRARY_DIRS}
    ${GIO_LIBRARY_DIRS}
    ${GTHREAD2_LIBRARY_DIRS}
    ${DBUSGLIB_LIBRARY_DIRS}
    ${LIBXML_LIBRARY_DIRS}
    ${TRACKER_LIBRARY_DIRS}
    ${TRACKER_CLIENT_LIBRARY_DIRS}
    )

include_directories(
    ${FUSE_INCLUDE_DIRS}
    ${GIO_INCLUDE_DIRS}
    ${GTHREAD2_INCLUDE_DIRS}
    ${DBUSGLIB_INCLUDE_DIRS}
    ${LIBXML_INCLUDE_DIRS}
    ${TRACKER_INCLUDE_DIRS}
    ${TRACKER_CLIENT_INCLUDE_DIRS}
    )

add_executable(fster ${fster_SRCS})

target_link_libraries(fster
    fuse
    ${GIO_LIBRARIES}
    ${DBUSGLIB_LIBRARIES}
    ${LIBXML_LIBRARIES}
    ${TRACKER_LIBRARIES}
    ${TRACKER_CLIENT_LIBRARIES}
    )

install(TARGETS fster DESTINATION bin)
