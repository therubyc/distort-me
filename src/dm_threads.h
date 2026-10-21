/*
 * Distort Me - minimal portable "split rows across threads" helper.
 * Uses Win32 threads on Windows and pthreads everywhere else.
 * SPDX-License-Identifier: MIT
 */
#ifndef DM_THREADS_H
#define DM_THREADS_H

/* Called on a worker thread for rows [y_begin, y_end). */
typedef void (*dm_rows_fn)(void *arg, int y_begin, int y_end);

/* Run `fn` over `rows` rows using `threads` workers (0 = all cores).
 * Blocks until all rows are done. Falls back to the calling thread if
 * threads can't be created. */
void dm_parallel_rows(int rows, int threads, dm_rows_fn fn, void *arg);

int dm_cpu_count(void);

#endif /* DM_THREADS_H */
