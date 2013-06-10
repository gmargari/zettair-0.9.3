/* Define to 1 if you have the <inttypes.h> header file. 
#undef HAVE_INTTYPES_H */

/* Define to 1 if you have the <stdint.h> header file. 
#undef HAVE_STDINT_H */

/* Define to 1 if you have the <unistd.h> header file. 
#undef HAVE_UNISTD_H 1 */

/* Name of package */
#define PACKAGE "zettair"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "zettair@cs.rmit.edu.au"

/* Define to the full name of this package. */
#define PACKAGE_NAME "zettair"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "zettair 0.6.0"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "zettair"

/* Define to the version of this package. */
#define PACKAGE_VERSION "0.6.0"

/* Version number of package */
#define VERSION "0.6.0"

/* Define to 1 if your processor stores words with the most significant byte
   first (like Motorola and SPARC, unlike Intel and VAX). */
/* #undef WORDS_BIGENDIAN */

/* Define to `unsigned' if <sys/types.h> does not define. */
/* #undef size_t */

#define uint8_t unsigned char
#define uint16_t unsigned short int
#define uint32_t unsigned long int
#define uint64_t __int64  /* win32 doesn't seem to have uint64_t :o( */
#define uintmax_t __int64  /* win32 doesn't seem to have uint64_t :o( */
#define uint_fast32_t unsigned long int

/* define floating point functions */
#ifndef sqrtf 
#define sqrtf sqrt
#endif 
#ifndef logf 
#define logf log
#endif 
#ifndef ldexpf 
#define ldexpf ldexp
#endif 
#ifndef frexpf 
#define frexpf frexp
#endif 

/* XXX: win32 doesn't have eoverflow :o( */
#define EOVERFLOW EINVAL

#define STDOUT_FILENO _fileno(stdout)

/* FIXME: (horrible hack) windows doesn't have sysconf, so just use 4096.  I
 * don't want to spend time buggering around with abstracting this facility ATM
 * */
#define sysconf(_SC_PAGESIZE) 4096

#define snprintf _snprintf
#define vsnprintf _vsnprintf

#define getcwd(x,y) _getcwd(x, y)
#define read(x,y,z) _read(x,y,z)
#define write(x,y,z) _write(x,y,z)
#define isatty(x) _isatty(x)
#define lseek(x,y,z) _lseek(x,y,z)

