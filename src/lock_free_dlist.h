/*  Copyright (c) Microsoft Corporation. All rights reserved. */
/*  Licensed under the MIT license. */
#ifndef _DOUBLEY_LINKED_LIST_H_
#define _DOUBLEY_LINKED_LIST_H_ 1

#include <stdint.h>
#include "util.h"
#include "rand_r.h"

typedef struct _dlist_node dlist_node_t;
struct _dlist_node
{
  volatile dlist_node_t * volatile prev; /*  8-byte */
  volatile dlist_node_t * volatile next; /*  8-byte */
};

typedef int32_t DL_STATUS;
enum _dl_status
{
  DL_STATUS_OK                 = 0,
  DL_STATUS_NOT_FOUND          = 1,
  DL_STATUS_CORRUPTION         = 2,
  DL_STATUS_NOT_SUPPORTED      = 3,
  DL_STATUS_INVALID_ARGUMENT   = 4,
  DL_STATUS_IOERROR            = 5,
  DL_STATUS_MERGE_IN_PROGRESS  = 6,
  DL_STATUS_INCOMPLETE         = 7,
  DL_STATUS_SHUTDOWN_IN_PROGRESS = 8,
  DL_STATUS_TIMEDOUT           = 9,
  DL_STATUS_ABORTED            = 10,
  DL_STATUS_BUSY               = 11,
  DL_STATUS_OUT_OF_MEMORY      = 12,
  DL_STATUS_KEY_ALREADY_EXISTS = 13,
  DL_STATUS_UNABLE_TO_MERGE    = 14
};

/* ****************************************************************************
 *  A lock-free doubly linked list using single-word CAS,
 *  based off of the following paper:
 *  Hakan Sundell and Philippas Tsigas. 2008.
 *  Lock-free deques and doubly linked lists.
 *  J. Parallel Distrib. Comput. 68, 7 (July 2008), 1008-1020. */

/* Least significant 2 bits of the next pointer to indicate
 * the underlying node is logically DELETED or DIRTY*/
/* NOTE: 최상위 2비트는 사용하지 않는 것이 좋음.
 * HP-UX의 메모리 모델  때문. */
#define DL_NODE_DIRTY    ((uint64_t)0x0000000000000001) // ((uint64_t)1 << 0)
#define DL_NODE_DELETED  ((uint64_t)0x0000000000000002) // ((uint64_t)1 << 1)
#define DL_NODE_DELETED_MASK  ((uint64_t)0xFFFFFFFFFFFFFFFD) // (~DL_NODE_DELETED)

typedef struct _lock_free_doubly_linked_list volatile lf_dlist_t;
struct _lock_free_doubly_linked_list
{
  volatile dlist_node_t * volatile head;
  volatile dlist_node_t * volatile tail;
  /*  A random number generator for back off loop count */
  RNG rng[1];
};

int32_t lf_dlist_initiaize( volatile lf_dlist_t    * l,
                            volatile dlist_node_t  * head,
                            volatile dlist_node_t  * tail,
                            int32_t backoff_cnt_max );
void lf_dlist_finalize( volatile lf_dlist_t * l );

/*  Verify the links between each pair of nodes (including head and tail). */
/*  For single-threaded cases only, no CC whatsoever. */
void lf_dlist_single_thread_sanity_check( volatile lf_dlist_t * l );
void lf_dlist_backoff( volatile lf_dlist_t * l );


/*  Insert [node] in front of [next] - [node] might end up before another node */
/*  in case [prev] is being deleted or due to concurrent insertions at the */
/*  same spot. */
DL_STATUS lf_dlist_insert_before( volatile lf_dlist_t * l,
                                  volatile dlist_node_t * next,
                                  volatile dlist_node_t * node );

/*  Similar to insert_before, but try to insert [node] after [prev]. */
DL_STATUS lf_dlist_insert_after( volatile lf_dlist_t * l,
                                 volatile dlist_node_t * prev,
                                 volatile dlist_node_t * node );

DL_STATUS lf_dlist_delete( volatile lf_dlist_t * l, volatile dlist_node_t * node );
dlist_node_t * lf_dlist_get_next( volatile lf_dlist_t * l, volatile dlist_node_t * node );
dlist_node_t * lf_dlist_get_prev( volatile lf_dlist_t * l, volatile dlist_node_t * node );

dlist_node_t * lf_dlist_correct_next( volatile lf_dlist_t * l, volatile dlist_node_t * node );

/*  Set the deleted bit on the given node */
void lf_dlist_mark_node_pointer( volatile lf_dlist_t * l, volatile dlist_node_t ** node );

/*  Extract the real underlying node (masking out the MSB and flush if needed) */
/* Do NOT use this macro function, if PMEM mode. */
#define lf_dlist_dereference_node_pointer_mem_only( _node ) \
  (dlist_node_t *)((uint64_t)(_node) & DL_NODE_DELETED_MASK)

dlist_node_t * lf_dlist_dereference_node_pointer( lf_dlist_t             * l,
                                                  volatile dlist_node_t ** node );
bool lf_dlist_marked_next( volatile dlist_node_t * node );
bool lf_dlist_marked_prev( volatile dlist_node_t * node );

/******************************************************************************
 * dlist_cursor_t */
typedef enum _dlist_cursor_move_direction dlist_cursor_dir_t;
enum _dlist_cursor_move_direction
{
  DL_CURSOR_DIR_NONE     = 0,
  DL_CURSOR_DIR_FORWARD  = 1,  // head -> tail
  DL_CURSOR_DIR_BACKWARD = 2   // tail -> head
};

typedef struct _dlist_cursor dlist_cursor_t;
struct _dlist_cursor
{
  volatile lf_dlist_t   * volatile l;
  volatile dlist_node_t * volatile cur_node;
  volatile dlist_node_t * volatile head;
  volatile dlist_node_t * volatile tail;
  dlist_cursor_dir_t dir;
};

int32_t dlist_cursor_open( dlist_cursor_t    * c,
                           lf_dlist_t        * l,
                           dlist_cursor_dir_t  dir );
void dlist_cursor_close( dlist_cursor_t * c );
bool dlist_cursor_is_eol( dlist_cursor_t * c );

void dlist_cursor_reset( dlist_cursor_t * c );
dlist_node_t * dlist_cursor_next( dlist_cursor_t * c );
dlist_node_t * dlist_cursor_prev( dlist_cursor_t * c );
#else // IMPRV_PERF

#endif /* _DOUBLEY_LINKED_LIST_H_ */
