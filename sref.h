/* Declarations for the sref API.

   This file is part of libsref.

   libsref is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

#ifndef SREF_H_
#define SREF_H_   1

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Sref_
{
  uintptr_t refcnt;
  void (*fini) (void *);
  struct Sref_ *next;
} Sref;

typedef struct
{
  void (*prepare) (void);
  void (*parent) (void);
  void (*child) (void);
} SrefAtFork;

/* Initialize the Sref library. */
extern int sref_lib_init (void);

/* Fetch the library version. */
extern void sref_lib_version (int *major, int *minor);

/* Initialize an Sref with finalizer. */
#define sref_init(ptr, fin)   \
  do   \
    {   \
      Sref *p_ = (Sref *)(ptr);   \
      p_->refcnt = 1;   \
      p_->fini = (fin);   \
      p_->next = 0;   \
    }   \
  while (0)

/* Force destruction of an Sref. */
#define sref_fini(ptr)   \
  do   \
    {   \
      Sref *p_ = (Sref *)(ptr);   \
      p_->fini (p_);   \
    }   \
  while (0)

/* Enter a critical section. */
extern void sref_read_enter (void);

/* Acquire an Sref, incrementing its local reference count. */
extern void* sref_acquire (void *refptr);

/* Release an Sref, decrementing its local reference count. */
extern void sref_release (void *refptr);

/* Exit a critical section. */
extern void sref_read_exit (void);

/* Flush the accumulated references for all threads. */
extern int sref_flush (void);

/* Get the 'pthread_atfork' callbacks for Sref. */
extern SrefAtFork sref_atfork (void);

#ifdef __cplusplus
}
#endif

#endif
