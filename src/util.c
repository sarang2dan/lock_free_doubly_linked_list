//
// Created by Lunar.Velvet on 2021/03/15.
//

#include <sys/types.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/time.h>
#include "atomic.h"
#include "util.h"

/* rdtsc(): https://docs.microsoft.com/ko-kr/cpp/intrinsics/rdtsc?view=vs-2017 */

uint64_t rdtsc(void)
{
    unsigned hi, lo;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    return ( (uint64_t)lo)|( ((uint64_t)hi)<<32 );
}

int thread_sleep( uint64_t sec, uint64_t usec )
{
#if 0
  return poll(NULL, 0, (sec * 1000) + (usec / 1000));
#else
  struct timeval tval;
  struct timeval *tvalp = NULL;
  if( sec != 0 || usec != 0 )
    {
      tval.tv_sec = sec;
      tval.tv_usec = usec;
      tvalp = &tval;
    }

  return select(0,
                NULL, /* readfds */
                NULL, /* writefds */
                NULL, /* exceptfds */
                tvalp);
#endif
}

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_time.h>

pid_t gettid( void )
{
  uint64_t tid = 0;
  pthread_threadid_np(NULL, &tid);
  return (pid_t)tid;
}

uint64_t get_time( void )
{
  return mach_absolute_time();
}

double get_elapsed_time( uint64_t elapsed )
{
  mach_timebase_info_data_t sTimebaseInfo;
  mach_timebase_info(&sTimebaseInfo);
  return (double)(elapsed * sTimebaseInfo.numer) / sTimebaseInfo.denom;
}

int pthread_barrier_init(pthread_barrier_t            * barrier,
                         const pthread_barrierattr_t  * attr,
                         unsigned int                   count)
{
  if(count == 0)
    {
      errno = EINVAL;
      return -1;
    }

  if(pthread_mutex_init(&barrier->mutex, 0) < 0)
    {
      return -1;
    }

  if(pthread_cond_init(&barrier->cond, 0) < 0)
    {
      pthread_mutex_destroy(&barrier->mutex);
      return -1;
    }

  barrier->tripCount = count;
  barrier->count = 0;

  return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *barrier)
{
  pthread_cond_destroy(&barrier->cond);
  pthread_mutex_destroy(&barrier->mutex);
  return 0;
}

int pthread_barrier_wait(pthread_barrier_t *barrier)
{
  int ret = 0;

  pthread_mutex_lock(&barrier->mutex);
  atomic_fetch_inc(&(barrier->count));

  if(barrier->count >= barrier->tripCount)
    {
      barrier->count = 0;
      pthread_cond_broadcast(&barrier->cond);
      ret = 1;
    }
  else
    {
      pthread_cond_wait(&barrier->cond, &(barrier->mutex));
      pthread_mutex_unlock(&barrier->mutex);
      ret = 0;
    }

  pthread_mutex_unlock(&barrier->mutex);
  return ret;
}
#endif /* __APPLE__ */
