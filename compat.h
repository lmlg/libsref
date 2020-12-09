/* Compatibility header for supported platforms.

   This file is part of libsref.

   libsref is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

#ifdef SREF_USE_C11

#include <stdatomic.h>
#include <threads.h>

#define xatomic_load_rlx(ptr)   \
  atomic_load_explicit ((ptr), memory_order_relaxed)

#define xatomic_load_acq(ptr)   \
  atomic_load_explicit ((ptr), memory_order_acquire)

#define xatomic_store_rel(ptr, val)   \
  atomic_store_explicit ((ptr), (val), memory_order_release)

#define xatomic_mfence_acq()   atomic_signal_fence (memory_order_acquire)

#define xatomic_mfence_full()   atomic_signal_fence (memory_order_seq_cst)

#define xthread_sleep(mlsec)   \
  thrd_sleep (&(struct timespec) { .tv_sec = 0,   \
                                   .tv_nsec = (mlsec) * 1000000 }, NULL)
typedef mtx_t xmutex_t;

static inline int
xmutex_init (xmutex_t *mtx)
{
  return (mtx_init (mtx, mtx_plain) == thrd_success ? 0 : -1);
}

#define xmutex_lock      mtx_lock
#define xmutex_unlock    mtx_unlock
#define xmutex_destroy   mtx_destroy

typedef tss_t xkey_t;

#define xkey_set   tss_set

static inline int
xkey_create (xkey_t *key, void (*fini) (void *))
{
  return (tss_create (key, fini) == thrd_success ? 0 : -1);
}

#define xkey_delete   tss_delete

#define xthread_local   thread_local

#elif defined (SREF_USE_PTHREADS) &&   \
    (defined (__GNUC__) || defined (__clang__))

#include <pthread.h>
#include <unistd.h>

#define xatomic_load_rlx(ptr)   __atomic_load_n ((ptr), __ATOMIC_RELAXED)

#define xatomic_load_acq(ptr)   __atomic_load_n ((ptr), __ATOMIC_ACQUIRE)

#define xatomic_store_rel(ptr, val)   \
   __atomic_store_n ((ptr), (val), __ATOMIC_RELEASE)

#define xatomic_mfence_acq()   __atomic_thread_fence (__ATOMIC_ACQUIRE)

#define xatomic_mfence_full()   __atomic_thread_fence (__ATOMIC_SEQ_CST)

#define xthread_sleep(mlsec)   usleep ((mlsec) * 1000)

typedef pthread_mutex_t xmutex_t;

#define xmutex_init(mutex)   pthread_mutex_init ((mutex), NULL)

#define xmutex_lock      pthread_mutex_lock
#define xmutex_unlock    pthread_mutex_unlock
#define xmutex_destroy   pthread_mutex_destroy

typedef pthread_key_t xkey_t;

#define xkey_create   pthread_key_create
#define xkey_set      pthread_setspecific
#define xkey_delete   pthread_key_delete

#define xthread_local   __thread

#elif defined (_MSC_VER)

#include <windows.h>
#include <intrin.h>
#include <synchapi.h>

static inline uintptr_t
xatomic_load_rlx (uintptr_t *ptr)
{
  return (*(volatile uintptr_t *)ptr);
}

static inline uintptr_t
xatomic_load_acq (uintptr_t *ptr)
{
  _ReadWriteBarrier ();
  MemoryBarrier ();
  return (*ptr);
}

static inline void
xatomic_store_rel (uintptr_t *ptr, uintptr_t val)
{
  *ptr = val;
  _WriteBarrier ();
  MemoryBarrier ();
}

#define xatomic_mfence_acq()   \
  do   \
    {   \
      _ReadWriteBarrier ();   \
      MemoryBarrier ();   \
    }   \
  while (0)

#define xatomic_mfence_full   xatomic_mfence_acq

#define xthread_sleep   Sleep

typedef SRWLOCK xmutex_t;

#define xmutex_init(mtx)   (InitializeSRWLock (mtx), 0)

#define xmutex_lock      AcquireSRWLockExclusive
#define xmutex_unlock    ReleaseSRWLockExclusive

#define xmutex_destroy(mtx)   ((void)(mtx))

typedef int xkey_t;

extern int __tlregdtor (void (*fn) (void));

static inline void
xkey_set (xkey_t *key, void (*fn) (void))
{
  (void)key;
  while (1)
    if (__tlregdtor (fn) == 0)
      break;
}

#define xkey_create(key, cb)   0
#define xkey_delete(key)       ((void)(key))

#define XKEY_ARG(arg)
#define XKEY_LOCAL(x, y)   x

#define xthread_local   __declspec(thread)

#else

#  error "unsupported platform"

#endif
