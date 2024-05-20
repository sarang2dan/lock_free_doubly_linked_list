#ifndef _ATOMIC_H_
#define _ATOMIC_H_ 1

#ifdef __GCC_HAVE_SYNC_COMPARE_AND_SWAP_8
#ifdef USE_GCC_BUILTIN_ATOMIC
#define atomic_cas_32 __sync_val_compare_and_swap
#define atomic_cas_64 __sync_val_compare_and_swap
#define atomic_inc_fetch(_ptr) __sync_add_and_fetch(_ptr, 1)
#define atomic_dec_fetch(_ptr) __sync_sub_and_fetch(_ptr, 1)
#define atomic_fetch_inc(_ptr) __sync_fetch_and_add(_ptr, 1)
#define atomic_fetch_dec(_ptr) __sync_fetch_and_sub(_ptr, 1)
#define mem_barrier()  __sync_synchronize()
#else /* USE_GCC_BUILTIN_ATOMIC */
int32_t __cas_32( volatile void * p, int32_t oldval, int32_t newval );
int64_t __cas_64( volatile void * p, int64_t oldval, int64_t newval );

#define atomic_cas_32( _p, _old, _new) \
  __cas_32((volatile void *)(_p), (int32_t)(_old), (int32_t)(_new))
#define atomic_cas_64( _p, _old, _new) \
  __cas_64((volatile void *)(_p), (int64_t)(_old), (int64_t)(_new))
#define atomic_inc_fetch(_ptr) __sync_add_and_fetch(_ptr, 1)
#define atomic_dec_fetch(_ptr) __sync_sub_and_fetch(_ptr, 1)
#define atomic_fetch_inc(_ptr) __sync_fetch_and_add(_ptr, 1)
#define atomic_fetch_dec(_ptr) __sync_fetch_and_sub(_ptr, 1)
#define mem_barrier()  asm("mfence")  // nop
#endif /* USE_GCC_BUILTIN_ATOMIC */
#else /* __GCC_HAVE_SYNC_COMPARE_AND_SWAP_8 */

#error Declare CAS functions are here

#endif /* __GCC_HAVE_SYNC_COMPARE_AND_SWAP_8 */

#endif /* _ATOMIC_H_ */
