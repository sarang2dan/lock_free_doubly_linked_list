#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>
#include <sched.h>
#include <sys/errno.h>
#include <signal.h>

#include "util.h"
#include "atomic.h"
#include "lock_free_dlist.h"


// #define DEBUG 1
// #define FIXED_THREADS 1

#define THRESHOLD_WORKING_SLOW_EVICTOR  32
#define THRESHOLD_WORKING_SLOW_AGER     32

#ifdef FIXED_THREADS
#define MAX_ARGC   2
#define THR_NUM_INSERT  20
#define THR_NUM_READ    5
#define THR_NUM_EVICTOR 1
#define THR_NUM_AGER    1
#else /* FIXED_THREADS */
#define MAX_ARGC   4
int32_t THR_NUM_INSERT        = 1;
int32_t THR_NUM_READ          = 1;
const int32_t THR_NUM_EVICTOR = 1;
const int32_t THR_NUM_AGER    = 1;
#endif /* FIXED_THREADS */

#define THR_NUM_MAX (THR_NUM_INSERT + THR_NUM_READ + THR_NUM_EVICTOR + THR_NUM_AGER)

#define RC_EXIT_PROGRAM  1

typedef void * (*thread_func_t) ( void * arg );

volatile int32_t  g_next_key = 0;
volatile int32_t  g_delete_cnt = 0;
volatile int32_t  MAX_ITEM_CNT = 0;

volatile bool     g_exit_flag = false;
volatile int32_t  g_created_thread_cnt = 0;
pthread_barrier_t g_thr_barrier[1];

char * make_error_prefix( char        * buf,
                          const char  * filename,
                          int32_t       lineno,
                          const char  * funcname );
char * make_thr_error_prefix( char        * buf,
                              int32_t       tid,
                              const char  * filename,
                              int32_t       lineno,
                              const char  * funcname );

// esb: a buffer of error string
#define get_thr_error_prefix(_tid, _esb) \
  make_thr_error_prefix( _esb, _tid, __FILE__, __LINE__, __FUNCTION__ )

char * make_thr_error_prefix( char        * buf,
                              int32_t       tid,
                              const char  * filename,
                              int32_t       lineno,
                              const char  * funcname )
{
  sprintf( buf, "[%d] %s:%d:%s():", tid, filename, lineno, funcname );
  return buf;
}

#define get_error_prefix(_esb) \
  make_error_prefix( _esb, __FILE__, __LINE__, __FUNCTION__ )

char * make_error_prefix( char        * buf,
                          const char  * filename,
                          int32_t       lineno,
                          const char  * funcname )
{
  sprintf( buf, "%s:%d:%s():", filename, lineno, funcname );
  return buf;
}


#define _MKSTR( _arg ) #_arg
#define MKSTR( _arg ) _MKSTR( _arg )

#ifdef DEBUG
#define print_data_list_node( _n )    \
  do {                                                                  \
    data_list_node_t * _node = (data_list_node_t *)(_n);                \
    if( _node ) {                                                       \
      fprintf( stdout,                                                  \
               "%s():%d : [n:%p] [prev: %16p] [next: %16p]"                 \
               " [ag_prev: %16p] [ag_next: %16p] "                          \
               " [key:%d] [state:%s]\n",                                 \
               __func__, __LINE__,                                      \
               (_node),                                                 \
               (_node)->prev,                                           \
               (_node)->next,                                           \
               (_node)->ag_prev,                                        \
               (_node)->ag_next,                                        \
               (_node)->key,                                            \
               g_state_str[(_node)->state] );                           \
    } else {                                                            \
      fprintf( stderr,                                                  \
               "%s():%d : [node:NULL]\n",                               \
               __func__, __LINE__ );                                    \
    }                                                                   \
  } while ( 0 )
#else
#define print_data_list_node( _node )
#endif

typedef enum _dlist_node_state dlist_node_state_t;
enum _dlist_node_state
{
  DLIST_NODE_STATE_INIT = 0,
  DLIST_NODE_STATE_AVAIL,
  DLIST_NODE_STATE_NEED_EVICT,
  DLIST_NODE_STATE_EVICTED,
  DLIST_NODE_STATE_ON_AGING,
  DLIST_NODE_STATE_MAX
};

char g_state_str[DLIST_NODE_STATE_MAX+1][16] =
{
  MKSTR(INIT),
  MKSTR(AVAIL),
  MKSTR(NEED_EVICT),
  MKSTR(EVICTED),
  MKSTR(ON_AGING),
  "" // DLIST_NODE_STATE_MAX
};


// 'read_latch' just define to int32_t type, because we can use atomic functions.
// However it does not support recovery feature,
// so that 'read_latch' should be replaced another  sync tool as like pthread_mutex_t.
#define DATA_LIST_NODE_DEFINE_MEMBER_VARS       \
  volatile  int32_t            key;             \
  volatile  int32_t            read_cnt;        \
  volatile  int32_t            read_latch;      \
  volatile  dlist_node_state_t state

typedef struct _aging_list_node aging_list_node_t;
struct _aging_list_node
{
  volatile aging_list_node_t   * volatile ag_prev;  // for aging list
  volatile aging_list_node_t   * volatile ag_next;  // for aging list
  DATA_LIST_NODE_DEFINE_MEMBER_VARS;
};

typedef struct _data_list_node   data_list_node_t;
typedef data_list_node_t         data_list_t;
struct _data_list_node
{
  volatile data_list_node_t   * volatile prev;
  volatile data_list_node_t   * volatile next;
  volatile aging_list_node_t  * volatile ag_prev;  // for aging list
  volatile aging_list_node_t  * volatile ag_next;  // for aging list
  DATA_LIST_NODE_DEFINE_MEMBER_VARS;
};

bool data_list_node_is_read_latched( data_list_node_t * node )
{
  return (node->read_latch != 0) ? true : false;
}

typedef struct _data_table data_table_t;
struct _data_table
{
  volatile lf_dlist_t     list[1];       // entry pointer of log list
  volatile lf_dlist_t     aging_list[1]; // entry pointer of aging list

  volatile data_list_node_t    lhead[1];     // list head
  volatile data_list_node_t    ltail[1];

  volatile aging_list_node_t    ahead[1];     // aging list head
  volatile aging_list_node_t    atail[1];

  volatile int32_t data_list_count;
  volatile int32_t aging_list_count;
};

// data_list_node_t to aging list node(dlist_node_t)
#define data_list_n_to_aging_list_n( _lnode )   \
  ((aging_list_node_t *)((char *)(_lnode) + offsetof(data_list_node_t, ag_prev)))

// aging list node to data_list_node_t
#define aging_list_n_to_data_list_n( _anode )  \
  ((data_list_node_t *)((char *)(_anode) - offsetof(data_list_node_t, ag_prev)))

#define dlist_cursor_conv_anode_to_lnode( _cursor ) \
  aging_list_n_to_data_list_n((_cursor)->cur_node)

#define dlist_cursor_conv_lnode_to_anode( _cursor ) \
  data_list_n_to_aging_list_n((_cursor)->cur_node)

#define dlist_cursor_get_list_node( _cursor ) \
  ((data_list_node_t *)((_cursor)->cur_node))

#define DLIST_DEFAULT_MAX_BACKOFF_LOG_LIST   700
#define DLIST_DEFAULT_MAX_BACKOFF_AGING_LIST 1000

#define dlist_is_empty( _l ) \
  (((lf_dlist_get_next( (_l), (_l)->head ) == (_l)->tail) && \
    (lf_dlist_get_prev( (_l), (_l)->tail ) == (_l)->head)) ? true : false )

#define DLIST_ITERATE( _cursor )             \
  for( dlist_cursor_next( (_cursor) ) ;         \
       (dlist_cursor_is_eol(_cursor ) != true) && ((_cursor)->cur_node != NULL) ; \
       dlist_cursor_next( (_cursor) ) )

#define DLIST_ITERATE_FROM( _node, _cursor )  \
  DLIST_ITERATE( _cursor )

#define DLIST_ITERATE_BACK( _cursor )        \
  for( dlist_cursor_prev( ( _cursor) ) ;         \
       (dlist_cursor_is_eol( _cursor ) != true) && ((_cursor)->cur_node != NULL) ; \
       dlist_cursor_prev( (_cursor) ) )

#define DLIST_ITERATE_BACK_FROM( _cursor )   \
  DLIST_ITERATE_BACK( _cursor )

#define data_list_key_cmp( _key1, _node ) \
  ((_key1) - ((data_list_node_t *)(_node))->key)

typedef struct _thr_arg thr_arg_t;
struct _thr_arg
{
  int32_t         tid; // human-friendly
  pthread_t       thr;
  data_table_t  * tbl;
  thread_func_t   func;
};


int32_t insert_data( data_table_t * tbl );
void rollback_callback( int32_t sigid );
void * func_insert( void * arg );
void * func_read( void * arg );
void * func_evict( void * arg );
void * func_aging( void * arg );
int32_t working_threads_create( thr_arg_t * targs );
int32_t working_threads_join( thr_arg_t * volatile targs, int32_t thr_cnt );
int32_t data_list_node_get_state( data_list_node_t * node );
int32_t data_list_node_set_state( data_list_node_t * node, int32_t state );

int32_t data_table_init( data_table_t ** _t )
{
  char esb[512];
  data_table_t * t = NULL;

  TRY_GOTO( _t == NULL, err_invalid_arg );

  t = (data_table_t *)calloc( 1, sizeof(data_table_t) );
  TRY_GOTO( t == NULL, err_fail_alloc );

  // list init
  lf_dlist_initiaize( t->list,
                      (dlist_node_t *)t->lhead,
                      (dlist_node_t *)t->ltail,
                      DLIST_DEFAULT_MAX_BACKOFF_LOG_LIST );

  // aging list init
  lf_dlist_initiaize( t->aging_list, 
                      (dlist_node_t *)data_list_n_to_aging_list_n(t->ahead),
                      (dlist_node_t *)data_list_n_to_aging_list_n(t->atail),
                      DLIST_DEFAULT_MAX_BACKOFF_AGING_LIST );

  *_t = t;

  return RC_SUCCESS;

  CATCH( err_fail_alloc )
    {
      perror(get_error_prefix(esb));
    }
  CATCH( err_invalid_arg )
    {
      errno = EINVAL;
      perror(get_error_prefix(esb));
    }
  CATCH_END;

  return RC_FAIL;
}

void data_table_finalize( data_table_t * volatile t )
{
  /* omited: free nodes in t.list and t.anging_list */
  if( t ) {
    free( t );
  }
}

/* head | -- (key1) --- (key2) --- (key3) --- ... --- (newest_key) -- | tail */

data_list_node_t * data_table_insert( data_table_t * volatile t, int32_t key )
{
  char esb[512];
  data_list_node_t * new_node;
  data_list_node_t * node;
  dlist_cursor_t    cursor[1];
  DL_STATUS         st = 0;

  int32_t  cmp_ret = 0;
  int32_t  state = 0;
  bool     is_cursor_open = false;
  bool     is_inserted = false;

  new_node = (data_list_node_t *)calloc(1, sizeof(data_list_node_t) );
  TRY_GOTO( new_node == NULL, err_alloc_data_list_node );
  state = 1;

  new_node->key = key;

label_insert_again:
  TRY( dlist_cursor_open( cursor, t->list, DL_CURSOR_DIR_BACKWARD ) != RC_SUCCESS );
  is_cursor_open = true;

  mem_barrier();

  for( dlist_cursor_prev( cursor ) ;
       cursor->cur_node != NULL ;
       dlist_cursor_prev( cursor ) )
    {
      node = dlist_cursor_get_list_node( cursor );

      if( dlist_cursor_is_eol( cursor ) != true )
        {
          cmp_ret = data_list_key_cmp( key, node );
        }
      else
        {
          cmp_ret = 1;
        }

      if( cmp_ret > 0 )
        {
          st = lf_dlist_insert_after( t->list,
                                      (dlist_node_t *)node,
                                      (dlist_node_t *)new_node );

          TRY_GOTO( st != DL_STATUS_OK, label_insert_again );

          is_inserted = true;
          break;
        }
    }

  TRY( is_inserted == false );

  is_cursor_open = false;
  dlist_cursor_close( cursor );

  atomic_inc_fetch( &(t->data_list_count) );

  return new_node;

  CATCH( err_alloc_data_list_node )
    {
      perror(get_error_prefix(esb));
    }
  CATCH_END;

  if( is_cursor_open == true )
    {
      dlist_cursor_close( cursor );
    }

  if( state == 1 )
    {
      free( new_node );
    }

  return NULL;
}

bool data_list_check_need_evict( int32_t read_cnt, int32_t cond_read )
{
  if( read_cnt >= cond_read )
    {
      return true;
    }
  else
    {
      return false;
    }
}

int32_t data_list_evict( volatile data_table_t * t )
{
  volatile data_list_node_t  * volatile node = NULL;
  volatile dlist_cursor_t     cursor[1] = {};
  volatile dlist_node_t     * volatile tmp = NULL;
  volatile dlist_node_t     * volatile cur_node_next = NULL;
  bool     is_cursor_open = false;
  int32_t  ret = 0;
  int32_t  evict_cnt = 0;
  bool     is_need_evict = false;

  TRY( dlist_cursor_open( cursor, t->list, DL_CURSOR_DIR_FORWARD ) != RC_SUCCESS );
  is_cursor_open = true;

label_try_again:
  (void)dlist_cursor_reset( cursor );

  mem_barrier();

  DLIST_ITERATE( cursor )
    {
      if( g_exit_flag == true )
        {
          break;
        }

      node = dlist_cursor_get_list_node( cursor );

      if( node->state < DLIST_NODE_STATE_NEED_EVICT )
        {
          /* evictor는 퇴거대상인지 검사한 후 '상태 변경' 및 퇴거한다 */
          is_need_evict = data_list_check_need_evict( node->read_cnt, THR_NUM_READ );
          if( is_need_evict == true )
            {
              do {
                ret = data_list_node_set_state( node, DLIST_NODE_STATE_NEED_EVICT );
              } while( ret != RC_SUCCESS );
              /* continue to evict */
            }
          else
            {
              /* 무조건 첫 노드만 에이징한다.
               * node가 free되었으므로, 이 노드를 액세스하는 것은,
               * 쓰레기 값을 읽는 것이다. 다른 트랜잭션이 동일 주소의 노드를
               * 할당받아 다른 값을 기록할 수도 있다. 따라서 free된 노드의 next를
               * 참조하면 꼬이는 수가 있다. */
              goto label_try_again;
            }
        }

#ifdef DEBUG
      printf(" - evict node - key:%d\n", node->key );
#endif /* DEBUG */

      /* EVICT를 수행하는 블럭! */
        {
          /* node->state == DLIST_NODE_STATE_NEED_EVICT */
          while( true )
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
                              lf_dlist_dereference_node_pointer( cursor->l, &tmp ) );
                      if( tmp == cur_node_next )
                        {
                          break;
                        }
                    }

                  /* 트랜잭션 유입이 있다면 evictor가 동작할 것이다.
                   * 이때, list에 리스트 노드가 적을 수록, 
                   * insert 스레드와 evictor가 서로 충돌하여 역전될 확률이 높아진다. 
                   * 이를 방지하기 위해 데이터 리스트의 개수가 적고, 수행되는
                   * 트랜잭션이 있다면, evictor가 느리게 동작해야 한다. */
                  if( t->data_list_count <= THRESHOLD_WORKING_SLOW_EVICTOR ) /* && insert threads are doing some operations. */
                    {
                      lf_dlist_backoff( t->aging_list );
                      lf_dlist_backoff( t->aging_list );
                    }

                  break;
                }
            }

          while( true )
            {
              if( data_list_node_is_read_latched( node ) != true )
                {
                  break;
                }

              lf_dlist_backoff( t->aging_list );
            }

          /* NOTE:
           * 이후에 이 삭제할 노드로 들어올 수 있는 링크는 없어야 한다.
           * 만일, tb_f_malloc()  함수에서 DL_NODE_DELETED 비트로 새겨진
           * 링크를 액세스하다가 죽는 문제가 발생하면, backoff 시간이
           * 짧아서 생기는 문제다. */
          mem_barrier();

          while( true )
            {
              DL_STATUS st = DL_STATUS_OK;

              st = lf_dlist_insert_before( t->aging_list, 
                                           (dlist_node_t *)(t->aging_list->tail), 
                                           dlist_cursor_conv_lnode_to_anode( cursor ) );
              if( st == DL_STATUS_OK )
                {
                  do {
                    ret = data_list_node_set_state( node, DLIST_NODE_STATE_EVICTED );
                  } while( ret != RC_SUCCESS );

                  atomic_fetch_dec( &(t->data_list_count) );
                  atomic_fetch_inc( &(t->aging_list_count) );
                  break;
                }
              else
                {
                  /* ager와 충돌이 났을 것이다. 
                   * 백오프하여 삽입 시점을 조정한다 */
                  lf_dlist_backoff( t->aging_list );
                  lf_dlist_backoff( t->aging_list );
                }
            }

          evict_cnt++;

#if 1 // IMPRV_SAFETY
          /* 무조건 첫 노드만 에이징한다. 
           * node가 free되었으므로, 이 노드를 액세스하는 것은, 
           * 쓰레기 값을 읽는 것이다. 다른 트랜잭션이 동일 주소의 노드를
           * 할당받아 다른 값을 기록할 수도 있다. 따라서 free된 노드의 next를
           * 참조하면 꼬이는 수가 있다. */
          goto label_try_again;
#endif // IMPRV_SAFETY
        } /* if node->status */
    } /* DLIST_ITERATE */

  is_cursor_open = false;
  dlist_cursor_close( cursor );

  return evict_cnt;

  CATCH_END;

  if( is_cursor_open == true )
    {
      dlist_cursor_close( cursor );
    }

  return RC_FAIL;
}

static volatile uint64_t g_total_aging_cnt = 0;

uint64_t data_list_get_total_aging_cnt( void )
{
  return g_total_aging_cnt;
}

int32_t data_list_delete_evicted( volatile data_table_t * t )
{
  volatile data_list_node_t  * node = NULL;
  volatile dlist_cursor_t     cursor[1] = {};
  volatile dlist_node_t     * tmp = NULL;
  volatile dlist_node_t     * cur_node_next = NULL;
  volatile bool       is_cursor_open = false;
  volatile uint32_t   aging_cnt = 0;
  int32_t     ret = 0;
  int32_t     print_unit = (int)(MAX_ITEM_CNT/10);

  (void)dlist_cursor_open( cursor, t->aging_list, DL_CURSOR_DIR_FORWARD );
  is_cursor_open = true;

label_aging_again:
  (void)dlist_cursor_reset( cursor );

  mem_barrier();

  DLIST_ITERATE( cursor )
    {
      if( g_exit_flag == true )
        {
          break;
        }

      /* 데이터 삽입이 있다면 evictor가 동작할 것인데, 
       * aging list에 대한 충돌확률이 높아진다. 이때는 ager가 살짝 쉬어준다. */
      if( (t->aging_list_count <= THRESHOLD_WORKING_SLOW_AGER ) &&
          (t->data_list_count > 0) )
        {
          lf_dlist_backoff( t->aging_list );
          lf_dlist_backoff( t->aging_list );
#if 1
          lf_dlist_backoff( t->aging_list );
#endif
        }

      node = dlist_cursor_conv_anode_to_lnode( cursor );
      mem_barrier();
      TRY_GOTO( node->state < DLIST_NODE_STATE_EVICTED, label_aging_again ); 

      /* 아래 코드는 multi-ager를 염두해둔 코드다.
       * 2개이상의 ager가 동작할 때는 이 코드가 유용할 것. 
       * 즉, 현재 노드는 다른 에이징 스레드가 aging 하는 중이니 다음 노드를
       * 시도한다. */
      ret = data_list_node_set_state( node, DLIST_NODE_STATE_ON_AGING );
      if( ret != RC_SUCCESS )
        {
          continue;
        }

      while( true )
        {
          if( data_list_node_is_read_latched( node ) != true )
            {
              break;
            }
        }

      while( true )
        {
          cur_node_next = cursor->cur_node->next;

          if( DL_STATUS_OK == lf_dlist_delete( cursor->l, cursor->cur_node ) )
            {
              /* IMPORTANT:
               * 아래 함수 호출 이전까지 아래와 같은 상황이다.
               * node1    <-------------------  node2
               *     |<-----d---|                 ^
               *     -------->  delnode -----d----|
               *
               *   따라서 아래함수를 호출하여 
               * node1  <----------------->   node2
               *     ^                          ^
               *     ----d---- delnode ----d----|
               *  와 같은 생태로 만들어 준다*/
              for( tmp = cursor->l->head; tmp != cursor->l->tail ; )
                {
                  tmp = lf_dlist_correct_next( cursor->l,
                                               lf_dlist_dereference_node_pointer(
                                                          cursor->l,
                                                          &tmp ) );
                  if( tmp == cur_node_next )
                    {
                      break;
                    }
                }
              break;
            }
        }

      /* wait for resolving conflictions */
      mem_barrier();

      /* 3. if needed, free contents */
#if 0
      data_ptr = node->data;
      mem_barrier();
      if( data_ptr != NULL )
        {
          free( data_ptr );
          node->data = NULL;
        }
#endif

      /* 4. free table entry */
      free( node );
      node = NULL;

      mem_barrier();
      atomic_fetch_dec( &(t->aging_list_count) );

      aging_cnt++;
      atomic_fetch_inc( &g_total_aging_cnt );

      mem_barrier();

#ifdef DEBUG
      printf("[total aging #:%d][data list #:%d][aging list #:%d]\n",
             g_total_aging_cnt,
             t->data_list_count,
             t->aging_list_count );
#else
      // print trace log 10 times
      if( g_total_aging_cnt % print_unit == 0 ) {
        printf("[total aging #:%d][data list #:%d][aging list #:%d]\n",
               g_total_aging_cnt,
               t->data_list_count,
               t->aging_list_count );
      }
#endif /* DEBUG */

#if 1 // IMPRV_SAFETY
      /* 무조건 한 노드만 aging 한다.
       * node가 free되었으므로, 이 노드를 액세스하는 것은, 
       * 쓰레기 값을 읽는 것이다. 다른 트랜잭션이 동일 주소의 노드를
       * 할당받아 다른 값을 기록할 수도 있다. 따라서 free된 노드의 next를
       * 참조하면 꼬이는 수가 있다. */
      goto label_aging_again;
#endif
    }

  is_cursor_open = false;
  dlist_cursor_close( cursor );

  return aging_cnt;

  CATCH_END;

  if( is_cursor_open == true )
    {
      dlist_cursor_close( cursor );
    }

  return RC_FAIL;
}


void dump_list( void * l )
{
  lf_dlist_t * list = NULL;
  data_list_node_t * node = NULL;
  dlist_cursor_t    cursor[1] = {};
  int32_t cnt = 0;

  printf("head -----------------------\n");
  dlist_cursor_open( cursor, list, DL_CURSOR_DIR_FORWARD );

  /* print head */
  print_data_list_node( cursor->cur_node );

  DLIST_ITERATE( cursor )
    {
      node = dlist_cursor_get_list_node( cursor );
      ++cnt;
      print_data_list_node( node );
    }

  /* print tail */
  print_data_list_node( node );

  printf("cnt: %d\n\n", cnt); cnt = 0;
}

int32_t working_threads_create( thr_arg_t * targs )
{
  char    esb[64] = {0, };
  int32_t created_thr_cnt = 0;
  int32_t i = 0;
  int32_t ret = 0;

  for ( i = 0 ; i < THR_NUM_MAX ; i++ )
    {
      ret = pthread_create( &(targs[i].thr),
                            NULL,
                            targs[i].func,
                            &targs[i] );
      TRY( ret != 0 );
    }

  return RC_SUCCESS;

  CATCH_END;

  perror(get_error_prefix(esb));

  created_thr_cnt = i;

  for( i = 0 ; i < created_thr_cnt ; i++ )
    {
      pthread_kill( targs[i].thr, 9 ); // send SIGKILL thread
    }

  return RC_FAIL;
}

int32_t working_threads_join( thr_arg_t * volatile targs, int32_t thr_cnt )
{
  volatile int32_t i = 0;

  /* 7. wait for completing thread jobs */
  for ( i = 0 ; i < thr_cnt ; i++ )
    {
      void * ret;
      (void)pthread_join( targs[i].thr, &ret );
    }

  return RC_SUCCESS;
}

int32_t main( int32_t argc, char ** argv )
{
  char esb[512];
  data_table_t * tbl = NULL;

  int32_t     state   = 0;
  int32_t     ret     = RC_FAIL;
  int32_t     i       = 0;
  int32_t     tid     = 0;
  thr_arg_t * targs = NULL;

  TRY_GOTO( argc != MAX_ARGC, label_print_usage );

  /* 1. get program options */
  MAX_ITEM_CNT = atoi(argv[1]);
  TRY_GOTO( MAX_ITEM_CNT <= 0, label_print_usage );

#ifndef FIXED_THREADS
  // count of insert threads
  THR_NUM_INSERT = atoi( argv[2] );
  TRY_GOTO( THR_NUM_READ <= 0, label_print_usage );

  // count of read threads
  THR_NUM_READ = atoi( argv[3] );
  TRY_GOTO( THR_NUM_READ <= 0, label_print_usage );
#endif /* FIXED_THREADS */

  /* 2. create data table */
  ret = data_table_init( &tbl );
  TRY_GOTO( ret != RC_SUCCESS, err_create_data_table );
  state = 1;

  /* 3. alloc threads args structure */
  targs = (thr_arg_t *)calloc( THR_NUM_MAX, sizeof(thr_arg_t) );
  TRY_GOTO( targs == NULL, err_fail_alloc_thr_args );
  state = 2;

  /* 4. initialize threads */
  do {
    // "+ 1" indicates main process thread.
    ret = pthread_barrier_init( g_thr_barrier, NULL, THR_NUM_MAX + 1 );
    TRY_GOTO( ret != 0, err_fail_init_barrier );

    for( i = 0; i < THR_NUM_READ ; i++ )
      {
        targs[tid].func = func_read;
        targs[tid].tid  = tid;
        targs[tid].tbl  = tbl;
        tid++;
      }

    for( i = 0; i < THR_NUM_INSERT ; i++ )
      {
        targs[tid].func = func_insert;
        targs[tid].tid  = tid;
        targs[tid].tbl  = tbl;
        tid++;
      }

    for( i = 0; i < THR_NUM_EVICTOR ; i++ )
      {
        targs[tid].func = func_evict;
        targs[tid].tid  = tid;
        targs[tid].tbl  = tbl;
        tid++;
      }

    for( i = 0; i < THR_NUM_AGER ; i++ )
      {
        targs[tid].func = func_aging;
        targs[tid].tid  = tid;
        targs[tid].tbl  = tbl;
        tid++;
      }
  } while( 0 );

  /* 5. create & run threads */
  ret = working_threads_create( targs );
  TRY_GOTO( ret != RC_SUCCESS, err_fail_create_thread );
  state = 3;

#if 1
  /* 6. wait for all threads ready */
  pthread_barrier_wait( g_thr_barrier );
  TRY_GOTO( errno != 0, err_wait_barrier );
#endif

  /* 7. join threads */
  state = 2;
  (void)working_threads_join( targs, THR_NUM_MAX );

  /* A termination condition of ager thread is that 
   *    "produced data count(program args) == free nodes count",
   * So, if this program reaches here, this means all items are produced, consumed
   * and freed correctly */

  /* 8. check results */
  TRY_GOTO( (tbl->data_list_count + tbl->aging_list_count) > 0,
            err_bad_works_on_data_list );

  /* 9. dealloc thr args */
  /* IMPORTANT: free() is system call, so this code line leads
   * to performance lack consequently. To overcome, you should declare and use
   * a data structure un-releated to system call like as memory pool. */
  (void)free( targs );
  targs = NULL;

  /* 10. finalize */
  state = 0;
  data_table_finalize( tbl );

  return 0;

  CATCH( err_fail_init_barrier )
    {
      perror(get_error_prefix(esb));
    }
  CATCH( label_print_usage )
    {
      fprintf( stderr,
#ifdef FIXED_THREADS
               "    Usage: %s <item #>\n"
#else /* FIXED_THREADS */
               "    Usage: %s <item #> <insert threads #> <read threads #>\n"
#endif /* FIXED_THREADS */
               "        <item #>: count of item that would be inserted and read\n",
               basename(argv[0]) );
    }
  CATCH( err_fail_alloc_thr_args )
    {
      perror(get_error_prefix(esb));
    }
  CATCH( err_bad_works_on_data_list )
    {
      fprintf( stderr,
               "Not match\n"
               "    data_list entry count[%d]\n"
               "    aging_list entry count[%d]\n"
               "  ----------------------------------------------\n"
               "    tbl.head[%p].next[%p]\n"
               "    tbl.tail[%p].prev[%p]\n"
               "    tbl.aging_head[%p].next[%p]\n"
               "    tbl.aging_tail[%p].prev[%p]\n"
               "  ----------------------------------------------\n",
               tbl->data_list_count,
               tbl->aging_list_count,
               tbl->list->head,
               tbl->list->head->next,
               tbl->list->tail,
               tbl->list->tail->prev,
               tbl->aging_list->head,
               tbl->aging_list->head->next,
               tbl->aging_list->tail,
               tbl->aging_list->tail->prev );
      abort();
    }
  CATCH( err_wait_barrier )
    {
      fprintf( stderr, "fail wait barrier\n" );
    }
  CATCH( err_create_data_table )
    {
      fprintf( stderr, "can not create data table\n" );
    }
  CATCH( err_fail_create_thread )
    {
      fprintf( stderr, "can not create threads \n" );
    }
  CATCH_END;


  switch( state )
    {
    case 3:
      (void)working_threads_join( targs, THR_NUM_MAX );
      /* fall through */
    case 2:
      (void)free( targs );
      /* fall through */
    case 1:
      (void)data_table_finalize( tbl );
      tbl = NULL;
      /* fall through */
    default:
      break;
    }

  return -1;
}

int32_t insert_data( data_table_t * tbl )
{
  data_list_node_t   * node = NULL;;
  int32_t    key = atomic_fetch_inc( &g_next_key );

  if( key < MAX_ITEM_CNT ) {
    node = data_table_insert( tbl, key );
    TRY( node == NULL );

#if 0
    thread_sleep( 0, 1 ); // simulate that copy & set data on the node
#else
    // set some data;
    // node->data = some data;
#endif

    // complete and then change state to (reference) available
    data_list_node_set_state( node, DLIST_NODE_STATE_AVAIL );
  }

  return RC_SUCCESS;

  CATCH_END;

  return RC_FAIL;
}

/*******************************************************
 * log list 구조 및 오퍼레이션이 일어나는 위치
 *
 *            (evict)             (insert)
 * +-> HEAD <--> N1 <--> N2 <--> Ni <--> TAIL <-+
 * +--------------------------------------------+
 *              (       read       )
 *
 *                 (del:free)           (insert)
 * +-> AGING_HEAD <--> N1 <--> N2 <--> Nj <--> TAIL <-+
 * +--------------------------------------------------+
 ********************************************************/

int32_t data_list_node_get_state( data_list_node_t * node )
{
  TRY_GOTO( node == NULL, err_null_node );

  mem_barrier();
  return (int32_t)node->state;

  CATCH( err_null_node )
    {
      char esb[64];
      perror(get_error_prefix(esb));
      abort(); 
    }
  CATCH_END;

  return RC_FAIL;
}

int32_t data_list_node_set_state( data_list_node_t * node, int32_t state )
{
  int32_t oldval = 0;

  if( node != NULL && node->state < state )
    {
      do {
        oldval = node->state;
        if( oldval == atomic_cas_32( &(node->state),
                                     oldval,
                                     state ) )
          {
            break;
          }
      } while ( true );
    }

  return RC_SUCCESS;

  CATCH_END;

  return RC_FAIL;
}

void * func_insert( void * arg )
{
  char           esb[64];
  int32_t        ret = 0;
  thr_arg_t    * targ = (thr_arg_t *)arg;
  data_table_t * tbl = targ->tbl;

  ret = pthread_barrier_wait( g_thr_barrier );
  TRY_GOTO( errno != 0, err_wait_barrier );

  while( g_exit_flag == false )
    {
      if( g_next_key >= MAX_ITEM_CNT )
        {
          break;
        }

      ret = insert_data( tbl );
      if( ret != 0 )
        {
          fprintf(stderr, "alloc fail!\n" );
          exit( 0 );
        }
    }

  return NULL;

  CATCH( err_wait_barrier )
    {
      perror(get_thr_error_prefix(targ->tid, esb));
    }
  CATCH_END;

  return NULL;
}

int32_t data_list_search( data_table_t      * t,
                          dlist_cursor_t    * cursor,
                          int32_t             key,
                          data_list_node_t ** _node )
//                          volatile data_list_node_t ** volatile _node )
{
  char esb[64];
  volatile data_list_node_t * volatile node;
  int32_t node_state = 0;

  DLIST_ITERATE( cursor )
    {
      node = dlist_cursor_get_list_node( cursor );
      if( node->key == key )
        {
          node_state = data_list_node_get_state(node);
          switch( node_state )
            {
            case DLIST_NODE_STATE_AVAIL:
              /* node can be read */
              *_node = node;
              break;

            case DLIST_NODE_STATE_INIT:
              /* node cannot be read, but it will be after few minute 
               * need to reopen cursor */
              *_node = NULL;
              break;

            case DLIST_NODE_STATE_NEED_EVICT:
            case DLIST_NODE_STATE_EVICTED:
              /* actually, nightmare things are happend */
#ifdef DEBUG
              perror(get_error_prefix(esb));
              print_data_list_node( node );
              abort();
#endif /* DEBUG */
              TRY( 1 );
              break;

            case DLIST_NODE_STATE_ON_AGING:
            default:
              print_data_list_node( node );
#ifdef DEBUG
              perror(get_error_prefix(esb));
              abort(); 
#endif /* DEBUG */
              TRY( 1 ); // return FAIL;
              break;
            }

          goto label_break_iterate;
        }

       if( node->key > key + 300 )
         {
           *_node = NULL;
           goto label_break_iterate;
         }
    }

label_break_iterate:

  return RC_SUCCESS;

  CATCH_END;

  return RC_FAIL;
}

static void _simulate_do_something( data_table_t * t, data_list_node_t * node )
{
  // lf_dlist_backoff( t->list );
  print_data_list_node( node );             // consume
}

void * func_read( void * arg )
{
  char                esb[64];
  int32_t             ret = 0;
  int32_t             search_key  = 0;
  thr_arg_t         * targ = (thr_arg_t *)arg;
  data_table_t      * tbl = targ->tbl;
  data_list_node_t  * node = NULL;
  // data_list_node_t  * volatile node = NULL;
  dlist_cursor_t      cursor[1] = {};

  pthread_barrier_wait( g_thr_barrier );
  TRY_GOTO( errno != 0, err_wait_barrier );

  dlist_cursor_open( cursor, tbl->list, DL_CURSOR_DIR_FORWARD );

  while( g_exit_flag == false )
    {
      // all items must be read
      if( search_key >= MAX_ITEM_CNT ) {
        break;
      }

      node = NULL;

      ret = data_list_search( tbl, cursor, search_key, &node );
      TRY( ret == RC_FAIL );

      if( node == NULL )
        {
          // there is no item to read
          dlist_cursor_reset( cursor );
          continue;
        }
      else
        {
          // success to get item with 'key'
          atomic_fetch_inc( &(node->read_latch) );  // get read lock
          _simulate_do_something( tbl, node );
          atomic_fetch_dec( &(node->read_latch) );  // release read lock

          atomic_fetch_inc( &(node->read_cnt) );

          search_key++;  // want to search next key
        }

    }

  return NULL;

  CATCH( err_wait_barrier )
    {
      perror(get_thr_error_prefix(targ->tid, esb));
    }
  CATCH_END;

  return NULL;
}

void * func_evict( void * arg )
{
  char            esb[64];
  int32_t         evicted_cnt = 0;
  thr_arg_t     * targ = (thr_arg_t *)arg;
  data_table_t  * tbl = targ->tbl;

  pthread_barrier_wait( g_thr_barrier );
  TRY_GOTO( errno != 0, err_wait_barrier );

  while( g_exit_flag == false )
    {
      evicted_cnt = 0;

      if( tbl->data_list_count > 0 ) {
        evicted_cnt = data_list_evict( tbl );
      }

      if( evicted_cnt == 0 )
        {
          thread_sleep( 0, 10 );
        }
    }

  return NULL;

  CATCH( err_wait_barrier )
    {
      perror(get_thr_error_prefix(targ->tid, esb));
    }
  CATCH_END;

  return NULL;
}

void * func_aging( void * arg )
{
  char            esb[64];
  int32_t         ret = 0;
  thr_arg_t     * targ = (thr_arg_t *)arg;
  data_table_t  * tbl = targ->tbl;

  pthread_barrier_wait( g_thr_barrier );
  TRY_GOTO( errno != 0, err_wait_barrier );

  while( g_exit_flag == false )
    {
      if( tbl->aging_list_count > 0 )
        {
          ret = data_list_delete_evicted( tbl );
          g_delete_cnt += ret;

          if( g_delete_cnt == MAX_ITEM_CNT )
            {
              g_exit_flag = true;
              break;
            }
        }
    }

  return NULL;

  CATCH( err_wait_barrier )
    {
      perror(get_thr_error_prefix(targ->tid, esb));
    }
  CATCH_END;

  return NULL;
}
