/* rbtree.c implements a red-black tree as described in rbtree.h
 *
 * note that our implementation of a red-black tree uses a single
 * sentinal NIL value for algorithmic convenience, instead of checking
 * NULL values.  We also use parent pointers, since they seemed easier than
 * maintaining a stack in this instance.
 *
 * if DEAR_DEBUG is set to 1, and NDEBUG is left undefined, then the red-black
 * tree will consume potentially a lot of time and stack space recursively
 * checking that it is a valid red-black tree.  This is useful for debugging,
 * but you'll probably want to turn this off in production.
 *
 * written nml 2004-07-12
 *
 */

#include "firstinclude.h"

#include "rbtree.h"

#include "def.h"
#include "bit.h"
#include "stack.h"
#include "objalloc.h"

#include <assert.h>
#include <stdlib.h>                     /* for NULL, malloc, free */
#include <stdio.h> 

enum rbtree_child {
    RBTREE_LEFT = 0,
    RBTREE_RIGHT = 1
};

/* the type of the red-black tree */
enum rbtree_type {
    RBTREE_TYPE_UNKNOWN,
    RBTREE_TYPE_LUINT,
    RBTREE_TYPE_PTR
};

struct rbtree_node {
    struct rbtree_node *parent;         /* parent of this node */
    struct rbtree_node *child[2];       /* left and right children, as indexed 
                                         * by rbtree_child enum (i'd use left
                                         * and right symbolic names, but this
                                         * makes it easy to switch them for
                                         * symmetrical algorithms) */
    union {
        unsigned long int k_luint;
        void *k_ptr;
        /* add other types as needed */
    } key;                              /* lookup key */

    union {
        unsigned long int d_luint;
        void *d_ptr;
        double d_dbl;
        /* add other types as needed */
    } data;                             /* data associated with key */

    int red;                            /* whether this node is red */
};

struct rbtree {
    unsigned int size;                  /* number of non-NIL nodes */
    struct rbtree_node *root;           /* root node */
    struct rbtree_node nil;             /* sentinal NIL node for this tree */

    /* comparison function */
    int (*cmp)(const void *key1, const void *key2);

    enum rbtree_type key_type;
    enum rbtree_type data_type;

    unsigned int timestamp;             /* allows checks that rbtree doesn't 
                                         * get modified during iteration */

    struct objalloc *alloc;             /* node allocator */
};

struct rbtree_iter {
    struct rbtree *rb;                  /* red-black tree in iteration */
    struct stack *stack;                /* stack of nodes left to visit */
    struct rbtree_node *curr;           /* ptr to current node */
    enum rbtree_child left;             /* logical left child, to allow 
                                         * symmetrically reversed iteration */
    enum rbtree_iter_order order;       /* order in which we iterate over the 
                                         * tree (pre, post or in) */
    struct rbtree_node *last;           /* last visited node (in postorder) */
    unsigned int timestamp;             /* timestamp of rbtree when iteration 
                                         * started */
};

void rbtree_print_node(const struct rbtree *rb, const struct rbtree_node *node, 
  FILE *output, unsigned int depth, int left) {
    assert(node);
    if (node != &rb->nil) {
        unsigned int i;

        for (i = 0; i < depth; i++) {
            fprintf(output, "  ");
        }
        fprintf(output, "%lu %lu %s %s depth %u\n", node->key.k_luint, 
          node->data.d_luint, node->red ? "red" : "black", 
          left ? "left" : "right", depth);
        rbtree_print_node(rb, node->child[RBTREE_LEFT], output, depth + 1, 1);
        rbtree_print_node(rb, node->child[RBTREE_RIGHT], output, depth + 1, 0);
    }
}

void rbtree_print(const struct rbtree *rb, FILE *output) {
    if (rb->root == &rb->nil) {

    } else {
        fprintf(output, "%lu %lu %s root\n", rb->root->key.k_luint, 
          rb->root->data.d_luint, rb->root->red ? "red" : "black");
        rbtree_print_node(rb, rb->root->child[RBTREE_LEFT], output, 1, 1);
        rbtree_print_node(rb, rb->root->child[RBTREE_RIGHT], output, 1, 0);
    }
}

/* invariant checking stuff */

static enum rbtree_ret rbtree_node_not_under(const struct rbtree *rb, 
  const struct rbtree_node *node, const struct rbtree_node *banned) {
    enum rbtree_ret ret;

    if (DEAR_DEBUG) {

        assert(node);

        if (node == banned) {
            assert(!CRASH);
            return RBTREE_EINVAL;
        }

        if (node->parent == banned) {
            assert(!CRASH);
            return RBTREE_EINVAL;
        }

        if (node->child[RBTREE_LEFT] == banned) {
            assert(!CRASH);
            return RBTREE_EINVAL;
        }

        if (node->child[RBTREE_RIGHT] == banned) {
            assert(!CRASH);
            return RBTREE_EINVAL;
        }

        if (node != &rb->nil) {
            if ((ret 
              = rbtree_node_not_under(rb, node->child[RBTREE_LEFT], banned)) 
                != RBTREE_OK) {

                assert(!CRASH);
                return ret;
            }

            if ((ret 
              = rbtree_node_not_under(rb, node->child[RBTREE_RIGHT], banned)) 
                != RBTREE_OK) {

                assert(!CRASH);
                return ret;
            }
        }

        return RBTREE_OK;
    } else {
        return RBTREE_OK;
    }
}

/* internal function to recursively find the maximum height of a node */
static unsigned int rbtree_node_max_height(const struct rbtree *rb, 
  const struct rbtree_node *node) {
    assert(node);
    if (node == &rb->nil) {
        return 0;
    } else {
        unsigned int l = rbtree_node_max_height(rb, node->child[RBTREE_LEFT]),
                     r = rbtree_node_max_height(rb, node->child[RBTREE_RIGHT]);

        if (r > l) {
            return r;
        } else {
            return l;
        }
    }
}

/* internal function to recursively find the minimum height of a node */
static unsigned int rbtree_node_min_height(const struct rbtree *rb, 
  const struct rbtree_node *node) {
    assert(node);
    if (node == &rb->nil) {
        return 0;
    } else {
        unsigned int l = rbtree_node_min_height(rb, node->child[RBTREE_LEFT]),
                     r = rbtree_node_min_height(rb, node->child[RBTREE_RIGHT]);

        if (r < l) {
            return r;
        } else {
            return l;
        }
    }
}

/* internal function to recursively check that all links are internally
 * consistent */
static enum rbtree_ret rbtree_node_check_links(const struct rbtree *rb, 
  const struct rbtree_node *node, enum rbtree_child side) {
    enum rbtree_ret ret;

    assert(node);

    if (node == &rb->nil) {
        return RBTREE_OK;
    }

    if (node->parent->child[side] != node) {
        assert(!CRASH);
        return RBTREE_EINVAL;
    }

    if ((ret 
      = rbtree_node_check_links(rb, node->child[RBTREE_LEFT], RBTREE_LEFT))
        != RBTREE_OK) {

        return ret;
    }
 
    if ((ret 
      = rbtree_node_check_links(rb, node->child[RBTREE_RIGHT], RBTREE_RIGHT))
        != RBTREE_OK) {

        return ret;
    }

    return RBTREE_OK;
}

/* internal function to recursively check the red-black tree invariant that all
 * paths from a node to its children involve the same number of black nodes */
static enum rbtree_ret rbtree_node_check_black(const struct rbtree *rb, 
  const struct rbtree_node *node, unsigned int *black_height) {
    assert(node);

    /* check that red nodes have only black children */
    if (node->red) {
        if (node->child[RBTREE_LEFT]->red || node->child[RBTREE_RIGHT]->red) {
            assert(!CRASH);
            return RBTREE_EINVAL;
        }
    }

    /* check black node count */
    if (node == &rb->nil) {
        *black_height = 1;
        return RBTREE_OK;
    } else {
        unsigned int l, 
                     r;
        enum rbtree_ret ret;

        if (((ret = rbtree_node_check_black(rb, node->child[RBTREE_LEFT], &l)) 
            == RBTREE_OK)
          && ((ret = rbtree_node_check_black(rb, node->child[RBTREE_RIGHT], &r))
            == RBTREE_OK)

          /* return value is EINVAL if we get this far but fail final check */
          && ((ret = RBTREE_EINVAL), (l == r))) {
            *black_height = r + !node->red;
            return RBTREE_OK;
        } else {
            printf("l %u r %u node %lu\n", l, r, node->key.k_luint);
            rbtree_print(rb, stdout);
            assert(!CRASH);
            return ret;
        }
    }
}

/* internal function (not static to avoid warnings) to check red-black tree
 * invariants */
enum rbtree_ret rbtree_invariant(const struct rbtree *rb) {
    unsigned int max,
                 min,
                 bh;
    enum rbtree_ret ret;

    if (DEAR_DEBUG) {
        /* check that link structure is sane */
        if ((rb->nil.parent != &rb->nil)
          || (rb->nil.child[RBTREE_LEFT] != &rb->nil)
          || (rb->nil.child[RBTREE_RIGHT] != &rb->nil)) {
            assert(!CRASH);
            return RBTREE_EINVAL;
        }

        if (rb->root->parent != &rb->nil) {
            assert(!CRASH);
            return RBTREE_EINVAL;
        }

        ret = rbtree_node_check_links(rb, rb->root->child[RBTREE_LEFT], 
            RBTREE_LEFT);
        if (ret != RBTREE_OK) {
            assert(!CRASH);
            return ret;
        }

        ret = rbtree_node_check_links(rb, rb->root->child[RBTREE_RIGHT], 
            RBTREE_RIGHT);
        if (ret != RBTREE_OK) {
            assert(!CRASH);
            return ret;
        }

        /* check red-black invariants */

        if (rb->root->red) {
            assert(!CRASH);
            return RBTREE_EINVAL;
        }

        if (rb->nil.red) {
            assert(!CRASH);
            return RBTREE_EINVAL;
        }

        max = rbtree_node_max_height(rb, rb->root);
        min = rbtree_node_min_height(rb, rb->root);

        if (max > 2 * min) {
            assert(!CRASH);
            return RBTREE_EINVAL;
        }

        ret = rbtree_node_check_black(rb, rb->root, &bh);
        if (ret != RBTREE_OK) {
            assert(!CRASH);
        }
        return ret;
    } else {
        return RBTREE_OK;
    }
}

/* generic operations */

static struct rbtree *rbtree_new() {
    struct rbtree *rb;

    if ((rb = malloc(sizeof(*rb))) 
      && (rb->alloc = objalloc_new(sizeof(struct rbtree_node), 0, 
          !!DEAR_DEBUG, 4096, NULL))) {

        rb->size = 0;
        rb->nil.red = 0;
        rb->nil.child[RBTREE_LEFT] = rb->nil.child[RBTREE_RIGHT]
          = rb->nil.parent = &rb->nil;
        rb->root = &rb->nil;
        rb->cmp = NULL;
        rb->data_type = RBTREE_TYPE_UNKNOWN;
        rb->key_type = RBTREE_TYPE_UNKNOWN;
        rb->timestamp = 0;

        assert(rbtree_invariant(rb) == RBTREE_OK);
    } else {
        if (rb) {
            free(rb);
            rb = NULL;
        }
    }

    return rb;
}

struct rbtree *rbtree_ptr_new(int (*cmp)(const void *key1, const void *key2)) {
    struct rbtree *rb = rbtree_new();

    if (rb) {
        rb->cmp = cmp;
        rb->key_type = RBTREE_TYPE_PTR;
    }

    return rb;
}

struct rbtree *rbtree_luint_new() {
    struct rbtree *rb = rbtree_new();

    if (rb) {
        rb->key_type = RBTREE_TYPE_LUINT;
    }

    return rb;
}

enum rbtree_ret rbtree_delete(struct rbtree *rb) {
    enum rbtree_ret ret = rbtree_clear(rb);
    objalloc_delete(rb->alloc);
    free(rb);
    return ret;
}

unsigned int rbtree_size(struct rbtree *rb) {
    return rb->size;
}

/* forward declaration of node iterator function */
static enum rbtree_ret rbtree_iter_node_next(struct rbtree_iter *rbi,
  struct rbtree_node **key);

enum rbtree_ret rbtree_clear(struct rbtree *rb) {
    assert(rbtree_invariant(rb) == RBTREE_OK);

    /* delete all nodes */
    objalloc_clear(rb->alloc);
    rb->root = &rb->nil;
    rb->size = 0;

    assert(rbtree_invariant(rb) == RBTREE_OK);

    return RBTREE_OK;
}

struct rbtree_iter *rbtree_iter_new(struct rbtree *rb, 
  enum rbtree_iter_order order, int reversed) {
    struct rbtree_iter *rbi = malloc(sizeof(*rbi));

    if (rbi) {
        if ((rbi->stack = stack_new(bit_log2(rb->size) + 1))) {
            rbi->last = &rb->nil;
            rbi->curr = rb->root;
            rbi->rb = rb;
            rbi->order = order;
            rbi->timestamp = rb->timestamp;
            if (reversed) {
                rbi->left = RBTREE_RIGHT;
            } else {
                rbi->left = RBTREE_LEFT;
            }
            return rbi;
        } else {
            free(rbi);
        }
    }

    return NULL;
}

void rbtree_iter_delete(struct rbtree_iter *rbi) {
    stack_delete(rbi->stack);
    free(rbi);
}

/* generic stuff needed for maintaining the red-black-ness of the tree */

/* rotation to rebalance red-black tree.  left indicates logical left, whether 
 * it is a left or right rotation.  With step numbers from Cormen pg 278. */
void rbtree_rotate(struct rbtree *rb, struct rbtree_node *x, 
  enum rbtree_child left) {
    enum rbtree_child right = !left;

    struct rbtree_node *y = x->child[right];      /* 1: Set y */

    assert(y != &rb->nil);
    assert(rb->root->parent == &rb->nil);

    x->child[right] = y->child[left];             /* 2: Turn y's left subtree 
                                                   * into x's right subtree */

    y->child[left]->parent = x;                   /* 3 */
    y->parent = x->parent;                        /* 4: Link x's parent to y */

    if (x->parent == &rb->nil) {                  /* 5 */
        rb->root = y;                             /* 6 */
    } else {
        if ((x == x->parent->child[left])) {      /* 7 */
            x->parent->child[left] = y;           /* 8 */
        } else {
            assert(x->parent->child[right] == x);
            x->parent->child[right] = y;          /* 9 */
        }
    }

    y->child[left] = x;                           /* 10: Put x on y's left */
    x->parent = y;

    /* ensure we haven't changed child NIL pointers (note that we need to be
     * able to change NIL's parent to make some of the fixup cases work) */
    assert(rb->nil.child[left] == &rb->nil);
    assert(rb->nil.child[right] == &rb->nil);
}

/* internal function to restore the red-black tree property to a tree that has
 * had node z inserted.  With step numbers from Cormen pg 281. */
static void rbtree_insert_fixup(struct rbtree *rb, struct rbtree_node *z) {
    enum rbtree_child left,               /* logical left */
                      right;              /* logical right */
    struct rbtree_node *y;

    while (z->parent->red) {                                        /* 1 */

        assert(z->red);
        assert((z->parent != rb->root) || !rb->root->red);
            
        assert(z->parent->parent->red == 0);

        /* figure out which side of the grandparent parent is on */
        if (z->parent->parent->child[RBTREE_LEFT] == z->parent) {   /* 2 */
            left = RBTREE_LEFT;
        } else {
            left = RBTREE_RIGHT;
        }
        right = !left;

        y = z->parent->parent->child[right];                        /* 3 */
        if (y->red) {                                               /* 4 */
            z->parent->red = 0;                                     /* 5 */
            y->red = 0;                                             /* 6 */
            z->parent->parent->red = 1;                             /* 7 */
            z = z->parent->parent;                                  /* 8 */
        } else {
            if (z == z->parent->child[right]) {                     /* 9 */
                z = z->parent;                                      /* 10 */
                rbtree_rotate(rb, z, left);                         /* 11 */
            }
            z->parent->red = 0;                                     /* 12 */
            z->parent->parent->red = 1;                             /* 13 */
            rbtree_rotate(rb, z->parent->parent, right);            /* 14 */
        }
    }
    rb->nil.parent = &rb->nil;
}

/* internal function to restore the red-black tree property to a tree that has
 * had a node deleted.  With step numbers from Cormen pg 289. */
static void rbtree_remove_fixup(struct rbtree *rb, struct rbtree_node *x) {
    struct rbtree_node *w,
                       *xparent;          /* x's parent */
    enum rbtree_child left,               /* logical left */
                      right;              /* logical right */

    /* note that if x is NIL, we need to save its parent where cases can follow
     * a rotate, since rotation can change the parent of the NIL pointer */

    while (!x->red && (x != rb->root)) {                        /* 1 */
        if (x == x->parent->child[RBTREE_LEFT]) {               /* 2 */
            /* perform step with logical left as left */
            left = RBTREE_LEFT;
        } else {
            /* perform step symmetrically with right as logical left */
            left = RBTREE_RIGHT;
        }
        right = !left;

        w = x->parent->child[right];                            /* 3 */
        assert(w != &rb->nil);
        if (w->red) {                                           /* 4 */
            /* case 1 */
            xparent = x->parent;
            w->red = 0;                                         /* 5 */
            x->parent->red = 1;                                 /* 6 */
            rbtree_rotate(rb, x->parent, left);                 /* 7 */
            x->parent = xparent;     /* rotate can change x's parent */
            w = x->parent->child[right];                        /* 8 */
        }

        /* w can't be red after case 1 has been dealt with */
        assert(!w->red);

        if (!w->child[left]->red && !w->child[right]->red) {    /* 9 */
            /* case 2 */
            w->red = 1;                                         /* 10 */
            x = x->parent;                                      /* 11 */
        } else {
            if (!w->child[right]->red) {                        /* 12 */
                /* case 3 */
                xparent = x->parent;
                w->child[left]->red = 0;                        /* 13 */
                w->red = 1;                                     /* 14 */
                rbtree_rotate(rb, w, right);                    /* 15 */
                x->parent = xparent;  /* rotate can change x's parent */
                w = x->parent->child[right];                    /* 16 */
            }
            /* case 4 */
            w->red = x->parent->red;                            /* 17 */
            x->parent->red = 0;                                 /* 18 */
            w->child[right]->red = 0;                           /* 19 */
            rbtree_rotate(rb, x->parent, left);                 /* 20 */
            x = rb->root;                                       /* 21 */
        }
    }

    x->red = 0;                                                 /* 23 */
}

/* templates */

/* template for iteration over a red-black tree.  Note that this version
 * doesn't take advantage of the parent pointers in the red-black tree.
 * nodeiter indicates whether key should have nodes or their values assigned.
 * nodetype is a hack to cast nodes to other values to avoid warnings (note
 * that the type can't be surrounded in parens). */
#define RBTREE_ITER_NEXT(rbi, ky, kytype, dta, dtatype, nodeiter, nodetype)   \
    do {                                                                      \
        enum stack_ret sret;                                                  \
        void *ptr;                                                            \
        enum rbtree_child left = rbi->left,                                   \
                          right = !rbi->left;                                 \
                                                                              \
        /* ensure that tree hasn't changed since iteration */                 \
        assert(rbi->timestamp == rbi->rb->timestamp);                         \
                                                                              \
        do {  /* infinite loop, only needed for POSTORDER */                  \
                                                                              \
            switch (rbi->order) {                                             \
            case RBTREE_ITER_INORDER:                                         \
                /* refer knuth vol 1 pg 320 (3rd ed) */                       \
                                                                              \
                while (rbi->curr != &rbi->rb->nil) {                          \
                    if ((sret = stack_ptr_push(rbi->stack, rbi->curr)         \
                      == STACK_OK)) {                                         \
                        rbi->curr = rbi->curr->child[left];                   \
                    } else {                                                  \
                        return sret;                                          \
                    }                                                         \
                }                                                             \
                                                                              \
                if ((sret = stack_ptr_pop(rbi->stack, &ptr)) == STACK_OK) {   \
                    /* visit node */                                          \
                    rbi->curr = ptr;                                          \
                    if (nodeiter) {                                           \
                        *ky = (nodetype) rbi->curr;                           \
                    } else {                                                  \
                        *ky = rbi->curr->key.k_##kytype;                      \
                        *dta = &rbi->curr->data.d_##dtatype;                  \
                    }                                                         \
                                                                              \
                    rbi->curr = rbi->curr->child[right];                      \
                                                                              \
                    return RBTREE_OK;                                         \
                } else {                                                      \
                    if ((sret == STACK_ENOENT) && !stack_size(rbi->stack)) {  \
                        return RBTREE_ITER_END;                               \
                    } else {                                                  \
                        return sret;                                          \
                    }                                                         \
                }                                                             \
                assert(0);  /* can't get here */                              \
                break;                                                        \
                                                                              \
            case RBTREE_ITER_PREORDER:                                        \
                do {                                                          \
                    if (rbi->curr != &rbi->rb->nil) {                         \
                        /* visit node */                                      \
                        if (nodeiter) {                                       \
                            *ky = (nodetype) rbi->curr;                       \
                        } else {                                              \
                            *ky = rbi->curr->key.k_##kytype;                  \
                            *dta = &rbi->curr->data.d_##dtatype;              \
                        }                                                     \
                                                                              \
                        /* follow left subtree, and push right subtree */     \
                        if ((sret = stack_ptr_push(rbi->stack,                \
                            rbi->curr->child[right]))                         \
                          == STACK_OK) {                                      \
                                                                              \
                            rbi->curr = rbi->curr->child[left];               \
                            return RBTREE_OK;                                 \
                        } else {                                              \
                            return sret;                                      \
                        }                                                     \
                    }                                                         \
                } while (((sret = stack_ptr_pop(rbi->stack, &ptr))            \
                    == STACK_OK)                                              \
                  && (rbi->curr = ptr));                                      \
                                                                              \
                if ((sret == STACK_ENOENT) && !stack_size(rbi->stack)) {      \
                    return RBTREE_ITER_END;                                   \
                } else {                                                      \
                    return sret;                                              \
                }                                                             \
                assert(0);  /* can't get here */                              \
                break;                                                        \
                                                                              \
            case RBTREE_ITER_POSTORDER:                                       \
                 /* see knuth vol 1 (3rd ed) pg 565 */                        \
                 /* this label corresponds with t2 in knuth vol 1 pg 565,     \
                  * ex 13 */                                                  \
                                                                              \
                while (rbi->curr != &rbi->rb->nil) {                          \
                    if ((sret = stack_ptr_push(rbi->stack, rbi->curr)         \
                      == STACK_OK)) {                                         \
                        rbi->curr = rbi->curr->child[left];                   \
                    } else {                                                  \
                        return sret;                                          \
                    }                                                         \
                }                                                             \
                                                                              \
                /* fallthrough (this is a hack to get around the difficulty   \
                 * of writing this algorithm using structure constructs,      \
                 * because we *eed two seperate entry points) */              \
                                                                              \
            case RBTREE_ITER_POSTORDER + 1:                                   \
                /* this label corresponds with t4 in knuth vol 1 pg 565,      \
                 * ex 13 */                                                   \
                                                                              \
                if ((sret = stack_ptr_pop(rbi->stack, &ptr)) == STACK_OK) {   \
                    rbi->curr = ptr;                                          \
                                                                              \
                    if ((rbi->curr->child[right] == &rbi->rb->nil)            \
                      || (rbi->curr->child[right] == rbi->last)) {            \
                        /* visit node */                                      \
                        if (nodeiter) {                                       \
                            *ky = (nodetype) rbi->curr;                       \
                        } else {                                              \
                            *ky = rbi->curr->key.k_##kytype;                  \
                            *dta = &rbi->curr->data.d_##dtatype;              \
                        }                                                     \
                                                                              \
                        rbi->last = rbi->curr;                                \
                        rbi->order = RBTREE_ITER_POSTORDER + 1;               \
                        return RBTREE_OK; /* return at t4 */                  \
                    } else {                                                  \
                        if ((sret = stack_ptr_push(rbi->stack, rbi->curr))    \
                          == STACK_OK) {                                      \
                                                                              \
                            rbi->curr = rbi->curr->child[right];              \
                            rbi->order = RBTREE_ITER_POSTORDER;               \
                            break; /* loop to t2 */                           \
                        } else {                                              \
                            return sret;                                      \
                        }                                                     \
                    }                                                         \
                } else {                                                      \
                    if ((sret == STACK_ENOENT) && !stack_size(rbi->stack)) {  \
                        rbi->order = RBTREE_ITER_POSTORDER + 1;               \
                        return RBTREE_ITER_END;                               \
                    } else {                                                  \
                        return sret;                                          \
                    }                                                         \
                }                                                             \
                assert(0);  /* can't get here */                              \
                                                                              \
            default: return RBTREE_EINVAL;                                    \
            }                                                                 \
        } while (1);                                                          \
    } while (0)

/* find function, pretty simple as its the same as any binary-search-tree 
 * search.  Note that it has a superfluous (for finding) parent pointer and
 * side indication, which can be used for a 'find-or-insert' function.  
 * It also falls through if finding fails, for the same purpose. */
#define RBTREE_FIND(rb, ky, kytype, dta, dtatype, isptr)                      \
    do {                                                                      \
        struct rbtree_node *curr = rb->root,                                  \
                           *parent = &rb->nil;                                \
        enum rbtree_child side;                                               \
                                                                              \
        while (curr != &rb->nil) {                                            \
            int cmp;                                                          \
                                                                              \
            parent = curr;                                                    \
                                                                              \
            if ((isptr                                                        \
                && ((cmp = rb->cmp((void *) ky, curr->key.k_ptr)) < 0))       \
              || ((!isptr) && (ky < curr->key.k_##kytype))) {                 \
                /* key is less than, take left child */                       \
                side = RBTREE_LEFT;                                           \
                curr = curr->child[RBTREE_LEFT];                              \
            } else if ((isptr && (cmp > 0))                                   \
              || (!isptr && (ky > curr->key.k_##kytype))) {                   \
                /* key is greater than, take right child */                   \
                side = RBTREE_RIGHT;                                          \
                curr = curr->child[RBTREE_RIGHT];                             \
            } else {                                                          \
                /* found it */                                                \
                assert((isptr && (cmp == 0))                                  \
                  || (!isptr && (ky == curr->key.k_##kytype)));               \
                cmp = 0; /* stop complaints about unused variable */          \
                *data = &curr->data.d_##dtatype;                              \
                return RBTREE_OK;                                             \
            }                                                                 \
        }                                                                     \
    } while (0)

/* find near function, same as find function except that it'll stop on smallest
 * entry greater than or equal to key */
#define RBTREE_FIND_NEAR(rb, ky, fkey, kytype, dta, dtatype, isptr)           \
    do {                                                                      \
        struct rbtree_node *curr = rb->root,                                  \
                           *candidate = &rb->nil;                             \
                                                                              \
        while (curr != &rb->nil) {                                            \
            int cmp;                                                          \
                                                                              \
            if ((isptr                                                        \
                && ((cmp = rb->cmp((void *) ky, curr->key.k_ptr)) < 0))       \
              || ((!isptr) && (ky < curr->key.k_##kytype))) {                 \
                /* key is less than, take left child.  Candidate doesn't      \
                 * change, because this node is greater than key */           \
                curr = curr->child[RBTREE_LEFT];                              \
            } else if ((isptr && (cmp > 0))                                   \
              || (!isptr && (ky > curr->key.k_##kytype))) {                   \
                /* key is greater than, take right child, set candidate to    \
                 * this node */                                               \
                candidate = curr;                                             \
                curr = curr->child[RBTREE_RIGHT];                             \
            } else {                                                          \
                /* found it */                                                \
                assert((isptr && (cmp == 0))                                  \
                  || (!isptr && (ky == curr->key.k_##kytype)));               \
                cmp = 0; /* stop complaints about unused variable */          \
                *fkey = curr->key.k_##kytype;                                 \
                *dta = &curr->data.d_##dtatype;                               \
                return RBTREE_OK;                                             \
            }                                                                 \
        }                                                                     \
                                                                              \
        if (candidate != &rb->nil) {                                          \
            /* found a suitable candidate, return it */                       \
            *fkey = candidate->key.k_##kytype;                                \
            *dta = &candidate->data.d_##dtatype;                              \
            return RBTREE_OK;                                                 \
        } else {                                                              \
            /* couldn't find anything less than or equal to key,              \
             * (have taken left-most path through tree) return not found */   \
            return RBTREE_EEXIST;                                             \
        }                                                                     \
    } while (0)

/* template to execute a function for each node in the red-black tree */
#define RBTREE_FOREACH(rb, keytype, datatype, opaque, fn)                     \
    do {                                                                      \
        struct rbtree_node *node;        /* current node */                   \
        struct rbtree_iter *iter;        /* node iterator */                  \
        enum rbtree_ret ret;                                                  \
                                                                              \
        if (!(iter = rbtree_iter_new(rb, RBTREE_ITER_INORDER, 0))) {          \
            return RBTREE_ENOMEM;                                             \
        }                                                                     \
                                                                              \
        while ((ret = rbtree_iter_node_next(iter, &node)) == RBTREE_OK) {     \
            /* visit node */                                                  \
            fn(opaque, node->key.k_##keytype, &node->data.d_##datatype);      \
        }                                                                     \
                                                                              \
        rbtree_iter_delete(iter);                                             \
                                                                              \
        if (ret == RBTREE_ITER_END) {                                         \
            return RBTREE_OK;                                                 \
        } else {                                                              \
            return ret;                                                       \
        }                                                                     \
    } while (0)

/* template to insert a new node into the red-black tree.  Parent
 * points to place of insertion, and side indicates which side new node is
 * supposed to be inserted on. */
#define RBTREE_INSERT_NODE(rb, ky, kytype, dta, dtatype, parent, side)        \
    do {                                                                      \
        struct rbtree_node *newnode;                                          \
                                                                              \
        if ((newnode = objalloc_malloc(rb->alloc, sizeof(*child)))) {         \
            /* allocation succeeded */                                        \
            newnode->child[RBTREE_LEFT] = newnode->child[RBTREE_RIGHT]        \
              = &rb->nil;                                                     \
            newnode->red = 1;                                                 \
            newnode->parent = (parent);                                       \
            newnode->key.k_##kytype = ky;                                     \
            newnode->data.d_##dtatype = dta;                                  \
                                                                              \
            if ((parent) == &rb->nil) {                                       \
                /* need to insert new root node */                            \
                rb->root = newnode;                                           \
            } else {                                                          \
                assert((parent)->child[(side)] == &rb->nil);                  \
                (parent)->child[(side)] = newnode;                            \
                rbtree_insert_fixup(rb, newnode);                             \
            }                                                                 \
                                                                              \
            rb->root->red = 0;                                                \
            rb->size++;                                                       \
            assert(rbtree_invariant(rb) == RBTREE_OK);                        \
            return RBTREE_OK;                                                 \
        } else {                                                              \
            return RBTREE_ENOMEM;                                             \
        }                                                                     \
    } while (0)

/* template to perform insertion into a red-black tree.  Uses
 * RBTREE_INSERT_NODE to actually insert the new node once its found the
 * correct location. */
#define RBTREE_INSERT(rb, ky, kytype, dta, dtatype, isptr)                    \
    do {                                                                      \
        struct rbtree_node *child = rb->root,                                 \
                           *parent = &rb->nil;                                \
        enum rbtree_child side = RBTREE_LEFT;                                 \
                                                                              \
        rb->timestamp++;                          /* update timestamp */      \
                                                                              \
        while (child != &rb->nil) {                                           \
            int cmp;                                                          \
                                                                              \
            parent = child;                                                   \
                                                                              \
            if ((isptr                                                        \
                && ((cmp = rb->cmp((void *) ky, child->key.k_ptr)) < 0))      \
              || ((!isptr) && (ky < child->key.k_##kytype))) {                \
                /* key is less than, take left child */                       \
                side = RBTREE_LEFT;                                           \
                child = child->child[RBTREE_LEFT];                            \
            } else if ((isptr && (cmp > 0))                                   \
              || (!isptr && (ky > child->key.k_##kytype))) {                  \
                /* key is greater than, take right child */                   \
                side = RBTREE_RIGHT;                                          \
                child = child->child[RBTREE_RIGHT];                           \
            } else {                                                          \
                /* found a duplicate */                                       \
                cmp = 0; /* stop complaints about unused variable */          \
                return RBTREE_EEXIST;                                         \
            }                                                                 \
        }                                                                     \
                                                                              \
        RBTREE_INSERT_NODE(rb, ky, kytype, dta, dtatype, parent, side);       \
    } while (0)

/* template to remove items from a red-black tree */
#define RBTREE_REMOVE(rb, ky, kytype, dta, dtatype, isptr)                    \
    do {                                                                      \
        enum rbtree_child side = RBTREE_LEFT;                                 \
        struct rbtree_node *child = rb->root,                                 \
                           *parent = &rb->nil;                                \
                                                                              \
        rb->timestamp++;                          /* update timestamp */      \
                                                                              \
        while (child != &rb->nil) {                                           \
            int cmp;                                                          \
                                                                              \
            parent = child;                                                   \
                                                                              \
            if ((isptr                                                        \
                && ((cmp = rb->cmp((void *) ky, child->key.k_ptr)) < 0))      \
              || ((!isptr) && (ky < child->key.k_##kytype))) {                \
                /* key is less than, take left child */                       \
                side = RBTREE_LEFT;                                           \
                child = child->child[side];                                   \
            } else if ((isptr && (cmp > 0))                                   \
              || (!isptr && (ky > child->key.k_##kytype))) {                  \
                /* key is greater than, take right child */                   \
                side = RBTREE_RIGHT;                                          \
                child = child->child[side];                                   \
            } else {                                                          \
                /* found a match at child, remove that item */                \
                struct rbtree_node *x,                                        \
                                   *y;                                        \
                                                                              \
                assert((isptr && (cmp == 0))                                  \
                  || (!isptr && (ky == child->key.k_##kytype)));              \
                cmp = 0; /* stop complaints about unused variable */          \
                                                                              \
                *dta = child->data.d_##dtatype;                               \
                                                                              \
                /* this comes pretty much directly from cormen pg 288 */      \
                                                                              \
                if ((child->child[RBTREE_LEFT] == &rb->nil)                   \
                  || (child->child[RBTREE_RIGHT] == &rb->nil)) {              \
                    y = child;                                                \
                } else {                                                      \
                    /* has both children, find next node in right subtree     \
                     * that has no left children, so we can splice that in */ \
                    for (y = child->child[RBTREE_RIGHT];                      \
                      y->child[RBTREE_LEFT] != &rb->nil;                      \
                      y = y->child[RBTREE_LEFT]) ;                            \
                }                                                             \
                                                                              \
                assert(y != &rb->nil);                                        \
                                                                              \
                if (y->child[RBTREE_LEFT] == &rb->nil) {                      \
                    x = y->child[RBTREE_RIGHT];                               \
                } else {                                                      \
                    x = y->child[RBTREE_LEFT];                                \
                }                                                             \
                                                                              \
                /* note that this can change NIL's parent */                  \
                x->parent = y->parent;                                        \
                                                                              \
                if (y->parent == &rb->nil) {                                  \
                    assert(child == y);                                       \
                    rb->root = x;                                             \
                } else {                                                      \
                    if (y == y->parent->child[RBTREE_LEFT]) {                 \
                        y->parent->child[RBTREE_LEFT] = x;                    \
                    } else {                                                  \
                        y->parent->child[RBTREE_RIGHT] = x;                   \
                    }                                                         \
                                                                              \
                    if (y != child) {                                         \
                        int tmpred;                                           \
                        struct rbtree_node *nilparent = rb->nil.parent;       \
                                                                              \
                        /* splice y into the position occupied by child.      \
                         * Cormen recommends copying the data from y into     \
                         * child, which is implmented with:                   \
                         *                                                    \
                         *   child->key.k_##kytype = y->key.k_##kytype;       \
                         *   child->data.d_##dtatype = y->data.d_##dtatype;   \
                         *                                                    \
                         * however, the problem with that is that it          \
                         * invalidates data pointers to y, which could be     \
                         * a source of (very) subtle bugs.  We get around     \
                         * this by laboriously changing all pointers from     \
                         * child to y, which has a number of traps. */        \
                                                                              \
                        /* splice y in to allow removal of child */           \
                                                                              \
                        /* preserve y's red value, we'll need it below */     \
                        tmpred = y->red;                                      \
                        y->red = child->red;                                  \
                                                                              \
                        /* copy over y's pointers */                          \
                        y->parent = child->parent;                            \
                        y->child[RBTREE_LEFT] = child->child[RBTREE_LEFT];    \
                        y->child[RBTREE_RIGHT] = child->child[RBTREE_RIGHT];  \
                                                                              \
                        /* set pointers to y */                               \
                        assert((y->parent->child[side] == child)              \
                          || (y->parent->child[side] == &rb->nil));           \
                        if (y->parent == &rb->nil) {                          \
                            /* nasty case where child was root, need to       \
                             * set new root to y */                           \
                            rb->root = y;                                     \
                        } else {                                              \
                            y->parent->child[side] = y;                       \
                        }                                                     \
                        y->child[RBTREE_LEFT]->parent = y;                    \
                        y->child[RBTREE_RIGHT]->parent = y;                   \
                                                                              \
                        /* correct nil's parent, as we may have changed it */ \
                        if (nilparent != child) {                             \
                            rb->nil.parent = nilparent;                       \
                        } else {                                              \
                            /* nasty case where nil's parent was child, has   \
                             * to stay changed, pointing to y */              \
                            assert(rb->nil.parent == y);                      \
                        }                                                     \
                                                                              \
                        /* make child the new y, so that the code below       \
                         * still works */                                     \
                        y = child;                                            \
                        y->red = tmpred;                                      \
                                                                              \
                        assert(y != &rb->nil);                                \
                    }                                                         \
                }                                                             \
                                                                              \
                /* make sure we've removed all links to deleted node */       \
                assert(rbtree_node_not_under(rb, rb->root, y) == RBTREE_OK);  \
                                                                              \
                if (!y->red) {                                                \
                    rbtree_remove_fixup(rb, x);                               \
                }                                                             \
                                                                              \
                objalloc_free(rb->alloc, y);                                  \
                rb->nil.parent = &rb->nil;    /* fix NIL parent pointer */    \
                rb->nil.red = 0;              /* and NIL colour */            \
                rb->size--;                                                   \
                                                                              \
                assert(rbtree_invariant(rb) == RBTREE_OK);                    \
                return RBTREE_OK;                                             \
            }                                                                 \
        }                                                                     \
                                                                              \
        return RBTREE_ENOENT;                                                 \
    } while (0)

/* templated operations */

enum rbtree_ret rbtree_luint_luint_remove(struct rbtree *rb, 
  unsigned long int key, unsigned long int *data) {

    assert(rbtree_invariant(rb) == RBTREE_OK);
    assert(rb->key_type == RBTREE_TYPE_LUINT);
    assert((rb->data_type == RBTREE_TYPE_LUINT) 
      || (rb->data_type == RBTREE_TYPE_UNKNOWN));
    rb->data_type = RBTREE_TYPE_LUINT;

    RBTREE_REMOVE(rb, key, luint, data, luint, 0);
}

enum rbtree_ret rbtree_luint_luint_insert(struct rbtree *rb, 
  unsigned long int key, unsigned long int data) {

    assert(rbtree_invariant(rb) == RBTREE_OK);
    assert(rb->key_type == RBTREE_TYPE_LUINT);
    assert((rb->data_type == RBTREE_TYPE_LUINT) 
      || (rb->data_type == RBTREE_TYPE_UNKNOWN));
    rb->data_type = RBTREE_TYPE_LUINT;

    RBTREE_INSERT(rb, key, luint, data, luint, 0);
}

enum rbtree_ret rbtree_luint_luint_find(struct rbtree *rb, 
  unsigned long int key, unsigned long int **data) {

    assert(rbtree_invariant(rb) == RBTREE_OK);
    assert(rb->key_type == RBTREE_TYPE_LUINT);
    assert((rb->data_type == RBTREE_TYPE_LUINT) 
      || (rb->data_type == RBTREE_TYPE_UNKNOWN));
    rb->data_type = RBTREE_TYPE_LUINT;

    RBTREE_FIND(rb, key, luint, data, luint, 0);

    return RBTREE_ENOENT;
}

enum rbtree_ret rbtree_luint_luint_find_near(struct rbtree *rb, 
  unsigned long int key, unsigned long int *found_key, 
  unsigned long int **data) {

    assert(rbtree_invariant(rb) == RBTREE_OK);
    assert(rb->key_type == RBTREE_TYPE_LUINT);
    assert((rb->data_type == RBTREE_TYPE_LUINT) 
      || (rb->data_type == RBTREE_TYPE_UNKNOWN));
    rb->data_type = RBTREE_TYPE_LUINT;

    RBTREE_FIND_NEAR(rb, key, found_key, luint, data, luint, 0); 
}

enum rbtree_ret rbtree_luint_luint_foreach(struct rbtree *rb, void *opaque, 
  void (*fn)(void *opaque, unsigned long int key, unsigned long int *data)) {

    assert(rbtree_invariant(rb) == RBTREE_OK);
    assert(rb->key_type == RBTREE_TYPE_LUINT);
    assert((rb->data_type == RBTREE_TYPE_LUINT) 
      || (rb->data_type == RBTREE_TYPE_UNKNOWN));
    rb->data_type = RBTREE_TYPE_LUINT;

    RBTREE_FOREACH(rb, luint, luint, opaque, fn);
}

enum rbtree_ret rbtree_iter_luint_luint_next(struct rbtree_iter *rbi, 
  unsigned long int *key, unsigned long int **data) {

    assert(rbi->rb->key_type == RBTREE_TYPE_LUINT);
    assert((rbi->rb->data_type == RBTREE_TYPE_LUINT) 
      || (rbi->rb->data_type == RBTREE_TYPE_UNKNOWN));
    rbi->rb->data_type = RBTREE_TYPE_LUINT;

    RBTREE_ITER_NEXT(rbi, key, luint, data, luint, 0, unsigned long int);
}


enum rbtree_ret rbtree_ptr_ptr_remove(struct rbtree *rb, 
  void *key, void **data) {

    assert(rbtree_invariant(rb) == RBTREE_OK);
    assert(rb->key_type == RBTREE_TYPE_PTR);
    assert((rb->data_type == RBTREE_TYPE_PTR) 
      || (rb->data_type == RBTREE_TYPE_UNKNOWN));
    rb->data_type = RBTREE_TYPE_PTR;

    RBTREE_REMOVE(rb, key, ptr, data, ptr, 1);
}

enum rbtree_ret rbtree_ptr_ptr_insert(struct rbtree *rb, 
  void *key, void *data) {

    assert(rbtree_invariant(rb) == RBTREE_OK);
    assert(rb->key_type == RBTREE_TYPE_PTR);
    assert((rb->data_type == RBTREE_TYPE_PTR) 
      || (rb->data_type == RBTREE_TYPE_UNKNOWN));
    rb->data_type = RBTREE_TYPE_PTR;

    RBTREE_INSERT(rb, key, ptr, data, ptr, 1); 
}

enum rbtree_ret rbtree_ptr_ptr_find(struct rbtree *rb, 
  void *key, void ***data) {

    assert(rbtree_invariant(rb) == RBTREE_OK);
    assert(rb->key_type == RBTREE_TYPE_PTR);
    assert((rb->data_type == RBTREE_TYPE_PTR) 
      || (rb->data_type == RBTREE_TYPE_UNKNOWN));
    rb->data_type = RBTREE_TYPE_PTR;

    RBTREE_FIND(rb, key, ptr, data, ptr, 1);

    return RBTREE_ENOENT;
}

enum rbtree_ret rbtree_ptr_ptr_find_near(struct rbtree *rb, 
  void *key, void **found_key, void ***data) {

    assert(rbtree_invariant(rb) == RBTREE_OK);
    assert(rb->key_type == RBTREE_TYPE_PTR);
    assert((rb->data_type == RBTREE_TYPE_PTR) 
      || (rb->data_type == RBTREE_TYPE_UNKNOWN));
    rb->data_type = RBTREE_TYPE_PTR;

    RBTREE_FIND_NEAR(rb, key, found_key, ptr, data, ptr, 1);
}

enum rbtree_ret rbtree_ptr_ptr_foreach(struct rbtree *rb, void *opaque, 
  void (*fn)(void *opaque, void *key, void **data)) {

    assert(rbtree_invariant(rb) == RBTREE_OK);
    assert(rb->key_type == RBTREE_TYPE_PTR);
    assert((rb->data_type == RBTREE_TYPE_PTR) 
      || (rb->data_type == RBTREE_TYPE_UNKNOWN));
    rb->data_type = RBTREE_TYPE_PTR;

    RBTREE_FOREACH(rb, ptr, ptr, opaque, fn);
}

enum rbtree_ret rbtree_iter_ptr_ptr_next(struct rbtree_iter *rbi, 
  void **key, void ***data) {

    assert(rbi->rb->key_type == RBTREE_TYPE_PTR);
    assert((rbi->rb->data_type == RBTREE_TYPE_PTR) 
      || (rbi->rb->data_type == RBTREE_TYPE_UNKNOWN));
    rbi->rb->data_type = RBTREE_TYPE_PTR;

    RBTREE_ITER_NEXT(rbi, key, ptr, data, ptr, 0, void *);
}

static enum rbtree_ret rbtree_iter_node_next(struct rbtree_iter *rbi,
  struct rbtree_node **key) {
    unsigned long int **data = NULL;

    /* note that keytype and datatype lie (about being luint), but they have to
     * be something valid */
    RBTREE_ITER_NEXT(rbi, key, ptr, data, luint, 1, struct rbtree_node *); 
}

