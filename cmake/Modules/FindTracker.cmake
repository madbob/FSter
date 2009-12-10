#
# Find an installation of Tracker
#
# Sets the following variables:
#  Tracker_FOUND            - true is Tracker has been found
#  TRACKER_INCLUDE_DIR      - The include directory
#  TRACKER_LIBRARIES        - The Tracker core library to link to (tracker-common)
#  TRACKER_VERSION          - The Tracker version (string value)
#
# Options:
#  Set TRACKER_MIN_VERSION to set the minimum required Tracker version (default: 0.7)
#

#if(TRACKER_INCLUDE_DIR AND TRACKER_LIBRARIES)

  # read from cache
#  set(Tracker_FOUND TRUE)

#else(TRACKER_INCLUDE_DIR AND TRACKER_LIBRARIES)

  set(TRACKER_SUFFIX tracker-0.7)
  include(CustomFind)
  message (STATUS "ECCO    /usr/include/${TRACKER_SUFFIX}")
  
  custom_find_path(TRACKER_INCLUDE_DIR 
    NAMES libtracker-common/tracker-common.h
    PATHS
    ${TRACKER_PREFIX}/include
    ${INCLUDE_INSTALL_DIR}/tracker
    /usr/include/${TRACKER_SUFFIX}
    /usr/local/include/${TRACKER_SUFFIX}
    )

  custom_find_library(TRACKER_LIBRARIES
    NAMES tracker-common
    PATHS
    ${TRACKER_PREFIX}/lib
    ${LIB_INSTALL_DIR}
    /usr/lib/${TRACKER_SUFFIX}
    /usr/local/lib/${TRACKER_SUFFIX}
    )

  # check for all the libs as required to make sure that we do not try to compile with an old version

  if(TRACKER_INCLUDE_DIR AND TRACKER_LIBRARIES)
    set(Tracker_FOUND TRUE)
  else(TRACKER_INCLUDE_DIR AND TRACKER_LIBRARIES)
    message(STATUS "${TRACKER_PREFIX} does not contain Tracker")
  endif(TRACKER_INCLUDE_DIR AND TRACKER_LIBRARIES)

  # check Tracker version

  # We set a default for the minimum required version to be backwards compatible
  if(NOT TRACKER_MIN_VERSION)
    set(TRACKER_MIN_VERSION "0.7")
  endif(NOT TRACKER_MIN_VERSION)

  if(Tracker_FOUND)
    if(NOT Tracker_FIND_QUIETLY)
      message(STATUS "Found Tracker: ${TRACKER_LIBRARIES}")
      message(STATUS "Found Tracker includes: ${TRACKER_INCLUDE_DIR}")
    endif(NOT Tracker_FIND_QUIETLY)
  else(Tracker_FOUND)
    if(Tracker_FIND_REQUIRED)
      if(NOT TRACKER_INCLUDE_DIR)
        message(FATAL_ERROR "Could not find Tracker includes.")
      endif(NOT TRACKER_INCLUDE_DIR)
      if(NOT TRACKER_LIBRARIES)
        message(FATAL_ERROR "Could not find Tracker library.")
      endif(NOT TRACKER_LIBRARIES)
    else(Tracker_FIND_REQUIRED)
      if(NOT TRACKER_INCLUDE_DIR)
        message(STATUS "Could not find Tracker includes.")
      endif(NOT TRACKER_INCLUDE_DIR)
      if(NOT TRACKER_LIBRARIES)
        message(STATUS "Could not find Tracker library.")
      endif(NOT TRACKER_LIBRARIES)
    endif(Tracker_FIND_REQUIRED)
  endif(Tracker_FOUND)

mark_as_advanced(TRACKER_LIBRARIES
                 TRACKER_INCLUDE_DIR)
message (STATUS "S:: ${TRACKER_LIBRARIES}")

#endif(TRACKER_INCLUDE_DIR AND TRACKER_LIBRARIES AND TRACKER_INDEX_LIBRARIES AND TRACKER_SERVER_LIBRARIES)
