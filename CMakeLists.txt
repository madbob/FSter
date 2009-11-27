# Setup Cmake
set(CMAKE_MODULE_PATH
    ${Guglielmo_SOURCE_DIR}/cmake/Modules
    )

find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE REQUIRED fuse)
pkg_check_modules(GLIB2 REQUIRED glib-2.0)
pkg_check_modules(GIO REQUIRED gio-2.0)
pkg_check_modules(GOBJECT2 REQUIRED gobject-2.0)
pkg_check_modules(DBUSGLIB REQUIRED dbus-glib-1)
pkg_check_modules(JSONGLIB REQUIRED json-glib-1.0)
pkg_check_modules(GTHREAD2 REQUIRED gthread-2.0)
pkg_check_modules(DAEMON REQUIRED libdaemon)
pkg_check_modules(LIBXML REQUIRED libxml-2.0)

ADD_DEFINITIONS(
    -D_FILE_OFFSET_BITS=64
    -Wall
    )

set(avfs_SRCS
    fuse.c
    hierarchy.c
    )

set(avfsdo_SRCS 
    avfs_do.c
    )

set(avfs_HEADERS 
    common.h
    hierarchy.h
    )

link_directories(
    ${FUSE_LIBRARY_DIRS}
    ${GLIB2_LIBRARY_DIRS}
    ${GIO_LIBRARY_DIRS}
    ${GOBJECT2_LIBRARY_DIRS}
    ${DBUSGLIB_LIBRARY_DIRS}
    ${JSONGLIB_LIBRARY_DIRS}
    ${GTHREAD2_LIBRARY_DIRS}
    ${DAEMON_LIBRARY_DIRS}
    ${LIBXML_LIBRARY_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/guglielmo-glib
    )

include_directories(
    ${FUSE_INCLUDE_DIRS}
    ${GLIB2_INCLUDE_DIRS}
    ${GIO_INCLUDE_DIRS}
    ${GOBJECT2_INCLUDE_DIRS}
    ${DBUSGLIB_INCLUDE_DIRS}
    ${JSONGLIB_INCLUDE_DIRS}
    ${GTHREAD2_INCLUDE_DIRS}
    ${DAEMON_INCLUDE_DIRS}
    ${LIBXML_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/guglielmo-glib
    )

add_executable(avfs ${avfs_SRCS})
target_link_libraries(avfs
    ${LIBXML_LIBRARIES}
    ${GIO_LIBRARIES}
    fuse
    guglielmo-glib
    )

add_executable(avfsdo ${avfsdo_SRCS})
target_link_libraries(avfsdo
    ${GLIB2_LIBRARIES}
    ${DAEMON_LIBRARIES}
    )

install(TARGETS avfs DESTINATION bin)

INSTALL(CODE "FILE(MAKE_DIRECTORY etc/guglielmo/avfs)")
INSTALL(FILES config/avfs.xml DESTINATION etc/guglielmo/avfs/)
