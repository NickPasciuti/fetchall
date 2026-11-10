# Measuring what threads buy you

Five `make` targets let you see your program at work. The quiz asks for numbers
from `make experiment` and `make compare`, so run both before you open it.

| Command | Needs | What it shows | Time |
|---|---|---|---|
| `make run` | stage 1 | a small mixed list, four threads | 1 s |
| `make race` | stage 1 | lost updates when the totals have no lock | 1 s |
| `make watch` | stage 1 | the threads, asleep, while they "download" | 2 s |
| `make experiment` | stages 1 and 3 | time against threads, and against connections | 30 s |
| `make compare` | stages 1 and 4 | fixed slices against a shared queue | 8 s |

---

## `make race`: see the bug before you fix it

Run this as soon as stage 1 passes, **before** you start stage 2. It runs
50,000 instant fetches with 8 threads, five times as your program stands and
five times with every `pthread_mutex_lock`/`unlock` compiled out. It prints the
correct summary first, so you can compare.

Before stage 2, both halves are lock-free, and most runs come out short: a few
`ok`s missing and a few thousand bytes gone, and a different amount every time.
Each missing count is two threads doing `totals.ok++` at the same moment: both
load the same old value, both add one, both store, and one of the increments
vanishes. It is the `race.c` and `count.c` examples from the slides, in your
own program.

After stage 2, the first half should be exactly right every time, and the
second half still shows the race, because it is your program with the locks
removed.

(It is a race, so now and then a lock-free run happens to come out right.
Run it again.)

## `make watch`: what the threads are doing

This starts `./fetchall -t 8` on 40 slow URLs, waits half a second, and runs
`ps -L`, which lists every thread of a process on its own row:

```
  PID   LWP STAT %CPU WCHAN            COMMAND
 4217  4217 Sl    0.0 futex_do_wait    fetchall
 4217  4219 Sl    0.0 hrtimer_nanoslee fetchall
 4217  4220 Sl    0.0 hrtimer_nanoslee fetchall
 ...
```

Nine rows: `main` plus eight workers, all with the same PID (one process)
and different LWP numbers (one kernel thread each: the one-to-one model).
`S` means asleep and `%CPU` is 0. The workers are sleeping in the kernel's
timer (`hrtimer_nanosleep`), which is what `fake_fetch()` does in place of
waiting for a network. `main` is asleep in `futex_do_wait`, which is what
`pthread_join()` looks like from the kernel's side. Nobody is using the CPU,
and yet eight downloads are in progress.

## `make experiment`: threads against time

`examples/urls40.txt` holds 40 downloads of 0.25 seconds each.

**Part 1** runs it with 1, 2, 4, 8, 10, 20 and 40 threads and no connection
limit. With one thread it takes 40 × 0.25 = 10 seconds. With T threads, each
thread has 40 / T downloads to do one after another, all T at the same time,
so the time is about 10 / T seconds. That holds well past 2 threads, on a
2-core machine. [BEYOND_THE_SLIDES.md section 6](BEYOND_THE_SLIDES.md#6-why-this-gets-faster-on-a-2-core-machine)
explains why.

**Part 2** gives every URL its own thread (40 threads) and then limits the
connections with `-c`. Now the threads are not the limit. The semaphore is.
At most C downloads run at once, so the 40 go through in 40 / C rounds of
0.25 s. Threads beyond C add nothing: they sleep in `sem_wait()`.

You can work out every number in both tables before you run it. If yours
are well off, something is serializing your threads.

## `make compare`: fixed slices against a queue

`examples/uneven.txt` has 16 downloads: four slow ones (1.5 s each) first,
then twelve quick ones (0.25 s each). Both runs use 4 threads.

With **`-m static`**, worker 1's slice is URLs 1 to 4, so it gets all four
slow ones and needs 4 × 1.5 = 6 seconds. The other three finish their quick
slices in 1 second and then sit idle for 5. The program takes as long as its
unluckiest thread.

With **`-m queue`**, each worker takes the next job when it is free. The four
slow downloads start together and take 1.5 s; meanwhile nobody is free, so
the twelve quick ones wait. When the slow ones finish, the four workers clear
the twelve quick ones in 3 rounds of 0.25 s. Total: 1.5 + 0.75 = 2.25 s.

Same work, same threads, nearly three times faster, because whoever is free
takes the next job. This is why real thread pools are built around a shared
queue rather than fixed slices.
