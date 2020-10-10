# libsref design

This document specifies the design decisions for libsref.

## About reference counting

Reference counting is a method for managing memory. With reference counting,
an object has an associated _reference count_ - an integer that specifies
the number of users that particular object has. Every object starts with a
reference count of 1, and with each time it is used, this count is incremented;
similarly, when it is no longer used, the count drops by 1. When the reference
count reaches 0, it is deallocated and no longer valid.

## Reference counting in memory-unsafe languages

Reference countins is quite common in the runtime of several high level
languages like Python or Perl. However, when applied to languages like C and
C++, care must be taken when multiple threads are involved.

The reason why things are different in lower level languages is that there is
generally no built-in protection against data races, which means that multiple
threads may be operating on the same reference counted object at the same time.
Therefore, one thread may be accessing a reference counted object whose count
was dropped to 0 by another thread unreferencing it.

## Atomic updates

A common fix to the above problem involves using _atomic operations_ on the
reference count so that different threads can operate safely on the counter.

In the above scenario, a thread that wishes to use a reference counted object
would first try to increment the counter, but only if it wasn't 0. If it was,
that means the object was already deallocated and therefore invalid.

While atomic updates can solve the problem, it's worth pointing out that this
solution is rather heavy-handed: Atomic operations are very expensive, even
in modern hardware, and they severly limit the scalability of using multiple
reference counted objects in a multithreaded situation. Unless the counter is
aligned to a cache line, there is expected to be a lot of false sharing, but
if it **is** aligned, then it comes at the price of much bigger objects.

## Read side critical sections

As an alternative, libsref provides an implementation of _critical sections_,
that is, portions of code during which reference counted objects cannot be
deallocated. In libsref, critical sections are implemented via RCU, a technique
that provides very low overhead for reading operations while putting a bigger
burden on the memory reclamation side.

## Implementing reference counting on top of RCU

RCU by itself doesn't have anything to do with reference counting - It is
merely concerned by memory deallocation and critical sectons. libsref thus
implements reference counting with RCU as support.

When _acquiring_ or _releasing_ a reference counted object in libsref, its
counter isn't modified in any way. Instead, such object is placed in a
thread-local hash table that maps pointers to _deltas_, an integer that
records the temporary difference that needs to be applied by a thread.

The tables used by the threads have a fixed capacity, and so when a certain
occupancy is reached, the reclamation phase begins. A global lock is acquired,
and every thread that has used the libsref API is scanned: Once it is outside
a critical section, it is known to be in a _quiescent state_, and so its
deltas can be added to each object.

In this implementation, each thread has 2 sets of these tables: One for
positive deltas, and one for negative ones. The reason they are split is so
that checks for liveness (i.e: when the reference count of an object is 0) can
be made only when processing the negative delta table.

## Implications

Because acquiring and releasing an object involve no atomic operations in
the general case, libsref can scale much better as the number of CPU's and
objects increase. The reason being, most operations have much better
locality of reference by virtue of using (mostly) thread-local data.

## Limitations

Like any reference counting scheme, libsref cannot detect cycles by itself.
That is to say, if an object was accessible itself through one of its own
members, then a _cycle_ would be generated, under which the reference count
would never drop to 0.

On another note, since the table of deltas has a fixed size, it is possible
to have it filled while being inside a critical section. Such a situation can
be handled by libsref, but it involves another global lock and a bit more
processing during the reclamation phase.
