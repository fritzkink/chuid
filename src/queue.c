/*
 *
 */

#include "chuid.h"

extern short int    verbose;

queue_anchor_t 
        *deq_init(void) {
    
/*
 * Description:
 * This function initializes a double ended queue by creating an anchor for this queue.
 *
 * Parameter:
 *
 * Return value:
 * anchor of double ended queue.
 *
 */
    queue_anchor_t *anchor;
    
    if ((anchor = (queue_anchor_t *) malloc(sizeof(queue_anchor_t))) != NULL) {
        anchor->element_counter = 0;
        anchor->speed = 0.;
        anchor->first = NULL;
        anchor->last = NULL;
        return anchor;
    } else {
        fprintf(stderr, "ERROR: Problems allocating memory/initializing queue!!\n");
        exit(ENOMEM);
    }
}

void
        deq_put(queue_anchor_t *anchor, queue_element_t *new) {

/*
 * Description:
 * This function appends an element to a double ended queue (FIFO).
 *
 * Parameter:
 * anchor: anchor of double ended queue
 * new:    new element which is appended to double ended queue
 */
    if (new != NULL) {
        if (anchor->element_counter > 0) {
            anchor->last->next = new;
        } else {
            anchor->first = new;
        }
        anchor->last = new;
        anchor->element_counter++;
    } else {
        if (verbose)
            fprintf(stdout, "INFO: New element = NULL!\n");
    }
}

void
        deq_push(queue_anchor_t *anchor, queue_element_t *new) {

/*
 * Description:
 * This function prepends an element to a double ended queue (LIFO).
 *
 * Parameter:
 * anchor: anchor of double ended queue
 * new:    new element which is prepended to double ended queue
 */
    if (new != NULL) {
        if (anchor->element_counter > 0) {
            new->next = anchor->first;             
        } else {
            anchor->last = new;
        }
        anchor->first = new;
        anchor->element_counter++;
    } else {
        if (verbose)
            fprintf(stdout, "INFO: New element = NULL!\n");
    }
}

void
        deq_append(queue_anchor_t *global, queue_anchor_t *local) {

/*
 * Description:
 * This function concatenates by appending a double ended queue to a double ended queue (FIFO).
 *
 * Parameter:
 * anchor: anchor of double ended queue
 * local:  double ended queue which is concatenated by appending to double ended queue
 */
    if (local != NULL ) {
        if (local->element_counter > 0) {
            if (global->element_counter > 0) {
                global->last->next = local->first;
            } else {
                global->first = local->first;
            }
            global->last = local->last;
            global->element_counter += local->element_counter;
        }
        local->element_counter = 0;
        local->speed = 0.;
        local->first = NULL;
        local->last = NULL;
    }
}

void
        deq_prepend(queue_anchor_t *global, queue_anchor_t *local) {

/*
 * Description:
 * This function concatenates by prepending a double ended queue to a double ended queue (LIFO).
 *
 * Parameter:
 * anchor: anchor of double ended queue
 * local:  double ended queue which is concatenated by prepending to double ended queue
 */
    if (local != NULL ) {
        if (local->element_counter > 0) {
            if (global->element_counter > 0) {
                local->last->next = global->first;
            } else {
                global->last = local->last;
            }
            global->first = local->first;
            global->element_counter += local->element_counter;
        }
        local->element_counter = 0;
        local->speed = 0.;
        local->first = NULL;
        local->last = NULL;
    }
}

queue_element_t *
        deq_get(queue_anchor_t *anchor) {

/*
 * Description:
 * This function gets an element from a double ended queue.
 *
 * Parameter:
 * anchor: anchor of double ended queue
 * 
 * Return value:
 * first queue element upon anchor of a double ended queue
 */
    queue_element_t *ptr;
    if (anchor != NULL) {
        /* is something in the queue? */
        if (anchor->element_counter > 0) { /* yes...! */
            ptr = anchor->first;
            anchor->first = ptr->next;
            ptr->next = NULL;
            anchor->element_counter--;
            if (anchor->element_counter == 0)
                anchor->last = NULL;
            return ptr;
        } else {
            if (verbose)
                fprintf(stdout, "INFO: No elements in queue!\n");
            return NULL;
        }
    } else {
        fprintf(stderr, "WARNING: No queue at all...\n");
        exit(EXIT_FAILURE);
    }
}
