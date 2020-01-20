#ifndef _ATOMIC_H_
#define _ATOMIC_H_ 1

#ifdef __GCC_HAVE_SYNC_COMPARE_AND_SWAP_8

#define mem_barrier      __sync_synchronize

#define atomic_cas32 __sync_val_compare_and_swap
#define atomic_cas64 __sync_val_compare_and_swap

#define atomic_inc_fetch(_ptr) __sync_add_and_fetch(_ptr, 1)
#define atomic_dec_fetch(_ptr) __sync_sub_and_fetch(_ptr, 1)
#define atomic_fetch_inc(_ptr) __sync_fetch_and_add(_ptr, 1)
#define atomic_fetch_dec(_ptr) __sync_fetch_and_sub(_ptr, 1)

#else
#define mem_barrier()  asm("mfence")
#error Declare CAS functions are here
#endif /* __GCC_HAVE_SYNC_COMPARE_AND_SWAP_8 */

#endif /* _ATOMIC_H_ */
