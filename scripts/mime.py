"""mime.py is a script to turn lists of media types into a c 
   module for manipulating MIME types (mime.[ch]).

   this module accepts lists of MIME types in various formats.  I
   strongly suggest that you use the canonical list at 
   http://www.isi.edu/in-notes/iana/assignments/media-types/media-types
   and the list of important MIME types at src/mime-types.txt when
   regenerating the MIME module.

   FIXME: should compress trie recognition code down for recognition
   of shared sequences (i.e. for 'application', after first 'a' there
   is no non-error divergence until whole thing is matched)

   written nml 2004-06-11
"""

import getopt
import sys
import string
import operator
from time import strftime, gmtime

def dedent(str, dent = 0):
    """Simple function to uniformly remove whitespace from the front of
       lines."""
    lines = str.split('\n')
    try: 
        dent = reduce(min, map(lambda x: len(x) - len(x.lstrip()), 
          filter(lambda x: len(x.lstrip()) > 0, lines)))
    except TypeError:
        dent = 0
    return '\n'.join(map(lambda x: len(x.lstrip()) > 0 and x[dent:] or x, lines))

def indent(str, dent):
    """Simple function to uniformly add whitespace to the front of lines."""
    return '\n'.join(map(lambda x: ' ' * dent + x, str.split('\n')))

def maptrans(string):
    """Remove c-incompatible characters from mime types."""
    return string.upper().replace('-', '_').replace('.', '_')\
      .replace('$', '').replace('+', '_')

def get_prefix(string_list):
    '''Returns the common prefix between a list of strings'''
    if (len(string_list) == 0):
        return ''
    elif (len(string_list) == 1):
        return string_list[0]
    else:
        pre = string_list[0]
        lpre = len(pre)
        for s in string_list[1:]:
            while (pre != s[0:lpre]):
                lpre -= 1
                pre = pre[0:lpre]
        return pre

class TopType:
    """Class to represent a top-level MIME type."""

    def __init__(self, name):
        self._name = name;
        self._members = {}
        self._size = 0;
        self._types = 1

    def addType(self, type):
        key = maptrans(type)
        if (self._members.has_key(key)):
            if (self._members[key][2] != type):
                # stupid lists defined two very similar mime types!
                #sys.stderr.writelines('key clash b/w %s (%s) and %s (%s)\n' 
                  #% (type, key, self._members[key][2], self._members[key][1]))
                self._members[key][3] = 1
            else:
                self._members[key][3] += 1
        else:
            self._members[key] = [self._name, key, type, 1]
        self._size += 1

    def __str__(self):
        return self._name

    def cmp(self, toptype):
        if (self._size < toptype._size):
            return -1
        elif (self._size > toptype._size):
            return 1
        elif (self._name < toptype._name):
            return -1
        elif (self._name > toptype._name):
            return 1
        else:
            return 0

#class TopTypeCombo:
    #def __init__(self, left, right):
        #self._name = left._name + "/" + right._name
        #self._size = left._size + right._size
        #self._left = left
        #self._right = right
        #self._types = left._types + right._types

    #def cmp(self, toptype):
        #if (self._size < toptype._size):
            #return -1
        #elif (self._size > toptype._size):
            #return 1
        #elif (self._name < toptype._name):
            #return -1
        #elif (self._name > toptype._name):
            #return 1
        #else:
            #return 0

#class Huffman:
    #def __init__(self, types, cmp):
        #while (len(types) > 1):
            # sort list with comparsion function
            #types.sort(cmp)

            # merge bottom two
            #types[0:2] = [TopTypeCombo(types[0], types[1])]

        # form codes
        #depth = 0
        #stack = []
        #tt = types.pop()
        #code = []
        #self._codes = {}

        #print "push", stack[0], stack[0]._left, stack[0]._right
        #while (tt != None):
            #if (tt._types == 1):
                # now form the huffman code by combining and reversing all of
                # the stuff in code
                #assert(depth == len(code))
                #num = 0
                #for i in range(depth):
                    #num += code[i] << i

                #print 'think about', tt._name, tt._size, depth, code, num
                #self._codes[tt._name] = [num, depth]

                #try:
                    #tt, depth, code = stack.pop()
                    #print 'pop', tt._name, depth, code
                #except IndexError:
                    #tt = None
            #else:
                #depth += 1
                #code.append(1)
                ##print 'push', tt._right._name, depth, code
                #stack.append([tt._right, depth, copy.copy(code)])
                #code.pop()
                #code.append(0)
                #print 'follow', tt._left._name, depth, code
                #tt = tt._left

    #def codes(self):
        #return self._codes

def typecmp(x, y):
    # compare by number of occurrances, and then inverse string length
    if (x[3] == y[3]):
        xlen = len(x[0]) + len(x[2])
        ylen = len(y[0]) + len(y[2])
        if (xlen == ylen):
            if (x[0] == y[0]):
                return -1 * cmp(x[2], y[2])
            else:
                return -1 * cmp(x[0], y[0])
        else:
            return -1 * cmp(xlen, ylen)
    else:
        return cmp(x[3], y[3])

def usage(progname):
    print 'usage: %s [--c-header|--c-body|--debug] mimefile*' % progname

def maketrie(list, prefix):
    """function to make a trie in c code from a list of strings representing
       mime_types."""
    if (len(prefix) > 0):
        print '%s_label:' % maptrans(prefix).replace("/", "_").lower()

    # get first characters from list
    chars = map(lambda x: len(x) > 0 and x[0] or "", list)

    # count duplicates
    counted = {}
    count = 0
    for c in chars:
        if counted.has_key(c):
            counted[c][0] = counted[c][0] + 1
        else:
            counted[c] = [1, count]
        count += 1

    print '    switch (*str++) {'
    for c in counted:
        if (len(c)):
            print '    case \'%s\':' % c
            if (c in string.lowercase):
                print '    case \'%s\':' % c.upper()
            if (counted[c][0] == 1):
                print '        /* must be \'%s\' or unrecognised */' % (prefix + list[counted[c][1]])
                print '        if (!str_casecmp(str, '
                print '          &lookup[MIME_TYPE_%s].name[%u])) {' % (maptrans(prefix + list[counted[c][1]]).replace("/", "_").upper(), len(prefix) + 1)
                print '            return MIME_TYPE_%s;' % maptrans(prefix + list[counted[c][1]]).replace("/", "_").upper()
                print '        } else {'
                print '            return MIME_TYPE_UNKNOWN_UNKNOWN;'
                print '        }'
                print '        break;'
            else:
                nprefix = get_prefix(list[counted[c][1]:counted[c][1] + counted[c][0]])
                assert(len(nprefix) >= 1);
                counted[c].append(nprefix)
                if (nprefix == c):
                    # can't remove any further characters without
                    # branches, go to next label
                    print '        goto %s_label;' % maptrans(prefix + c[0]).replace("/", "_").lower()
                else:
                    print '        /* skip to prefix \'%s\' */' % (prefix + nprefix)
                    print '        if (!str_ncasecmp(str, "%s", %d)) {' \
                      % (nprefix[1:], len(nprefix[1:]))
                    print '            str += %d;' % (len(nprefix[1:]))
                    print '            goto %s_label;' % maptrans(prefix + nprefix).replace("/", "_").lower()
                    print '        } else {'
                    print '            return MIME_TYPE_UNKNOWN_UNKNOWN;'
                    print '        }'
        else:
            print '    case \'\\0\':'
            print '        return MIME_TYPE_%s;' % maptrans(prefix).replace("/", "_").upper()
        print

    print '    default: '
    print '        return MIME_TYPE_UNKNOWN_UNKNOWN;'
    print '    }'

    for c in counted:
        if (len(c) and counted[c][0] > 1):
            assert(len(counted[c]) == 3)
            print
            nprefix = prefix + counted[c][2]
            maketrie(map(lambda x: x[len(counted[c][2]):], list[counted[c][1]:counted[c][1] + counted[c][0]]), nprefix)

if __name__ == "__main__":
    try:
        (options, args) = getopt.getopt(sys.argv[1:], 'hv', 
            ['help', 'version', 'c-header', 'c-body', 'debug'])
    except getopt.GetoptError:
        usage(sys.argv[0])
        sys.exit(2)

    # simple processing of options
    header = False
    body = False
    debug = False
    for opt, arg in options:
        if opt in ['--help', '-h']:
            usage(sys.argv[0])
            sys.exit(2)
        elif opt in ['--version', '-v']:
            print '%s version 0.1\n' % sys.argv[0]
            sys.exit(2)
        elif opt == '--c-header':
            header = True
            debug = False
            body = False
        elif opt == '--c-body':
            header = False
            debug = False
            body = True
        elif opt == '--debug':
            header = False
            body = False
            debug = True

    if (not header and not body and not debug):
        usage(sys.argv[0])
        sys.exit(2)

    types = {}

    for filename in args:
        try: 
            file = open(filename)

            # process files by treating them as three sections: a preamble that we
            # want to ignore (before), a meaningful section that we need to
            # process, and references and discussion at the end that we need to
            # ignore (after).  The key to this is recognising lines that have no
            # whitespace at the start of the line, since the meaningful section
            # starts and ends with these lines.  We match the first word with known
            # toplevel types to figure out where things start and end

            before = 1
            after = 0
            toptype = None
            toptypes = ['text', 'multipart', 'message', 'chemical', 'application', 
                'image', 'audio', 'video', 'model', 'inode']

            for line in file:
                if (not after):
                    # two blank lines end section we process
                    if (len(line.lstrip()) == 0 or line.lstrip()[0] == '#'):
                        # blank, ignore
                        pass
                    elif (line.lstrip() == line): 
                        # toplevel type specified first on this line
                        words = line.split()

                        # handle lines listed as topleveltype/subtype
                        if (words[0].find('/') != -1):
                            words = words[0].split('/')

                        if ((words[0] in toptypes)
                          or (words[0][0:2] == 'x-')):
                            # start of a new top-level type
                            if (types.has_key(words[0])):
                                toptype = types[words[0]]
                            else:
                                toptype = TopType(words[0])
                                types[words[0]] = toptype

                            # need conditional to exclude stupid inconsistency 
                            # with model type (doesn't have first subtype on same
                            # line
                            if (len(words) > 1 
                              and (words[1][0] != '[' or words[1][-1] != ']')):
                                toptype.addType(words[1])
                            before = 0
                        elif (not before):
                            # end of types section
                            after = 1
                    elif (not before):
                        words = line.split()
                        toptype.addType(words[0])
                else:
                    # after
                    pass

            file.close()
        except IOError:
            # error reading from file, ignore it
            pass

    # form a list of all types
    ordtypes = reduce(operator.add, 
      map(lambda x: types[x]._members.values(), types))
    ordtypes.sort(typecmp)
    ordtypes.reverse()

    # number them 
    i = 0
    for t in ordtypes:
        t.append(i)
        i += 1

    if (debug):
        for l in ordtypes:
            print l
    elif (header):
        # resort types to look pretty
        ordtypes.sort()

        # print out c header file 
        str = '''\
          /* mime.h provides support for MIME types as originally proposed 
           * by RFCs 1521 and 1522, and updated by RFCs 2045 through 2049.
           * It provides enumerations for convenient representation of 
           * recognised MIME types, as well as a functions for determining
           * MIME types for given MIME names and file contents.
           *
           * DO NOT modify this file, as it is automatically generated 
           * and changes will be lost upon subsequent regeneration.
           *
           * It is automatically generated from media-type files describing 
           * valid MIME types by %s.  The definitive list can be found at 
           * %s, 
           * although %s will accept multiple files (in the same format), 
           * allowing you to define your own MIME types as convenient.
           *
           * This file was generated on %s 
           * by %s
           *
           */''' 
               
        print dedent(str) % (sys.argv[0], 
          'http://www.isi.edu/in-notes/iana/assignments/media-types/' \
            + 'media-types', sys.argv[0], 
            strftime("%a, %d %b %Y %H:%M:%S +0000", gmtime()),
            sys.argv[0])

        print
        print dedent('''\
          #ifndef MIME_H
          #define MIME_H

          #ifdef _cplusplus
          extern "C" {
          #endif''')
                            
        print
        print 'enum mime_top_types {'
        i = 0
        sorttypes = types.keys()
        sorttypes.sort()
        for tt in sorttypes:
            print '    MIME_TOP_TYPE_' + maptrans(tt).upper(), '= %u,' % i
            i += 1
        print '    MIME_TOP_TYPE_ERR = -1'
        print '};'
        print

        print '/* numbered so that (hopefully) commonly used MIME types have '
        print '   low numbers */'
        print 'enum mime_types {'
        prevtt = ordtypes[0][0]
        for tt, key, type, freq, rank in ordtypes:
            if (tt != prevtt):
                print
            print '    MIME_TYPE_%s_%s = %u,' \
              % (maptrans(tt).upper(), maptrans(type).upper(), rank)
            prevtt = tt
        print
        print '    MIME_TYPE_UNKNOWN_UNKNOWN = -1'
        print '};'
        print

        print '/* returns string representation of mime type */'
        print 'const char *mime_string(enum mime_types mtype);'
        print

        print '/* determine which toplevel-type a given mimetype is */'
        print 'enum mime_top_types mime_top_type(enum mime_types mtype);'
        print

        print '/* attempts to hueristically identify MIME types by content, '
        print ' * returns MIME_TYPE_APPLICATION_OCTET_STREAM if unable to do '
        print ' * so. */'
        print 'enum mime_types mime_content_guess(const void *buf, unsigned int len);'

        print
        print '/* returns the mime type indicated by the provided string.  '
        print ' * This function probably needs to be turned '
        print ' * into an object accepting a stream at some stage. */'
        print 'enum mime_types mime_type(const char *str);'

        print
        print dedent('''\
          #ifdef _cplusplus
          }
          #endif
          
          #endif''')
        print
             
    elif (body):
        # generate c definitions
        str = '''\
          /* mime.c implements the interface declared by mime.h
           *
           * DO NOT modify this file, as it is automatically generated 
           * and changes will be lost upon subsequent regeneration.
           *
           * It is automatically generated from media-type files describing 
           * valid MIME types by %s.  The definitive list can be found at 
           * %s, 
           * although %s will accept multiple files (in the same format), 
           * allowing you to define your own MIME types as convenient.
           *
           * This file was generated on %s 
           * by %s
           *
           */
               
          #include "firstinclude.h"

          #include "mime.h"

          #include "str.h"

          #include <ctype.h>
          ''' 
               
        print dedent(str) % (sys.argv[0], 
          'http://www.isi.edu/in-notes/iana/assignments/media-types/' \
            + 'media-types', sys.argv[0], 
            strftime("%a, %d %b %Y %H:%M:%S +0000", gmtime()),
            sys.argv[0])

        print dedent('''\
          struct mime_lookup {
              const char *name;
              enum mime_top_types toptype;
          };
          ''')
        print 'static const struct mime_lookup lookup[] = {'
        for tt, key, type, freq, rank in ordtypes:
            if (len(tt) + len(type) < 28):
                print '    {"%s/%s", MIME_TOP_TYPE_%s},' \
                  % (tt, type, maptrans(tt).upper())
            else:
                print '    {"%s/%s",' % (tt, type)
                print '      MIME_TOP_TYPE_%s},' % (maptrans(tt).upper())
        print
        print '    {NULL, MIME_TOP_TYPE_ERR}'
        print '};'
        print

        print dedent('''\
          const char *mime_string(enum mime_types mtype) {
              if (mtype <= %u) {
                  return lookup[mtype].name;
              } else {
                  return NULL;
              }
          }''') % len(ordtypes)
        print

        print dedent('''\
          enum mime_top_types mime_top_type(enum mime_types mtype) {
              if (mtype <= %u) {
                  return lookup[mtype].toptype;
              } else {
                  return MIME_TOP_TYPE_ERR;
              }
          }''') % len(ordtypes)
        print
        print '''\
       
/* FIXME: do this properly, using parsing-type stuff */
enum mime_types mime_content_guess(const void *buf, unsigned int len) {
    const char *cbuf = buf;

    if (len >= 4) {
        /* test for JPEG */
        if ((cbuf[0] == (char) 0xff) && (cbuf[1] == (char) 0xd8) 
          && (cbuf[2] == (char) 0xff) && (cbuf[3] == (char) 0xe0)) {
            return MIME_TYPE_IMAGE_JPEG;
        }
    }

    if (len >= 6) {
        /* test for GIF */
        if (!str_ncmp(cbuf, "GIF8", 4) && ((cbuf[4] == '9') || (cbuf[4] == '7'))
          && (cbuf[5] == 'a')) {
            return MIME_TYPE_IMAGE_GIF;
        }
    }

    if (len >= 4) {
        /* test for MS cbuf (badly) */
        if ((cbuf[0] == (char) 0xd0) && (cbuf[1] == (char) 0xcf) 
          && (cbuf[2] == (char) 0x11) && (cbuf[3] == (char) 0xe0)) {
            /* XXX: not strictly true, its just an OLE document, but most of 
             * them are MS cbuf docs anyway.  Testing for MS cbuf requires 
             * looking further into the document, so we won't be doing that 
             * here. */
            return MIME_TYPE_APPLICATION_MSWORD;
        }

        if (len == 2) {
            if ((cbuf[0] == (char) 0x31) && (cbuf[1] == (char) 0xbe) 
              && (cbuf[0] == '\\0')) {
                /* more MS cbuf stuff :o( */
                return MIME_TYPE_APPLICATION_MSWORD;
            } else if ((cbuf[0] == (char) 0xfe) && (cbuf[1] == '7') 
              && (cbuf[0] == '\\0')) {
                /* office document */
                return MIME_TYPE_APPLICATION_MSWORD;
            }
        }
    }

    /* test for wordperfect files */
    if (len >= 4) {
        if ((cbuf[0] == (char) 0xff) && (cbuf[1] == 'W') && (cbuf[2] == 'P') 
          && (cbuf[3] == 'C')) {
            return MIME_TYPE_APPLICATION_WORDPERFECT5_1;
        }
    }

    /* test for postscript files */
    if (len >= 2) {
        if ((cbuf[0] == '%') && (cbuf[1] == '!')) {
            return MIME_TYPE_APPLICATION_POSTSCRIPT;
        }
    }

    /* test for PDF files */
    if (len >= 5) {
        if ((cbuf[0] == '%') && (cbuf[1] == 'P') && (cbuf[2] == 'D') 
          && (cbuf[3] == 'F') && (cbuf[4] == '-')) {
            return MIME_TYPE_APPLICATION_PDF;
        }
    }

    /* test for markup languages, first locating the first
     * non-whitespace character */
    while (isspace(*cbuf) && len) {
        cbuf++;
        len--;
    }

    /* TREC documents */
    if (((len >= str_len("<doc>")) 
        && !str_ncasecmp("<doc>", cbuf, str_len("<doc>")))) {
        return MIME_TYPE_APPLICATION_X_TREC;
    }

    /* INEX documents */
    if (((len >= str_len("<article>")) 
        && !str_ncasecmp("<article>", cbuf, str_len("<article>")))) {
        return MIME_TYPE_APPLICATION_X_INEX;
    }

    /* HTML */
    if (((len >= str_len("<!doctype html")) 
        && !str_ncasecmp("<!doctype html", cbuf, str_len("<!doctype html")))
      || ((len >= str_len("<head")) 
        && !str_ncasecmp("<head", cbuf, str_len("<head")))
      || ((len >= str_len("<title")) 
        && !str_ncasecmp("<title", cbuf, str_len("<title")))
      || ((len >= str_len("<html")) 
        && !str_ncasecmp("<html", cbuf, str_len("<html")))) {
        return MIME_TYPE_TEXT_HTML;
    }

    /* SGML */
    if (((len >= str_len("<!doctype ")) 
        && !str_ncasecmp("<!doctype ", cbuf, str_len("<!doctype ")))
      || ((len >= str_len("<subdoc")) 
        && !str_ncasecmp("<subdoc", cbuf, str_len("<subdoc")))) {
        return MIME_TYPE_TEXT_SGML;
    }

    /* XML */
    if (((len >= str_len("<?xml")) 
        && !str_ncasecmp("<?xml", cbuf, str_len("<?xml")))) {
        return MIME_TYPE_TEXT_XML;
    }

    /* XXX: test for tar files */

    return MIME_TYPE_APPLICATION_OCTET_STREAM;
}

enum mime_types mime_type(const char *str) {
'''

        # for tt, key, type, freq, rank in ordtypes:

        # trie to identify types
        listtypes = map(lambda x: x[0].lower() + "/" + x[2].lower(), ordtypes)
        listtypes.sort()

        maketrie(listtypes, "")
        print '}'
        print

        print '#ifdef MIME_TEST'
        print 
        print '#include <stdlib.h>'
        print '#include <stdio.h>'
        print '#include "str.h"'
        print
        print 'int main() {'
        print '    char buf[BUFSIZ + 1];'
        print '    enum mime_types mtype;'
        print 
        print '    while (fgets(buf, BUFSIZ, stdin)) {'
        print '        str_rtrim(buf);'
        print '        mtype = mime_type(str_ltrim(buf));'
        print '        printf("%s\\n", mime_string(mtype));'
        print '    }'
        print
        print '    return EXIT_SUCCESS;'
        print '}'
        print
        print '#endif'
        print
 
    else:
        assert(False)






