/*  Copyright (c) Microsoft Corporation. All rights reserved. */
/*  Licensed under the MIT license. */

#define UNUSE_ARG( _arg ) (void)(_arg)

#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lock_free_dlist.h"
#include "util.h"
#include "atomic.h"

#ifdef DEBUG
#include <assert.h>

#define dassert(c) assert(c)
#define RAW_CHECK(_cond, _msg, ...)   \
  do {                           \
    if( !(_cond) ) {             \
      fprintf(stderr, "Check %s failed: | ", #_cond ); \
      fprintf(stderr, _msg, ##__VA_ARGS__ ); \
      fprintf(stderr,"\n");       \
      abort();                    \
      dassert( 0 );              \
    }                             \
  } while(0)
#else
#define dassert(c) 
#define RAW_CHECK(_cond, _msg, ...) 
#endif

static dlist_node_t * lf_dlist_correct_prev( volatile lf_dlist_t   * l,
                                             volatile dlist_node_t * prev,
                                             volatile dlist_node_t * node );

#if 0
static void lf_dlist_unmark_node_pointer( volatile lf_dlist_t * l,
                                          volatile dlist_node_t ** node );
#endif

/* ****************************************************************************
 * dlist_node_t
 */
void dlist_node_init( volatile dlist_node_t * node )
{
  if( node != NULL )
    {
      node->prev = NULL;
      node->next = NULL;
    }
}

/* ****************************************************************************
 *  A lock-free doubly linked list using CAS,
 *  based off of the following paper:
 *  Hakan Sundell and Philippas Tsigas. 2008.
 *  Lock-free deques and doubly linked lists.
 *  J. Parallel Distrib. Comput. 68, 7 (July 2008), 1008-1020. */
int32_t lf_dlist_initiaize( volatile lf_dlist_t    * l,
                            volatile dlist_node_t  * head,
                            volatile dlist_node_t  * tail,
                            int32_t backoff_cnt_max)
{
  dassert( l != NULL );
  dassert( head != NULL );
  dassert( tail != NULL );

  memset( (void *)l, 0x00, sizeof(lf_dlist_t) );

  (void)RNG_init( (RNG *)(l->rng), (uint32_t)rdtsc(), 0, backoff_cnt_max );

  l->head = head;
  l->head->next = tail;
  l->tail = tail;
  l->tail->prev = head;

  return RC_SUCCESS;
}

void lf_dlist_finalize( volatile lf_dlist_t * l )
{
  dassert( l != NULL );

  memset( (void *)l, 0x00, sizeof(lf_dlist_t) );

#ifdef DEBUG
  CATCH_END;

  return;
#endif
}

void lf_dlist_single_thread_sanity_check( volatile lf_dlist_t * l )
{
  volatile dlist_node_t * node = NULL;
  volatile dlist_node_t * prev = NULL;

  RAW_CHECK( l->head->prev == NULL, "head->prev doesn't point to null" );
  RAW_CHECK( l->head->next, "head->next is null" );
  RAW_CHECK( l->tail->prev, "tail->prev is null" );
  RAW_CHECK( l->tail->next == NULL, "tail->next doesn't point to null" );

  node = l->head->next;
  prev = l->head;

  do
    {
      RAW_CHECK( node, "null dlist node" );
      RAW_CHECK( prev->next == node, "node.prev doesn't match prev.next" );
      RAW_CHECK( node->prev == prev, "node.prev doesn't match prev.next" );

      prev = node;
      node = node->next;
    } while( node && node->next != l->tail );
}

dlist_node_t * lf_dlist_get_next( volatile lf_dlist_t * l, volatile dlist_node_t * node )
{
  volatile dlist_node_t * volatile next      = NULL;
  volatile dlist_node_t * volatile node_next = NULL;
  volatile dlist_node_t * volatile next_next = NULL;

  while( node != l->tail )
    {
      RAW_CHECK( node, "null current node" );
      next = lf_dlist_dereference_node_pointer( l, (volatile dlist_node_t **)&(node->next) );
      if( next == NULL )
        {
          return NULL;
        }

      // RAW_CHECK( next, "null next pointer in list" );

      mem_barrier();
      next_next = next->next;

      if( (uint64_t)next_next & DL_NODE_DELETED )
        {
          /*  The next pointer of the node behind me has the deleted mark set */
          node_next = node->next;

          mem_barrier();

          if( (uint64_t)node_next != ((uint64_t)next | DL_NODE_DELETED) )
            {
              /*  But my next pointer isn't pointing the next with the deleted bit set, */
              /*  so we set the deleted bit in next's prev pointer. */
#if 0 // IMPRV_SAFETY
              lf_dlist_mark_node_pointer( l, (volatile dlist_node_t **)&(next->prev) );
#endif // IMPRV_SAFETY

              /*  Now try to unlink the deleted next node */
              (void)atomic_cas_64( &(node->next),
                                   next,
                                   (dlist_node_t *)((uint64_t)next_next & ~DL_NODE_DELETED) );
              continue;
            }
        }

      node = next;

      mem_barrier();

      if( ((uint64_t)next_next & DL_NODE_DELETED) == 0 )
        {
          return (dlist_node_t *)next;
        }
    }

  return NULL; /*  nothing after tail */
}

dlist_node_t * lf_dlist_get_prev( volatile lf_dlist_t * l, volatile dlist_node_t * node )
{
  volatile dlist_node_t * volatile prev      = NULL;
  volatile dlist_node_t * volatile prev_next = NULL;
  volatile dlist_node_t * volatile next      = NULL;

  while( node != l->head )
    {
      RAW_CHECK( node, "null current node" );

      prev = lf_dlist_dereference_node_pointer( l, (volatile dlist_node_t **)&(node->prev) );
      RAW_CHECK( prev, "null prev pointer in list" );

      prev_next = prev->next;
      mem_barrier();
      next = node->next;

      if( (prev_next == node) && 
          ((uint64_t)next & DL_NODE_DELETED) == 0 )
        {
          return (dlist_node_t *)prev;
        }
      else
        {
          if( (uint64_t)next & DL_NODE_DELETED )
            {
              node = lf_dlist_get_next( l, node );
            }
          else
            {
              prev = lf_dlist_correct_prev( l, prev, node );
            }
        }
    }

  return NULL;
}

DL_STATUS lf_dlist_insert_before( volatile lf_dlist_t   * l,
                                  volatile dlist_node_t * pivot,
                                  volatile dlist_node_t * node )
{
  volatile dlist_node_t * volatile pivot_prev = NULL;
  volatile dlist_node_t * volatile pivot_next = NULL;
  volatile dlist_node_t * volatile expected = NULL;

  RAW_CHECK( !((uint64_t )pivot & DL_NODE_DELETED), "invalid next pointer state" );

  if( pivot == l->head )
    {
      return lf_dlist_insert_after( l, pivot, node );
    }

  while( true )
    {
      pivot_prev = lf_dlist_dereference_node_pointer( 
                            l, 
                            (volatile dlist_node_t **)&(pivot->prev) );

      /*  If the guy supposed to be behind me got deleted, fast */
      /*  forward to its next node and retry */
      pivot_next = pivot->next;
      if( (uint64_t)pivot_next & DL_NODE_DELETED )
        {
          pivot = lf_dlist_get_next( l, pivot );
          pivot_prev = lf_dlist_correct_prev( l, pivot_prev, pivot ); /*  using the new pivot */
          continue;
        }

      node->prev = (dlist_node_t *)((uint64_t)pivot_prev & ~DL_NODE_DELETED);
      node->next = (dlist_node_t *)((uint64_t)pivot & ~DL_NODE_DELETED);

      mem_barrier();

      /*  Install [node] on prev->next */
      expected = (dlist_node_t *)((uint64_t)pivot & ~DL_NODE_DELETED);
      if( expected == atomic_cas_64( &(pivot_prev->next),
                                     expected,
                                     node ) )
        {
          mem_barrier();
          break;
        }

#if 1 
      pivot_prev = lf_dlist_correct_prev( l, pivot_prev, pivot );

      lf_dlist_backoff( l );
      mem_barrier();
      lf_dlist_backoff( l );

      return DL_STATUS_MERGE_IN_PROGRESS;
#else
      /*  Failed, get a new hopefully-correct prev */
      pivot_prev = lf_dlist_correct_prev( l, pivot_prev, next );
      lf_dlist_backoff( l );      
#endif
    }

  RAW_CHECK( pivot_prev, "invalid prev pointer" );
  lf_dlist_correct_prev( l, pivot_prev, pivot);

  return DL_STATUS_OK;
}

DL_STATUS lf_dlist_insert_after( volatile lf_dlist_t   * l, 
                                 volatile dlist_node_t * prev, 
                                 volatile dlist_node_t * node )
{
  volatile dlist_node_t * volatile prev_next = NULL;
  volatile dlist_node_t * volatile expected = NULL;

  RAW_CHECK( !((uint64_t )prev & DL_NODE_DELETED), "invalid prev pointer state" );

  if( prev == l->tail )
    {
      return lf_dlist_insert_before( l, prev, node );
    }

  while( true )
    {
      prev_next = prev->next;
      node->prev = (dlist_node_t *)((uint64_t)prev & ~DL_NODE_DELETED);
      node->next = (dlist_node_t *)((uint64_t)prev_next & ~DL_NODE_DELETED);

      mem_barrier();

      /*  Install [node] after [next] */
      expected = (dlist_node_t *)((uint64_t)prev_next & ~DL_NODE_DELETED);
      if( expected == atomic_cas_64( &prev->next, expected, node ) )
        {
          mem_barrier();
          break;
        }

      if( (uint64_t)prev_next & DL_NODE_DELETED )
        {
#if 1 
          return DL_STATUS_MERGE_IN_PROGRESS;
#else
          lf_dlist_delete( l, node );
          return lf_dlist_insert_before( l, prev, node );
#endif
        }

      lf_dlist_backoff( l );
#if 1
      return DL_STATUS_MERGE_IN_PROGRESS;
#endif
    }

  RAW_CHECK( prev_next, "invalid prev_next pointer" );
  lf_dlist_correct_prev( l, prev, prev_next );
  return DL_STATUS_OK;
}


#if 0
리스트 노드 삭제시 lf_dlist_delete()를 호출하는데, 
이후에 lf_dlist_correct_next()를 호출해야 한다.
사용방법과 이유는 아래와 같다. 
{
  cur_node_next = cursor->cur_node->next;
  if( DL_STATUS_OK == lf_dlist_delete( cursor->l, cursor->cur_node ) )
    {
      /* IMPORTANT:
       * 아래 함수 호출 이전까지 아래와 같은 상황이다.
       * node1    <-------------------  node2
       *   |  \-----d---|                 ^
       *   ---------->  delnode -----d----|
       *
       *   따라서 아래함수를 호출하여
       * node1  <----------------->   node2
       *     ^                          ^
       *     ----d---- delnode ----d----|
       *  와 같은 생태로 만들어 준다*/
      for( tmp = cursor->l->head; tmp != cursor->l->tail ; )
        {
          tmp = lf_dlist_correct_next( cursor->l,
                                       lf_dlist_dereference_node_pointer( cursor->l,
                                                                          &tmp ) );
          if( tmp == cur_node_next )
            {
              break;
            }
        }
    }
}
#endif

DL_STATUS lf_dlist_delete( volatile lf_dlist_t * l, volatile dlist_node_t * node )
{
  volatile dlist_node_t * volatile node_next = NULL;
  volatile dlist_node_t * volatile desired   = NULL;
  volatile dlist_node_t * volatile rnode     = NULL;
  volatile dlist_node_t * volatile node_prev = NULL;

  if( node == l->head || node == l->tail )
    {
      RAW_CHECK( ((uint64_t )node->next & DL_NODE_DELETED) == 0,
                 "invalid next pointer" );
      RAW_CHECK( ((uint64_t )node->prev & DL_NODE_DELETED) == 0,
                 "invalid next pointer" );

      return DL_STATUS_OK;
    }

  while( true )
    {
      node_next = node->next;
      if( (uint64_t)node_next & DL_NODE_DELETED )
        {
          return DL_STATUS_OK;
        }

      /*  Try to set the deleted bit in node->next */
      desired = (dlist_node_t *)((uint64_t)node_next | DL_NODE_DELETED);

      rnode = atomic_cas_64( &(node->next), node_next, desired );

      mem_barrier();

      if( rnode == node_next )
        {
          node_prev = NULL;
          while( true )
            {
              node_prev = node->prev;
              if( (uint64_t)node_prev & DL_NODE_DELETED )
                {
                  break;
                }

              desired = (dlist_node_t *)((uint64_t)node_prev | DL_NODE_DELETED);

              if( node_prev == atomic_cas_64( &node->prev, node_prev, desired ) )
                {
                  mem_barrier();
                  break;
                }
            }

          RAW_CHECK( ((uint64_t )l->head->next & DL_NODE_DELETED) == 0,
                     "invalid next pointer" );

          mem_barrier();
          lf_dlist_correct_prev( l, 
                                 (dlist_node_t *)((uint64_t)node_prev & ~DL_NODE_DELETED),
                                 node_next );

          return DL_STATUS_OK;
        }
    }
}

static dlist_node_t * lf_dlist_correct_prev( volatile lf_dlist_t   * l,
                                             volatile dlist_node_t * prev,
                                             volatile dlist_node_t * node )
{
  volatile dlist_node_t * volatile link1 = NULL;
  volatile dlist_node_t * volatile last_link = NULL;
  volatile dlist_node_t * volatile prev_next = NULL;
  volatile dlist_node_t * volatile desired   = NULL;
  volatile dlist_node_t * volatile p         = NULL;
  volatile dlist_node_t * volatile prev_cleared      = NULL;
  volatile dlist_node_t * volatile prev_cleared_prev = NULL;

  RAW_CHECK( ((uint64_t )node & DL_NODE_DELETED) == 0, "node has deleted mark" );
  RAW_CHECK( prev, "invalid prev pointer" );

  while( true )
    {
      link1 = node->prev;
      if( (uint64_t)link1 & DL_NODE_DELETED )
        {
          break;
        }

      prev_cleared = (dlist_node_t *)((uint64_t)prev & ~DL_NODE_DELETED);
#if 1
      if( prev_cleared == NULL )
        {
          return NULL;
        }
#endif

      prev_next = prev_cleared->next;
      if( (uint64_t)prev_next & DL_NODE_DELETED )
        {
          if( last_link )
            {
              lf_dlist_mark_node_pointer( l, (volatile dlist_node_t **)&prev_cleared->prev );
              mem_barrier();

              desired = (dlist_node_t *)(((uint64_t)prev_next & ~DL_NODE_DELETED));
              (void)atomic_cas_64( &(last_link->next), prev, desired );
              prev = last_link;
              last_link = NULL;

              continue;
            }

          mem_barrier();
          prev_next = prev_cleared->prev;
          prev = prev_next;
          RAW_CHECK( prev, "invalid prev pointer" );
          continue;
        }

      RAW_CHECK( ((uint64_t )prev_next & DL_NODE_DELETED) == 0,
                 "invalid next field in predecessor" );

      if( prev_next != node )
        {
          last_link = prev_cleared;
          prev = prev_next;
          continue;
        }

      p = (dlist_node_t *)(((uint64_t)prev & ~DL_NODE_DELETED));

#if 1 // imprv safety
      if( p == link1 )
        {
          break;
        }
#endif

      if( link1 == atomic_cas_64( &node->prev, link1, p ) )
        {
          mem_barrier();
          prev_cleared_prev = prev_cleared->prev;
          if( (uint64_t)prev_cleared_prev & DL_NODE_DELETED )
            {
              continue;
            }
          break;
        }
      lf_dlist_backoff( l );
    }

  return (dlist_node_t *)prev;
}

dlist_node_t * lf_dlist_correct_next( volatile lf_dlist_t   * l,
                                      volatile dlist_node_t * node )
{
  volatile dlist_node_t * volatile next      = NULL;
  volatile dlist_node_t * volatile node_next = NULL;
  volatile dlist_node_t * volatile next_next = NULL;

  while( node != l->tail )
    {
      RAW_CHECK( node, "null current node" );
      next = lf_dlist_dereference_node_pointer( l, (volatile dlist_node_t **)&(node->next) );
      if( next == NULL )
        {
          return NULL;
        }

      mem_barrier();
      next_next = next->next;

      if( (uint64_t)next_next & DL_NODE_DELETED )
        {
          /*  But my next pointer isn't pointing the next with the deleted bit set, */
          /*  so we set the deleted bit in next's prev pointer. */
          lf_dlist_mark_node_pointer( l, (volatile dlist_node_t **)&(next->prev) );

          mem_barrier();
          /*  The next pointer of the node behind me has the deleted mark set */
          node_next = node->next;
          if( (uint64_t)node_next != ((uint64_t)next | DL_NODE_DELETED) )
            {
              /*  Now try to unlink the deleted next node */
              (void)atomic_cas_64( &(node->next),
                                   next,
                                   (dlist_node_t *)((uint64_t)next_next & ~DL_NODE_DELETED) );
              continue;
            }
        }

      node = next;

      mem_barrier();

      if( ((uint64_t)next_next & DL_NODE_DELETED) == 0 )
        {
          return (dlist_node_t *)next;
        }
    }

  return NULL; /*  nothing after tail */
}

void lf_dlist_backoff( volatile lf_dlist_t * l )
{
  volatile uint64_t loops = (uint64_t)RNG_generate( (RNG *)(l->rng) );
  mem_barrier();
  while( loops-- )
    {
      mem_barrier();
      /* do nothing */
    }
}

void lf_dlist_mark_node_pointer( volatile lf_dlist_t * l, volatile dlist_node_t ** node )
{
  volatile dlist_node_t * volatile node_ptr = NULL;
  uint64_t flags = DL_NODE_DELETED;

  while( true )
    {
      node_ptr = *node;

      RAW_CHECK( node_ptr != l->head->next,
                 "cannot mark head node's next pointer" );

      if( ((uint64_t)node_ptr & DL_NODE_DELETED) ||
          ( node_ptr == atomic_cas_64( node,
                                       node_ptr,
                                       (dlist_node_t *)((uint64_t)node_ptr | flags) ) ) )
        {
          break;
        }
    }
}

#if 0
static void lf_dlist_unmark_node_pointer( volatile lf_dlist_t * l,
                                          volatile dlist_node_t ** node )
{
  volatile dlist_node_t * node_ptr = NULL;
  uint64_t flags = ~DL_NODE_DELETED;

  while( true )
    {
      node_ptr = *node;
      if( node_ptr == atomic_cas_64( node,
                                     node_ptr,
                                     (dlist_node_t *)((uint64_t)node_ptr & flags) ) )
        {
          break;
        }
    }
}
#endif

/*  Extract the real underlying node (masking out the MSB and flush if needed) */
dlist_node_t * lf_dlist_dereference_node_pointer( volatile lf_dlist_t     * l,
                                                  volatile dlist_node_t  ** node )
{
  return (dlist_node_t *)((uint64_t)(*node) & ~DL_NODE_DELETED);
}

bool lf_dlist_marked_next( volatile dlist_node_t * node )
{
  mem_barrier();
  return ((((uint64_t)(node->next)) & DL_NODE_DELETED) ? true : false);
}

bool lf_dlist_marked_prev( volatile dlist_node_t * node )
{
  mem_barrier();
  return ((((uint64_t)(node->prev)) & DL_NODE_DELETED) ? true : false);
}


/******************************************************************************
 * dlist_cursor_t
 * */

int32_t dlist_cursor_open( volatile dlist_cursor_t    * c, 
                           volatile lf_dlist_t        * l, 
                           dlist_cursor_dir_t  dir )
{
  TRY( c == NULL || l == NULL );

  c->l = l;
  c->head = l->head;
  c->tail = l->tail;

  c->dir = dir;

  if( dir == DL_CURSOR_DIR_FORWARD )
    {
      c->cur_node = c->head;
    }
  else
    {
      c->cur_node = c->tail;
    }

  return RC_SUCCESS;

  CATCH_END;

  return RC_FAIL;
}

void dlist_cursor_close( volatile dlist_cursor_t * c )
{
  if( c != NULL )
    {
      c->l = NULL;
      c->head = NULL;
      c->tail = NULL;

      c->cur_node = NULL;

      c->dir = DL_CURSOR_DIR_NONE;
    }
}

void dlist_cursor_reset( volatile dlist_cursor_t * c )
{
  if( c != NULL )
    {
      if( c->dir == DL_CURSOR_DIR_FORWARD )
        {
          c->cur_node = c->head;
        }
      else
        {
          c->cur_node = c->tail;
        }
    }
}

dlist_node_t * dlist_cursor_next( volatile dlist_cursor_t * c )
{
#ifdef DEBUG
  TRY( c == NULL );
#endif

  c->dir = DL_CURSOR_DIR_FORWARD;
  c->cur_node = lf_dlist_get_next( c->l, c->cur_node );

  return (dlist_node_t *)c->cur_node;

#ifdef DEBUG
  CATCH_END;

  return NULL;
#endif
}

dlist_node_t * dlist_cursor_prev( volatile dlist_cursor_t * c )
{
#ifdef DEBUG
  TRY( c == NULL );
#endif
  c->dir = DL_CURSOR_DIR_BACKWARD;
  c->cur_node = lf_dlist_get_prev( c->l, c->cur_node );

  return (dlist_node_t *)c->cur_node;

#ifdef DEBUG
  CATCH_END;

  return NULL;
#endif
}

bool dlist_cursor_is_eol( volatile dlist_cursor_t * c )
{
  bool ret = false;

#if 0
  // This check code would make program performance to degrade.
  if(( c == NULL ) || (c->cur_node == NULL ) )
    {
      return true;
    }
#endif

  if( c->dir == DL_CURSOR_DIR_FORWARD )
    {
      ret = (c->cur_node == c->tail) ? true : false;
    }
  else
    {
      ret = (c->cur_node == c->head)? true : false;
    }

  return ret;
}
