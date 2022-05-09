#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.71])
AC_INIT([chuid], [1.0], [info@fkink.de])
AC_CONFIG_SRCDIR([src/chuid.c])
AC_CONFIG_HEADERS([config.h])

PKG_PROG_PKG_CONFIG

CHUID_FEATURE_STRING="$PACKAGE_NAME $PACKAGE_VERSION:"

# Checks for programs.
AC_LANG(C)
AC_PROG_CC

AC_ARG_ENABLE([debug], [AS_HELP_STRING([--enable-debug],
             [produce debugging binaries and libraries @<:@default=no@:>@])],,
             [enable_debug=no])
enable_debug=`echo $enable_debug`
case $enable_debug in
  yes) CFLAGS=`echo $CFLAGS | sed -e 's/ -O[[0-9s]]*//g'`;AC_MSG_RESULT(enabling debug symbols $CFLAGS) ;;
  no)  CFLAGS=`echo $CFLAGS | sed 's/-g //g'`;AC_MSG_RESULT(disabling debug symbols $CFLAGS) ;;
  *)   AC_MSG_ERROR([unknown option '$enable_debug' for --enable-debug]);;
esac
AM_CONDITIONAL([DEBUG], [test x"$enable_debug" == x"yes" ])

# Checks for libraries.
AC_CHECK_LIB([m], [ceil])
AC_CHECK_LIB([pthread], [pthread_mutex_lock])
AC_SEARCH_LIBS([umem_reap], [umem], [], [
  AC_MSG_ERROR([not using libumem])
])

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h strings.h sys/time.h unistd.h utime.h limits.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_STRUCT_ST_BLOCKS

# Checks for library functions.
AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK
AC_FUNC_MALLOC
AC_FUNC_STRERROR_R
AC_FUNC_STRTOD
AC_CHECK_FUNCS([gettimeofday localtime_r memmove memset setlocale strcasecmp strchr strdup strerror strrchr strtol utime])

AC_CONFIG_AUX_DIR([build-aux])

AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])

AC_CONFIG_FILES([Makefile
		man/Makefile
		src/Makefile])
dnl AC_MSG_RESULT(echo "$options" "$all_options" | tr ' ' '\n' | sort | uniq -u | tr '\n' ' ')
AC_OUTPUT
