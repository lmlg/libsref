# API documentation
This document describes the public interface as exposed by libsref.

## Headers
The header <sref.h> contains all the declarations needed to use the library.

## Types
libsref defines 2 types: **SrefAtFork** and **Sref**

The type **SrefAtFork** is a structure of 3 callbacks that is only used when
mixing threads and process creation via the POSIX call **fork**. The function
used to retrieve these callbacks will be detailed later on.

The type **Sref** is the base type used for reference counted objects. It
contains 2 public members that should be considered read-only: The reference
count, **refcnt** and the destructor, **fini**. There's an additional, private
member that is used internally by the library that won't be documented here.

When creating custom reference counted objects, the user-defined type should
include an **Sref** within. It is recommented that it be the first member of
the new type, although it isn't strictly necessary, as with other libraries.

## Public API

```C
int sref_lib_init (void);
```

Initializes the library so the rest of the API can be used. Returns 0 on
success and a negative value otherwise. If any call to a libsref function
is made before initializing the library, the results are undefined.

This function can be safely called more than once.

```C
void sref_lib_version (int *major, int *minor);
```

Get the major and minor version for the library. This is useful to make
sure that you're linking against a compatible version at runtime.

```C
sref_init (void *ptr, void (*fini) (void *));
```

This macro initializes an **Sref** pointer with a user-supplied destructor.
The destructor will be called when the reference count of the pointer goes
down to zero, and receives as a pointer to the **Sref** as its sole argument.

If the **Sref** is embedded inside a custom object, you can get the full
object back with some simple pointer arithmetic. For example:

```C
struct myt
{
  int x;
  Sref sref;
  double d;
};

void myt_fini (void *arg)
{
  struct myt *p = (struct myt *)((char *)arg - offsetof (struct myt, sref));
  /* Use the full object. */
}
```

```C
sref_fini (void *);
```

Force the destruction of an **Sref** pointer. Note that this macro should only
be called if you are absolutely sure that no other thread may be manipulating
this pointer.

```C
void sref_read_enter (void);
```

Make the calling thread enter a read-side critical section. As long as any
thread is in a critical section, no **Sref** will be finalized. As such, this
call is typically done _before_ reading a pointer that could be potentially
modified by another thread.

Critical sections can be nested, and so threads maintain a counter that
indicates the depth.

```C
void sref_read_exit (void);
```

Make the calling thread exit a read-side critical section. If the calling
thread wasn't in a critical section, the effects are undefined.

```C
void* sref_acquire (void *ptr);
```

Increment the reference count of the **Sref** pointer _ptr_. The effects of
calling the function with a NULL or invalid pointer are undefined.

This function returns the same pointer it was passed.

```C
void sref_release (void *ptr);
```

Decrement the reference count of the **Sref** pointer _ptr_. The effects of
calling the function with a NULL or invalid pointer are undefined.

```C
int sref_flush (void);
```

Synchronize with other threads, so that the accumulated reference counts for
all **Sref** pointers are applied (or 'flushed').

Note that this function can only succeed when the calling thread is _not_ in
a read-side critical section. If it is, a value of -1 is returned, and no
action is performed. Otherwise, this function returns 0.

```C
SrefAtFork sref_atfork (void);
```

Returns the set of callbacks needed to pass to a function like **pthread_atfork**
to maintain state consistency when mixing threads and process creation with the
POSIX interface **fork**.

The **SrefAtfork** type is defined as such:

```C
typedef struct
{
  void (*prepare) (void);
  void (*parent) (void);
  void (*child) (void);
} SrefAtFork;
```

Where each of its members is named after the corresponding callback passed to
the pthread call **pthread_atfork**.
