#ifdef CONFIG_X86_32
# include "mutex_32.h"
#else
# include "mutex_64.h"
#endif

#ifndef	__ASM_MUTEX_H
#define	__ASM_MUTEX_H

#ifdef MUTEX_SHOULD_XCHG_COUNT
#undef MUTEX_SHOULD_XCHG_COUNT
#endif
/*
 * For the x86 architecture, it allows any negative number (besides -1) in
 * the mutex counter to indicate that some other threads are waiting on the
 * mutex. So the atomic_xchg() function should not be called in
 * __mutex_lock_common() if the value of the counter has already been set
 * to a negative number.
 */
#define MUTEX_SHOULD_XCHG_COUNT(mutex)	(atomic_read(&(mutex)->count) >= 0)
#endif
