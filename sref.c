/* Definitions for the sref API.

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

#include "sref.h"
#include "compat.h"
#include "version.h"
#include <assert.h>
#include <stdlib.h>
#include <stddef.h>

typedef struct
{
  void *ptr;
  intptr_t delta;
} SrefDelta;

#ifndef SREF_NDELTAS
#  define SREF_NDELTAS   128
#endif

#if (SREF_NDELTAS & (SREF_NDELTAS - 1)) != 0
#  error "number of deltas must be a power of 2"
#endif

/* These tables are used to store the refcounted pointers and their deltas;
 * the accumulated increments and decrements produced by a specific thread. */

typedef struct
{
  SrefDelta deltas[SREF_NDELTAS];
  unsigned int n_used;
} SrefTable;

static int
sref_add (SrefTable *tp, void *ptr, intptr_t add, uintptr_t *outp)
{
  uintptr_t idx = ((uintptr_t)ptr >> 3) % SREF_NDELTAS;
  uintptr_t nprobe = 1;
  assert (tp->n_used < SREF_NDELTAS);

  for ( ; ; ++nprobe)
    {
      SrefDelta *dp = tp->deltas + idx;
      if (!dp->ptr)
        {
          dp->ptr = ptr;
          dp->delta = add;
          *outp = idx;
          return (++tp->n_used * 100 >= SREF_NDELTAS * 75);
        }
      else if (dp->ptr == ptr)
        {
          dp->delta += add;
          return (0);
        }

      idx = (idx + nprobe) % SREF_NDELTAS;
    }
}

static void
sref_merge (SrefTable *dst, SrefTable *src)
{
  uintptr_t idx;
  for (unsigned int i = 0, j = 0; j < src->n_used; ++i)
    {
      SrefDelta *dp = src->deltas + i;
      if (!dp->ptr)
        continue;

      int rv = sref_add (dst, dp->ptr, dp->delta, &idx);
      dp->ptr = NULL;
      --src->n_used;

      if (rv)
        break;
    }
}

typedef struct Dlist
{
  struct Dlist *prev;
  struct Dlist *next;
} Dlist;

static inline void
dlist_init_head (Dlist *dlp)
{
  dlp->next = dlp->prev = dlp;
}

static inline void
dlist_add (Dlist *head, Dlist *node)
{
  node->next = head->next;
  node->prev = head;
  head->next->prev = node;
  head->next = node;
}

static inline void
dlist_del (Dlist *node)
{
  node->next->prev = node->prev;
  node->prev->next = node->next;
}

static inline int
dlist_empty_p (const Dlist *head)
{
  return (head == head->next);
}

static inline int
dlist_linked_p (const Dlist *node)
{
  return (node->next != NULL);
}

static void
dlist_splice (Dlist *head, Dlist *dst)
{
  if (dlist_empty_p (head))
    return;

  head->next->prev = dst;
  head->prev->next = dst->next;
  dst->next->prev = head->prev;
  dst->next = head->next;
}

/*
 * Thread registry.
 *
 * Once a thread uses the sref API, it's lazily added to the registry, and
 * will be inspected when a grace period elapses.
 *
 * The registry has 2 locks: One to serialize a thread being registered,
 * and one to serialize thread processing on each grace period.
 */

static const uintptr_t GP_PHASE_BIT = 1;

typedef struct
{
  uintptr_t counter;
  Dlist root;
  Sref *review;
  xmutex_t td_lock;
  xmutex_t gp_lock;
} SrefRegistry;

typedef struct
{
  SrefTable refs;
  SrefTable unrefs;
  int flush;
} SrefCache;

/* Global variables initialized in 'sref_init'. */

static SrefRegistry registry;
static xkey_t reg_key;

/*
 * Thread data.
 *
 * Each thread maintains 2 tables: One for reference count increments,
 * and another for decrements. They are separated so that checks for liveness -
 * i.e: refcount != 0 - can be made only when decrementing instead of every
 * time.
 *
 * We further need 2 sets of these tables so that acquiring and releasing an
 * sref pointer can be done concurrently with registry synchronization that
 * occurs at a different window.
 */

typedef struct
{
  Dlist link;
  uintptr_t counter;
  SrefCache cache[2];
} SrefData;

/* Thread-specific descriptor for sref operations. */

static xthread_local SrefData local_data;

static void
registry_add (SrefRegistry *regp, SrefData *dp)
{
  xkey_set (reg_key, dp);
  xmutex_lock (&regp->td_lock);
  dlist_add (&regp->root, &dp->link);
  xmutex_unlock (&regp->td_lock);
}

static uintptr_t
registry_counter (void)
{
  return (xatomic_load_rlx (&registry.counter));
}

static SrefData*
sref_local (void)
{
  SrefData *ret = &local_data;
  if (!dlist_linked_p (&ret->link))
    registry_add (&registry, ret);

  return (ret);
}

static uintptr_t
local_counter (const SrefData *dp)
{
  return (xatomic_load_rlx (&dp->counter));
}

#define sref_table_process(table, dec)   \
  do   \
    {   \
      for (unsigned int i = 0, j = 0; j < (table)->n_used; ++i)   \
        {   \
          SrefDelta *dep = &(table)->deltas[i];   \
          if (!dep->ptr)   \
            continue;   \
          \
          Sref *p = (Sref *)dep->ptr;   \
          p->refcnt += dep->delta;   \
          assert (p->refcnt >= 0);   \
          if (dec && !p->refcnt && p->fini)   \
            p->fini (p);   \
          \
          dep->ptr = NULL;   \
          dep->delta = 0;   \
          ++j;   \
        }   \
      \
      (table)->n_used = 0;   \
    }   \
  while (0)

static void
sref_process_inc (SrefData *dp, uintptr_t idx)
{
  sref_table_process (&dp->cache[idx].refs, 0);
}

static void
sref_process_dec (SrefData *dp, uintptr_t idx)
{
  sref_table_process (&dp->cache[idx].unrefs, 1);
}

#undef sref_table_process

#define STATE_ACTIVE     0
#define STATE_INACTIVE   1
#define STATE_OLD        2

static inline int
local_state (SrefData *dp)
{
  uintptr_t val = xatomic_load_acq (&dp->counter);
  if (!(val >> GP_PHASE_BIT))
    return (STATE_INACTIVE);
  else if (!((val ^ registry_counter ()) & GP_PHASE_BIT))
    return (STATE_ACTIVE);
  else
    return (STATE_OLD);
}

static void
registry_poll (SrefRegistry *regp, Dlist *readers, Dlist *outp, Dlist *qsp)
{
  for (unsigned int loops = 0 ; ; ++loops)
    {
      Dlist *next, *runp = readers->next;
      for (; runp != readers; runp = next)
        {
          next = runp->next;
          switch (local_state ((SrefData *)runp))
            {
              case STATE_ACTIVE:
                if (outp)
                  {
                    dlist_del (runp);
                    dlist_add (outp, runp);
                    break;
                  }

              /* FALLTHROUGH. */
              case STATE_INACTIVE:
                dlist_del (runp);
                dlist_add (qsp, runp);
                break;

              case STATE_OLD:
                break;

              default:
                assert ("invalid state");
            }
        }

      if (dlist_empty_p (readers))
        break;

      xmutex_unlock (&regp->td_lock);

      if (loops < 1000)
        xatomic_mfence_acq ();
      else
        {
          xthread_sleep (1);
          loops = 0;
        }

      xmutex_lock (&regp->td_lock);
    }
}

static void
registry_lock (SrefRegistry *rp)
{
  xmutex_lock (&rp->gp_lock);
  xmutex_lock (&rp->td_lock);
}

static void
registry_unlock (SrefRegistry *rp)
{
  xmutex_unlock (&rp->td_lock);
  xmutex_unlock (&rp->gp_lock);
}

static void
registry_sync (int acquire)
{
  SrefRegistry *rp = &registry;

  if (acquire)
    registry_lock (rp);

  if (dlist_empty_p (&rp->root))
    {
      if (acquire)
        registry_unlock (rp);

      return;
    }

  Dlist out, qs;
  dlist_init_head (&qs);
  dlist_init_head (&out);

  xatomic_mfence_full ();
  registry_poll (rp, &rp->root, &out, &qs);

  uintptr_t prev_idx = xatomic_load_rlx (&rp->counter);
  xatomic_store_rel (&rp->counter, prev_idx ^ GP_PHASE_BIT);

  registry_poll (rp, &out, NULL, &qs);
  dlist_splice (&qs, &rp->root);

  /* Now process increments first, and then decrements, after checking
   * for any object whose refcount is zero, so that it's destroyed timely. */

  for (Dlist *qp = rp->root.next; qp != &rp->root; qp = qp->next)
    sref_process_inc ((SrefData *)qp, prev_idx);

  for (Dlist *qp = rp->root.next; qp != &rp->root; qp = qp->next)
    sref_process_dec ((SrefData *)qp, prev_idx);

  for (Sref *sp = rp->review; sp ; )
    {
      Sref *next = sp->next;
      if (sp->refcnt)
        /* Still live - Clear the review link. */
        sp->next = NULL;
      else
        sp->fini (sp);

      sp = next;
    }

  rp->review = NULL;
  if (acquire)
    registry_unlock (rp);
}

void sref_read_enter (void)
{
  SrefData *self = sref_local ();
  uintptr_t value = local_counter (self);
  if (!(value >> GP_PHASE_BIT))
    { /* A grace period has elapsed, so we can reset the 'flush' flag. */
      value = registry_counter ();
      self->cache[value & GP_PHASE_BIT].flush = 0;
    }

  uintptr_t nval = value + (1 << GP_PHASE_BIT);
  assert (nval > value);
  xatomic_store_rel (&self->counter, nval);
}

static int
sref_flush_impl (SrefData *self, uintptr_t value)
{
  if (value >> GP_PHASE_BIT)
    /* We are currently in a critical section, and can't flush our deltas. */
    return (-1);

  self->cache[value & GP_PHASE_BIT].flush = 0;
  registry_sync (1);
  return (0);
}

void sref_read_exit (void)
{
  SrefData *self = sref_local ();
  uintptr_t value = local_counter (self);

  assert (value >= (1 << GP_PHASE_BIT));
  value -= 1 << GP_PHASE_BIT;
  xatomic_store_rel (&self->counter, value);

  if (self->cache[value & GP_PHASE_BIT].flush)
    sref_flush_impl (self, value);
}

static void
sref_acq_rel (void *refptr, intptr_t delta, size_t off)
{
  assert (refptr);
  SrefData *self = sref_local ();
  uintptr_t idx = registry_counter () & GP_PHASE_BIT;
  SrefCache *cache = &self->cache[idx];
  SrefTable *tp = (SrefTable *)((char *)cache + off);

  cache->flush += sref_add (tp, refptr, delta, &idx);
  if (cache->flush > 1 && sref_flush_impl (self, local_counter (self)) < 0)
    { /* This is an emergency situation. Our cache is full, and we are inside
       * a read-side critical section, and thus can't flush deltas. So we have
       * to resort to adding this sref pointer to the review list. We use the
       * thread registry lock to act as a serialization barrier. */

      Sref *sp = (Sref *)refptr;
      assert (sp == tp->deltas[idx].ptr);
      tp->deltas[idx].ptr = NULL;
      tp->deltas[idx].delta = 0;
      --tp->n_used;

      xmutex_lock (&registry.td_lock);
      sp->refcnt += delta;
      if (!sp->next)
        {
          sp->next = registry.review;
          registry.review = sp;
        }
      xmutex_unlock (&registry.td_lock);
    }
}

void* sref_acquire (void *refptr)
{
  sref_acq_rel (refptr, +1, offsetof (SrefCache, refs));
  return (refptr);
}

void sref_release (void *refptr)
{
  sref_acq_rel (refptr, -1, offsetof (SrefCache, unrefs));
}

int sref_flush (void)
{
  SrefData *self = sref_local ();
  uintptr_t value = local_counter (self);
  int ret = sref_flush_impl (self, value);

  if (ret < 0)
    /* If we didn't manage to flush, set the flat to do it ASAP. */
    self->cache[value & GP_PHASE_BIT].flush = 1;

  return (ret);
}

#ifndef XKEY_ARG
#  define XKEY_ARG(arg)   arg
#endif

static void
sref_data_fini (XKEY_ARG (void *ptr))
{
  SrefData *self = &local_data;
  if (!dlist_linked_p (&self->link))
    return;

  xatomic_store_rel (&self->counter, 0);
  registry_lock (&registry);

  uintptr_t idx = registry_counter () & GP_PHASE_BIT;
  SrefCache *cache = self->cache;

  sref_merge (&cache[idx].refs, &cache[idx ^ GP_PHASE_BIT].refs);
  sref_merge (&cache[idx].unrefs, &cache[idx ^ GP_PHASE_BIT].unrefs);

  if (cache[idx].refs.n_used || cache[idx].unrefs.n_used)
    registry_sync (0);

  idx ^= GP_PHASE_BIT;
  if (cache[idx].refs.n_used || cache[idx].unrefs.n_used)
    registry_sync (0);

  dlist_del (&self->link);
  registry_unlock (&registry);
}

static void
sref_atexit (void)
{
  sref_data_fini (&local_data);   /* avoid sref_local. */
}

static int sref_initialized;

int sref_lib_init (void)
{
  if (sref_initialized)
    return (0);
  else if (xkey_create (&reg_key, sref_data_fini) < 0)
    return (-1);
  else if (xmutex_init (&registry.td_lock) < 0)
    {
      xkey_delete (reg_key);
      return (-1);
    }
  else if (xmutex_init (&registry.gp_lock) < 0)
    {
      xkey_delete (reg_key);
      xmutex_destroy (&registry.td_lock);
      return (-1);
    }
  else if (atexit (sref_atexit) != 0)
    {
      xkey_delete (reg_key);
      xmutex_destroy (&registry.td_lock);
      xmutex_destroy (&registry.gp_lock);
      return (-1);
    }

  dlist_init_head (&registry.root);
  sref_initialized = 1;
  return (0);
}

static void
sref_atfork_prepare (void)
{
  registry_lock (&registry);
}

static void
sref_atfork_parent (void)
{
  registry_unlock (&registry);
}

static void
sref_atfork_child (void)
{
  registry_unlock (&registry);
  dlist_init_head (&registry.root);

  SrefData *self = &local_data;
  if (dlist_linked_p (&self->link))
    dlist_add (&registry.root, &self->link);
}

SrefAtFork sref_atfork (void)
{
  SrefAtFork ret;
  ret.prepare = sref_atfork_prepare;
  ret.parent = sref_atfork_parent;
  ret.child = sref_atfork_child;
  return (ret);
}

void sref_lib_version (int *major, int *minor)
{
  *major = MAJOR;
  *minor = MINOR;
}
