#include "sref.h"
#include <stdio.h>
#include <threads.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>

typedef struct
{
  Sref base;
  int used;
  int value;
} Object;

/* Very basic (and crappy) PRNG. */
static unsigned int
xrand (unsigned int *prev)
{
  unsigned int x = *prev * 1103515245 + 12345;
  *prev = x;
  return (x >> 16);
}

static unsigned long n_existing;

static void
fini_obj (void *ptr)
{
  free (ptr);
  atomic_fetch_sub (&n_existing, 1ul);
}

static Object*
make_obj (int value)
{
  Object *p = (Object *)malloc (sizeof (*p));
  if (!p)
    abort ();

  sref_init (p, fini_obj);
  p->value = value;
  p->used = 0;
  atomic_fetch_add (&n_existing, 1ul);
  return (p);
}

#define N_ELEM   16

static Object *array_1[N_ELEM];
static Object *array_2[N_ELEM];

#define N_LOOPS   100

static int
reader (void *arg)
{
  unsigned int rand_val = (uintptr_t)arg;
  for (int i = 0; i < N_LOOPS; ++i)
    {
      Object **base = (i & 1) ? array_1 : array_2;
      sref_read_enter ();
      Object *p = base[xrand (&rand_val) % N_ELEM];

      if (i % 16 == 0)
        printf ("got value: %d\n", p->value);

      sref_read_exit ();
    }

  return (0);
}

static int
swapper (void *arg)
{
  unsigned int rand_val = (uintptr_t)arg;
  for (int i = 0; i < N_LOOPS; ++i)
    {
      sref_read_enter ();
      Object *p = sref_acquire (array_1[xrand (&rand_val) % N_ELEM]);
      Object *old = atomic_exchange_explicit (array_2 +
        (xrand (&rand_val) % N_ELEM), p, memory_order_acq_rel);

      sref_release (old);
      sref_read_exit ();
    }

  return (0);
}

static int
mutator (void *arg)
{
  unsigned int rand_val = (uintptr_t)arg;
  for (int i = 0; i < N_LOOPS; ++i)
    {
      Object **base = (i & 1) ? array_2 : array_1;
      unsigned int index = xrand (&rand_val) % N_ELEM;

      sref_read_enter ();
      Object *p = base[index];
      Object *nv = make_obj (p->value * 2);

      if (!atomic_compare_exchange_strong_explicit (&base[index],
          &p, nv, memory_order_acq_rel, memory_order_relaxed))
        sref_fini (nv);
      else
        sref_release (p);

      sref_read_exit ();
    }

  return (0);
}

#define N_THREADS   5

int main ()
{
  if (sref_lib_init () < 0)
    abort ();

  unsigned int seed = time (0);
  for (int i = 0; i < N_ELEM; ++i)
    {
      array_1[i] = make_obj (xrand (&seed));
      array_2[i] = make_obj (xrand (&seed));
    }

  thrd_t thrs[N_THREADS * 3];
  int n_thr = (int)(sizeof (thrs) / sizeof (thrs[0]));

  for (int i = 0; i < n_thr; ++i)
    {
      int (*fn) (void *) = (i % 3) == 0 ?
        reader : ((i % 3) == 1 ? swapper : mutator);
      if (thrd_create (&thrs[i], fn, (void *)(uintptr_t)seed) != thrd_success)
        abort ();
    }

  puts ("joining threads");
  for (int i = 0; i < n_thr; ++i)
    thrd_join (thrs[i], NULL);

  for (int i = 0; i < N_ELEM; ++i)
    {
      sref_release (array_1[i]);
      sref_release (array_2[i]);
    }

  assert (sref_flush () == 0);
  assert (n_existing == 0);
  return (0);
}
