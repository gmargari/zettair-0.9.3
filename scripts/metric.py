""" metric.py is a small script to turn psuedo-c metric descriptions
    into executable c code as needed by zettair.  Usage:

      metric.py [--debug | --help] metricname.metric

    If you want to debug your metric, i suggest you insert printf statements, 
    as there's nothing stopping you doing so.

    Our input format is this:

    - comments are allowed at any point in the file, using either # at the
      start of the comment and continuing to the end of the line.  e.g.

      # this is a comment

    - a parameters section, where you provide whatever parameters the
      metric needs by putting the word 'parameter ' before a c declaration of 
      the parameter, with a default if necessary.  Each parameter declaration 
      must be on a single line. e.g.

      parameter float k1;

    - two functions, decode and post, declared as:

      post() {

      }

    - these functions contain an initial declarations section, where
      you can declare intermediate quantities in c declarations as needed.  i.e.

      decode() {
          float w_t;
      }

    - a series of expressions and logic involving the parameters,
      intermediate quantities, special quantities.  To find out the
      special quantities available, type python metric.py --help.

    - note: you are almost certainly better off using the ternary
      operator in decode calculations, as if statement processing is fragile

    written nml 2004-12-15
"""

import getopt
import sys
import string
import operator
from time import strftime, gmtime

def indent(str, dent = 0, prestr = ' '):
    '''Simple function to uniformly indent lines.'''
    return '\n'.join(map(lambda x: (prestr * dent) + x, str.split('\n')))

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

def isop(char):
    """Simple function to determine whether a character is a c 
       operator character (not including semi-colon)."""
    if ((char == '-') or (char == '+') or (char == ',') or (char == '=') 
      or (char == '(') or (char == ')') or (char == '?') or (char == ':') 
      or (char == '*') or (char == '/') or (char == '~') or (char == '!') 
      or (char == '^') or (char == '|') or (char == '&') or (char == '[') 
      or (char == ']') or (char == '{') or (char == '}') or (char == '%')
      or (char == '<') or (char == '>')):
        return 1
    else:
        return 0

def isdecl(word):
    if (word == 'float' or word == 'double' or word == 'long' 
      or word == 'unsigned' or word == 'signed' or word == 'const' 
      or word == 'volatile' or word == 'int' or word == 'short' 
      or word == 'char' or word == 'register' or word == 'void' 
      or word == 'union' or word == 'struct' or word == '*' 
      or word == '**' or word == '***' or word == '****'):
        return 1
    else:
        return 0

# function to filter whitespace tokens from ctok
def ctok_nspace(line):
    toks, chars = ctok(line)

    filtered = filter(lambda x: not x[0].isspace(), zip(toks, chars))

    toks = map(lambda x: x[0], filtered)
    chars = map(lambda x: x[1], filtered)

    return toks, chars

def ctok(line):
    toks = []
    chars = []
    char = 0
    while (len(line)):
        c = line[0]
        schar = char
        line = line[1:]
        if (c.isspace()):
            tok = c
            while (len(line) and line[0].isspace()):
                tok = tok + line[0]
                line = line[1:]
                char += 1
            toks.append(tok)
            chars.append(schar)
        elif (isop(c) == 1):
            tok = c
            while (len(line) and isop(line[0])):
                tok = tok + line[0]
                line = line[1:]
                char += 1
            toks.append(tok)
            chars.append(schar)
        elif (c == ';'):
            toks.append(';')
            chars.append(schar)
        else:
            tok = c
            while (len(line) and isop(line[0]) == 0 
              and line[0] != ';' and not line[0].isspace()):
                tok = tok + line[0]
                line = line[1:]
                char += 1
            toks.append(tok)
            chars.append(schar)
        char += 1
    return toks, chars

def usage(progname, decode = None, post = None):
    print 'usage: %s [--debug] metricfile templatefile' \
      % progname

    if (decode != None):
        dkeys = decode.keys()
        dkeys.sort()
        print
        print 'these quantities are available in decode routines:'
        for d in dkeys:
            print '   ', d + ':', decode[d].ex

    if (post != None):
        dkeys = post.keys()
        dkeys.sort()
        print
        print 'these quantities are available in post routines:'
        for d in dkeys:
            print '   ', d + ':', post[d].ex

class Decl:
    def __init__(self, lineno, name, type, init, level, macro, 
      comment, fninit, pre, contrib, ex):

        self.pre = pre
        self.lineno = lineno
        self.name = name
        self.type = type
        self.init = init
        # fn init is if we can't initialise by assignment, which happens 
        # sometimes (only available to built-in quantities)
        self.fninit = fninit  
        self.level = level
        self.macro = macro
        self.comment = comment.strip()
        # contrib is what happens if we have to calculate the
        # contribution without a specific document 
        self.contrib = contrib
        self.ex = ex

    def __repr__(self):
        return self.__str__()

    def __str__(self):
        return "type/name/init/fninit %s %s %s %s; lvl %u line %s macro %s comment %s pre %s" \
          % (self.type, self.name, self.init, self.fninit, self.level, self.lineno, self.macro, self.comment, self.pre)

def ins_decl(namespaces, used, line, level, macro = '', 
  lineno = -1, fninit = '', pre = '', contrib = '', ex = ''):
    # check for and remove trailing comment
    comment = ''
    pos = line.find('#')
    if (pos > 0):
        comment = line[pos + 1:]
        line = line[0:pos]
    pos = line.find('/*')
    if (pos > 0):
        pos2 = line.find('*/')
        if (pos2 > 0):
            pos2 += 2
            comment = line[pos + 2:pos2 - 2]
            line = line[0:pos] + line[pos2:]
        else:
            print '#line', lineno, '"' + args[0] + '"'
            print '#error "multiline comment in declaration"'

    toks, chars = ctok_nspace(line.rstrip())

    type = ''
    while (len(toks) > 0 and isdecl(toks[0]) == 1):
        # this token is part of the type

        # skip tag in struct and union
        if (toks[0] == 'struct' or toks[0] == 'union'):
            type += toks[0] + ' '
            toks = toks[1:]

        type += toks[0] + ' '
        toks = toks[1:]

    if (len(toks)):
        # check for namespace collisions
        for n in namespaces:
            if (n.has_key(toks[0])):
                # collision
                print '#line', lineno, '"' + args[0] + '"'
                print '#error "duplicate','declaration \'', line.rstrip(), '\'"'
                sys.exit(2)

        # check for and remove semi-colon
        if (toks[-1] == ';'):
            toks = toks[0:-1]
        else:
            print '#line', lineno, '"' + args[0] + '"'
            print '#error "declaration without semi-colon ending"'
            sys.exit(2)

        # identify initialiser without tokenising (makes the spacing odd 
        # otherwise)
        init = line[chars[len(chars) - len(toks)]:]
        init = init[0:init.find(';')]

        # identify quantities used
        for t in toks[1:]:
            for n in namespaces:
                if (t in n):
                    for u in used:
                        u[t] = t

        # accept declaration
        for n in namespaces:
            n[toks[0]] = Decl(lineno, toks[0], type.strip(), 
              init.strip(), level, macro, comment, fninit, pre, contrib, ex)

        # identify quantities used in contrib (XXX: we only insert
        # them in the first namespace/used combo - this is dodgy) 
        name = toks[0]
        toks, chars = ctok_nspace(contrib)
        for t in toks:
            if (t in namespaces[0]):
                used[0] = t
    else:
        # huh?
        print '#line', lineno, '"' + args[0] + '"'
        print '#error "error parsing declaration\'',\
          line.rstrip(), '\'"'
        sys.exit(2)
 
def process_decl(file, decl, decl_used, lineno):
    """Function to process declarations in post, decode sections.  
       Returns first line that isn't a declaration."""

    # start with next line
    line = file.readline()
    lineno += 1

    while (line != '' and line.rstrip() != '}'):
        # in decl section
        if (len(line.strip()) > 0 and line.strip()[0] != '#'):
            toks, chars = ctok_nspace(line)
            # tokenise line and process it
            if (isdecl(toks[0]) == 1):
                ins_decl([decl], [decl_used], line=line, level=1, macro='', lineno=lineno, contrib='', ex='user declared quantity')
            else:
                return [lineno, line]

        # next line
        line = file.readline()
        lineno += 1

    if (line == ''):
        print '#line', lineno, '"' + args[0] + '"'
        print '#error "unexpected EOF"'
        sys.exit(2)

    return [lineno, line]

def levelise(file, line, lineno, decl, list, used):
    """assign lines in a file to a list, annotating them with logical levels."""

    # declarations processed, now for statements
    while (line != '' and line.rstrip() != '}'):
        # process statements 
        toks, chars = ctok_nspace(line)
 
        if (len(toks) > 2 
          and (toks[0] in decl.keys())
          and (toks[1] == '=' or toks[1] == '+='
            or toks[1] == '-=' or toks[1] == '/='
            or toks[1] == '*=' or toks[1] == '&='
            or toks[1] == '|=' or toks[1] == '^=')):

            # assignment statement

            # extend logical 'line' and tokenise it
            while (toks[-1] != ';'):
                nline = file.readline()
                lineno += 1
                ntoks, nchars = ctok_nspace(nline)
                toks.extend(ntoks)
                line = line.rstrip() + ' ' + nline

            target = decl[toks[0]]
            level = 1

            # check that we're not assigning to something
            # we're not supposed to
            if (target.lineno == 0 
              and target.type[0:len('const')] == 'const'):
                print '#line', lineno, '"' + args[0] + '"'
                print '#error "assigning to const quantity"'

            used[toks[0]] = toks[0]

            for t in toks[2:]:
                if (t in decl.keys()):
                    used[t] = t
                    if (decl[t].level == 6):
                        print '#line', lineno, '"' + args[0] + '"'
                        print '#error "accumulator used as rvalue"'
                    elif (decl[t].level > level):
                        level = decl[t].level

            # increase level of assigned quantity, providing basic level
            # inference 
            if (toks[0] in decl.keys()):
                decl[toks[0]].level = level

            list.append([lineno, level, line.strip()])
        elif (len(toks) and line.strip()[0] == '#'):
            list.append([lineno, 1, '/* ' + line.strip()[1:].lstrip() + ' */'])
        else:
            # its a different type of statement, check level differently 
            level = 1
            for t in toks:
                if (t in decl.keys()):
                    used[t] = t
                    if (decl[t].level == 6):
                        print '#line', lineno, '"' + args[0] + '"'
                        print '#error "accumulator used as rvalue"'
                    elif (decl[t].level > level):
                        level = decl[t].level

            list.append([lineno, level, line.strip()])

        # next line
        line = file.readline()
        lineno += 1

    return [lineno, line]

def propagate(statements):
    """function to propagate levels from inside of braced statements
       to the whole braced statement."""
    # FIXME: needs to handle multiline blocks properly
    prevlevel = 0
    for s in statements:
        if (s[2].find('}') >= 0):
            if (s[2].find('{') < 0):
                if (prevlevel > s[1]):
                    s[1] = prevlevel
        prevlevel = s[1]

    rev = map(lambda x: x, statements)
    rev.reverse

    prevlevel = 0
    for s in rev:
        if (s[2].find('{') >= 0):
            if (s[2].find('}') < 0):
                if (prevlevel > s[1]):
                    s[1] = prevlevel
        prevlevel = s[1]

def output_decl(definitions, dent, mfile, params, macros):
    prevline = -1

    definitions.sort(lambda x, y: x.lineno - y.lineno)

    for d in definitions:
        # check that it isn't a pre-defined quantity that we've
        # already declared
        if (d.lineno != -1 or len(d.macro) > 0 or len(d.init) > 0 or len(d.fninit) > 0):
            # check if we have to output a #line directive
            if (d.lineno != prevline + 1 and d.lineno != -1):
                #print '#line', d.lineno, '"' + mfile + '"'
                pass
            prevline = d.lineno

            if (not len(d.macro)):
                str = dent + d.type
                if (len(d.type) and d.type[-1] == '*'):
                    str += d.name
                else:
                    str += ' ' + d.name

                if (len(d.init)):
                    str += ' '
                    # output the line, replacing parameters
                    toks, spaces = ctok(d.init)
                    for t in toks:
                        if (t in macros):
                            str += macros[t]
                        else:
                            str += t
                    print

                str += ';'
                print str

    num = 0
    for d in definitions:
        # check that it isn't a pre-defined quantity that we've
        # already declared
        if (len(d.fninit) > 0):
            print indent(dedent(d.fninit), len(dent))

def print_level(statements, level, dent, name, macros):
    # output statements that depend on level
    prevline = -1
    while (len(statements)):
        l, statements = statements[0], statements[1:]
        if (l[1] == level):

            # check if we have to output a #line directive
            if (l[0] != prevline + 1):
                #print '#line', l[0], '"' + args[0] + '"'
                pass
            prevline = l[0]

            # check if we need to decrease the dent (has to be before
            # output for ending } brackets to be indented properly)
            if (l[2].find('}') >= 0):
                if (l[2].find('{') < 0):
                    dent -= 4

            # output the line, replacing parameters
            toks, spaces = ctok(l[2])
            sys.stdout.write(' ' * dent)
            for t in toks:
                if (t in macros):
                    sys.stdout.write(macros[t])
                else:
                    sys.stdout.write(t)
            print

            # check if we need to increase the dent
            if (l[2].find('{') >= 0):
                if (l[2].find('}') < 0):
                    dent += 4

# get the next non-whitespace token, or nothing if there isn't one
def next_tok(toks):
    while (len(toks) and toks[0].isspace() == True):
        toks = toks[1:]
    return toks

def contrib_replace(x):
    if (len(x.contrib)):
        return x.contrib
    else:
        return x.macro

def get_replace_map(decls, fn = (lambda x: x.macro)):
    map = {}
    for i in filter(lambda x: len(fn(decls[x])) > 0, decls):
        map[i] = '(' + fn(decls[i]) + ')'
    return map

if __name__ == "__main__":
    try:
        (options, args) = getopt.getopt(sys.argv[1:], 'hv', 
            ['help', 'version', 'debug'])
    except getopt.GetoptError:
        usage(sys.argv[0])
        sys.exit(2)

    # simple processing of options
    header = False
    body = False
    debug = False
    help = False
    for opt, arg in options:
        if opt in ['--help', '-h']:
            help = True
        elif opt in ['--version', '-v']:
            print '%s version 0.2\n' % sys.argv[0]
            sys.exit(2)
        elif opt == '--debug':
            debug = True

    if (len(args) != 2 and not help):
        usage(sys.argv[0])
        sys.exit(2)

    params = {}      # parameter declarations
    post_decl = {}   # declarations in post
    post = []        # statements in post
    post_used = {}   # quantities used in post
    decode_decl = {} # declarations in decode
    decode = []      # statements in decode
    comments = []    # initial comments
    decode_used = {} # quantities used in decode

    # define pre-declared quantities in all three function namespaces
    #
    # levels are:
    #   - 0: undetermined
    #   - 1: depends on nothing
    #   - 2: depends on docno
    #   - 3: depends on f_dt
    #   - 4: depends on offset
    #   - 5: depends on attr

    # declarations for inherent quantities (these are not output directly) 
    ins_decl([decode_decl], [decode_used], 'const unsigned int f_t;', 1, 'query->term[qterm].f_t', 
      ex='number of documents in collection term occurs in')
    ins_decl([decode_decl], [decode_used], 'const unsigned int F_t;', 1, 'query->term[qterm].F_t',
      ex='number of times term occurs in collection')
    ins_decl([decode_decl], [decode_used], 'const unsigned int f_dt;', 3,
      ex='number of times term occurs in current document')
    #ins_decl([decode_decl], [decode_used], 'const unsigned int offset;', 4)
    #ins_decl([decode_decl], [decode_used], 'const unsigned int attr;', 5)

    ins_decl([decode_decl, post_decl], [decode_used, post_used], 
      'float accumulator;', 2, 'acc->acc.weight',
      ex='accumulated score of document')
    ins_decl([decode_decl, post_decl], [decode_used, post_used], 
      'const unsigned int dterms = iobtree_size(idx->vocab);', 1,
      ex='number of distinct terms in the collection')
    ins_decl([decode_decl, post_decl], [decode_used, post_used], 
      'const double terms = ((double) UINT_MAX) * idx->stats.terms_high + idx->stats.terms_low;', 1,
      ex='number of terms in the collection')
    ins_decl([decode_decl, post_decl], [decode_used, post_used], 
      'const unsigned int N = docmap_entries(idx->map);', 1,
      ex='number of documents in the collection')
    ins_decl([decode_decl, post_decl], [decode_used, post_used], 
      'double avg_D_bytes;', 1, '', -1, 
      '''\
         if (docmap_avg_bytes(idx->map, &avg_D_bytes) != DOCMAP_OK) {
             return SEARCH_EINVAL;
         }''',
      ex='average bytes per document in the collection')
    ins_decl([decode_decl, post_decl], [decode_used, post_used], 
      'double avg_D_terms;', 1, '', -1,
      '''\
         if (docmap_avg_words(idx->map, &avg_D_terms) != DOCMAP_OK) {
             return SEARCH_EINVAL;
         }''',
      ex='average terms per document in the collection')
    ins_decl([decode_decl, post_decl], [decode_used, post_used], 
      'double avg_D_dterms;', 1, '', -1,
      '''\
         if (docmap_avg_distinct_words(idx->map, &avg_D_dterms) != DOCMAP_OK) {
             return SEARCH_EINVAL;
         }''',
      ex='average distinct terms per document in the collection')
    ins_decl([decode_decl, post_decl], [decode_used, post_used], 
      'double avg_D_weight;', 1, '', -1,
      '''if (docmap_avg_weight(idx->map, &avg_D_weight) != DOCMAP_OK) {
             return SEARCH_EINVAL;
         }''',
      ex='average cosine weight per document in the collection')
    # ins_decl([decode_decl, post_decl], None, 'const unsigned int Q_bytes;', 1, 'qstat->bytes',
    #  ex='number of bytes in the query string')
    ins_decl([decode_decl, post_decl], [decode_used, post_used], 
      'const unsigned int Q_terms = search_qterms(query);', 1,
      ex='number of terms in the query')
    ins_decl([decode_decl, post_decl], [decode_used, post_used], 
      'const unsigned int Q_dterms;', 1, 'query->terms',
      ex='number of distinct terms in the query')
    ins_decl([decode_decl, post_decl], [decode_used, post_used], 
      'const float Q_weight = search_qweight(query);', 1,
      ex='cosine weight of query')
    ins_decl([decode_decl, post_decl], [decode_used, post_used], 
      'const unsigned int D_bytes;', 2, 'docmap_get_bytes_cached(idx->map, acc->acc.docno)', -1, '', 
      'if (docmap_cache(idx->map, docmap_get_cache(idx->map) | DOCMAP_CACHE_BYTES) != DOCMAP_OK) return SEARCH_EINVAL;', '((float) avg_D_bytes)',
      ex='number of bytes in the current document')
    ins_decl([decode_decl, post_decl], [decode_used, post_used], 
      'const unsigned int D_terms;', 2, 'DOCMAP_GET_WORDS(idx->map, acc->acc.docno)', -1, '',
      'if (docmap_cache(idx->map, docmap_get_cache(idx->map) | DOCMAP_CACHE_WORDS) != DOCMAP_OK) return SEARCH_EINVAL;', '((float) avg_D_terms)',
      ex='number of terms in the current document')
    ins_decl([decode_decl, post_decl], [decode_used, post_used], 
      'const unsigned int D_dterms;', 2, 'DOCMAP_GET_DISTINCT_WORDS(idx->map, acc->acc.docno)', -1, '',
      'if (docmap_cache(idx->map, docmap_get_cache(idx->map) | DOCMAP_CACHE_DISTINCT_WORDS) != DOCMAP_OK) return SEARCH_EINVAL;', '((float) avg_D_dterms)',
      ex='number of distinct terms in the current document')
    ins_decl([decode_decl, post_decl], [decode_used, post_used], 
      'const float D_weight;', 2, 'DOCMAP_GET_WEIGHT(idx->map, acc->acc.docno)', -1, '',
      'if (docmap_cache(idx->map, docmap_get_cache(idx->map) | DOCMAP_CACHE_WEIGHT) != DOCMAP_OK) return SEARCH_EINVAL;', '((float) avg_D_weight)',
      ex='cosine weight of the current document')
    ins_decl([decode_decl, post_decl], [decode_used, post_used], 
      'const unsigned int f_qt;', 1, 'query->term[qterm].f_qt',
      ex='number of times the current term occurred in the query')

    if (help):
        usage(sys.argv[0], decode_decl, post_decl)
        sys.exit(2)

    path = args[0].split('/')
    parts = path[-1].split('.')
    if (len(parts) > 0):
        name = parts[0]
    else:
        name = args[0]

    try:
        file = open(args[0])

        lineno = 1
        line = file.readline()
        while (line != ''):
            sline = line.strip()
            words = line.split()

            if (len(sline) == 0 or sline[0] == '#'):
                # its a or empty comment, do nothing
                if (lineno <= len(comments) + 1):
                    # preserve initial comments for insertion into
                    # final file
                    if (len(sline) > 0):
                        comments.append(line.strip()[1:].lstrip())
                    else:
                        comments.append('')
            elif (len(words) > 1 and words[0] == 'parameter'):
                # got parameter, process it (MUST be one line)

                vname = words[1:]
                while (isdecl(vname[0]) == 1):
                    vname = vname[1:]
                vname = vname[0].strip()
                if (vname[-1] == ';'):
                    vname = vname[0:-1]

                ins_decl([params, post_decl, decode_decl], 
                  [decode_used, post_used], line[len('parameter'):], 
                  level=1, lineno=lineno, 
                  macro='opt->u.' + name + '.' + vname)

            elif (len(words) == 2 and words[0] == 'post()' 
              and words[1] == '{'):
                # in post section

                [lineno, line] = process_decl(file, post_decl, post_used, lineno)

                [lineno, line] = levelise(file, line, lineno, post_decl, 
                  post, post_used)

                if (line == ''):
                    print '#line', lineno, '"' + args[0] + '"'
                    print '#error "unexpected EOF in post"'
                    sys.exit(2)

            elif (len(words) == 2 and words[0] == 'decode()' 
              and words[1] == '{'):
                # in decode section

                [lineno, line] = process_decl(file, decode_decl, decode_used, lineno)

                [lineno, line] = levelise(file, line, lineno, decode_decl, 
                  decode, decode_used)

                if (line == ''):
                    print '#line', lineno, '"' + args[0] + '"'
                    print '#error "unexpected EOF in decode"'
                    sys.exit(2)

            else:
                print '#line', lineno, '"' + args[0] + '"'
                print '#error "unexpected line:', line.rstrip(), '"'
                sys.exit(2)
       
            # next line
            line = file.readline()
            lineno += 1
        file.close()
    except IOError:
        # error reading from file
        print 'error opening file', args[0]
        sys.exit(2)

    # remove empty lines from the end of comments
    while (len(comments) > 0 and comments[-1] == ''):
        comments = comments[0:-1]

    # propagate levels backward to start/end of braced statements
    propagate(post)
    propagate(decode)

    # infer what goes into pre
    pre_infer = lambda used, decl: map(lambda x: decl[x].pre, filter(lambda x: x in used and len(decl[x].pre) > 0, decl))
    pre = pre_infer(post_used, post_decl)
    pre.extend(pre_infer(decode_used, decode_decl))

    if (debug): 
        # just dump everything
        print 'comments'
        print comments
        print
        print 'params'
        print params
        print
        print 'decode_decl'
        print decode_decl
        print
        print 'decode'
        print decode
        print
        print 'decode_used'
        print decode_used
        print
        print 'post_decl'
        print post_decl
        print
        print 'post'
        print post
        print
        print 'post_used'
        print post_used
    else:
        try:
            template = open(args[1])

            # insert our comment
            str = '''\
              /* %s.c implements the %s metric for the zettair query
               * subsystem.  This file was automatically generated from
               * %s and %s
               * by %s on %s.  
               *
               * DO NOT MODIFY THIS FILE, as changes will be lost upon 
               * subsequent regeneration (and this code is repetitive enough 
               * that you probably don't want to anyway).  
               * Go modify %s or %s instead.  
               * 
               * Comments from %s.metric:
               *'''
 
            print (dedent(str) % (name.lower(), name, args[0], args[1], 
              sys.argv[0], strftime("%a, %d %b %Y %H:%M:%S GMT", gmtime()), 
              args[0], args[1], name))

            for l in comments:
                print ' *', l

            if (len(comments) > 0):
                print ' *'

            print ' */'
            print 

            # process the file, tokenising so we can process comments
            # that aren't the only thing on a line
            first = 1
            currline = 0
            for l in template:
                currline += 1
                if (first):
                    if (l.find('*/') != -1):
                        # first comment over, print out line we're at in 
                        # the template
                        first = 0
                        #print '#line %u "%s"' % (currline, args[1])
                else:
                    toks, chars = ctok(l)
                    dent = len(l) - len(l.lstrip()) 

                    while (len(toks)):
                        nt = next_tok(toks[1:])
                        nnt = next_tok(nt[1:])
                        if (toks[0] == '/*' and len(nnt) and nnt[0] == '*/' 
                          and nt[0] == 'METRIC_NAME'):
                            print '/* METRIC_NAME */', name,
                            toks = nnt[1:]
                        elif (toks[0] == '/*' and len(nnt) and nnt[0] == '*/' 
                          and nt[0] == 'METRIC_DEPENDS_POST'):
                            print '/* METRIC_DEPENDS_POST */', len(post),
                            toks = nnt[1:]
                        elif (toks[0] == '/*' and len(nnt) and nnt[0] == '*/' 
                          and nt[0] == 'METRIC_PRE'):
                            print '/* METRIC_PRE */'
                            for l in pre:
                                print indent(l, dent)
                            #print '#line %u "%s"' % (currline, args[1])
                            toks = nnt[1:]
                        elif (toks[0] == '/*' and len(nnt) and nnt[0] == '*/' 
                          and nt[0] == 'METRIC_POST'):
                            print '/* METRIC_POST */'
                            output_decl(map(lambda x: post_decl[x], post_used.keys()), dent * ' ', args[0], params,
                              get_replace_map(post_decl))
                            print_level(post, 1, dent, name, 
                              get_replace_map(post_decl))
                            #print '#line %u "%s"' % (currline, args[1])
                            toks = nnt[1:]
                        elif (toks[0] == '/*' and len(nnt) and nnt[0] == '*/' 
                          and nt[0] == 'METRIC_POST_PER_DOC'):
                            print '/* METRIC_POST_PER_DOC */'
                            print_level(post, 2, dent, name, 
                              get_replace_map(post_decl))
                            #print '#line %u "%s"' % (currline, args[1])
                            toks = nnt[1:]
                        elif (toks[0] == '/*' and len(nnt) and nnt[0] == '*/' 
                          and nt[0] == 'METRIC_DECL'):
                            print '/* METRIC_DECL */'
                            output_decl(map(lambda x: decode_decl[x], decode_used.keys()), dent * ' ', args[0], params,
                              get_replace_map(decode_decl))
                            toks = nnt[1:]
                        elif (toks[0] == '/*' and len(nnt) and nnt[0] == '*/' 
                          and nt[0] == 'METRIC_PER_CALL'):
                            print '/* METRIC_PER_CALL */'
                            # output level one stuff
                            print_level(decode, 1, dent, name, 
                              get_replace_map(decode_decl))
                            #print '#line %u "%s"' % (currline, args[1])
                            toks = nnt[1:]
                        elif (toks[0] == '/*' and len(nnt) and nnt[0] == '*/' 
                          and nt[0] == 'METRIC_PER_DOC'):
                            print '/* METRIC_PER_DOC */'
                            # output level two and level three stuff
                            print_level(decode, 2, dent, name, 
                              get_replace_map(decode_decl))
                            print_level(decode, 3, dent, name,
                              get_replace_map(decode_decl))
                            #print '#line %u "%s"' % (currline, args[1])
                            toks = nnt[1:]
                        elif (toks[0] == '/*' and len(nnt) and nnt[0] == '*/' 
                          and nt[0] == 'METRIC_CONTRIB'):
                            print '/* METRIC_CONTRIB */'
                            # output level two and three stuff,
                            # but using averages instead of specific
                            # doc values
                            print_level(decode, 2, dent, name, 
                              get_replace_map(decode_decl, contrib_replace))
                            print_level(decode, 3, dent, name,
                              get_replace_map(decode_decl, contrib_replace))
                            #print '#line %u "%s"' % (currline, args[1])
                            toks = nnt[1:]
                        else:
                            sys.stdout.write(toks[0])
                            toks = toks[1:]
        except IOError:
            # error reading from file
            print 'error opening file', args[1]
            sys.exit(2)

