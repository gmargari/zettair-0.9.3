/* stem.h implements a generic stemming interface that caches a
 * limited number of stemmed entries.  It also provides some
 * implementations of common stemming algorithms (currently just
 * Porter's stemmer)
 *
 * written nml 2004-06-17
 *
 */

#ifndef STEM_H
#define STEM_H

#ifdef __cplusplus
extern "C" {
#endif

/* structure to speed up stemming by caching previous results */
struct stem_cache;

/* create a new stem cache using stemming algorithm stemmer, and that
 * stores entries number of cached entries 
 *
 * FIXME: describe interface better, because we wouldn't want to 
 * confuse bodo ;o) */
struct stem_cache *stem_cache_new(void (*stemmer)(void *opaque, char *term), 
  void *opaque, unsigned int entries);

/* stem the given term using cache */
void stem_cache_stem(struct stem_cache *cache, char *term);

/* delete the stem cache */
void stem_cache_delete(struct stem_cache *cache);

/* returns maximum number of entries stem_cache can handle */
unsigned int stem_cache_capacity(struct stem_cache *cache);

/* individual stemmers below */

/* stuff used to implement a (bad) porters stemmer */
void stem_porters(void *opaque, char *term);

/* stem_eds implements a function to stem terms by removing (from the end)
 * -e, -ed, and -s */
void stem_eds(void *opaque, char *term);

/* stem_light implements a function to lightly stem english terms by 
 *  - removing suffix -e -es -s -ed -ing -ly -ingly
 *  - replacing suffix -ies, -ied  with -y */
void stem_light(void *opaque, char *term);

#ifdef __cplusplus
}
#endif

#endif

