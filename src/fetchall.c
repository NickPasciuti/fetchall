/*
 * CSC 139 - Operating System Principles
 * Assignment 3: fetchall
 *
 * WHAT YOU ARE BUILDING
 *   A program that downloads every URL in a file, using several threads at
 *   once, and never opening more than C connections at the same time:
 *
 *       ./fetchall urls.txt                    one thread
 *       ./fetchall -t 8 urls.txt               eight worker threads
 *       ./fetchall -t 8 -c 3 urls.txt          eight threads, at most three
 *                                              downloads in flight at once
 *       ./fetchall -t 4 -m queue urls.txt      workers share a job queue
 *
 *   The "network" is fake (see provided.h): a fetch just sleeps for as long
 *   as its URL says, the way a real download spends its time waiting. That
 *   makes every run repeatable, and it is exactly the situation where threads
 *   pay off, even on a machine with only two cores.
 *
 * YOU BUILD IT IN FOUR STAGES. Do them in order. After each one, run its
 * checkpoint. When a checkpoint passes it prints a STAGE CODE for the quiz.
 *
 *   Stage 1  threads           TODO 1a, 1b      make check-1
 *   Stage 2  a mutex           TODO 2a, 2b      make check-2
 *   Stage 3  a semaphore       TODO 3a, 3b      make check-3
 *   Stage 4  a bounded buffer  TODO 4a .. 4e    make check-4
 *
 *   make test runs all four checkpoints together.
 *
 * RULES
 *   1. Every fetch happens in a worker thread you created with
 *      pthread_create(). The main thread never fetches.
 *   2. Join every thread you create before main() prints the summary.
 *   3. Use pthread mutexes and POSIX semaphores (sem_init, sem_wait,
 *      sem_post). No busy-waiting: never spin in a loop checking a variable.
 *   4. Do not edit provided.c or provided.h, and do not change the FMT_
 *      formats below. The checkpoints match them character for character.
 *   5. Anything else you want to print (errors, debugging) goes to stderr.
 *
 * BEFORE YOU START, read docs/BEYOND_THE_SLIDES.md. It covers what this
 * assignment uses that the lectures did not: passing a struct to a thread,
 * pthread error codes, sem_init on Linux, and shutting down workers with a
 * sentinel.
 *
 * AND READ THE MANUAL:  man pthread_create | man pthread_join |
 *                       man pthread_mutex_lock | man sem_init | man sem_wait
 */

/* Ask the C library for the POSIX functions, which strict -std=c17 would
 * otherwise hide. Must come before any #include. */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "provided.h"

#define MAX_URLS     50000   /* most URLs a file may hold                 */
#define URL_LEN      256     /* longest URL, in characters                */
#define MAX_THREADS  64      /* largest value accepted for -t             */
#define SENTINEL     (-1)    /* "no more work" marker for stage 4         */

/* The exact output formats. Printed for you by do_fetch() and main(). */
#define FMT_OK       "[fetchall] worker %d: %s -> %ld bytes (%.2fs)\n"
#define FMT_FAIL     "[fetchall] worker %d: %s -> FAILED (%.2fs)\n"
#define FMT_SUMMARY  "[fetchall] summary: %d urls, %d ok, %d failed, %ld bytes in %.2fs\n"

/* The URLs from the file, one per row: urls[0] .. urls[nurls-1]. */
static char urls[MAX_URLS][URL_LEN];
static int  nurls;
static int  quiet;            /* -q: do not print a line per fetch */

/* Running totals, shared by every worker thread. */
struct totals {
    int  ok;                  /* fetches that succeeded         */
    int  failed;              /* fetches that failed            */
    long bytes;               /* bytes delivered, all fetches   */
};
static struct totals totals;

/* The shared job queue for stage 4 (see provided.h for struct ring). */
static struct ring buf;

/* Forward declarations, so the functions can appear in stage order. */
static int  connect_and_fetch(const char *url, long *bytes);
static void record_result(int ok, long bytes);

/* ------------------------------------------------------------------ */
/* Provided: fetch one URL and report it                                */
/* ------------------------------------------------------------------ */
/*
 * Fetches urls[i] on behalf of worker number `worker` (1, 2, 3 ...), prints
 * one line about it, and adds it to the totals. Your worker threads call
 * this; you never need to change it.
 */
static void do_fetch(int i, int worker)
{
    double t0 = now_seconds();
    long bytes = 0;
    int rc = connect_and_fetch(urls[i], &bytes);
    double elapsed = now_seconds() - t0;

    if (!quiet) {
        if (rc == 0)
            printf(FMT_OK, worker, urls[i], bytes, elapsed);
        else
            printf(FMT_FAIL, worker, urls[i], elapsed);
    }
    record_result(rc == 0, bytes);
}

/* ================================================================== */
/* STAGE 1 - THREADS                                     make check-1   */
/* ================================================================== */
/*
 * Split the URLs into T equal, contiguous slices and give each slice to its
 * own thread. With 10 URLs and 3 threads:
 *
 *     worker 1: urls 0 1 2      worker 2: urls 3 4 5      worker 3: urls 6 7 8 9
 *
 * Worker k (counting from 0) gets urls[first .. first+count-1], where
 *     first = k * nurls / T
 *     end   = (k + 1) * nurls / T        and count = end - first
 * Use exactly this split: the checkpoint checks which worker fetched which
 * URL. Workers are numbered from 1 when they report (id = k + 1).
 */

/* What each static worker is told: who it is and which slice is its own.
 * One of these per thread, and each must stay alive until that thread is
 * done with it. (BEYOND_THE_SLIDES.md, section 1.) */
struct worker_arg {
    int id;          /* 1 .. T, used when reporting         */
    int first;       /* index of the first URL in its slice */
    int count;       /* how many URLs are in its slice      */
};

/* ------------------------------------------------------------------ */
/* TODO 1a - the worker thread                                          */
/* ------------------------------------------------------------------ */
/*
 * This is the function each thread runs. `arg` points at that thread's own
 * struct worker_arg. Fetch every URL in the slice with do_fetch(i, id),
 * then return NULL.
 */
static void *static_worker(void *arg)
{
    (void)arg;
    return NULL;       /* TODO: replace this */
}

/* ------------------------------------------------------------------ */
/* TODO 1b - create T threads, then wait for all of them                */
/* ------------------------------------------------------------------ */
/*
 * Sketch:
 *   pthread_t         tids[MAX_THREADS];
 *   struct worker_arg args[MAX_THREADS];
 *   for k = 0 .. nthreads-1:
 *       fill in args[k]: id, first, count (formulas above)
 *       pthread_create(&tids[k], NULL, static_worker, &args[k])
 *   for k = 0 .. nthreads-1:
 *       pthread_join(tids[k], NULL)
 *
 * Create ALL the threads before joining ANY of them, or they run one after
 * another and you have gained nothing.
 *
 * pthread_create() returns 0 on success and an error NUMBER on failure (it
 * does not set errno). If it fails, print strerror(err) to stderr and
 * exit(1). (BEYOND_THE_SLIDES.md, section 2.)
 */
static void run_static(int nthreads)
{
    (void)nthreads;
    (void)static_worker;
    (void)do_fetch;

    fprintf(stderr, "run_static() is not implemented yet (TODO 1b).\n");
}

/* ================================================================== */
/* STAGE 2 - A MUTEX                                     make check-2   */
/* ================================================================== */
/*
 * Every worker adds to `totals`. `totals.ok++` is a load, an add and a
 * store; two threads doing it at the same moment can lose an update. Before
 * you fix it, run `make race` and watch it happen.
 */

/* ------------------------------------------------------------------ */
/* TODO 2a - declare a mutex for the totals, right here                 */
/* ------------------------------------------------------------------ */
/* Use PTHREAD_MUTEX_INITIALIZER so it needs no separate init call. */

/* ------------------------------------------------------------------ */
/* TODO 2b - make the update a critical section                         */
/* ------------------------------------------------------------------ */
/*
 * Lock before the first line that touches `totals`, unlock after the last.
 * Keep the critical section this small: do not hold the lock while fetching.
 */
static void record_result(int ok, long bytes)
{
    totals.bytes += bytes;
    if (ok)
        totals.ok++;
    else
        totals.failed++;
}

/* ================================================================== */
/* STAGE 3 - A COUNTING SEMAPHORE                        make check-3   */
/* ================================================================== */
/*
 * A real server only lets each client open a few connections at once. With
 * -c C, at most C fetches may be in flight at the same moment, however many
 * threads there are. A counting semaphore that starts at C is exactly that:
 * each fetch takes a "connection slot" before it starts and gives it back
 * when it is done. With 8 threads and C = 3, five threads are always asleep
 * in sem_wait(), waiting their turn. (BEYOND_THE_SLIDES.md, section 3.)
 */

/* ------------------------------------------------------------------ */
/* TODO 3a - declare the semaphore here, and initialise it below        */
/* ------------------------------------------------------------------ */

/* Called once by main(), before any thread starts. max_conn is the -c value
 * (main() turns "no -c given" into MAX_THREADS, which is never a limit).
 * Initialise your semaphore to max_conn: sem_init(&sem, 0, max_conn). */
static void setup_limits(int max_conn)
{
    (void)max_conn;
}

/* ------------------------------------------------------------------ */
/* TODO 3b - take a connection slot around every fetch                  */
/* ------------------------------------------------------------------ */
/*
 * Wait for a slot, fetch, give the slot back. Give it back whether the
 * fetch worked or not: a slot that is never returned is gone for good.
 * Returns whatever fake_fetch() returned.
 */
static int connect_and_fetch(const char *url, long *bytes)
{
    return fake_fetch(url, bytes);
}

/* ================================================================== */
/* STAGE 4 - A BOUNDED BUFFER                            make check-4   */
/* ================================================================== */
/*
 * Fixed slices have a weakness: if the slow URLs all land in one slice, one
 * thread does all the slow work while the others sit idle. (`make compare`
 * shows it.) The fix is a shared queue. The main thread is the PRODUCER: it
 * puts URL numbers into `buf` (a ring of B slots, from provided.h). The
 * workers are CONSUMERS: each takes the next URL number, fetches it, comes
 * back for another. Whoever is free takes the next job.
 *
 * This is the bounded-buffer problem from Chapter 7, with semaphores:
 *   - a semaphore counting EMPTY slots, starting at B
 *   - a semaphore counting FULL slots, starting at 0
 *   - a mutex so only one thread at a time touches the ring
 *
 * When every URL is in the queue, the producer puts in one SENTINEL per
 * worker. A worker that takes a SENTINEL stops. (BEYOND_THE_SLIDES.md,
 * section 4.)
 */

/* ------------------------------------------------------------------ */
/* TODO 4a - declare the two semaphores and the mutex; initialise them  */
/* ------------------------------------------------------------------ */

/* Called once by main() in queue mode, before run_queue(). */
static void buf_init(int capacity)
{
    ring_init(&buf, capacity);
    /* TODO: initialise the empty-slots and full-slots semaphores */
}

/* ------------------------------------------------------------------ */
/* TODO 4b - the producer's side                                        */
/* ------------------------------------------------------------------ */
/* Wait for an empty slot, push the item under the mutex, signal a full
 * slot. Call ring_push(&buf, item) to do the actual push. */
static void buf_put(int item)
{
    (void)item;
}

/* ------------------------------------------------------------------ */
/* TODO 4c - the consumer's side                                        */
/* ------------------------------------------------------------------ */
/* Wait for a full slot, pop under the mutex, signal an empty slot, return
 * the item. Call ring_pop(&buf) to do the actual pop. */
static int buf_take(void)
{
    return SENTINEL;   /* TODO: replace this */
}

/* ------------------------------------------------------------------ */
/* TODO 4d - the queue worker                                           */
/* ------------------------------------------------------------------ */
/* `arg` points at this worker's id (an int). Loop: take an item; if it is
 * SENTINEL, stop; otherwise do_fetch(item, id). Return NULL. */
static void *queue_worker(void *arg)
{
    (void)arg;
    return NULL;       /* TODO: replace this */
}

/* ------------------------------------------------------------------ */
/* TODO 4e - start the workers, produce, shut down, join                */
/* ------------------------------------------------------------------ */
/*
 * Sketch:
 *   create nthreads queue_worker threads (ids 1 .. nthreads)
 *   for every URL i:          buf_put(i)
 *   for every worker:         buf_put(SENTINEL)
 *   join every worker
 */
static void run_queue(int nthreads)
{
    (void)nthreads;
    (void)buf_put;
    (void)buf_take;
    (void)queue_worker;

    fprintf(stderr, "run_queue() is not implemented yet (TODO 4e).\n");
}

/* ================================================================== */
/* Provided: reading the URL file, and main                            */
/* ================================================================== */

/* One URL per line. Blank lines and lines starting with '#' are skipped. */
static int load_urls(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        perror(path);
        return -1;
    }
    char line[URL_LEN + 2];
    int lineno = 0;
    while (fgets(line, sizeof line, fp) != NULL) {
        lineno++;
        size_t len = strcspn(line, "\r\n");
        if (line[len] == '\0' && !feof(fp)) {
            fprintf(stderr, "%s:%d: URL longer than %d characters\n",
                    path, lineno, URL_LEN - 1);
            fclose(fp);
            return -1;
        }
        line[len] = '\0';
        char *p = line + strspn(line, " \t");
        p[strcspn(p, " \t")] = '\0';
        if (*p == '\0' || *p == '#')
            continue;
        if (nurls == MAX_URLS) {
            fprintf(stderr, "%s: more than %d URLs\n", path, MAX_URLS);
            fclose(fp);
            return -1;
        }
        snprintf(urls[nurls++], URL_LEN, "%s", p);
    }
    fclose(fp);
    return 0;
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "usage: %s [-t THREADS] [-c CONNECTIONS] [-m static|queue] [-b SLOTS] [-q] URLFILE\n",
        prog);
    exit(2);
}

static int number_arg(const char *s, int lo, int hi, const char *what)
{
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < lo || v > hi) {
        fprintf(stderr, "%s must be a number from %d to %d\n", what, lo, hi);
        exit(2);
    }
    return (int)v;
}

int main(int argc, char *argv[])
{
    int nthreads = 1, max_conn = 0, slots = 4, use_queue = 0;
    const char *path = NULL;

    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "-t") == 0 && a + 1 < argc)
            nthreads = number_arg(argv[++a], 1, MAX_THREADS, "-t");
        else if (strcmp(argv[a], "-c") == 0 && a + 1 < argc)
            max_conn = number_arg(argv[++a], 1, MAX_THREADS, "-c");
        else if (strcmp(argv[a], "-b") == 0 && a + 1 < argc)
            slots = number_arg(argv[++a], 1, RING_MAX, "-b");
        else if (strcmp(argv[a], "-m") == 0 && a + 1 < argc) {
            a++;
            if (strcmp(argv[a], "queue") == 0)       use_queue = 1;
            else if (strcmp(argv[a], "static") == 0) use_queue = 0;
            else usage(argv[0]);
        }
        else if (strcmp(argv[a], "-q") == 0)
            quiet = 1;
        else if (argv[a][0] == '-' || path != NULL)
            usage(argv[0]);
        else
            path = argv[a];
    }
    if (path == NULL)
        usage(argv[0]);
    if (load_urls(path) != 0)
        return 2;

    provided_register_urls(urls[0], nurls, URL_LEN);
    provided_watch_ring(&buf);
    setup_limits(max_conn > 0 ? max_conn : MAX_THREADS);

    double t0 = now_seconds();
    if (use_queue) {
        buf_init(slots);
        run_queue(nthreads);
    } else {
        run_static(nthreads);
    }
    double elapsed = now_seconds() - t0;

    printf(FMT_SUMMARY, nurls, totals.ok, totals.failed, totals.bytes, elapsed);
    return (totals.ok == nurls) ? 0 : 1;
}
