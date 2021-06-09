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
#define RAW_CHECK(_cond, _msg, ...)                     \
  do {                                                  \
    if( !(_cond) ) {                                    \
      fprintf(stderr, "Check %s failed: | ", #_cond );  \
      fprintf(stderr, _msg, ##__VA_ARGS__ );            \
      fprintf(stderr,"\n");                             \
      abort();                                          \
      dassert( 0 );                                     \
    }                                                   \
  } while(0)
#else
#define dassert(c)
#define RAW_CHECK(_cond, _msg, ...)
#endif

static dlist_node_t * lf_dlist_correct_prev( lf_dlist_t   * volatile l,
                                             dlist_node_t * volatile prev,
                                             dlist_node_t * volatile node );

#if 0
static void lf_dlist_unmark_node_pointer( lf_dlist_t * volatile l,
                                          dlist_node_t ** volatile node );
#endif

/* ****************************************************************************
 * dlist_node_t
 */
void dlist_node_init( dlist_node_t * volatile node )
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
int32_t lf_dlist_initiaize( lf_dlist_t    * volatile l,
                            dlist_node_t  * volatile head,
                            dlist_node_t  * volatile tail,
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

void lf_dlist_finalize( lf_dlist_t * volatile l )
{
  dassert( l != NULL );

  memset( (void *)l, 0x00, sizeof(lf_dlist_t) );

#ifdef DEBUG
  CATCH_END;

  return;
#endif
}

void lf_dlist_single_thread_sanity_check( lf_dlist_t * volatile l )
{
  dlist_node_t * volatile node = NULL;
  dlist_node_t * volatile prev = NULL;

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

dlist_node_t * lf_dlist_get_next( lf_dlist_t * volatile l, dlist_node_t * volatile _node )
{
  dlist_node_t * volatile node      = _node;
  dlist_node_t * volatile next      = NULL;
  dlist_node_t * volatile node_next = NULL;
  dlist_node_t * volatile next_next = NULL;

  while( node != l->tail )
    {
      RAW_CHECK( node, "null current node" );
      next = lf_dlist_dereference_node_pointer_mem_only( node->next );
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
              lf_dlist_mark_node_pointer( l, (dlist_node_t ** volatile)&(next->prev) );

              /*  Now try to unlink the deleted next node */
              (void)atomic_cas_64( &(node->next),
                                   next,
                                   (dlist_node_t * volatile)((uint64_t)next_next & DL_NODE_DELETED_MASK) );
#endif // IMPRV_SAFETY
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

dlist_node_t * lf_dlist_get_prev( lf_dlist_t * volatile l, dlist_node_t * volatile _node )
{
  dlist_node_t * volatile node = _node;
  dlist_node_t * volatile prev;
  dlist_node_t * volatile prev_next;
  dlist_node_t * volatile next;

  while( node != l->head )
    {
      RAW_CHECK( node, "null current node" );
      prev = lf_dlist_dereference_node_pointer_mem_only( node->prev );
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
              node = lf_dlist_correct_next( l, node );
            }
          else
            {
              prev = lf_dlist_correct_prev( l, prev, node );
            }
        }
    }

  return NULL;
}

DL_STATUS lf_dlist_insert_before( lf_dlist_t   * volatile l,
                                  dlist_node_t * volatile _pivot,
                                  dlist_node_t * volatile _node )
{
  dlist_node_t * volatile pivot = _pivot;
  dlist_node_t * volatile node  = _node;
  dlist_node_t * volatile pivot_prev = NULL;
  dlist_node_t * volatile pivot_next = NULL;
  dlist_node_t * volatile expected = NULL;

  RAW_CHECK( !((uint64_t )pivot & DL_NODE_DELETED), "invalid next pointer state" );

  if( pivot == l->head )
    {
      return lf_dlist_insert_after( l, pivot, node );
    }

  while( true )
    {
      // mem_barrier();
      pivot_prev = lf_dlist_dereference_node_pointer_mem_only( pivot->prev );

      /*  If the guy supposed to be behind me got deleted, fast */
      /*  forward to its next node and retry */
      pivot_next = pivot->next;
      if( (uint64_t)pivot_next & DL_NODE_DELETED )
        {
          pivot = lf_dlist_get_next( l, pivot );
          pivot_prev = lf_dlist_correct_prev( l, pivot_prev, pivot ); /*  using the new pivot */
          continue;
        }

      node->prev = (dlist_node_t * volatile)((uint64_t)pivot_prev & DL_NODE_DELETED_MASK);
      node->next = (dlist_node_t * volatile)((uint64_t)pivot & DL_NODE_DELETED_MASK);

      mem_barrier();

      /*  Install [node] on prev->next */
      expected = (dlist_node_t * volatile)((uint64_t)pivot & DL_NODE_DELETED_MASK);
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

DL_STATUS lf_dlist_insert_after( lf_dlist_t   * volatile l,
                                 dlist_node_t * volatile _prev,
                                 dlist_node_t * volatile _node )
{
  dlist_node_t * volatile prev = _prev;
  dlist_node_t * volatile node = _node;
  dlist_node_t * volatile prev_next = NULL;
  dlist_node_t * volatile expected = NULL;

  RAW_CHECK( !((uint64_t )prev & DL_NODE_DELETED), "invalid prev pointer state" );

  if( prev == l->tail )
    {
      return lf_dlist_insert_before( l, prev, node );
    }

  while( true )
    {
      mem_barrier();
      prev_next = prev->next;
      node->prev = (dlist_node_t * volatile)((uint64_t)prev & DL_NODE_DELETED_MASK);
      node->next = (dlist_node_t * volatile)((uint64_t)prev_next & DL_NODE_DELETED_MASK);

      mem_barrier();

      /*  Install [node] after [next] */
      expected = (dlist_node_t * volatile)((uint64_t)prev_next & DL_NODE_DELETED_MASK);
      if( expected == atomic_cas_64( &prev->next, expected, node ) )
        {
          mem_barrier();
          break;
        }

      if( (uint64_t)prev_next & DL_NODE_DELETED )
        {
          return DL_STATUS_MERGE_IN_PROGRESS;
        }

      lf_dlist_backoff( l );
      return DL_STATUS_MERGE_IN_PROGRESS;
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

DL_STATUS lf_dlist_delete( lf_dlist_t * volatile l, dlist_node_t * volatile _node )
{
  dlist_node_t * volatile node = _node;
  dlist_node_t * volatile node_next = NULL;
  dlist_node_t * volatile desired   = NULL;
  dlist_node_t * volatile rnode     = NULL;
  dlist_node_t * volatile node_prev = NULL;

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
      mem_barrier();
      node_next = node->next;
      if( (uint64_t)node_next & DL_NODE_DELETED )
        {
          return DL_STATUS_OK;
        }

      /*  Try to set the deleted bit in node->next */
      desired = (dlist_node_t * volatile)((uint64_t)node_next | DL_NODE_DELETED);

      rnode = atomic_cas_64( &(node->next), node_next, desired );

      if( rnode == node_next )
        {
          node_prev = NULL;
          while( true )
            {
              mem_barrier();
              node_prev = node->prev;
              if( (uint64_t)node_prev & DL_NODE_DELETED )
                {
                  break;
                }

              desired = (dlist_node_t * volatile)((uint64_t)node_prev | DL_NODE_DELETED);

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
                                 (dlist_node_t * volatile)((uint64_t)node_prev & DL_NODE_DELETED_MASK),
                                 node_next );

          return DL_STATUS_OK;
        }
    }
}

static dlist_node_t * lf_dlist_correct_prev( lf_dlist_t   * volatile l,
                                             dlist_node_t * volatile _prev,
                                             dlist_node_t * volatile _node )
{
  dlist_node_t * volatile prev = _prev;
  dlist_node_t * volatile node = _node;
  dlist_node_t * volatile link1 = NULL;
  dlist_node_t * volatile last_link = NULL;
  dlist_node_t * volatile prev_next = NULL;
  dlist_node_t * volatile desired   = NULL;
  dlist_node_t * volatile p         = NULL;
  dlist_node_t * volatile prev_cleared      = NULL;
  dlist_node_t * volatile prev_cleared_prev = NULL;

  RAW_CHECK( ((uint64_t )node & DL_NODE_DELETED) == 0, "node has deleted mark" );
  RAW_CHECK( prev, "invalid prev pointer" );

  while( true )
    {
      mem_barrier();
      link1 = node->prev;
      if( (uint64_t)link1 & DL_NODE_DELETED )
        {
          break;
        }

      prev_cleared = (dlist_node_t * volatile)((uint64_t)prev & DL_NODE_DELETED_MASK);
#if 1
      if( prev_cleared == NULL )
        {
          return NULL;
        }
#endif

      mem_barrier();
      prev_next = prev_cleared->next;
      if( (uint64_t)prev_next & DL_NODE_DELETED )
        {
          if( last_link )
            {
              lf_dlist_mark_node_pointer( l, (dlist_node_t ** volatile)&prev_cleared->prev );
              mem_barrier();

              desired = (dlist_node_t * volatile)(((uint64_t)prev_next & DL_NODE_DELETED_MASK));
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
          mem_barrier();
          prev = prev_next;
          continue;
        }

      p = (dlist_node_t * volatile)(((uint64_t)prev & DL_NODE_DELETED_MASK));

#if 1 // IMPRV_SAFTEY
      if( p == link1 )
        {
          break;
        }
#endif // IMPRV_SAFTEY

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

dlist_node_t * lf_dlist_correct_next( lf_dlist_t   * volatile l,
                                      dlist_node_t * volatile _node )
{
  dlist_node_t * volatile node      = _node;
  dlist_node_t * volatile next      = NULL;
  dlist_node_t * volatile node_next = NULL;
  dlist_node_t * volatile next_next = NULL;

  while( node != l->tail )
    {
      RAW_CHECK( node, "null current node" );
      mem_barrier();
      next = lf_dlist_dereference_node_pointer_mem_only( node->next );
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
          lf_dlist_mark_node_pointer( l, (dlist_node_t **volatile)&(next->prev) );

          mem_barrier();
          /*  The next pointer of the node behind me has the deleted mark set */
          node_next = node->next;
          if( (uint64_t)node_next != ((uint64_t)next | DL_NODE_DELETED) )
            {
              /*  Now try to unlink the deleted next node */
              while( next !=
                     atomic_cas_64( &(node->next),
                                    next,
                                    (dlist_node_t * volatile)((uint64_t)next_next & DL_NODE_DELETED_MASK) ));
                {
                  break;
                }
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

void lf_dlist_backoff( lf_dlist_t * volatile l )
{
  volatile uint64_t loops = (uint64_t)RNG_generate( (RNG *)(l->rng) );
  mem_barrier();
  while( loops-- )
    {
      mem_barrier();
      /* do nothing */
    }
}

void lf_dlist_mark_node_pointer( lf_dlist_t * volatile l, dlist_node_t ** volatile _node )
{
  dlist_node_t ** volatile node = _node;
  dlist_node_t * volatile node_ptr = NULL;
  uint64_t flags = DL_NODE_DELETED;

  while( true )
    {
      mem_barrier();
      node_ptr = *node;

      RAW_CHECK( node_ptr != l->head->next,
                 "cannot mark head node's next pointer" );

      if( ((uint64_t)node_ptr & DL_NODE_DELETED) ||
          ( node_ptr == atomic_cas_64( node,
                                       node_ptr,
                                       (dlist_node_t * volatile)((uint64_t)node_ptr | flags) ) ) )
        {
          break;
        }
    }
}

#if 0
static void lf_dlist_unmark_node_pointer( lf_dlist_t * volatile l,
                                          dlist_node_t ** volatile node )
{
  dlist_node_t * volatile node_ptr = NULL;
  uint64_t flags = DL_NODE_DELETED_MASK;

  while( true )
    {
      node_ptr = *node;
      if( node_ptr == atomic_cas_64( node,
                                     node_ptr,
                                     (dlist_node_t * volatile)((uint64_t)node_ptr & flags) ) )
        {
          break;
        }
    }
}
#endif

/*  Extract the real underlying node (masking out the MSB and flush if needed) */
dlist_node_t * lf_dlist_dereference_node_pointer( lf_dlist_t     * volatile l,
                                                  dlist_node_t  ** volatile node )
{
  return (dlist_node_t *)((uint64_t)(*node) & DL_NODE_DELETED_MASK);
}

bool lf_dlist_marked_next( dlist_node_t * volatile node )
{
  mem_barrier();
  return ((((uint64_t)(node->next)) & DL_NODE_DELETED) ? true : false);
}

bool lf_dlist_marked_prev( dlist_node_t * volatile node )
{
  mem_barrier();
  return ((((uint64_t)(node->prev)) & DL_NODE_DELETED) ? true : false);
}


/******************************************************************************
 * dlist_cursor_t
 * */

int32_t dlist_cursor_open( dlist_cursor_t    * volatile c,
                           lf_dlist_t        * volatile l,
                           dlist_cursor_dir_t  dir )
{
  TRY( c == NULL || l == NULL );

  c->l = l;
  c->head = l->head;
  c->tail = l->tail;

  c->dir = dir;

  mem_barrier();
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

void dlist_cursor_close( dlist_cursor_t * volatile c )
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

void dlist_cursor_reset( dlist_cursor_t * volatile c )
{
  if( c != NULL )
    {
      mem_barrier();
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

dlist_node_t * dlist_cursor_next( dlist_cursor_t * volatile c )
{
#ifdef DEBUG
  TRY( c == NULL );
#endif

  c->dir = DL_CURSOR_DIR_FORWARD;
  mem_barrier();
  c->cur_node = lf_dlist_get_next( c->l, c->cur_node );

  return (dlist_node_t *)c->cur_node;

#ifdef DEBUG
  CATCH_END;

  return NULL;
#endif
}

dlist_node_t * dlist_cursor_prev( dlist_cursor_t * volatile c )
{
#ifdef DEBUG
  TRY( c == NULL );
#endif
  c->dir = DL_CURSOR_DIR_BACKWARD;
  mem_barrier();
  c->cur_node = lf_dlist_get_prev( c->l, c->cur_node );

  return (dlist_node_t *)c->cur_node;

#ifdef DEBUG
  CATCH_END;

  return NULL;
#endif
}

bool dlist_cursor_is_eol( dlist_cursor_t * volatile c )
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
