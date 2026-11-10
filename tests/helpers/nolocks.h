/*
 * Used only by `make race`. It is force-included ahead of fetchall.c and
 * turns every pthread_mutex_lock()/pthread_mutex_unlock() into a call that
 * does nothing, so you can see what your program does WITHOUT its locks.
 * Never include this yourself.
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
static inline int csc139_no_lock(pthread_mutex_t *m) { (void)m; return 0; }
#define pthread_mutex_lock(m)   csc139_no_lock(m)
#define pthread_mutex_unlock(m) csc139_no_lock(m)
