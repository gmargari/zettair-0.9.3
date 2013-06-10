/* stack.c implements a generic stack as specified in stack.h
 *
 * written nml 2004-07-12
 *
 */

#include "firstinclude.h"

#include "stack.h"

#include <stdlib.h>  /* for malloc and free */

union stack_node {
    void *d_ptr;
    unsigned long int d_luint;
    double d_dbl;
};

struct stack {
    unsigned int size;
    unsigned int capacity;
    union stack_node *stack;
};

struct stack *stack_new(unsigned int sizehint) {
    struct stack *stk;

    if ((stk = malloc(sizeof(*stk)))) {
        stk->size = stk->capacity = 0;
        stk->stack = NULL;

        if (!sizehint 
          || (stk->stack = malloc(sizeof(*stk->stack) * sizehint))) {
            stk->capacity = sizehint;
        }
    }

    return stk;
}

void stack_delete(struct stack *stk) {
    if (stk->stack) {
        free(stk->stack);
    }
    free(stk);
}

unsigned int stack_size(struct stack *stk) {
    return stk->size;
}

/* push template */
#define STACK_PUSH(stk, data, type)                                           \
    do {                                                                      \
        /* check for enough stack space */                                    \
        if ((stk)->size >= (stk)->capacity) {                                 \
            void *ptr;                                                        \
                                                                              \
            if ((ptr = realloc((stk)->stack,                                  \
                sizeof(*(stk)->stack) * ((stk)->capacity * 2 + 1)))) {        \
                                                                              \
                (stk)->stack = ptr;                                           \
                (stk)->capacity = (stk)->capacity * 2 + 1;                    \
            } else {                                                          \
                return STACK_ENOMEM;                                          \
            }                                                                 \
        }                                                                     \
                                                                              \
        (stk)->stack[(stk)->size++].d_##type = (data);                        \
        return STACK_OK;                                                      \
    } while (0)

/* pop template */
#define STACK_POP(stk, pointer, type)                                         \
    do {                                                                      \
        if ((stk)->size) {                                                    \
            *(pointer) = (stk)->stack[--(stk)->size].d_##type;                \
            return STACK_OK;                                                  \
        } else {                                                              \
            return STACK_ENOENT;                                              \
        }                                                                     \
    } while (0)

/* peek template */
#define STACK_PEEK(stk, pointer, type)                                        \
    do {                                                                      \
        if ((stk)->size) {                                                    \
            *(pointer) = (stk)->stack[(stk)->size - 1].d_##type;              \
            return STACK_OK;                                                  \
        } else {                                                              \
            return STACK_ENOENT;                                              \
        }                                                                     \
    } while (0)

/* foreach template */
#define STACK_FOREACH(stk, opaque, fn, type)                                  \
    do {                                                                      \
        unsigned int i;                                                       \
                                                                              \
        for (i = 0; i < (stk)->size; i++) {                                   \
            (fn)((opaque), &(stk)->stack[i].d_##type);                        \
        }                                                                     \
                                                                              \
        return STACK_OK;                                                      \
    } while (0)

/* fetch template */
#define STACK_FETCH(stk, index, pointer, type)                                \
    do {                                                                      \
        if ((index) < (stk)->size) {                                          \
            *(pointer) = (stk)->stack[index].d_##type;                        \
            return STACK_OK;                                                  \
        } else {                                                              \
            return STACK_ENOENT;                                              \
        }                                                                     \
    } while (0)

/* luint functions */

enum stack_ret stack_luint_push(struct stack *stk, unsigned long int ptr) {
    STACK_PUSH(stk, ptr, luint);
}

enum stack_ret stack_luint_pop(struct stack *stk, unsigned long int *ptr) {
    STACK_POP(stk, ptr, luint);
}

enum stack_ret stack_luint_peek(struct stack *stk, unsigned long int *ptr) {
    STACK_PEEK(stk, ptr, luint);
}

enum stack_ret stack_luint_foreach(struct stack *stk, void *opaque,
  void (*fn)(void *opaque, unsigned long int *data)) {
    STACK_FOREACH(stk, opaque, fn, luint);
}

enum stack_ret stack_luint_fetch(struct stack *stk, unsigned int index, 
  unsigned long int *ptr) {
    STACK_FETCH(stk, index, ptr, luint);
}

/* ptr functions */

enum stack_ret stack_ptr_push(struct stack *stk, void *ptr) {
    STACK_PUSH(stk, ptr, ptr);
}

enum stack_ret stack_ptr_pop(struct stack *stk, void **ptr) {
    STACK_POP(stk, ptr, ptr);
}

enum stack_ret stack_ptr_peek(struct stack *stk, void **ptr) {
    STACK_PEEK(stk, ptr, ptr);
}

enum stack_ret stack_ptr_foreach(struct stack *stk, void *opaque,
  void (*fn)(void *opaque, void **data)) {
    STACK_FOREACH(stk, opaque, fn, ptr);
}

enum stack_ret stack_ptr_fetch(struct stack *stk, unsigned int index, 
  void **ptr) {
    STACK_FETCH(stk, index, ptr, ptr);
}

/* dbl functions */

enum stack_ret stack_dbl_push(struct stack *stk, double ptr) {
    STACK_PUSH(stk, ptr, dbl);
}

enum stack_ret stack_dbl_pop(struct stack *stk, double *ptr) {
    STACK_POP(stk, ptr, dbl);
}

enum stack_ret stack_dbl_peek(struct stack *stk, double *ptr) {
    STACK_PEEK(stk, ptr, dbl);
}

enum stack_ret stack_dbl_foreach(struct stack *stk, void *opaque,
  void (*fn)(void *opaque, double *data)) {
    STACK_FOREACH(stk, opaque, fn, dbl);
}

enum stack_ret stack_dbl_fetch(struct stack *stk, unsigned int index, 
  double *ptr) {
    STACK_FETCH(stk, index, ptr, dbl);
}

