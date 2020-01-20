#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "rand_r.h"
#include "util.h"

/* ****************************************************************************
 * RNG: random number generator
 * A fast random (32-bit) number generator, for a uniform distribution (all
 * numbers in the range are equally likely). */
/* rdtsc(): https://docs.microsoft.com/ko-kr/cpp/intrinsics/rdtsc?view=vs-2017 */

unsigned long long rdtsc(void)
{
  unsigned hi, lo;
  __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
  return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}

static int generate_seed( void )
{
  uint32_t ret = 0;
  int    sz = sizeof(ret);
  char * p  = (char *)(&ret);
  int    fd = -1;

  /* open random device */
  fd = open( "/dev/urandom", O_RDONLY );
  if( fd == -1 )
    {
      fd = open( "/dev/random", O_RDONLY );
      TRY( fd == -1 );
    }

  while( sz != 0 )
    {
      int len = 0;
      len = read( fd, p, sz );
      TRY( len == -1 );

      sz -= len;
      p += len;

      if( sz )
        {
          usleep( 20 );
        }
    }

  close( fd );

  return ret;

  CATCH_END;

  if( fd != -1 )
    {
      close( fd );
    }

  return 27644437; /* prime number */
}

int RNG_init( RNG * rng, uint32_t seed, uint32_t min, uint32_t max)
{
  TRY( rng == NULL );

  rng->max_ = max;
  if( seed == 0 )
    {
      rng->x_ = generate_seed() & 0x0FFFFFFF;
    }
  else
    {
      rng->x_ = seed;
    }

  rng->y_ = 362436069;
  rng->z_ = 521288629;
  rng->w_ = 88675123;

  return RC_SUCCESS;

  CATCH_END;

  return RC_FAIL;
}

uint32_t RNG_generate( RNG * rng )
{
  uint32_t t;
  uint32_t result;

  TRY( rng == NULL );

  t = (rng->x_ ^ (rng->x_ << 11));

  rng->x_ = rng->y_;
  rng->y_ = rng->z_;
  rng->z_ = rng->w_;

  result = (rng->w_ = (rng->w_ ^ (rng->w_ >> 19)) ^ (t ^ (t >> 8)));

  return (rng->max_ > 0) ? result % rng->max_ : result;

  CATCH_END;

  return 1000;
}

void RNG_backoff( volatile RNG * rng )
{
  volatile uint64_t loops = (uint64_t)RNG_generate( rng );
  mem_barrier();
  while( loops-- )
    {
      mem_barrier();
      /* do nothing */
    }
}
