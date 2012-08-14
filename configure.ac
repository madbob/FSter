m4_define([fster_major_version], [0])
m4_define([fster_minor_version], [2])
m4_define([fster_micro_version], [0])

m4_define([fster_version],
          [fster_major_version.fster_minor_version.fster_micro_version])

m4_define([fster_interface_age], [0])
m4_define([fster_binary_age],
          [m4_eval(100 * fster_minor_version + fster_micro_version)])

m4_define([lt_current],
          [m4_eval(100 * fster_minor_version + fster_micro_version - fster_interface_age)])
m4_define([lt_revision], [fster_interface_age])
m4_define([lt_age],
          [m4_eval(fster_binary_age - fster_interface_age)])

m4_define([fuse_req_version], [2.9.0])
m4_define([gio_req_version], [2.32.3])
m4_define([gthread_req_version], [2.32.3])
m4_define([xml_req_version], [2.8.0])

AC_PREREQ([2.59])
AC_INIT([fster], [fster_version], [], [fster])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([1.10])

GNOME_COMMON_INIT
GNOME_COMPILE_WARNINGS
GNOME_DEBUG_CHECK

IT_PROG_INTLTOOL([0.35.0])

AM_DISABLE_STATIC
AM_PATH_GLIB_2_0
AM_PROG_CC_C_O
AM_PROG_LIBTOOL

AC_HEADER_STDC
AC_C_CONST

FSTER_VERSION=fster_version
AC_SUBST(FSTER_VERSION)

dnl fster checks
PKG_CHECK_MODULES(FSTER,
                  fuse >= fuse_req_version dnl
                  gio-2.0 >= gio_req_version dnl
                  gthread-2.0 >= gthread_req_version dnl
                  libxml-2.0 >= xml_req_version)
AC_SUBST(FSTER_CFLAGS)
AC_SUBST(FSTER_LIBS)

dnl = Enable debug level ===================================================

m4_define([debug_default],
          m4_if(m4_eval(fster_minor_version % 2), [1], [yes], [minimum]))

AC_ARG_ENABLE(debug,
              AC_HELP_STRING([--enable-debug=@<:@no/minimum/yes@:>@],
                             [turn on debugging @<:@default=debug_default@:>@]),
,
              enable_debug=debug_default)

if test "x$enable_debug" = "xyes"; then
  test "$cflags_set" = set || CFLAGS="$CFLAGS -g"
  FSTER_DEBUG_CFLAGS="-DFSTER_ENABLE_DEBUG"
else
  if test "x$enable_debug" = "xno"; then
    FSTER_DEBUG_CFLAGS="-DG_DISABLE_ASSERT -DG_DISABLE_CHECKS -DG_DISABLE_CAST_CHECKS"
  else # minimum
    FSTER_DEBUG_CFLAGS="-DFSTER_ENABLE_DEBUG -DG_DISABLE_CAST_CHECKS"
  fi
fi

AC_SUBST(FSTER_DEBUG_CFLAGS)

dnl = Enable strict compiler flags =========================================

# use strict compiler flags only on development releases
#m4_define([maintainer_flags_default],
#          m4_if(m4_eval(fster_minor_version % 2), [1], [yes], [no]))
m4_define([maintainer_flags_default], [no])
AC_ARG_ENABLE([maintainer-flags],
              AC_HELP_STRING([--enable-maintainer-flags=@<:@no/yes@:>@],
                             [Use strict compiler flags @<:@default=maintainer_flags_default@:>@]),,
              enable_maintainer_flags=maintainer_flags_default)

if test "x$enable_maintainer_flags" = "xyes"; then
  CPPFLAGS="$CPPFLAGS -g -Wall -Wshadow -Wcast-align -Wno-uninitialized -Werror"
else
  CPPFLAGS="$CPPFLAGS -g -Wall"
fi

plugindir='${libdir}/fster/plugins/'
AC_SUBST(plugindir)
CFLAGS="$CFLAGS '-DPLUGIN_DIR=\"\$(plugindir)\"'"

AC_CONFIG_FILES([
        Makefile
        src/Makefile
        content-plugins/Makefile
        po/Makefile.in
])

AC_OUTPUT

echo ""
echo "                  fster: $VERSION"
echo ""
echo "                 Prefix: ${prefix}"
echo "         Compiler flags: ${CPPFLAGS}"
echo ""
