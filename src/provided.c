/*
 * CSC 139 - Assignment 3: fetchall
 * provided.c - written for you. DO NOT EDIT.
 *
 * You are welcome to read it. Nothing in here does your synchronization for
 * you: the fake network is safe to call from many threads, but the ring is
 * deliberately not, and the totals in fetchall.c are yours to protect.
 */
#define _POSIX_C_SOURCE 200809L

#include "provided.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Bookkeeping read by the self-checks                                  */
/* ------------------------------------------------------------------ */
/*
 * If the environment variable FETCHALL_STATS names a file, these numbers are
 * written to it when the program exits. tests/run_tests.sh uses them to see
 * things the output alone cannot show, such as how many fetches were ever in
 * flight at the same moment.
 */
static atomic_int inflight;          /* fetches in progress right now     */
static atomic_int peak;              /* the most there ever were at once  */
static atomic_int calls;             /* fake_fetch() calls in total       */
static atomic_int threads_seen;      /* distinct threads that fetched     */
static atomic_int main_fetches;      /* fetches made by the main thread   */
static atomic_int unknown_urls;      /* fetches of a URL not from the file*/
static atomic_int ring_high;         /* fullest the watched ring got      */
static _Thread_local int this_thread_counted;

static const char  *url_base;        /* the url table in fetchall.c       */
static int          url_count;
static int          url_row;
static atomic_int  *url_hits;        /* how many times each was fetched   */
static pthread_t    main_thread;
static const struct ring *watched;

static void write_stats(void)
{
    const char *path = getenv("FETCHALL_STATS");
    if (path == NULL || *path == '\0')
        return;
    FILE *fp = fopen(path, "w");
    if (fp == NULL)
        return;

    int never = 0, twice = 0;
    for (int i = 0; i < url_count; i++) {
        int h = atomic_load(&url_hits[i]);
        if (h == 0) never++;
        if (h > 1)  twice++;
    }
    fprintf(fp, "calls=%d\n",            atomic_load(&calls));
    fprintf(fp, "peak=%d\n",             atomic_load(&peak));
    fprintf(fp, "threads=%d\n",          atomic_load(&threads_seen));
    fprintf(fp, "main_fetches=%d\n",     atomic_load(&main_fetches));
    fprintf(fp, "never_fetched=%d\n",    never);
    fprintf(fp, "fetched_twice=%d\n",    twice);
    fprintf(fp, "unknown=%d\n",          atomic_load(&unknown_urls));
    fprintf(fp, "inflight_at_exit=%d\n", atomic_load(&inflight));
    fprintf(fp, "ring_high=%d\n",        atomic_load(&ring_high));
    fclose(fp);
}

void provided_register_urls(const char *first_url, int count, int row_len)
{
    main_thread = pthread_self();
    url_base  = first_url;
    url_count = count;
    url_row   = row_len;
    url_hits  = calloc(count > 0 ? (size_t)count : 1, sizeof *url_hits);
    if (url_hits == NULL) {
        perror("calloc");
        exit(2);
    }
    atexit(write_stats);
}

void provided_watch_ring(const struct ring *r)
{
    watched = r;
}

/* ------------------------------------------------------------------ */
/* Time                                                                 */
/* ------------------------------------------------------------------ */
double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void sleep_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
        ;
}

/* ------------------------------------------------------------------ */
/* The fake network                                                     */
/* ------------------------------------------------------------------ */

/* Value of `name=` in the query string, or `fallback` if it is absent. */
static long param(const char *url, const char *name, long fallback)
{
    const char *q = strchr(url, '?');
    size_t n = strlen(name);
    while (q != NULL) {
        q++;                                    /* skip '?' or '&' */
        if (strncmp(q, name, n) == 0 && q[n] == '=')
            return strtol(q + n + 1, NULL, 10);
        q = strchr(q, '&');
    }
    return fallback;
}

static void note_start(const char *url)
{
    atomic_fetch_add(&calls, 1);

    if (!this_thread_counted) {
        this_thread_counted = 1;
        atomic_fetch_add(&threads_seen, 1);
    }
    if (url_hits != NULL && pthread_equal(pthread_self(), main_thread))
        atomic_fetch_add(&main_fetches, 1);

    /* Which line of the url file is this? */
    if (url_base != NULL && url >= url_base
        && url < url_base + (size_t)url_count * (size_t)url_row
        && (url - url_base) % url_row == 0) {
        atomic_fetch_add(&url_hits[(url - url_base) / url_row], 1);
    } else {
        atomic_fetch_add(&unknown_urls, 1);
    }

    int now = atomic_fetch_add(&inflight, 1) + 1;
    int old = atomic_load(&peak);
    while (now > old && !atomic_compare_exchange_weak(&peak, &old, now))
        ;
}

int fake_fetch(const char *url, long *bytes)
{
    note_start(url);

    long ms     = param(url, "ms", 0);
    long size   = param(url, "bytes", 0);
    long status = param(url, "status", 200);

    if (ms > 0)
        sleep_ms(ms);                 /* "waiting for the network" */

    atomic_fetch_sub(&inflight, 1);

    if (status != 200) {
        *bytes = 0;
        return -1;
    }
    *bytes = size;
    return 0;
}

/* ------------------------------------------------------------------ */
/* The ring (NOT thread-safe)                                           */
/* ------------------------------------------------------------------ */
void ring_init(struct ring *r, int capacity)
{
    if (capacity < 1 || capacity > RING_MAX) {
        fprintf(stderr, "ring_init: capacity must be 1..%d\n", RING_MAX);
        exit(2);
    }
    r->capacity = capacity;
    r->in = r->out = r->count = 0;
}

void ring_push(struct ring *r, int item)
{
    if (r->count >= r->capacity) {
        fprintf(stderr,
            "\nfetchall: ring_push() called on a FULL buffer (%d of %d slots used).\n"
            "          Something let a producer in without a free slot.\n"
            "          Check how buf_put() waits for an empty slot.\n",
            r->count, r->capacity);
        abort();
    }
    r->slots[r->in] = item;
    r->in = (r->in + 1) % r->capacity;
    r->count++;

    if (r == watched) {
        int now = r->count, old = atomic_load(&ring_high);
        while (now > old && !atomic_compare_exchange_weak(&ring_high, &old, now))
            ;
    }
}

int ring_pop(struct ring *r)
{
    if (r->count <= 0) {
        fprintf(stderr,
            "\nfetchall: ring_pop() called on an EMPTY buffer.\n"
            "          Something let a consumer in without an item to take.\n"
            "          Check how buf_take() waits for a full slot.\n");
        abort();
    }
    int item = r->slots[r->out];
    r->out = (r->out + 1) % r->capacity;
    r->count--;
    return item;
}
