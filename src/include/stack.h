/* stack.h declares a templated version of the very useful data
 * structure, the LIFO (Last-In, First-Out) stack.  
 * See http://en.wikipedia.org/wiki/Stack_(computing) if you need more
 * information on what a stack is.
 *
 * written nml 2004-12-07
 *
 */

#ifndef STACK_H
#define STACK_H

#include <errno.h>

/* return values from stack functions */
enum stack_ret {
    STACK_OK = 0,
    STACK_ENOMEM = -ENOMEM,
    STACK_ENOENT = -ENOENT
};

struct stack;

struct stack *stack_new(unsigned int sizehint);

void stack_delete(struct stack *stk);

unsigned int stack_size(struct stack *stk);

/* ptr functions */

enum stack_ret stack_ptr_push(struct stack *stk, void *ptr);
enum stack_ret stack_ptr_pop(struct stack *stk, void **ptr);
enum stack_ret stack_ptr_peek(struct stack *stk, void **ptr);
enum stack_ret stack_ptr_foreach(struct stack *stk, void *opaque, 
  void (*fn)(void *opaque, void **data));
enum stack_ret stack_ptr_fetch(struct stack *stk, unsigned int index, 
  void **ptr);

/* luint functions */

enum stack_ret stack_luint_push(struct stack *stk, unsigned long int ptr);
enum stack_ret stack_luint_pop(struct stack *stk, unsigned long int *ptr);
enum stack_ret stack_luint_peek(struct stack *stk, unsigned long int *ptr);
enum stack_ret stack_luint_foreach(struct stack *stk, void *opaque,
  void (*fn)(void *opaque, unsigned long int *data));
enum stack_ret stack_luint_fetch(struct stack *stk, unsigned int index, 
  unsigned long int *ptr);

/* double functions */

enum stack_ret stack_dbl_push(struct stack *stk, double ptr);
enum stack_ret stack_dbl_pop(struct stack *stk, double *ptr);
enum stack_ret stack_dbl_peek(struct stack *stk, double *ptr);
enum stack_ret stack_dbl_foreach(struct stack *stk, void *opaque,  
  void (*fn)(void *opaque, double *data));
enum stack_ret stack_dbl_fetch(struct stack *stk, unsigned int index, 
  double *ptr);

#endif

