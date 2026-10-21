/*
 * Distort Me - minimal portable thread pool for row ranges.
 * SPDX-License-Identifier: MIT
 */
#include "dm_threads.h"

#include <stdlib.h>

#define DM_MAX_THREADS 64
#define DM_ROWS_PER_CHUNK 16

/* Workers pull chunks of rows from a shared counter so fast threads
 * take over the work of slow ones. */
typedef struct pool {
    dm_rows_fn    fn;
    void         *arg;
    int           rows;
    volatile long next;
} pool;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static long take_chunk(pool *p)
{
    return InterlockedExchangeAdd(&p->next, DM_ROWS_PER_CHUNK);
}

int dm_cpu_count(void)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
}
#else
#include <pthread.h>
#include <unistd.h>

static long take_chunk(pool *p)
{
    return __sync_fetch_and_add(&p->next, DM_ROWS_PER_CHUNK);
}

int dm_cpu_count(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
}
#endif

static void work(pool *p)
{
    for (;;) {
        long y = take_chunk(p);
        if (y >= p->rows)
            break;
        long end = y + DM_ROWS_PER_CHUNK;
        p->fn(p->arg, (int)y, (int)(end < p->rows ? end : p->rows));
    }
}

#ifdef _WIN32
static DWORD WINAPI worker(LPVOID arg)
{
    work((pool *)arg);
    return 0;
}
#else
static void *worker(void *arg)
{
    work((pool *)arg);
    return NULL;
}
#endif

void dm_parallel_rows(int rows, int threads, dm_rows_fn fn, void *arg)
{
    pool p;
    p.fn   = fn;
    p.arg  = arg;
    p.rows = rows;
    p.next = 0;

    if (threads <= 0)
        threads = dm_cpu_count();
    if (threads > DM_MAX_THREADS)
        threads = DM_MAX_THREADS;
    if (threads > (rows + DM_ROWS_PER_CHUNK - 1) / DM_ROWS_PER_CHUNK)
        threads = (rows + DM_ROWS_PER_CHUNK - 1) / DM_ROWS_PER_CHUNK;

    /* the calling thread is worker number 0 */
    int started = 0;
#ifdef _WIN32
    HANDLE handles[DM_MAX_THREADS];
    for (int i = 1; i < threads; i++) {
        handles[started] = CreateThread(NULL, 0, worker, &p, 0, NULL);
        if (handles[started])
            started++;
    }
    work(&p);
    if (started)
        WaitForMultipleObjects((DWORD)started, handles, TRUE, INFINITE);
    for (int i = 0; i < started; i++)
        CloseHandle(handles[i]);
#else
    pthread_t ids[DM_MAX_THREADS];
    for (int i = 1; i < threads; i++) {
        if (pthread_create(&ids[started], NULL, worker, &p) == 0)
            started++;
    }
    work(&p);
    for (int i = 0; i < started; i++)
        pthread_join(ids[i], NULL);
#endif
}
