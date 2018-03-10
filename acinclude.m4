# -*- autoconf -*-

dnl PS_SET_DEFAULT_PREFIX
AC_DEFUN([PS_SET_DEFAULT_PREFIX],[
	prefix=`pwd`/build
	if test -d "$prefix"; then
		AC_MSG_NOTICE([Set default prefix to $prefix])
	else
		mkdir -p "$prefix"
		AC_MSG_NOTICE([Set default prefix to $prefix])
	fi])

dnl PS_CHECK_DPDK
AC_DEFUN([PS_CHECK_DPDK],[
	AC_ARG_WITH([dpdk],
				[AC_HELP_STRING([--with-dpdk=/path/to/dpdk],
								[Specify the DPDK build directroy])],
				[dpdk_spec=true])

	DPDK_AUTOLD=false
	AC_MSG_CHECKING([Whether dpdk datapath is specified])
	if test "$dpdk_spec" != true || test "$with_dpdk" = no; then
		AC_MSG_RESULT([no])
		if test -n "$RTE_SDK" && test -n "$RTE_TARGET"; then
			DPDK_PATH="$RTE_SDK/$RTE_TARGET"
			AC_MSG_NOTICE([RTE_SDK and RTE_TARGET exist, set dpdkpath to $DPDK_PATH])
			DPDK_INCLUDE="$DPDK_PATH/include"
			DPDK_LIB="$DPDK_PATH/lib"
		else
			AC_MSG_NOTICE([Set dpdk path to /usr/local/])
			DPDK_INCLUDE=/usr/local/include/dpdk
			DPDK_LIB=/usr/local/lib
			DPDK_AUTOLD=true
		fi
	else
		AC_MSG_RESULT([yes])
		DPDK_PATH="$with_dpdk"
		DPDK_INCLUDE="$DPDK_PATH/include"
		DPDK_LIB="$DPDK_PATH/lib"
	fi

	AC_MSG_CHECKING([Check whether dpdk headers are installed])
	if test -f "$DPDK_INCLUDE/rte_config.h"; then
		AC_MSG_RESULT([yes])
	else
		if test -f "$DPDK_INCLUDE/dpdk/rte_config.h"; then
			AC_MSG_RESULT([yes])
			DPDK_INCLIDE="$DPDK_INCLUDE/dpdk"
		else
			AC_MSG_RESULT([no])
			AC_ERROR([No dpdk headers found])
		fi
	fi

	AC_MSG_CHECKING([Check whether dpdk libraries are installed])
	if test -f "$DPDK_LIB/libdpdk.a"; then
		AC_MSG_RESULT([yes])
	else
		AC_MSG_RESULT([no])
		AC_ERROR([No dpdk libraries found])
	fi

	PS_CFLAGS="$PS_CFLAGS -I$DPDK_INCLUDE -mssse3"
	if test "$DPDK_AUTOLD" = "false"; then
		PS_LDFLAGS="$PS_LDFLAGS -L${DPDK_LIB}"
	fi
	DPDK_LDFLAGS=-Wl,--whole-archive,-ldpdk,--no-whole-archive
	AC_SUBST([DPDK_LDFLAGS])
])

dnl Checks for --enable-debug and defines PS_DEBUG if it is specified.
AC_DEFUN([PS_CHECK_DEBUG],
	[AC_ARG_ENABLE(
		[debug],
		[AC_HELP_STRING([--enable-debug],
			[Enable debugging features])],
		[case "${enableval}" in
			(yes) debug=true ;;
			(no)  debug=false ;;
			(*) AC_MSG_ERROR([bad value ${enableval} for --enable-debug]) ;;
		esac],
		[debug=false])
	AM_CONDITIONAL([PS_DEBUG], [test x$debug = xtrue])
])

dnl Checks for --enable-nic-time and defines PT_NIC_TIMESTAMP if it is specified.
AC_DEFUN([PS_CHECK_NIC_TIME],
	[AC_ARG_ENABLE(
		[nic_time],
		[AC_HELP_STRING([--enable-nic-time],
			[Use NIC timestamps instead of cpu cycles])],
		[case "${enableval}" in
			(yes) nic_time=true ;;
			(no)  nic_time=false ;;
			(*) AC_MSG_ERROR([bad value ${enableval} for --enable-nic-time]) ;;
		esac],
		[nic_time=false])
	AM_CONDITIONAL([PT_NIC_TIMESTAMP], [test x$nic_time = xtrue])
])


