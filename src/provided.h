/*
 * CSC 139 - Assignment 3: fetchall
 * provided.h - the pieces that are written for you. DO NOT EDIT.
 *
 * Three things live here:
 *
 *   1. fake_fetch()   A pretend network. It "downloads" a URL by sleeping for
 *                     as long as the URL says, then reporting a size. No real
 *                     network is touched, so every run is repeatable.
 *
 *   2. struct ring    A plain circular buffer of ints, with ring_push() and
 *                     ring_pop(). It is NOT thread-safe on purpose: making it
 *                     safe is your job in stage 4. It stops the program with
 *                     a clear message if you push into a full buffer or pop
 *                     from an empty one.
 *
 *   3. now_seconds()  A clock for measuring elapsed time.
 *
 * The self-checks also use some bookkeeping inside provided.c (how many
 * fetches were in flight at once, which threads made them). You never need
 * to call any of that yourself.
 */
#ifndef CSC139_PROVIDED_H
#define CSC139_PROVIDED_H

/* ------------------------------------------------------------------ */
/* 1. The fake network                                                  */
/* ------------------------------------------------------------------ */
/*
 * A URL carries its own behaviour in its query string:
 *
 *     http://csc139.test/img/07.png?ms=250&bytes=48000
 *     http://csc139.test/gone.html?ms=100&status=404
 *
 *   ms=N       the fetch takes N milliseconds (default 0)
 *   bytes=N    a successful fetch delivers N bytes (default 0)
 *   status=N   the server answers N; anything but 200 is a failure
 *              (default 200)
 *
 * fake_fetch() BLOCKS for ms milliseconds, the way a real download blocks
 * while it waits for the network. A thread blocked in it is asleep: it uses
 * no CPU while it waits.
 *
 * Returns 0 on success and stores the size in *bytes.
 * Returns -1 on failure (status not 200) and stores 0 in *bytes.
 *
 * It is safe to call from many threads at once.
 */
int fake_fetch(const char *url, long *bytes);

/* ------------------------------------------------------------------ */
/* 2. A circular buffer of ints (NOT thread-safe)                       */
/* ------------------------------------------------------------------ */
#define RING_MAX 64

struct ring {
    int slots[RING_MAX];
    int capacity;     /* how many slots are in use: 1 .. RING_MAX        */
    int in;           /* where the next push goes                         */
    int out;          /* where the next pop comes from                    */
    int count;        /* how many items are in the buffer right now       */
};

/* Empty the ring and give it room for `capacity` items. */
void ring_init(struct ring *r, int capacity);

/* Add one item at the back. Stops the program if the ring is full. */
void ring_push(struct ring *r, int item);

/* Remove and return the item at the front. Stops the program if the ring
 * is empty. */
int ring_pop(struct ring *r);

/* ------------------------------------------------------------------ */
/* 3. Time                                                              */
/* ------------------------------------------------------------------ */
/* Seconds since some fixed moment. Only the DIFFERENCE between two calls
 * means anything: end - start = elapsed. */
double now_seconds(void);

/* ------------------------------------------------------------------ */
/* Bookkeeping for the self-checks (main() calls these; you do not)     */
/* ------------------------------------------------------------------ */
void provided_register_urls(const char *first_url, int count, int row_len);
void provided_watch_ring(const struct ring *r);

#endif
