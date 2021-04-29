#ifndef _UTIL_H_
#define _UTIL_H_

#ifdef __cplusplus
#define EXTERN_C       extern "C"
#define EXTERN_C_BEGIN extern "C" {
#define EXTERN_C_END   }
#else
#define EXTERN_C       extern
#define EXTERN_C_BEGIN
#define EXTERN_C_END
#endif

#ifndef __cplusplus
#include <stdint.h>
typedef int32_t bool;
#define true    1
#define false   0

#ifndef offsetof
#define offsetof(type, field) ((long) &((type *)0)->field)
#endif /* offsetof */
#endif /* __cplusplus */

// return code
enum {
    RC_ERR_OP_TIMEOUT = -1000,
    RC_ERR_LOCK_TIMEOUT,
    RC_ERR_LOCK_INTERRUPTED,
    RC_ERR_LOCK_BUSY,
    RC_FAIL     = -1,
    RC_SUCCESS  = 0,
};

// exception processor
#define TRY( cond ) if( cond ) { goto _label_catch_end; }

#define TRY_GOTO( cond, goto_label ) if( cond ) { goto goto_label; }

#define CATCH( catch_label )  goto _label_catch_end; \
  catch_label:

#define CATCH_END _label_catch_end:

/* rdtsc(): https://docs.microsoft.com/ko-kr/cpp/intrinsics/rdtsc?view=vs-2017 */
uint64_t rdtsc(void);

int thread_sleep( uint64_t sec, uint64_t usec );

#ifdef __APPLE__
#include <sys/types.h>
pid_t gettid( void );

#ifndef PTHREAD_BARRIER_H_
#define PTHREAD_BARRIER_H_

#include <pthread.h>
#include <errno.h>
#include <sys/errno.h>

typedef int pthread_barrierattr_t;
typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int count;
    int tripCount;
} pthread_barrier_t;

int pthread_barrier_init(pthread_barrier_t            * barrier,
                         const pthread_barrierattr_t  * attr,
                         unsigned int                   count);
int pthread_barrier_destroy(pthread_barrier_t *barrier);
int pthread_barrier_wait(pthread_barrier_t *barrier);

#endif // PTHREAD_BARRIER_H_
#endif // __APPLE__
#endif /* _UTIL_H_ */
