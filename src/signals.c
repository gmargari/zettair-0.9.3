/*
 *  Code for handling signals in a reasonable way.
 *
 *  The main thing we need to do upon receiving a signal is clean up
 *  any incomplete index files or temporary files made during a build
 *  process.  Zettair is fairly strict about not overwriting existing
 *  files, so if files are left over from an aborted earlier build,
 *  then the next Zettair build will generally fail.  This can be
 *  confusing for the user.
 *
 *  In normal operation, the files that can need to be cleared up
 *  are:
 *
 *   a.) index files, of type FDSET_TYPE_INDEX.  The number of
 *       these is held in index->vectors, although as a special
 *       case a single index file may created at startup to
 *       determine the maximum filesize, without being recorded
 *       in index->vectors.
 *   b.) the docmap file.  This is currently not managed by the
 *       fdset.
 *   c.) temporary files used during the merge process.  The index
 *       itself doesn't know how many of these there are; they
 *       are held in the pyramid structure.
 *
 *  The docmap and the pyramid are accessible through the
 *  index structure.   Thus, the signal handler only needs to have 
 *  access to the index  currently under construction (if any).
 */

#include "firstinclude.h"

#include <stdlib.h>
#include <stdio.h>
#include "signals.h"
#include "_index.h"

static struct index * constr_idx = NULL;

void signals_set_index_under_construction(struct index * index) {
    constr_idx = index;
}

void signals_clear_index_under_construction(void) {
    constr_idx = NULL;
}

void signals_cleanup_handler(int signal) {
    fprintf(stderr, "Signal %d received, cleaning up...\n", signal);

    if (constr_idx != NULL) {
        if (constr_idx->merger!= NULL) {
            pyramid_delete(constr_idx->merger);
            constr_idx->merger = NULL;
        }
        index_rm(constr_idx);
        constr_idx = NULL;
    }
    exit(1);
}
