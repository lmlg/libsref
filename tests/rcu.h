/* Tests for the public API.

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

static int rcu_obj_counter;

static void
rcu_obj_fini (void *ptr)
{
  free (ptr);
  --rcu_obj_counter;
}

static Object*
rcu_obj_make (unsigned int value)
{
  Object *ret = (Object *)xmalloc (sizeof (*ret));
  ret->value = value;
  sref_init (ret, rcu_obj_fini);
  atomic_inc (&rcu_obj_counter, 1);
  return (ret);
}

static void*
rcu_thread_fn (void *arg)
{
  (void)arg;
  Object *p = rcu_obj_make (0);

  sref_release (p);
  return (0);
}

static void
test_rcu_single_thread (void)
{
  Object *p = rcu_obj_make (0);

  sref_read_enter ();
  sref_release (p);
  sref_flush ();

  ASSERT (rcu_obj_counter == 1);
  sref_read_exit ();
  ASSERT (rcu_obj_counter == 0);

  pthread_t thr;
  if (pthread_create (&thr, NULL, rcu_thread_fn, 0) < 0)
    abort ();

  pthread_join (thr, 0);
  ASSERT (rcu_obj_counter == 0);
}

static void
fini_basic (void *ptr)
{
  (void)ptr;
  --rcu_obj_counter;
}

static void
test_rcu_limits (void)
{
  Object objs[SREF_NDELTAS];
  for (int i = 0; i < SREF_NDELTAS; ++i)
    sref_init (&objs[i], fini_basic);

  rcu_obj_counter = (int)SREF_NDELTAS;
  for (int i = 0; i < SREF_NDELTAS; ++i)
    sref_release (&objs[i]);

  sref_flush ();
  ASSERT (rcu_obj_counter == 0);
  for (int i = 0; i < SREF_NDELTAS; ++i)
    sref_init (&objs[i], fini_basic);

  sref_read_enter ();
  rcu_obj_counter = (int)SREF_NDELTAS;
  for (int i = 0; i < SREF_NDELTAS; ++i)
    sref_release (&objs[i]);

  sref_acquire (&objs[1]);
  sref_read_exit ();
  ASSERT (rcu_obj_counter == 1);

  Object single;
  sref_init (&single, fini_basic);
  rcu_obj_counter = 1;
  sref_flush ();

  for (int i = 0; i < SREF_NMAXOPS / 2; ++i)
    {
      sref_acquire (&single);
      sref_release (&single);
    }

  sref_release (&single);
  ASSERT (rcu_obj_counter == 0);
}

static unsigned int
xrand (unsigned int *prev)
{
  unsigned int x = *prev * 1103515245 + 12345;
  *prev = x;
  return (x >> 16);
}

typedef struct
{
  unsigned int cnt;
  unsigned int rand_val;
} ThrInfo;

#define THREAD_LOOPS   1000

static Object *global_obj;

static void*
rcu_thread (void *arg)
{
  ThrInfo *info = (ThrInfo *)arg;
  info->cnt = 0;

  for (int i = 0; i < THREAD_LOOPS; ++i)
    {
      sref_read_enter ();
      unsigned int value = global_obj->value;

      if ((value % 4) == 0)
        {
          Object *p = rcu_obj_make (xrand (&info->rand_val));
          xmutex_lock (&global_lock);
          if (global_obj->value == value)
            {
              Object *prev = global_obj;
              global_obj = p;
              xatomic_mfence_full ();
              sref_release (prev);
              ++rcu_obj_counter;
            }
          else
            sref_fini (p);

          xmutex_unlock (&global_lock);
        }
      else
        ++info->cnt;
    }

  return (0);
}

#define NTHR   16

static void
test_rcu_mt (void)
{
  pthread_t thrs[NTHR];
  ThrInfo info[NTHR];
  unsigned int seed = (unsigned int)time (0);

  global_obj = rcu_obj_make (seed);
  rcu_obj_counter = 0;

  for (int i = 0; i < NTHR; ++i)
    {
      info[i].rand_val = xrand (&seed);
      pthread_create (&thrs[i], NULL, rcu_thread, &info[i]);
    }

  seed = 0;
  for (int i = 0; i < NTHR; ++i)
    {
      pthread_join (thrs[i], 0);
      seed += info[i].cnt;
    }

  seed += rcu_obj_counter;
  ASSERT (seed == NTHR * THREAD_LOOPS);
}

static const TestFn rcu_test_fns[] =
{
  {
    "single threaded API",
    test_rcu_single_thread
  },
  {
    "API limits",
    test_rcu_limits
  },
  {
    "multi threaded API",
    test_rcu_mt
  }
};

TEST_MODULE (RCU, rcu_test_fns);
