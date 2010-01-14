cmake_minimum_required(VERSION 2.6)

project(FSter)
set (V_MAJOR 0)
set (V_MINOR 1)

add_subdirectory(src)
add_subdirectory(content-plugins)

CONFIGURE_FILE(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
  IMMEDIATE @ONLY)

ADD_CUSTOM_TARGET(uninstall
  "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")

IF (NOT CMAKE_INSTALL_PREFIX)
  SET(CMAKE_INSTALL_PREFIX "/usr/local/")
ENDIF (NOT CMAKE_INSTALL_PREFIX)

INSTALL(CODE "FILE(MAKE_DIRECTORY \$ENV{DESTDIR}/etc/fster)")
INSTALL(CODE "FILE(MAKE_DIRECTORY \$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/fster/plugins)")
