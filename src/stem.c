/* stem.c implements a basic caching stemming framework.  The memory
 * management behind this could be much improved (with custom memory
 * management).
 *
 * written nml 2004-06-17
 *
 */

#include "firstinclude.h"

#include "stem.h"

#include "bit.h"
#include "chash.h"
#include "str.h"

#include <assert.h>
#include <stdlib.h>

/* constant used to approximate LRU algorithm with clock algorithm */
#define LRU_DEFAULT 2

struct stem_entry {
    unsigned int length;        /* length of term */
    unsigned int count;         /* clock algorithm count */
    char *src;                  /* term pre-stemming */
    char *dst;                  /* term post-stemming */
};

struct stem_cache {
    void (*stem) (void*, char*);/* stemming algorithm */
    void *opaque;               /* opaque stemmer data */
    unsigned int capacity;      /* maximum number of entries */
    unsigned int size;          /* current number of entries */
    struct chash *lookup;       /* hashtable lookup for entries */
    struct stem_entry **arr;    /* array of entries for clock algorithm */
    unsigned int clock_pos;     /* clock position in array */
    unsigned int stemmed;       /* number of terms stemmed */
    unsigned int cached;        /* number of terms stemmed from cache */
};

struct stem_cache *stem_cache_new(void (*stemmer) (void *opaque, char *term),
  void *opaque, unsigned int entries) {
    struct stem_cache *cache;

    if ((cache = malloc(sizeof(*cache)))
      && (cache->arr = malloc(sizeof(*cache->arr) * entries))
      && (cache->lookup = chash_ptr_new(bit_log2(entries) + 1, 2.0, 
          (unsigned int (*)(const void *)) str_hash,
          (int (*)(const void *, const void *)) str_cmp))) {

        cache->capacity = entries;
        cache->size = 0;
        cache->stem = stemmer;
        cache->opaque = opaque;
        cache->clock_pos = 0;
        cache->stemmed = cache->cached = 0;
    } else {
        if (cache) {
            if (cache->arr) {
                free(cache->arr);
            }
            free(cache);
            cache = NULL;
        }
    }

    return cache;
}

void stem_cache_delete(struct stem_cache *cache) {
    unsigned int i;

    for (i = 0; i < cache->size; i++) {
        free(cache->arr[i]->src);
        free(cache->arr[i]->dst);
        free(cache->arr[i]);
    }

    chash_delete(cache->lookup);
    free(cache->arr);
    free(cache);
}

static struct stem_entry *clock_select(struct stem_cache *cache) {
    unsigned int i;

    assert(cache->size);

    do {
        for (i = cache->clock_pos; i < cache->size; i++) {
            if (!cache->arr[i]->count) {
                /* selected one */
                cache->clock_pos = i + 1;
                return cache->arr[i];
            } else {
                cache->arr[i]->count--;
            }
        }

        for (i = 0; i < cache->clock_pos; i++) {
            if (!cache->arr[i]->count) {
                /* selected one */
                cache->clock_pos = i + 1;
                return cache->arr[i];
            } else {
                cache->arr[i]->count--;
            }
        }
    } while (1);
}

unsigned int stem_cache_capacity(struct stem_cache *cache) {
    return cache->capacity;
}

void stem_cache_stem(struct stem_cache *cache, char *term) {
    struct stem_entry *target;
    void **find;
    unsigned int len;

    cache->stemmed++;

    /* lookup term in stem cache */
    if (chash_ptr_ptr_find(cache->lookup, term, &find) == CHASH_OK) {
        /* found it, copy term in and return */
        target = *find;
        assert(str_len(target->dst) <= str_len(term));
        str_cpy(term, target->dst);
        cache->cached++;
        return;
    } else {
        /* didn't find it, find a stem_entry to use */
        if (cache->size < cache->capacity) {
            len = str_len(term) + 1;

            if ((target = malloc(sizeof(*target)))
              && (target->src = malloc(len))
              && (target->dst = malloc(len))) {
                target->count = LRU_DEFAULT;
                target->length = len;
                memcpy(target->src, term, len);
                memcpy(target->dst, term, len);
                cache->stem(cache->opaque, target->dst);
                str_cpy(term, target->dst);

                if (chash_ptr_ptr_insert(cache->lookup, 
                    target->src, target) == CHASH_OK) {

                    /* finished inserting new entry */
                    cache->arr[cache->size++] = target;
                    return;
                } else {
                    /* couldn't insert entry */
                    free(target->src);
                    free(target->dst);
                    free(target);
                }
            } else {
                if (target) {
                    if (target->src) {
                        free(target->src);
                    }
                    free(target);
                }
            }
        }

        /* couldn't allocate another entry for whatever reason. Remove an
           existing one using clock algorithm and reuse it */
        if (cache->size) {
            void *ptr_one,
                 *ptr_two;

            target = clock_select(cache);
            assert(target);

            len = str_len(term) + 1;

            /* obtained suitable entry, remove it under old string */
            if (chash_ptr_ptr_remove(cache->lookup, target->src, &ptr_one) 
              == CHASH_OK) {
                assert(ptr_one == target);

                if (target->length < len) {
                    if ((ptr_one = realloc(target->src, len))
                      && (ptr_two = realloc(target->dst, len))) {
                        target->src = ptr_one;
                        target->dst = ptr_two;
                        target->length = len;
                    } else {
                        unsigned int i;

                        if (ptr_one) {
                            target->src = ptr_one;
                        }

                        /* remove it from the array (by linear search) */
                        cache->size--;
                        for (i = 0; i <= cache->size; i++) {
                            if (target == cache->arr[i]) {
                                memcpy(cache->arr[i], cache->arr[i + 1],
                                  sizeof(cache->arr[0]) * (cache->size - i));
                                break;
                            }
                        }

                        free(target->src);
                        free(target->dst);
                        free(target);
                        target = NULL;

                        cache->stem(cache->opaque, term);
                        return;
                    }
                }

                memcpy(target->src, term, len);
                memcpy(target->dst, term, len);
                cache->stem(cache->opaque, target->dst);
                target->count = LRU_DEFAULT;

                assert(str_len(target->dst) < len);
                str_cpy(term, target->dst);

                /* insert new entry back into hashtable */
                if (chash_ptr_ptr_insert(cache->lookup, target->src, 
                    target) == CHASH_OK) {

                    /* our work here is done... */
                    return;
                } else {
                    /* removed an entry, but couldn't put it back (?) */
                    unsigned int i;

                    /* remove it from the array (by linear search) */
                    cache->size--;
                    for (i = 0; i <= cache->size; i++) {
                        if (target == cache->arr[i]) {
                            memcpy(cache->arr[i], cache->arr[i + 1],
                              sizeof(cache->arr[0]) * (cache->size - i));
                            break;
                        }
                    }

                    free(target->src);
                    free(target->dst);
                    free(target);
                    target = NULL;
                }
            }
        }

        /* couldn't do anything efficient, just stem the term and send it 
         * back */
        cache->stem(cache->opaque, term);
        return;
    }
}

/* This is the Porter stemming algorithm, coded up as thread-safe ANSI C
   by the author.

   It may be be regarded as cononical, in that it follows the algorithm
   presented in

   Porter, 1980, An algorithm for suffix stripping, Program, Vol. 14,
   no. 3, pp 130-137,

   only differing from it at the points maked --DEPARTURE-- below.

   See also http://www.tartarus.org/~martin/PorterStemmer

   The algorithm as described in the paper could be exactly replicated
   by adjusting the points of DEPARTURE, but this is barely necessary,
   because (a) the points of DEPARTURE are definitely improvements, and
   (b) no encoding of the Porter stemmer I have seen is anything like
   as exact as this version, even with the points of DEPARTURE!

   The algorithm as encoded here is particularly fast.

   Release 2 (the more old-fashioned, non-thread-safe version may be
   regarded as release 1.)
*/

#include <stdlib.h>  /* for malloc, free */
#include <string.h>  /* for memcmp, memmove */

/* You will probably want to move the following declarations to a central
   header file.
*/

struct stemmer;

extern struct stemmer * create_stemmer(void);
extern void free_stemmer(struct stemmer * z);

extern int stem(struct stemmer * z, char * b, int k);

/* The main part of the stemming algorithm starts here.
*/

#define TRUE 1
#define FALSE 0

/* stemmer is a structure for a few local bits of data,
*/

struct stemmer {
   char * b;       /* buffer for word to be stemmed */
   int k;          /* offset to the end of the string */
   int j;          /* a general offset into the string */
};

/* Member b is a buffer holding a word to be stemmed. The letters are in
   b[0], b[1] ... ending at b[z->k]. Member k is readjusted downwards as
   the stemming progresses. Zero termination is not in fact used in the
   algorithm.

   Note that only lower case sequences are stemmed. Forcing to lower case
   should be done before stem(...) is called.


   Typical usage is:

       struct stemmer * z = create_stemmer();
       char b[] = "pencils";
       int res = stem(z, b, 6);
           /- stem the 7 characters of b[0] to b[6]. The result, res,
              will be 5 (the 's' is removed). -/
       free_stemmer(z);
*/

extern struct stemmer * create_stemmer(void)
{
    return (struct stemmer *) malloc(sizeof(struct stemmer));
    /* assume malloc succeeds */
}

extern void free_stemmer(struct stemmer * z)
{
    free(z);
}

/* cons(z, i) is TRUE <=> b[i] is a consonant. ('b' means 'z->b', but here
   and below we drop 'z->' in comments.
*/

static int cons(struct stemmer * z, int i)
{  switch (z->b[i])
   {  case 'a': case 'e': case 'i': case 'o': case 'u': return FALSE;
      case 'y': return (i == 0) ? TRUE : !cons(z, i - 1);
      default: return TRUE;
   }
}

/* m(z) measures the number of consonant sequences between 0 and j. if c is
   a consonant sequence and v a vowel sequence, and <..> indicates arbitrary
   presence,

      <c><v>       gives 0
      <c>vc<v>     gives 1
      <c>vcvc<v>   gives 2
      <c>vcvcvc<v> gives 3
      ....
*/

static int m(struct stemmer * z)
{  int n = 0;
   int i = 0;
   int j = z->j;
   while(TRUE)
   {  if (i > j) return n;
      if (! cons(z, i)) break; i++;
   }
   i++;
   while(TRUE)
   {  while(TRUE)
      {  if (i > j) return n;
            if (cons(z, i)) break;
            i++;
      }
      i++;
      n++;
      while(TRUE)
      {  if (i > j) return n;
         if (! cons(z, i)) break;
         i++;
      }
      i++;
   }
}

/* vowelinstem(z) is TRUE <=> 0,...j contains a vowel */

static int vowelinstem(struct stemmer * z)
{
   int j = z->j;
   int i; for (i = 0; i <= j; i++) if (! cons(z, i)) return TRUE;
   return FALSE;
}

/* doublec(z, j) is TRUE <=> j,(j-1) contain a double consonant. */

static int doublec(struct stemmer * z, int j)
{
   char * b = z->b;
   if (j < 1) return FALSE;
   if (b[j] != b[j - 1]) return FALSE;
   return cons(z, j);
}

/* cvc(z, i) is TRUE <=> i-2,i-1,i has the form consonant - vowel - consonant
   and also if the second c is not w,x or y. this is used when trying to
   restore an e at the end of a short word. e.g.

      cav(e), lov(e), hop(e), crim(e), but
      snow, box, tray.

*/

static int cvc(struct stemmer * z, int i)
{  if (i < 2 || !cons(z, i) || cons(z, i - 1) || !cons(z, i - 2)) return FALSE;
   {  int ch = z->b[i];
      if (ch  == 'w' || ch == 'x' || ch == 'y') return FALSE;
   }
   return TRUE;
}

/* ends(z, s) is TRUE <=> 0,...k ends with the string s. */

static int ends(struct stemmer * z, char * s)
{  int length = s[0];
   char * b = z->b;
   int k = z->k;
   if (s[length] != b[k]) return FALSE; /* tiny speed-up */
   if (length > k + 1) return FALSE;
   if (memcmp(b + k - length + 1, s + 1, length) != 0) return FALSE;
   z->j = k-length;
   return TRUE;
}

/* setto(z, s) sets (j+1),...k to the characters in the string s, readjusting
   k. */

static void setto(struct stemmer * z, char * s)
{  int length = s[0];
   int j = z->j;
   memmove(z->b + j + 1, s + 1, length);
   z->k = j+length;
}

/* r(z, s) is used further down. */

static void r(struct stemmer * z, char * s) { if (m(z) > 0) setto(z, s); }

/* step1ab(z) gets rid of plurals and -ed or -ing. e.g.

       caresses  ->  caress
       ponies    ->  poni
       ties      ->  ti
       caress    ->  caress
       cats      ->  cat

       feed      ->  feed
       agreed    ->  agree
       disabled  ->  disable

       matting   ->  mat
       mating    ->  mate
       meeting   ->  meet
       milling   ->  mill
       messing   ->  mess

       meetings  ->  meet

*/

static void step1ab(struct stemmer * z)
{
   char * b = z->b;
   if (b[z->k] == 's')
   {  if (ends(z, "\04" "sses")) z->k -= 2; else
      if (ends(z, "\03" "ies")) setto(z, "\01" "i"); else
      if (b[z->k - 1] != 's') z->k--;
   }
   if (ends(z, "\03" "eed")) { if (m(z) > 0) z->k--; } else
   if ((ends(z, "\02" "ed") || ends(z, "\03" "ing")) && vowelinstem(z))
   {  z->k = z->j;
      if (ends(z, "\02" "at")) setto(z, "\03" "ate"); else
      if (ends(z, "\02" "bl")) setto(z, "\03" "ble"); else
      if (ends(z, "\02" "iz")) setto(z, "\03" "ize"); else
      if (doublec(z, z->k))
      {  z->k--;
         {  int ch = b[z->k];
            if (ch == 'l' || ch == 's' || ch == 'z') z->k++;
         }
      }
      else if (m(z) == 1 && cvc(z, z->k)) setto(z, "\01" "e");
   }
}

/* step1c(z) turns terminal y to i when there is another vowel in the stem. */

static void step1c(struct stemmer * z)
{
   if (ends(z, "\01" "y") && vowelinstem(z)) z->b[z->k] = 'i';
}

/* step2(z) maps double suffices to single ones. so -ization ( = -ize plus
   -ation) maps to -ize etc. note that the string before the suffix must give
   m(z) > 0. */

static void step2(struct stemmer * z) { switch (z->b[z->k-1])
{
   case 'a': if (ends(z, "\07" "ational")) { r(z, "\03" "ate"); break; }
             if (ends(z, "\06" "tional")) { r(z, "\04" "tion"); break; }
             break;
   case 'c': if (ends(z, "\04" "enci")) { r(z, "\04" "ence"); break; }
             if (ends(z, "\04" "anci")) { r(z, "\04" "ance"); break; }
             break;
   case 'e': if (ends(z, "\04" "izer")) { r(z, "\03" "ize"); break; }
             break;
   case 'l': if (ends(z, "\03" "bli")) { r(z, "\03" "ble"); break; } /*-DEPARTURE-*/

 /* To match the published algorithm, replace this line with
    case 'l': if (ends(z, "\04" "abli")) { r(z, "\04" "able"); break; } */

             if (ends(z, "\04" "alli")) { r(z, "\02" "al"); break; }
             if (ends(z, "\05" "entli")) { r(z, "\03" "ent"); break; }
             if (ends(z, "\03" "eli")) { r(z, "\01" "e"); break; }
             if (ends(z, "\05" "ousli")) { r(z, "\03" "ous"); break; }
             break;
   case 'o': if (ends(z, "\07" "ization")) { r(z, "\03" "ize"); break; }
             if (ends(z, "\05" "ation")) { r(z, "\03" "ate"); break; }
             if (ends(z, "\04" "ator")) { r(z, "\03" "ate"); break; }
             break;
   case 's': if (ends(z, "\05" "alism")) { r(z, "\02" "al"); break; }
             if (ends(z, "\07" "iveness")) { r(z, "\03" "ive"); break; }
             if (ends(z, "\07" "fulness")) { r(z, "\03" "ful"); break; }
             if (ends(z, "\07" "ousness")) { r(z, "\03" "ous"); break; }
             break;
   case 't': if (ends(z, "\05" "aliti")) { r(z, "\02" "al"); break; }
             if (ends(z, "\05" "iviti")) { r(z, "\03" "ive"); break; }
             if (ends(z, "\06" "biliti")) { r(z, "\03" "ble"); break; }
             break;
   case 'g': if (ends(z, "\04" "logi")) { r(z, "\03" "log"); break; } /*-DEPARTURE-*/

 /* To match the published algorithm, delete this line */

} }

/* step3(z) deals with -ic-, -full, -ness etc. similar strategy to step2. */

static void step3(struct stemmer * z) { switch (z->b[z->k])
{
   case 'e': if (ends(z, "\05" "icate")) { r(z, "\02" "ic"); break; }
             if (ends(z, "\05" "ative")) { r(z, "\00" ""); break; }
             if (ends(z, "\05" "alize")) { r(z, "\02" "al"); break; }
             break;
   case 'i': if (ends(z, "\05" "iciti")) { r(z, "\02" "ic"); break; }
             break;
   case 'l': if (ends(z, "\04" "ical")) { r(z, "\02" "ic"); break; }
             if (ends(z, "\03" "ful")) { r(z, "\00" ""); break; }
             break;
   case 's': if (ends(z, "\04" "ness")) { r(z, "\00" ""); break; }
             break;
} }

/* step4(z) takes off -ant, -ence etc., in context <c>vcvc<v>. */

static void step4(struct stemmer * z)
{  switch (z->b[z->k-1])
   {  case 'a': if (ends(z, "\02" "al")) break; return;
      case 'c': if (ends(z, "\04" "ance")) break;
                if (ends(z, "\04" "ence")) break; return;
      case 'e': if (ends(z, "\02" "er")) break; return;
      case 'i': if (ends(z, "\02" "ic")) break; return;
      case 'l': if (ends(z, "\04" "able")) break;
                if (ends(z, "\04" "ible")) break; return;
      case 'n': if (ends(z, "\03" "ant")) break;
                if (ends(z, "\05" "ement")) break;
                if (ends(z, "\04" "ment")) break;
                if (ends(z, "\03" "ent")) break; return;
      case 'o': if (ends(z, "\03" "ion") && (z->b[z->j] == 's' || z->b[z->j] == 't')) break;
                if (ends(z, "\02" "ou")) break; return;
                /* takes care of -ous */
      case 's': if (ends(z, "\03" "ism")) break; return;
      case 't': if (ends(z, "\03" "ate")) break;
                if (ends(z, "\03" "iti")) break; return;
      case 'u': if (ends(z, "\03" "ous")) break; return;
      case 'v': if (ends(z, "\03" "ive")) break; return;
      case 'z': if (ends(z, "\03" "ize")) break; return;
      default: return;
   }
   if (m(z) > 1) z->k = z->j;
}

/* step5(z) removes a final -e if m(z) > 1, and changes -ll to -l if
   m(z) > 1. */

static void step5(struct stemmer * z)
{
   char * b = z->b;
   z->j = z->k;
   if (b[z->k] == 'e')
   {  int a = m(z);
      if (a > 1 || (a == 1 && !cvc(z, z->k - 1))) z->k--;
   }
   if (b[z->k] == 'l' && doublec(z, z->k) && m(z) > 1) z->k--;
}

/* nml: this function slightly modified to not require external stemmer 
 * structure or length count (accepts NUL-terminated term) */
void stem_porters(void *opaque, char *term) {
   struct stemmer z;

   z.b = term; z.k = str_len(term) - 1; /* copy the parameters into z */
   if (z.k <= 1) return; /*-DEPARTURE-*/

   /* With this line, strings of length 1 or 2 don't go through the
      stemming process, although no mention is made of this in the
      published algorithm. Remove the line to match the published
      algorithm. */

   step1ab(&z); step1c(&z); step2(&z); step3(&z); step4(&z); step5(&z);
   term[z.k + 1] = '\0';  /* zero-terminate string */
}

/* stem_eds implements a function to stem terms by removing (from the end)
 * 'e', 'ed', and 's'.  Its also intended to be very fast, stemming via a
 * single pass through the term. */
void stem_eds(void *opaque, char *term) {
    char *suffix;

    /* don't stem away first or second letters of terms */
    if (!*term++ || !*term++) {
        return;
    }

    /* fallthrough to suffixless state */

suffixless_label:
    /* no suffixes currently recognised */
    do {
        switch (*term) {
        case 'e':
        case 'E':
            /* recognise potential e suffix */
            suffix = term++;
            goto e_label;
    
        case 's':
        case 'S':
            /* recognise potential s suffix */
            suffix = term++;
            goto s_label;
    
        case '\0':
            return;
    
        default: 
            term++;
            break;
        }
    } while (1);
    
e_label:
    do {
        switch (*term) {
        case 'e':
        case 'E':
            /* recognise potential e suffix */
            suffix = term++;
            break;

        case 'd':
        case 'D':
            /* continue recognising potential ed suffix */
            term++;
            goto ed_label;

        case 's':
        case 'S':
            /* recognise potential s suffix */
            suffix = term++;
            goto s_label;

        case '\0':
            /* stem e suffix away */
            *suffix = '\0';
            return;
    
        default: 
            /* no longer recognising a suffix */
            term++;
            goto suffixless_label;
        }
    } while (1);

ed_label:
    do {
        switch (*term) {
        case 'e':
        case 'E':
            /* recognise potential e suffix */
            suffix = term++;
            goto e_label;

        case 's':
        case 'S':
            /* recognise potential s suffix */
            suffix = term++;
            goto s_label;

        case '\0':
            /* stem ed suffix away */
            *suffix = '\0';
            return;
    
        default: 
            /* no longer recognising a suffix */
            term++;
            goto suffixless_label;
        }
    } while (1);

s_label:
    do {
        switch (*term) {
        case 'e':
        case 'E':
            /* recognise potential e suffix */
            suffix = term++;
            goto e_label;

        case 's':
        case 'S':
            /* recognise potential s suffix */
            suffix = term++;
            break;

        case '\0':
            /* stem s suffix away */
            *suffix = '\0';
            return;
    
        default: 
            /* no longer recognising a suffix */
            term++;
            goto suffixless_label;
        }
    } while (1);
}

/* stem_light implements a function to lightly stem english terms by 
 *  - removing suffix -e -es -s -ed -ing -ly -ingly
 *  - replacing suffix -ies, -ied  with -y
 * Its also intended to be very fast, stemming via a single pass through the 
 * term. */
void stem_light(void *opaque, char *term) {
    char *suffix;

    /* don't stem away first or second letters of terms */
    if (!*term++ || !*term++) {
        return;
    }

    /* fallthrough to suffixless state */

suffixless_label:
    /* no suffixes currently recognised */
    do {
        switch (*term) {
        case 'e':
        case 'E':
            /* recognise potential e suffix */
            suffix = term++;
            goto e_label;

        case 'i':
        case 'I':
            /* recognise potential ing, ingly, ies, ied suffixes */
            suffix = term++;
            goto i_label;

        case 'l':
        case 'L':
            /* recognise potential ly suffix */
            suffix = term++;
            goto l_label;
    
        case 's':
        case 'S':
            /* recognise potential s suffix */
            suffix = term++;
            goto s_label;
    
        case '\0':
            return;
    
        default: 
            term++;
            break;
        }
    } while (1);

l_label:
    do {
        switch (*term) {
        case 'y':
        case 'Y':
            /* continue recognising ly suffix */
            term++;
            goto ly_label;

        case 'e':
        case 'E':
            /* recognise potential e suffix */
            suffix = term++;
            goto e_label;

        case 'i':
        case 'I':
            /* recognise potential ing, ingly, ies, ied suffixes */
            suffix = term++;
            goto i_label;

        case 'l':
        case 'L':
            /* recognise potential ly suffix */
            suffix = term++;
            goto l_label;
    
        case 's':
        case 'S':
            /* recognise potential s suffix */
            suffix = term++;
            goto s_label;
    
        case '\0':
            return;
    
        default: 
            /* no longer recognising a suffix */
            term++;
            goto suffixless_label;
        }
    } while (1);

ly_label:
     do {
        switch (*term) {
        case 'e':
        case 'E':
            /* recognise potential e suffix */
            suffix = term++;
            goto e_label;

        case 'i':
        case 'I':
            /* recognise potential ing, ingly, ies, ied suffixes */
            suffix = term++;
            goto i_label;

        case 'l':
        case 'L':
            /* recognise potential ly suffix */
            suffix = term++;
            goto l_label;
    
        case 's':
        case 'S':
            /* recognise potential s suffix */
            suffix = term++;
            goto s_label;
    
        case '\0':
            /* stem away -ly suffix */
            *suffix = '\0';
            return;
    
        default: 
            /* no longer recognising a suffix */
            term++;
            goto suffixless_label;
        }
    } while (1);
  
i_label:
    do {
        switch (*term) {
        case 'e':
        case 'E':
            /* continue recognise potential ies, ied suffixes */
            term++;
            goto ie_label;

        case 'n':
        case 'N':
            /* continue recognise potential ing, ingly suffixes */
            term++;
            goto in_label;

        case 'i':
        case 'I':
            /* recognise potential ing, ingly, ies, ied suffixes */
            suffix = term++;
            goto i_label;

        case 'l':
        case 'L':
            /* recognise potential ly suffix */
            suffix = term++;
            goto l_label;
    
        case 's':
        case 'S':
            /* recognise potential s suffix */
            suffix = term++;
            goto s_label;
    
        case '\0':
            return;
    
        default: 
            /* no longer recognising a suffix */
            term++;
            goto suffixless_label;
        }
    } while (1);

ie_label:
    do {
        switch (*term) {
        case 'd':
        case 'D':
        case 's':
        case 'S':
            /* continue recognising potential ied or ies suffixes */
            term++;
            goto iesd_label;

        case 'e':
        case 'E':
            /* recognise potential e suffix */
            suffix = term++;
            goto e_label;

        case 'i':
        case 'I':
            /* recognise potential ing, ingly, ies, ied suffixes */
            suffix = term++;
            goto i_label;

        case 'l':
        case 'L':
            /* recognise potential ly suffix */
            suffix = term++;
            goto l_label;
    
        case '\0':
            return;
    
        default: 
            /* no longer recognising a suffix */
            term++;
            goto suffixless_label;
        }
    } while (1);

iesd_label:
    do {
        switch (*term) {
        case 's':
        case 'S':
            /* recognise potential s suffix */
            suffix = term++;
            goto s_label;

        case 'e':
        case 'E':
            /* recognise potential e suffix */
            suffix = term++;
            goto e_label;

        case 'i':
        case 'I':
            /* recognise potential ing, ingly, ies, ied suffixes */
            suffix = term++;
            goto i_label;

        case 'l':
        case 'L':
            /* recognise potential ly suffix */
            suffix = term++;
            goto l_label;
    
        case '\0':
            /* replace -ies or -ied suffixes with -y suffix */
            *suffix++ = 'y';
            *suffix = '\0';
            return;
    
        default: 
            /* no longer recognising a suffix */
            term++;
            goto suffixless_label;
        }
    } while (1);

in_label:
    do {
        switch (*term) {
        case 'g':
        case 'G':
            /* continue recognising potential ing, ingly suffixes */
            term++;
            goto ing_label;

        case 's':
        case 'S':
            /* recognise potential s suffix */
            suffix = term++;
            goto s_label;

        case 'e':
        case 'E':
            /* recognise potential e suffix */
            suffix = term++;
            goto e_label;

        case 'i':
        case 'I':
            /* recognise potential ing, ingly, ies, ied suffixes */
            suffix = term++;
            goto i_label;

        case 'l':
        case 'L':
            /* recognise potential ly suffix */
            suffix = term++;
            goto l_label;
    
        case '\0':
            return;
    
        default: 
            /* no longer recognising a suffix */
            term++;
            goto suffixless_label;
        }
    } while (1);

ing_label:
    do {
        switch (*term) {
        case 's':
        case 'S':
            /* recognise potential s suffix */
            suffix = term++;
            goto s_label;

        case 'e':
        case 'E':
            /* recognise potential e suffix */
            suffix = term++;
            goto e_label;

        case 'i':
        case 'I':
            /* recognise potential ing, ingly, ies, ied suffixes */
            suffix = term++;
            goto i_label;

        case 'l':
        case 'L':
            /* recognise potential ingly suffix */
            term++;
            goto ingl_label;
 
        case '\0':
            /* remove -ing suffix */
            *suffix = '\0';
            return;
    
        default: 
            /* no longer recognising a suffix */
            term++;
            goto suffixless_label;
        }
    } while (1);

ingl_label:
    do {
        switch (*term) {
        case 'y':
        case 'Y':
            /* recognise potential ingly suffix */
            term++;
            goto ingly_label;

        case 's':
        case 'S':
            /* recognise potential s suffix */
            suffix = term++;
            goto s_label;

        case 'e':
        case 'E':
            /* recognise potential e suffix */
            suffix = term++;
            goto e_label;

        case 'i':
        case 'I':
            /* recognise potential ing, ingly, ies, ied suffixes */
            suffix = term++;
            goto i_label;

        case 'l':
        case 'L':
            /* recognise potential ly suffix */
            suffix = term++;
            goto l_label;
 
        case '\0':
            return;
    
        default: 
            /* no longer recognising a suffix */
            term++;
            goto suffixless_label;
        }
    } while (1);

ingly_label:
    do {
        switch (*term) {
        case 's':
        case 'S':
            /* recognise potential s suffix */
            suffix = term++;
            goto s_label;

        case 'e':
        case 'E':
            /* recognise potential e suffix */
            suffix = term++;
            goto e_label;

        case 'i':
        case 'I':
            /* recognise potential ing, ingly, ies, ied suffixes */
            suffix = term++;
            goto i_label;

        case 'l':
        case 'L':
            /* recognise potential ly suffix */
            suffix = term++;
            goto l_label;
 
        case '\0':
            /* remove -ingly suffix */
            *suffix = '\0';
            return;
    
        default: 
            /* no longer recognising a suffix */
            term++;
            goto suffixless_label;
        }
    } while (1);

e_label:
    do {
        switch (*term) {
        case 'e':
        case 'E':
            /* recognise potential e suffix */
            suffix = term++;
            break;

        case 'd':
        case 'D':
        case 's':
        case 'S':
            /* continue recognising potential ed or es suffixes */
            term++;
            goto eds_label;

        case 'i':
        case 'I':
            /* recognise potential ing, ingly, ies, ied suffixes */
            suffix = term++;
            goto i_label;

        case 'l':
        case 'L':
            /* recognise potential ly suffix */
            suffix = term++;
            goto l_label;
    
        case '\0':
            /* stem e suffix away */
            *suffix = '\0';
            return;
    
        default: 
            /* no longer recognising a suffix */
            term++;
            goto suffixless_label;
        }
    } while (1);

eds_label:
    do {
        switch (*term) {
        case 'e':
        case 'E':
            /* recognise potential e suffix */
            suffix = term++;
            goto e_label;

        case 's':
        case 'S':
            /* recognise potential s suffix */
            suffix = term++;
            goto s_label;

        case 'i':
        case 'I':
            /* recognise potential ing, ingly, ies, ied suffixes */
            suffix = term++;
            goto i_label;

        case 'l':
        case 'L':
            /* recognise potential ly suffix */
            suffix = term++;
            goto l_label;

        case '\0':
            /* stem -ed or -es suffix away */
            *suffix = '\0';
            return;
    
        default: 
            /* no longer recognising a suffix */
            term++;
            goto suffixless_label;
        }
    } while (1);

s_label:
    do {
        switch (*term) {
        case 'e':
        case 'E':
            /* recognise potential e suffix */
            suffix = term++;
            goto e_label;

        case 's':
        case 'S':
            /* recognise potential s suffix */
            suffix = term++;
            break;

        case 'i':
        case 'I':
            /* recognise potential ing, ingly, ies, ied suffixes */
            suffix = term++;
            goto i_label;

        case 'l':
        case 'L':
            /* recognise potential ly suffix */
            suffix = term++;
            goto l_label;

        case '\0':
            /* stem -s suffix away */
            *suffix = '\0';
            return;
    
        default: 
            /* no longer recognising a suffix */
            term++;
            goto suffixless_label;
        }
    } while (1);
}

#ifdef STEM_TEST
#include <errno.h>
#include <stdio.h>

int main(int argc, char **argv) {
    struct stem_cache *stem = stem_cache_new(stem_porters, NULL, 200);
    char buf[BUFSIZ + 1];
    unsigned int i;

    buf[BUFSIZ] = '\0';

    if (!stem) {
        return EXIT_FAILURE;
    }

    if (argc == 1) {
        while (fgets(buf, BUFSIZ, stdin)) {
            char *start;

            str_rtrim(buf);
            start = (char *) str_ltrim(buf);
            stem_cache_stem(stem, start);
            printf("%s\n", start);
        }
    } else {
        for (i = 1; i < argc; i++) {
            FILE *input = fopen(argv[i], "rb+");

            if (!input) {
                fprintf(stderr, "failed to open file %s: %s\n", argv[i], 
                  strerror(errno));
                stem_cache_delete(stem);
                return EXIT_FAILURE;
            }

            while (fgets(buf, BUFSIZ, input)) {
                char *start;

                str_rtrim(buf);
                start = (char *) str_ltrim(buf);
                stem_cache_stem(stem, start);
                printf("%s\n", start);
            }
    
            fclose(input);
        }
    }

    stem_cache_delete(stem);

    return EXIT_SUCCESS;
}

#endif

