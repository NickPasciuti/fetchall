# CSC 139 - Assignment 3: `fetchall`

**100 points, all graded automatically.** Write a program that downloads a
list of URLs using several threads at once, with a lock protecting what they
share, a semaphore limiting how many connections are open, and finally a
bounded buffer that hands out the work. Then measure how much the threads buy
you.

You build it in **four stages**. Each stage has its own checkpoint that tells
you whether that stage works, and prints a **stage code** when it does.

## Where you will work

**GitHub Codespaces**, the same as Assignments 1 and 2. That is the
environment for this assignment: not your laptop, not a VM, not your own Linux
install. Everyone gets the same setup, and it is what your work is graded
against. Do not write this on macOS.

**Setup instructions: [docs/SETUP.md](docs/SETUP.md)** take about five minutes.
If you still have your Assignment 2 codespace, stop it. You need a new one,
made from *this* repository.

## Before you start

```bash
make check-env
```

Every line must say `PASS`. It also prints how many CPU cores your codespace
has (two). Remember that number; it matters later.

Then read **[docs/BEYOND_THE_SLIDES.md](docs/BEYOND_THE_SLIDES.md)**. It covers
what this assignment needs beyond the lectures: passing a struct to a thread,
pthread error codes, POSIX semaphores on Linux, shutting down workers with a
sentinel, and why threads speed this up on two cores.

## What `fetchall` does

```bash
./fetchall urls.txt                   # one worker thread
./fetchall -t 8 urls.txt              # eight worker threads
./fetchall -t 8 -c 3 urls.txt         # eight threads, at most 3 downloads at once
./fetchall -t 4 -m queue urls.txt     # workers share a queue instead of fixed slices
```

Other options: `-b N` sets the queue's size (default 4), `-q` prints only the
summary.

A **URL file** has one URL per line; blank lines and `#` lines are skipped.
The network is **fake**, so nothing is really downloaded: each URL says how
long its download takes and how big it is, and `fake_fetch()` sleeps for that
long. A thread waiting in `fake_fetch()` behaves exactly like one waiting on a
real network.

```
http://csc139.test/index.html?ms=200&bytes=5120      # 0.2 s, 5120 bytes
http://csc139.test/missing.png?ms=100&status=404     # fails after 0.1 s
```

Each fetch prints one line, and the program ends with a summary:

```
[fetchall] worker 1: http://csc139.test/index.html?ms=200&bytes=5120 -> 5120 bytes (0.20s)
[fetchall] worker 2: http://csc139.test/missing.png?ms=100&status=404 -> FAILED (0.10s)
...
[fetchall] summary: 8 urls, 7 ok, 1 failed, 509054 bytes in 0.70s
```

Those lines are printed for you. Reading the file, the options, the fetch
itself and the summary are all written. What you write is the threading and
the synchronization.

## Files

| File | |
|---|---|
| `src/fetchall.c` | **your work**: the rules at the top, then the TODOs, in stage order |
| `src/provided.c`, `src/provided.h` | the fake network, a ring buffer, a clock. Read, do not edit |
| `examples/` | URL files used by `make run`, `make experiment` and `make compare` |
| `tests/run_tests.sh` | the checkpoints |

---

## Stage 1: threads (TODO 1a, 1b)

Split the URLs into T equal, contiguous slices and give each slice to its own
thread, created with `pthread_create()`. The main thread waits for all of them
with `pthread_join()`. The exact split is in the comments above TODO 1a.

```bash
make check-1
```

The checkpoint checks that every URL is fetched exactly once, by T threads and
never by `main`, that each worker got the right slice, that the threads really
overlap (12 downloads of 0.3 s with 4 threads take about 0.9 s, not 3.6), and
that every thread is joined before the summary.

Then try `make run` and `make watch`, and **run `make race` before you start
stage 2.**

## Stage 2: a mutex (TODO 2a, 2b)

Every worker adds to the shared `totals`. `make race` showed you the result:
lost updates. Declare a mutex and make the update a critical section.

```bash
make check-2
```

The checkpoint runs 50,000 instant fetches with 8 threads, ten times, and
needs the totals exact every time. It also checks that your lock did not
serialize the downloads themselves.

## Stage 3: a counting semaphore (TODO 3a, 3b)

Servers limit how many connections one client may open. With `-c C`, at most
C downloads may be in flight at once, however many threads there are. Use a
semaphore that starts at C: take a slot before each fetch, give it back after.

```bash
make check-3
```

The checkpoint measures how many fetches were ever in flight at the same
moment. With `-t 8 -c 3` that number must be exactly 3. It also checks that a
failed fetch gives its slot back.

## Stage 4: a bounded buffer (TODO 4a to 4e)

Fixed slices have a weakness: if the slow URLs all land in one slice, one
thread does all the slow work while the rest sit idle. Replace them with a
shared queue, the bounded-buffer problem from Chapter 7: `main` is the
producer and puts URL numbers into a buffer of B slots; the workers are the
consumers. Two semaphores count empty and full slots, and a mutex protects the
buffer. When the URLs run out, `main` puts in one `SENTINEL` per worker so
each one knows to stop. This mode runs with `-m queue`.

```bash
make check-4
```

The checkpoint checks every URL is fetched once, the buffer never holds more
than B, workers stop cleanly even when there are more workers than URLs, the
load is balanced, and nothing is lost or doubled over 50,000 instant fetches.

## All four together

```bash
make test
```

runs every checkpoint and lists the stage codes you have earned. The
checkpoints run your program with a 20-second time limit, so a deadlock fails
a check instead of freezing your codespace.

## Measure

```bash
make experiment      # time against threads, then against connections (~30 s)
make compare         # fixed slices against the shared queue (~8 s)
```

What to look for, and why the numbers come out the way they do:
**[docs/MEASURING.md](docs/MEASURING.md)**. The quiz asks for numbers from
both.

## Rules

- Every fetch happens in a worker thread created with `pthread_create()`.
  The main thread never fetches.
- Join every thread before the summary.
- Synchronize with pthread mutexes and POSIX semaphores. No busy-waiting.
- Do not edit `provided.c`, `provided.h`, or the `FMT_` formats.
- Your own debugging output goes to `stderr`.

## Submit

Submission is the **Canvas quiz "Assignment 3: fetchall"**. There is nothing
to upload anywhere else, and every question is graded automatically. It asks
for:

1. your four stage codes (one question each)
2. four numbers from your own `make experiment` and `make compare` runs
3. two questions about what you saw

Run `make test`, `make experiment` and `make compare` **before** you open the
quiz, so the numbers are in front of you. A stage you have not finished
simply costs you that question; the others still count.

## A note on AI

Course policy applies: use it to understand the material, but do not submit
its work as your own. The exam will ask you about threads, mutexes,
semaphores and the bounded buffer, and about this program. A program you did
not write is a question you cannot answer.
