#ifndef _RAND_R_H_
#define _RAND_R_H_

#include <stdint.h>

// stdlib.h
int rand_r (unsigned int *seed);

/* ****************************************************************************
 * RNG: random number generator
 * A fast random (32-bit) number generator, for a uniform distribution (all
 * numbers in the range are equally likely). */
typedef struct _random_number_generator RNG;
struct _random_number_generator
{
  uint32_t max_;
  uint32_t x_;
  uint32_t y_;
  uint32_t z_;
  uint32_t w_;
};

unsigned long long rdtsc(void);

int RNG_init( RNG * rng, uint32_t seed, uint32_t min, uint32_t max);
uint32_t RNG_generate( RNG * rng );
void RNG_backoff( volatile RNG * rng );
#endif /* _RAND_R_H_ */
