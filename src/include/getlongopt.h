/* getlongopt.h attempts to emulate (but without some of the
 * stupidity) the GNU getopt_long library to portably allow the
 * parsing of long and short options.  Because getlongopt provides the ability
 * to provide optional arguments, this means that parsing of command lines is
 * somewhat ambiguous: '--option-with-optional-arg -f' could be interpreted as 
 * two options or an option with an argument (of '-f').  This conflict is solved
 * by checking whether the next argument can be parsed as an option.  If it can
 * be, its assumed to be an option, otherwise its assumed to be an argument.
 *
 * written nml 2003-04-11
 *
 */

#ifndef GETLONGOPT_H
#define GETLONGOPT_H

enum getlongopt_ret {
    GETLONGOPT_OK = 0,              /* everything went fine, value returned */
    GETLONGOPT_END = 1,             /* ran out of options */
    GETLONGOPT_UNKNOWN = -1,        /* parsed an unknown option */
    GETLONGOPT_MISSING_ARG = -2,    /* option has a missing argument */
    GETLONGOPT_ERR = -3             /* unexpected error occurred */
};

/* possible values for argument in getlongopt_opt */
enum getlongopt_arg {
    GETLONGOPT_ARG_NONE = 0,        /* no argument can be provided */
    GETLONGOPT_ARG_REQUIRED = 1,    /* an argument must be provided */
    GETLONGOPT_ARG_OPTIONAL = 2     /* an argument may be provided */
};

struct getlongopt_opt {
    const char *longname;           /* long option name (i.e. 'help').  Can be 
                                     * NULL if this option doesn't have a
                                     * long name. */
    char shortname;                 /* short option name (i.e. h).  Can be \0 
                                     * if this option doesn't have a short 
                                       name. */
    enum getlongopt_arg argument;   /* a value from above enum indicating 
                                     * whether this option takes an argument, 
                                     * and whether its optional */
    int id;                         /* identifying value returned when this 
                                     * option is parsed */
};

struct getlongopt;

/* create a new option parser that will parse at most argc strings
 * from argv.  optstrings options from the optstring array will be
 * recognised as options.  Returns NULL on failure (including passing
 * the wrong thing as argument value).  Don't change any
 * of the stuff pointed to by argv or optstring until after calling
 * getlongopt_delete or the results are undefined (and almost certainly bad). 
 * Note that the first element in argv is NOT skipped over as in historical
 * getopt and getopt_long, (so you probably want to pass argc - 1, &argv[1] as
 * argc and argv in the simple case) */
struct getlongopt *getlongopt_new(unsigned int argc, 
  const char **argv, struct getlongopt_opt *optstring, 
  unsigned int optstrings);

/* delete a getlongopt structure */
void getlongopt_delete(struct getlongopt *opts);

/* get the next option from the option set.  Returns GETLONGOPT_OK on
 * success, and writes optid with the id of the parsed option, and optarg
 * with a pointer to the option or NULL if no option was given.
 * Returns GETLONGOPT_END if there are no more options to get.
 * Returns GETLONGOPT_UNKNOWN if it parses an unknown option.
 * Returns GETLONGOPT_MISSING_ARG if it parses an option that requires
 * an argument but that doesn't have one. */
int getlongopt(struct getlongopt *opts, int *optid, const char **optarg);

/* returns the current parsing index, works the same as optind in
 * traditional getopt */
unsigned int getlongopt_optind(struct getlongopt *opts);

#endif

