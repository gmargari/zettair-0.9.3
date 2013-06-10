# This file gets processed with autoconf to produce a configure script
AC_INIT(zettair, 0.9.3, zettair@cs.rmit.edu.au)

# calculate variables for target system
AC_CANONICAL_TARGET()

# check for valgrind
AC_ARG_WITH(valgrind, 
  AC_HELP_STRING([--with-valgrind[=DIR]], [integrate valgrind support for better error detetion]))

if test "x$with_valgrind" != "x"; then
    AC_DEFINE(WITH_VALGRIND, 1, [indicates whether to use valgrind])
    # check in various locations for valgrind headers

    # note that we use CHECK_FILES instead of CHECK_HEADERS, because
    # CHECK_HEADERS tries to compile the headers, which fails and
    # causes confusion

    if test "x$with_valgrind" != "xyes"; then

        # also note that trying to get valgrind_include, HAVE_VALGRIND_H
        # and HAVE_MEMCHECK_H defined properly in this ridiculous
        # frankenstein of a so-called programming environment is absurdly
        # complicated, hence the mess below
        path=$with_valgrind
        if test "x$valgrind_include" == "x"; then
            AC_CHECK_FILE($path/valgrind.h, [valgrind_include=$path])
            if test "x$valgrind_include" != "x"; then
                AC_CHECK_FILE($path/memcheck.h, [memcheck=yes])
                if test "x$memcheck" == "xyes"; then
                    AC_DEFINE(HAVE_MEMCHECK_H, 1, [memcheck.h])
                fi
                AC_DEFINE(HAVE_VALGRIND_H, 1, [valgrind.h])
            fi
        fi

        path=$with_valgrind/include
        if test "x$valgrind_include" == "x"; then
            AC_CHECK_FILE($path/valgrind.h, [valgrind_include=$path])
            if test "x$valgrind_include" != "x"; then
                AC_CHECK_FILE($path/memcheck.h, [memcheck=yes])
                if test "x$memcheck" == "xyes"; then
                    AC_DEFINE(HAVE_MEMCHECK_H, 1, [memcheck.h])
                fi
                AC_DEFINE(HAVE_VALGRIND_H, 1, [valgrind.h])
            fi
        fi

        path=$with_valgrind/include/valgrind
        if test "x$valgrind_include" == "x"; then
            AC_CHECK_FILE($path/valgrind.h, [valgrind_include=$path])
            if test "x$valgrind_include" != "x"; then
                AC_CHECK_FILE($path/memcheck.h, [memcheck=yes])
                if test "x$memcheck" == "xyes"; then
                    AC_DEFINE(HAVE_MEMCHECK_H, 1, [memcheck.h])
                fi
                AC_DEFINE(HAVE_VALGRIND_H, 1, [valgrind.h])
            fi
        fi

        path=$with_valgrind/valgrind
        if test "x$valgrind_include" == "x"; then
            AC_CHECK_FILE($path/valgrind.h, [valgrind_include=$path])
            if test "x$valgrind_include" != "x"; then
                AC_CHECK_FILE($path/memcheck.h, [memcheck=yes])
                if test "x$memcheck" == "xyes"; then
                    AC_DEFINE(HAVE_MEMCHECK_H, 1, [memcheck.h])
                fi
                AC_DEFINE(HAVE_VALGRIND_H, 1, [valgrind.h])
            fi
        fi
    fi

    path=/usr/include/valgrind
    if test "x$valgrind_include" == "x"; then
        AC_CHECK_FILE($path/valgrind.h, [valgrind_include=$path])
        if test "x$valgrind_include" != "x"; then
            AC_CHECK_FILE($path/memcheck.h, [memcheck=yes])
            if test "x$memcheck" == "xyes"; then
                AC_DEFINE(HAVE_MEMCHECK_H, 1, [memcheck.h])
            fi
            AC_DEFINE(HAVE_VALGRIND_H, 1, [valgrind.h])
        fi
    fi

    path=/usr/include/
    if test "x$valgrind_include" == "x"; then
        AC_CHECK_FILE($path/valgrind.h, [valgrind_include=$path])
        if test "x$valgrind_include" != "x"; then
            AC_CHECK_FILE($path/memcheck.h, [memcheck=yes])
            if test "x$memcheck" == "xyes"; then
                AC_DEFINE(HAVE_MEMCHECK_H, 1, [memcheck.h])
            fi
            AC_DEFINE(HAVE_VALGRIND_H, 1, [valgrind.h])
        fi
    fi

    path=/usr/local/include/
    if test "x$valgrind_include" == "x"; then
        AC_CHECK_FILE($path/valgrind.h, [valgrind_include=$path])
        if test "x$valgrind_include" != "x"; then
            AC_CHECK_FILE($path/memcheck.h, [memcheck=yes])
            if test "x$memcheck" == "xyes"; then
                AC_DEFINE(HAVE_MEMCHECK_H, 1, [memcheck.h])
            fi
            AC_DEFINE(HAVE_VALGRIND_H, 1, [valgrind.h])
        fi
    fi

    if test "x$valgrind_include" == "x"; then
        AC_MSG_ERROR([Can't find valgrind.h include file])
    fi
fi

if test "x$valgrind_include" != "x"; then
    CPPFLAGS="$CPPFLAGS -I $valgrind_include"
fi

# note that we've switch from config.h.in to config_h.in for
# compatibility with various crappy win32 zip programs :o(
AM_CONFIG_HEADER([src/include/config.h:src/include/config_h.in])
#AM_INIT_AUTOMAKE(subdir-objects)
AM_INIT_AUTOMAKE(dist-bzip2 dist-zip subdir-objects)

AC_CONFIG_SRCDIR(src/commandline.c)

# figure out which OS specific include directory should be used (FIXME)
case $target_os in
*linux*)
  AC_SUBST(OS, LINUX) 
  osinclude=linux ;;
*solaris*) 
  AC_SUBST(OS, SUNOS) 
  osinclude=sunos ;;
*freebsd*) 
  AC_SUBST(OS, FREEBSD) 
  osinclude=freebsd ;;
*darwin*) 
  AC_SUBST(OS, DARWIN) 
  osinclude=darwin ;;
*cygwin*)
  AC_SUBST(OS, CYGWIN) 
  osinclude=cygwin ;;
default)
  AC_SUBST(OS, LINUX) 
  osinclude=linux ;;
esac

# export variable for use in makefile
AC_SUBST(OSINCLUDE, [$osinclude]) 

# checks for programs
AM_PROG_CC_C_O()    # support subdir objects for automake
AC_PROG_CC
AC_PROG_RANLIB
AC_PROG_LIBTOOL
AM_PATH_PYTHON(2.2,,:)  # need python2.2+ for build
AM_CONDITIONAL([HAVE_PYTHON], [test "$PYTHON" != :])

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([fcntl.h float.h limits.h memory.h stdlib.h string.h strings.h sys/time.h unistd.h])

# determine endian-ness of target compiler (sets WORDS_BIGENDIAN if true)
AC_C_BIGENDIAN()

AC_CHECK_TYPE(uint_fast32_t, unsigned long int)
AC_CHECK_TYPE(uint32_t, unsigned long int)
AC_CHECK_TYPE(uint16_t, unsigned short int)
AC_CHECK_TYPE(uint8_t, unsigned char)

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T
AC_HEADER_TIME

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_FUNC_REALLOC
AC_FUNC_STAT
AC_CHECK_FUNCS([getcwd gettimeofday memmove memset strcasecmp strchr strerror strncasecmp strrchr strtol])

# check for libraries that we use
AC_CHECK_LIB(z, deflate)
AC_CHECK_LIB(m, sqrt)
AC_CHECK_LIB(mx, sqrtf, [], # mx contains the floating point functions on darwin

  # failed, look for sqrtf in libm
  [AC_CHECK_LIB(m, sqrtf, 

    # on success, do nothing (prevents -lm being added twice)
    [echo -n ""], 

    # define away sqrtf and friends in the firstinclude file, since we
    # can't find them.  Filter out previous #define's first.
    [echo "reconfiguring to use double functions instead of float functions"
     cat src/include/$osinclude/firstinclude.h | grep -v "define sqrtf" | grep -v "define logf" | grep -v "define ldexpf" | grep -v "define frexpf" > firstinclude.h
     echo "#ifndef sqrtf /* define sqrtf */" >> firstinclude.h
     echo "#define sqrtf sqrt" >> firstinclude.h
     echo "#endif /* define sqrtf */" >> firstinclude.h
     echo "#ifndef logf /* define logf */" >> firstinclude.h
     echo "#define logf log" >> firstinclude.h
     echo "#endif /* define logf */" >> firstinclude.h
     echo "#ifndef ldexpf /* define ldexpf */" >> firstinclude.h
     echo "#define ldexpf ldexp" >> firstinclude.h
     echo "#endif /* define ldexpf */" >> firstinclude.h
     echo "#ifndef frexpf /* define frexpf */" >> firstinclude.h
     echo "#define frexpf frexp" >> firstinclude.h
     echo "#endif /* define frexpf */" >> firstinclude.h
     mv firstinclude.h src/include/$osinclude/firstinclude.h
    ])])
     
AC_OUTPUT(Makefile)
AC_OUTPUT(setup.py)

