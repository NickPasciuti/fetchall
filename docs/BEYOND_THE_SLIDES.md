# Beyond the slides

The lectures gave you `pthread_create()` and `pthread_join()` (Chapter 4),
mutexes and semaphores (Chapter 6), and the bounded-buffer problem
(Chapter 7). `fetchall` uses all of those, plus a few things the slides did not
cover. **Everything you need that was not in lecture is on this page.** Read
it once before you start, then come back to each section when you reach the
stage that points to it.

| Section | You need it for |
|---|---|
| [1. Giving each thread its own argument](#1-giving-each-thread-its-own-argument) | Stage 1 |
| [2. Errors from pthread functions](#2-errors-from-pthread-functions) | Stage 1 |
| [3. POSIX semaphores on Linux](#3-posix-semaphores-on-linux) | Stages 3 and 4 |
| [4. Stopping the workers: sentinels](#4-stopping-the-workers-sentinels) | Stage 4 |
| [5. The ring, and what its error messages mean](#5-the-ring-and-what-its-error-messages-mean) | Stage 4 |
| [6. Why this gets faster on a 2-core machine](#6-why-this-gets-faster-on-a-2-core-machine) | the quiz |
| [7. Debugging a program that hangs](#7-debugging-a-program-that-hangs) | whenever it hangs |

---

## 1. Giving each thread its own argument

`pthread_create()` passes exactly one value to the new thread: a `void *`.
When a thread needs several things (who it is, where its slice starts, how
long the slice is) you put them in a struct and pass a pointer to it:

```c
struct worker_arg { int id; int first; int count; };

static void *static_worker(void *arg)
{
    struct worker_arg *a = arg;          /* void * converts back automatically */
    for (int i = a->first; i < a->first + a->count; i++)
        ...
    return NULL;
}
```

Two rules, and both bite people:

1. **One struct per thread.** If every thread gets a pointer to the *same*
   struct, they all see whatever was written into it last. This is the
   `workers_bug.c` example from the Chapter 4 slides (every thread got `&i`),
   in a new costume. Use an array, `struct worker_arg args[MAX_THREADS]`, and
   pass `&args[k]` to thread `k`.
2. **The struct must stay alive until the thread is done with it.** A local
   array in `run_static()` is fine, because `run_static()` does not return
   until it has joined every thread. A local variable inside the `for` loop
   body is *not* fine: it is gone by the next iteration, while the thread may
   still be reading it.

Stage 4's workers need only an id, so there you pass a pointer to an `int`
from an array `int ids[MAX_THREADS]`, by the same two rules.

## 2. Errors from pthread functions

Most system calls return `-1` and set `errno`, which is why `perror()` works
for them. **The pthread functions do not.** They return `0` on success and the
error number itself on failure, and they leave `errno` alone. So:

```c
int err = pthread_create(&tids[k], NULL, static_worker, &args[k]);
if (err != 0) {
    fprintf(stderr, "pthread_create: %s\n", strerror(err));
    exit(1);
}
```

`strerror()` (from `<string.h>`) turns the number into a message such as
"Resource temporarily unavailable". In this assignment `pthread_create()` will
not fail, because you never ask for more than 64 threads, but check it anyway:
in real programs it does.

The Makefile compiles with `-pthread`. That flag is what links the thread
library in; if you ever build by hand, include it.

## 3. POSIX semaphores on Linux

The Chapter 6 slides wrote semaphores as `wait(S)` and `signal(S)`. The POSIX
versions, from `<semaphore.h>`, are:

| Slides | POSIX | What it does |
|---|---|---|
| *(set S to n)* | `sem_init(&s, 0, n)` | create the semaphore with value `n` |
| `wait(S)` | `sem_wait(&s)` | if the value is 0, sleep until it is not; then subtract 1 |
| `signal(S)` | `sem_post(&s)` | add 1, waking one sleeping thread if there is one |

```c
#include <semaphore.h>

static sem_t conn_slots;              /* a global, like a mutex */

sem_init(&conn_slots, 0, 3);          /* three connection slots */
...
sem_wait(&conn_slots);                /* take a slot (may sleep here) */
/* ... at most three threads can be between these two lines ... */
sem_post(&conn_slots);                /* give it back */
```

The middle argument of `sem_init` is `pshared`: `0` means the semaphore is
shared between the threads of one process, which is what you want. (The
Chapter 7 slides also showed *named* semaphores, `sem_open()`, for sharing
between processes. You do not need them here.)

**A counting semaphore is a pile of tokens.** That is the whole idea behind
stage 3. The value is how many tokens are left. `sem_wait` takes one, or
sleeps until one comes back; `sem_post` returns one. Start it at `C` and at
most `C` threads can hold a token at once.

A thread sleeping in `sem_wait` is not spinning. It is blocked, the scheduler
skips it, and it uses no CPU until someone posts. (`make watch` shows you
threads in that state.)

One warning, for the stage 3 code: **always give the token back**, on every
path. If a failed fetch returns early without `sem_post`, that slot is lost
for the rest of the run, and with `-c 1` the whole program stops.

## 4. Stopping the workers: sentinels

In stage 4 the workers loop forever: take a job, do it, take another. So how
does a worker know when to stop?

It cannot just check "is the queue empty?" An empty queue might only mean the
producer has not put the next job in *yet*. The producer is the only one who
knows there is no more work, so it has to *say* so, through the queue itself.
It does that with a **sentinel**: a value that is not a real job. Here it is
`SENTINEL`, which is `-1`, never a valid URL index.

```c
for (int i = 0; i < nurls; i++)
    buf_put(i);                 /* the real work */
for (int k = 0; k < nthreads; k++)
    buf_put(SENTINEL);          /* one "stop" per worker */
```

**One per worker**, because each worker stops after taking one. A worker that
takes a sentinel must not put it back and must not go round again. With one
sentinel for eight workers, one stops and seven sleep in `buf_take()` forever,
and `pthread_join()` waits for them forever.

Because the sentinels go in *after* the last real URL, and the queue is
first-in first-out, every real job has already been handed out by the time
any worker sees a sentinel.

## 5. The ring, and what its error messages mean

`provided.h` gives you `struct ring`: a circular array of ints with
`ring_push()` and `ring_pop()`. It does the index arithmetic for you so that
stage 4 is only about synchronization. It is **not** thread-safe: if two
threads call `ring_pop()` at the same moment, both can read the same slot.
That is what the mutex is for.

It also checks how it is used. If you see

```
fetchall: ring_push() called on a FULL buffer
```

then a producer got in without a free slot. Your empty-slots semaphore is
not doing its job: started at the wrong value, not waited on, or not posted
by the consumer. If you see

```
fetchall: ring_pop() called on an EMPTY buffer
```

then a consumer got in without an item: the same thing, for the full-slots
semaphore. Either message ends the program on purpose (`abort()`), so you
find the bug now rather than as a wrong total later.

The order inside `buf_put()` matters, too. Wait on the semaphore **first**,
then lock the mutex. If a producer locks the mutex and *then* sleeps waiting
for an empty slot, it sleeps holding the lock, and no consumer can get in to
empty a slot. That is a deadlock, Chapter 8 style.

## 6. Why this gets faster on a 2-core machine

Your codespace has two CPU cores (`make check-env` prints the number). Yet in
`make experiment`, 8 threads finish about 8 times faster than 1, and 40
threads about 40 times faster. How can 40 threads beat 2 cores?

Because a download spends nearly all its time **waiting**, not computing. A
thread inside `fake_fetch()` is asleep, exactly like a real thread waiting for
bytes from the network. Asleep means blocked: it is not on a core at all. So
40 threads can all be waiting at once, and the two cores are free the whole
time. The threads are not computing in parallel. They are **waiting in
parallel**, and that is the payoff of concurrency for anything I/O-bound: web
browsers, servers, download managers, database clients.

If each fetch were real computation instead (say, hashing the data), two cores
could run at most two of them at once, and you would never get past about 2×.
That is where Amdahl's Law from Chapter 4 comes in.

## 7. Debugging a program that hangs

Every checkpoint runs your program under a 20-second timeout, so a hang shows
up as a FAIL saying it was "still running after 20 seconds". To see *where*
it is stuck, run it under `gdb` yourself:

```bash
gdb --args ./fetchall -t 4 -m queue examples/urls40.txt
(gdb) run
          ... it hangs: press Ctrl+C ...
(gdb) thread apply all bt
```

`thread apply all bt` prints a backtrace for every thread. Look for each
thread's top frame from *your* code: a thread in `buf_take()` waiting on a
semaphore that nobody will ever post, or `main` stuck in `pthread_join()`,
usually tells you exactly what is missing. Type `quit` to leave.

`printf` debugging works too, but send it to `stderr`
(`fprintf(stderr, ...)`) so it does not upset the checkpoints.
