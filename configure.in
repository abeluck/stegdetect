dnl Process this file with autoconf to produce a configure script.
AC_INIT(stegdetect.c, 0.6, provos@citi.umich.edu)
AC_CANONICAL_TARGET
AC_CONFIG_SRCDIR(stegdetect.c)

AM_INIT_AUTOMAKE(stegdetect, 0.6)
AM_CONFIG_HEADER(config.h)

dnl Initialize prefix.
if test "$prefix" = "NONE"; then
   prefix="/usr/local"
fi

dnl Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_C_INLINE
AC_PROG_INSTALL

dnl Checks for libraries.
AC_MSG_CHECKING(blowfish object)
AC_SUBST(BFOBJ)
case "$target" in
i386-*-*)
 AC_MSG_RESULT(bf-586.o)
 BFOBJ=bf-586.o ;;
*)
 AC_MSG_RESULT(bf_enc.o)
 BFOBJ=bf_enc.o;;
esac

dnl Checking for gtk
AC_PATH_PROG(PATH_GTKCONFIG, gtk-config)
if test ! -z "$PATH_GTKCONFIG" ; then
	dnl Checks for libevent
	havelibevent=no
	AC_MSG_CHECKING(for libevent)
	AC_ARG_WITH(libevent,
	[  --with-libevent=DIR      use libevent build directory],
	[ case "$withval" in
	  yes|no)
	     AC_MSG_RESULT(no)
	     ;;
	  *)
	     AC_MSG_RESULT($withval)
	     if test -f $withval/event.h -a -f $withval/libevent.a; then
	        owd=`pwd`
	        if cd $withval; then withval=`pwd`; cd $owd; fi
	        EVENTINC="-I$withval"
	        EVENTLIB="-L$withval -levent"
		havelibevent=yes
	     else
	        AC_ERROR(event.h or libevent.a not found in $withval)
	     fi
	     ;;
	  esac ],
	[ if test -f ${prefix}/include/event.h; then
	     EVENTINC="-I${prefix}/include"
	     EVENTLIB="-L${prefix}/lib -levent"
		havelibevent=yes
	     AC_MSG_RESULT(yes)
	  elif test -f /usr/include/event/event.h; then
	     EVENTINC="-I/usr/include/event"
	     EVENTLIB="-levent"
	     havelibevent=yes
	     AC_MSG_RESULT(yes)
	  elif test -f /usr/include/event.h; then
	     EVENTLIB="-levent"
	     havelibevent=yes
	     AC_MSG_RESULT(yes)
	  else
	     AC_MSG_RESULT(no)
	  fi
	]
	)

	if test $havelibevent = yes; then
		AC_SUBST(EVENTINC)
		AC_SUBST(EVENTLIB)

		GTKINC=`$PATH_GTKCONFIG --cflags`
		GTKLIB=`$PATH_GTKCONFIG --libs`
		AC_SUBST(GTKINC)
		AC_SUBST(GTKLIB)
		XSTEG="xsteg"
		AC_SUBST(XSTEG)	
	fi
fi


dnl Checks for header files.
AC_HEADER_STDC

dnl Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_CHECK_TYPE(u_int32_t, unsigned int)
AC_CHECK_TYPE(u_int16_t, unsigned short)
AC_CHECK_TYPE(u_int8_t, unsigned char)

dnl Checks for library functions.
AC_REPLACE_FUNCS(strlcat strlcpy dirname)

if test "x$ac_cv_func_dirname" = "xyes"; then
	AC_MSG_CHECKING(for working dirname)
	AC_TRY_RUN(
#include <sys/types.h>
#include <string.h>
int
main(int argc, char **argv)
{
	char *name = "solaris/sucks";
	char *p;
	int len = strlen(name);
	p = dirname(name);
	exit(len != strlen(name));
}, AC_MSG_RESULT(yes), [
	AC_MSG_RESULT(no)
	LIBOBJS="$LIBOBJS dirname.o"
], AC_MSG_RESULT(yes))
fi

AC_CHECK_FUNCS(strerror)

needmd5=no
AC_CHECK_FUNCS(MD5Update, , [needmd5=yes])
if test $needmd5 = yes; then
   LIBOBJS="$LIBOBJS md5.o"
fi
neederr=no
AC_CHECK_FUNCS(warnx, , [neederr=yes])
if test $neederr = yes; then
   LIBOBJS="$LIBOBJS err.o"
fi

AC_MSG_CHECKING([for timeradd in sys/time.h])
AC_EGREP_CPP(yes,
[
#include <sys/time.h>
#ifdef timeradd
yes
#endif
],      [ AC_DEFINE(HAVE_TIMERADD)
          AC_MSG_RESULT([yes])], AC_MSG_RESULT([no])
)

dnl Other stuff
AC_DEFINE_UNQUOTED(_PATH_RULES, "$prefix/share/stegbreak/rules.ini")
AC_C_BIGENDIAN

AC_CONFIG_SUBDIRS(jpeg-6b file)

AC_OUTPUT(Makefile)
