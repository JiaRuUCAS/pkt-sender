#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT(pkt-sender, 0.1)
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE

AC_SUBST([PS_CFLAGS])
AC_SUBST([PS_LDFLAGS])

# Checks for programs.
AC_PROG_CC
AC_PROG_MKDIR_P
AC_PROG_RANLIB

# Checks for libraries.
AC_SEARCH_LIBS([pthread_create], [pthread])
AC_SEARCH_LIBS([dlopen], [dl])
AC_SEARCH_LIBS([pow], [m])

PS_CHECK_DPDK

# Checks for header files.
AC_CHECK_HEADERS([stdint.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

# Set default prefix to $(curdir)/build
PS_SET_DEFAULT_PREFIX

# Check debug
PS_CHECK_DEBUG

# Check if use NIC time instead of cpu cycles
PS_CHECK_NIC_TIME

AC_CONFIG_FILES(Makefile)

AC_OUTPUT
