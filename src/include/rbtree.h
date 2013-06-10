/* rbtree.h declares a generic red-black tree.  Red-black trees are
 * balanced binary search trees that operate according the following
 * invariants:
 *
 *   - The root is black.
 *   - All leaves are black (and NIL).
 *   - Red nodes can only have black children.
 *   - All paths from a node to its leaves contain the same number of black 
 *     nodes.
 *
 * these ensure that the longest path in the tree is at most twice as
 * long as the shortest path (which means that the tree is roughly
 * balanced).  Note that the above invariants assume that empty nodes
 * exist to indicate the end of the search structure and are treated as black 
 * nodes.
 *
 * see http://en.wikipedia.org/wiki/Red-black_tree or a good
 * datastructures and algorithms book (like Introduction to
 * Algorithms, Cormen et al, page 273 in 2nd ed) for more information.
 *
 * written nml 2004-07-12
 *
 */

#ifndef RBTREE_H
#define RBTREE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <errno.h>

enum rbtree_ret {
    RBTREE_EEXIST = -EEXIST,         /* element already exists */
    RBTREE_ENOENT = -ENOENT,         /* specified element doesn't exist */
    RBTREE_ENOMEM = -ENOMEM,         /* ran out of memory */
    RBTREE_EINVAL = -EINVAL,         /* kind of a last resort error */
    RBTREE_OK = 0,                   /* operation succeeded */
    RBTREE_ITER_END = 1              /* end of iteration (could return ENOENT 
                                      * instead, but thats an error, not a 
                                      * logical condition) */
};

struct rbtree;
struct rbtree_iter;

/* create a new red-black tree that will have pointers as keys, and uses cmp to
 * compare keys.  Returns NULL on failure. */
struct rbtree *rbtree_ptr_new(int (*cmp)(const void *key1, const void *key2));

/* create a new red-black tree that will have unsigned long ints as keys, with
 * natural ordering.  Returns NULL on failure. */
struct rbtree *rbtree_luint_new();

/* delete a red-black tree.  This operation can fail (as traversing the tree to
 * free the contained nodes requires dynamic memory), but operates on a best
 * effort basis.  This means that memory leaks are minimized and the red-black 
 * tree is unusable afterward regardless of whether the operation fails or not.
 * I'm not sure what you're supposed to do on failure, but at least it indicates
 * when failure occurs. */
enum rbtree_ret rbtree_delete(struct rbtree *rb);

/* remove all elements from the tree */
enum rbtree_ret rbtree_clear(struct rbtree *rb);

/* return the number of elements in the tree */
unsigned int rbtree_size(struct rbtree *rb);

/* specify the order of iteration over the tree */
enum rbtree_iter_order {
    RBTREE_ITER_PREORDER = 0,        /* root, left subtree, right subtree */
    RBTREE_ITER_INORDER = 1,         /* left subtree, root, right subtree */
    RBTREE_ITER_POSTORDER = 2        /* left subtree, right subtree, root */
};

/* get an iterator over the rbtree.  The iterator is only valid as long as the
 * tree remains unchanged.  Returns NULL on success. */
struct rbtree_iter *rbtree_iter_new(struct rbtree *rb, 
  enum rbtree_iter_order order, int reversed);

/* delete an iterator */
void rbtree_iter_delete(struct rbtree_iter *rbi);

/* insert functions accept a key and data, and insert it if it's not a
 * duplicate.
 *
 * remove functions accept a key, and writes the data that has been removed into
 * a data element that you provide a pointer to
 *
 * find accepts a key, and writes a pointer to the data element into a pointer
 * that you provide a pointer to.  The extra level of indirection is so that 
 * you can alter data elements stored within the rbtree.
 *
 * find_near accepts a key, and writes the found key into a key element that you
 * provide a pointer to, and a pointer to the found data into a pointer that you
 * provide a pointer to, same as find.  Key returned is the largest key that is
 * smaller than or equal to the key you provided.
 *
 * foreach executes a function over all elements in the tree
 *
 * iter_*_next functions iterate over the elements in the rbtree in the order
 * specified in the iter constructor, writing a each successive key
 * element into the pointer you provide, and writing a pointer to each data
 * element into the pointer you provide.
 *
 * in no cases are any of the pointers allowed to be NULL
 *
 */

/* ptr to ptr functions */

enum rbtree_ret rbtree_ptr_ptr_insert(struct rbtree *rb, void *key, 
  void *data);
enum rbtree_ret rbtree_ptr_ptr_remove(struct rbtree *rb, void *key, 
  void **data);
enum rbtree_ret rbtree_ptr_ptr_find(struct rbtree *rb, void *key, 
  void ***data);
enum rbtree_ret rbtree_ptr_ptr_find_near(struct rbtree *rb, void *key, 
  void **found_key, void ***data);
enum rbtree_ret rbtree_ptr_ptr_foreach(struct rbtree *rb, void *opaque, 
  void (*fn)(void *opaque, void *key, void **data));
enum rbtree_ret rbtree_iter_ptr_ptr_next(struct rbtree_iter *rbi, 
  void **key, void ***data);

/* uint to uint functions */

enum rbtree_ret rbtree_luint_luint_insert(struct rbtree *rb, 
  unsigned long int key, unsigned long int data);
enum rbtree_ret rbtree_luint_luint_remove(struct rbtree *rb, 
  unsigned long int key, unsigned long int *data);
enum rbtree_ret rbtree_luint_luint_find(struct rbtree *rb, 
  unsigned long int key, unsigned long int **data);
enum rbtree_ret rbtree_luint_luint_find_near(struct rbtree *rb, 
  unsigned long int key, unsigned long int *found_key, unsigned long int **data);
enum rbtree_ret rbtree_luint_luint_foreach(struct rbtree *rb, void *opaque,
  void (*fn)(void *opaque, unsigned long int key, unsigned long int *data));
enum rbtree_ret rbtree_iter_luint_luint_next(struct rbtree_iter *rbi, 
  unsigned long int *key, unsigned long int **data);

#ifdef __cplusplus
}
#endif

#endif

