/* _reposset.h declares a private interface to the reposset module.
 *
 * written nml 2006-04-21
 *
 */

#ifndef PRIVATE_REPOSSET
#define PRIVATE_REPOSSET

#ifdef __cplusplus
extern "C" {
#endif

/* our representation of repository numbers is based around
 * compressing the two common (only?) cases: many documents in a file
 * or one document per file.  In the first case we have a single file
 * containing many sequential docno's and in the second we have many
 * files with one sequential docno each.  We will represent this with
 * an array of records, where each record either represents a
 * sequential run of one-file-to-one-docno or one-file-with-many-docnos.
 * Alternating these records allows us to compactly represent any
 * series of docno/reposno allocations. */

/* XXX: idea for offsets: one-in-some delta coding, all single-files have
 * offset 0: should go in docmap, because it doesn't always live in
 * memory */

enum reposset_rectype {
    REPOSSET_SINGLE_FILE = 0,        /* single file, many docno's */
    REPOSSET_MANY_FILES = 1          /* many files, many docno's */
};

struct reposset_record {
    enum reposset_rectype rectype;
    unsigned int reposno;           /* start repository number */
    unsigned int docno;             /* start document number */
    unsigned int quantity;          /* number of docnos */
};

/* fetch the record containing docno from the reposset */
struct reposset_record *reposset_record(struct reposset *rset, 
  unsigned int docno);
struct reposset_record *reposset_record_last(struct reposset *rset);

/* the following methods allow the reposset to be compactly stored on
 * disk and restored by an external module */

/* insert a record into the reposset */
enum reposset_ret reposset_set_record(struct reposset *rset, 
  struct reposset_record *rec);

struct reposset_check {
    unsigned int reposno;              /* repository number */
    unsigned long int offset;          /* byte offset of checkpoint */
    enum mime_types comp;              /* compression type */
};

/* returns a pointer to the first checkpoint in repos array */
struct reposset_check *reposset_check_first(struct reposset *rset);

/* returns a pointer to first checkpoint relevant to the given repository 
 * number */
struct reposset_check *reposset_check(struct reposset *rset, 
  unsigned int reposno);

/* returns number of checkpoint entries */
unsigned int reposset_checks(struct reposset *rset);

unsigned int reposset_reposno_rec(struct reposset_record *rec, 
  unsigned int docno);

#ifdef __cplusplus
}
#endif

#endif

