#include <stdint.h>
#include "atomic.h"

#ifndef USE_GCC_BUILTIN_ATOMIC
int32_t __cas_32( volatile void  * p, int32_t oldval, int32_t newval )
{
  int32_t prev;

  asm volatile  ("lock;\n"
                 "cmpxchgl %1, %2"
                 : "=a"(prev)
                 : "r"(newval),
                 "m"(*p),
                 "0"(oldval)
                 : "memory");  // barrier for compiler reordering around this
  return prev;
}

int64_t  __cas_64( volatile void * p, int64_t oldval, int64_t newval )
{
  int64_t prev;

  asm volatile ("lock;\n"
                "cmpxchgq %1,%2"
                : "=a"(prev)
                : "r"(newval),
                "m"(*p),
                "0"(oldval)
                : "memory");
  return prev;
}

#endif
