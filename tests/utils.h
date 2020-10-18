/* General utilites for tests.

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

#ifndef TEST_UTILS_H_
#define TEST_UTILS_H_

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <time.h>
#include <pthread.h>
#include "../sref.h"
#include "../compat.h"

typedef struct
{
  const char *msg;
  void (*fct) (void);
} TestFn;

#define ASSERT(cond)   \
  do   \
    {   \
      if (!(cond))   \
        {   \
          fprintf (stderr, "\n\nassertion failed: "   \
                           "%s\nline: %u\nFile: %s\n\n",   \
                   #cond, __LINE__, __FILE__);   \
          exit (EXIT_FAILURE);   \
        }   \
    }   \
  while (0)


#define ARRAY_SIZE(x)   (sizeof (x) / sizeof (x[0]))

typedef struct
{
  const char *name;
  const TestFn *test_fns;
  size_t n_tests;
} TestModule;

#define TEST_MODULE(name, tests)   \
  static const TestModule name =   \
    {   \
      #name,   \
      tests,   \
      ARRAY_SIZE (tests)   \
    }

static void
test_module_run (const TestModule *mod)
{
  for (size_t i = 0; i < mod->n_tests; ++i)
    {
      printf ("Testing %s (%s) ...", mod->name, mod->test_fns[i].msg);
      mod->test_fns[i].fct ();
      puts (" OK");
    }
}

static void*
xmalloc (size_t size)
{
  void *ret = malloc (size);
  if (!ret)
    abort ();

  return (ret);
}

typedef struct
{
  Sref base;
  unsigned int value;
} Object;

static xmutex_t global_lock;

static void
test_init (void)
{
  if (xmutex_init (&global_lock) < 0)
    abort ();
}

static int
atomic_inc (int *ptr, int val)
{
  xmutex_lock (&global_lock);
  int ret = (*ptr += val) - val;
  xmutex_unlock (&global_lock);
  return (ret);
}

#endif
