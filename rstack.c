#include "rstack.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>

typedef enum {
    STACK_T,
    INT_T
} type_t;

typedef struct element {
    type_t type;
    
    union {
        uint64_t value_int;
        rstack_t *value_stack;
    } value;

    struct element *next;
    struct element *prev;
} element_t;

struct rstack {
    rstack_t *prev_global;
    rstack_t *next_global;
    bool is_deleted;
    bool is_reachable;
    uint32_t visited_gen; // Generation of the last traversal that visited this stack.
    bool is_written;
    element_t *top;
    element_t *bottom;
};

// Head of global rstack_t doubly linked list. 
static rstack_t* all_stacks = nullptr;

/*
 * Generation counter for cycle detection in traversal functions.
 * Incremented before each traversal to avoid resetting visited flags,
 * allowing each stack to be visited at most once per traversal.
 */
static uint32_t visit_gen = 0;

static void rstack_cleanup();

/*
 * Creates a new empty stack and prepends it to the global list of stacks.
 * Returns nullptr and sets errno to ENOMEM on allocation failure.
 */
rstack_t* rstack_new() {
    rstack_t *result = malloc(sizeof(*result));

    if (!result) {
        errno = ENOMEM;
        return nullptr;
    }

    result->top = nullptr;
    result->bottom = nullptr;
    result->is_deleted = false;
    result->is_reachable = true;
    result->visited_gen = 0;
    result->is_written = false;
    result->prev_global = nullptr;
    result->next_global = all_stacks;
    if (all_stacks) all_stacks->prev_global = result;
    all_stacks = result;

    return result;
}

/*
 * Marks the stack as deleted and triggers garbage collection.
 * Does nothing if rs is nullptr.
 */
void rstack_delete(rstack_t *rs) {
    if (rs) {
        rs->is_deleted = true;
        rstack_cleanup(); 
    }
}

/*
 * Pushes an integer value onto the stack.
 * Returns -1 and sets errno to EINVAL if rs is nullptr,
 * or ENOMEM on allocation failure.
 * Returns 0 on success.
 */
int rstack_push_value(rstack_t *rs, uint64_t value) {
    if (!rs) {
        errno = EINVAL;
        return -1;
    }

    element_t *new_value;
    new_value = malloc(sizeof(*new_value));

    if (!new_value) {
        errno = ENOMEM;
        return -1;
    }

    new_value->type = INT_T;
    new_value->value.value_int = value;
    new_value->prev = rs->top;
    new_value->next = nullptr;
    if (rs->top) rs->top->next = new_value;
    else rs->bottom = new_value;
    rs->top = new_value;

    return 0;
}

/*
 * Pushes rs2 onto rs1 as a stack reference (no copy).
 * Returns -1 and sets errno to EINVAL if either pointer is nullptr,
 * or ENOMEM on allocation failure.
 * Returns 0 on success.
 */
int rstack_push_rstack(rstack_t *rs1, rstack_t *rs2) {
    if (!rs1 || !rs2) {
        errno = EINVAL;
        return -1;
    }

    element_t *new_value;
    new_value = malloc(sizeof(*new_value));

    if (!new_value) {
        errno = ENOMEM;
        return -1;
    }

    new_value->type = STACK_T;
    new_value->value.value_stack = rs2;
    new_value->prev = rs1->top;
    new_value->next = nullptr;
    if (rs1->top) rs1->top->next = new_value;
    else rs1->bottom = new_value;
    rs1->top = new_value;

    return 0;
}

/*
 * Removes the top element of the stack.
 * Triggers garbage collection if the removed element was a stack reference.
 * Does nothing if rs is nullptr or the stack is empty.
 */
void rstack_pop(rstack_t *rs) {
    if (rs) {
        if (rs->top) {
            bool was_stack = rs->top->type == STACK_T;
            if (rs->top == rs->bottom) {
                free(rs->top);
                rs->top = nullptr;
                rs->bottom = nullptr;
            } 
            else {
                element_t *temporary = rs->top->prev;
                temporary->next = nullptr;
                free(rs->top);
                rs->top = temporary; 
            }
            if (was_stack) rstack_cleanup();
        }
    }
}

/*
 * Recursively checks if the stack contains no integer values.
 * Uses visited_gen to detect cycles and avoid revisiting stacks
 * within a single traversal.
 */
static bool rstack_empty_impl(rstack_t *rs) {
    if (!rs || !rs->top || rs->visited_gen == visit_gen) return true;
    rs->visited_gen = visit_gen;
    element_t *start = rs->top;
    bool result = true;
    while (result && start) {
        if (start->type == STACK_T)
            result = rstack_empty_impl(start->value.value_stack);
        else
            result = false;
        start = start->prev;
    }
    return result;
}

/*
 * Increments visit_gen to start a new traversal, then delegates to
 * rstack_empty_impl. Returns true if rs is nullptr.
 */
bool rstack_empty(rstack_t *rs) {
    visit_gen++;
    return rstack_empty_impl(rs);
}

/*
 * Increments visit_gen to start a new traversal, then delegates to
 * rstack_front_impl. Returns flag == false if no value is found.
 */
static result_t rstack_front_impl(rstack_t *rs) {
    result_t result = {.flag = false};
    if (!rs || !rs->top || rs->visited_gen == visit_gen) {
        return result;
    }

    rs->visited_gen = visit_gen;

    element_t *start = rs->top;
    while (!result.flag && start) {
        if (start->type == STACK_T) {
            result = rstack_front_impl(start->value.value_stack);
        }
        else if (start->type == INT_T) {
            result.flag = true;
            result.value = start->value.value_int;
        }
        start = start->prev;
    }

    return result;
}

/*
 * Recursively finds the integer value closest to the top of the stack.
 * Uses visited_gen to detect cycles and avoid revisiting stacks
 * within a single traversal.
 */
result_t rstack_front(rstack_t *rs) {
    visit_gen++;
    return rstack_front_impl(rs);
}

/*
 * Creates a new stack with values read from a file at the given path.
 * Numbers must be separated by whitespace, no other characters are allowed.
 * Returns nullptr and sets errno to EINVAL if path is nullptr or file is invalid,
 * ERANGE if a number exceeds uint64_t range, or ENOMEM on allocation failure.
 */
rstack_t* rstack_read(char const *path) {
    if (!path) { errno = EINVAL; return nullptr; }

    FILE *f = fopen(path, "r");
    if (!f) return nullptr;

    rstack_t *rs = rstack_new();
    if (!rs) { fclose(f); return nullptr; }

    char buf[21];
    int c;

    while (true) {
        do { c = fgetc(f); } while (c != EOF && isspace(c));

        if (c == EOF) break;

        if (!isdigit(c)) {
            errno = EINVAL;
            rstack_delete(rs);
            fclose(f);
            return nullptr;
        }

        int len = 0;
        while (isdigit(c)) {
            if (len >= 20) {
                errno = ERANGE;
                rstack_delete(rs);
                fclose(f);
                return nullptr;
            }
            buf[len++] = (char)c;
            c = fgetc(f);
        }
        buf[len] = '\0';

        if (c != EOF && !isspace(c)) {
            errno = EINVAL;
            rstack_delete(rs);
            fclose(f);
            return nullptr;
        }
        if (c != EOF && ungetc(c, f) == EOF) {
            errno = EIO;
            rstack_delete(rs);
            fclose(f);
            return nullptr;
        }

        errno = 0;
        char *endptr;
        uint64_t val = strtoull(buf, &endptr, 10);
        if (errno != 0 || *endptr != '\0') {
            if (errno == 0) errno = EINVAL;
            rstack_delete(rs);
            fclose(f);
            return nullptr;
        }

        if (rstack_push_value(rs, val) != 0) {
            rstack_delete(rs);
            fclose(f);
            return nullptr;
        }
    }

    fclose(f);
    return rs;
}

/*
 * Recursively writes all integer values in rs to f, from bottom to top.
 * Returns 0 on success, 1 if a cycle was detected, -1 on I/O error.
 */
static int write_recursive(FILE *f, rstack_t *rs) {
    if (!rs || rs->is_written) return 1;
    rs->is_written = true;
    element_t *start = rs->bottom;
    while (start) {
        int result = 0;
        if (start->type == STACK_T) {
            result = write_recursive(f, start->value.value_stack);
        }
        else {
            if (fprintf(f, "%" PRIu64 "\n", start->value.value_int) < 0)
                result = -1;
        }
        if (result != 0) {
            rs->is_written = false;
            return result;
        }
        start = start->next;
    }
    rs->is_written = false;
    return 0;
}

int rstack_write(char const *path, rstack_t *rs) {
    if (!path || !rs) { errno = EINVAL; return -1; }
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    int result = write_recursive(f, rs);
    int saved_errno = errno;
    fclose(f);
    if (result == -1) {
        errno = saved_errno;
        return -1;
    }
    return 0;
}

// Sets all stacks in the global list as unreachable.
static void set_unreachable() {
    rstack_t *start = all_stacks;
    
    while (start) {
        start->is_reachable = false;
        start = start->next_global;
    }
}

// Recursively marks rs and all stacks reachable from it as reachable.
static void set_reachable(rstack_t *rs) {
    if (!rs || rs->is_reachable) return;
    rs->is_reachable = true;
    element_t *start = rs->top;

    while (start) {
        if (start->type == STACK_T) {
            if (!start->value.value_stack->is_reachable) {
                set_reachable(start->value.value_stack);
            }
        }
        start = start->prev;
    }
}

// Marks all stacks reachable from non-deleted stacks as reachable.
static void check_reachable() {
    rstack_t *start = all_stacks;
    
    while (start) {
        if (!start->is_deleted) {
            set_reachable(start);
        }
        start = start->next_global;
    }
}

// Frees all elements and the stack structure itself.
static void rstack_free(rstack_t *rs) {
    element_t *start = rs->top;

    while (start) {
        element_t *temporary = start;
        start = start->prev;
        free(temporary);
    }
    free(rs);
}

// Removes and frees all unreachable stacks from the global list.
static void free_unreachable() {
    rstack_t *start = all_stacks;

    while (start) {
        if (!start->is_reachable) {
            if (start->prev_global) {
                start->prev_global->next_global = start->next_global;
            }
            else {
                all_stacks = start->next_global;
            }
            if (start->next_global) {
                start->next_global->prev_global = start->prev_global;
            }
            rstack_t *temporary = start;
            start = start->next_global;
            rstack_free(temporary);
        }
        else {
            start = start->next_global;
        }
    }
}

// Runs a full mark-and-sweep garbage collection cycle.
static void rstack_cleanup() {
    set_unreachable();
    check_reachable();
    free_unreachable();
}
