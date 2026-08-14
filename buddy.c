#include "buddy.h"
#include <stdlib.h>
#include <string.h>

#define NULLP ((void *)0)
#define PAGE_SIZE 4096
#define MAXRANK 16

static void *g_base = NULLP;
static int g_pgcount = 0;   /* valid page count */
static int g_total = 0;     /* total pages managed (power of two) */
static int g_top_rank = 0;  /* rank of the whole managed region */

static int *block_rank = NULLP;    /* rank of the block that currently owns this page */
static unsigned char *is_allocated = NULLP;
static unsigned char *is_free = NULLP;
static int *free_prev = NULLP, *free_next = NULLP;
static int free_head[MAXRANK + 1];
static int free_count_arr[MAXRANK + 1];

static int ilog2i(int x) {
    int r = 0;
    while (x > 1) { x >>= 1; r++; }
    return r;
}

static int next_pow2i(int x) {
    int p = 1;
    while (p < x) p <<= 1;
    return p;
}

static void list_remove(int off, int rank) {
    int p = free_prev[off], n = free_next[off];
    if (p >= 0) free_next[p] = n; else free_head[rank] = n;
    if (n >= 0) free_prev[n] = p;
    free_count_arr[rank]--;
}

static void list_push(int off, int rank) {
    free_prev[off] = -1;
    free_next[off] = free_head[rank];
    if (free_head[rank] >= 0) free_prev[free_head[rank]] = off;
    free_head[rank] = off;
    free_count_arr[rank]++;
}

static void mark_block(int off, int size, int rank, int allocated) {
    int i;
    for (i = off; i < off + size; i++) {
        block_rank[i] = rank;
        is_allocated[i] = allocated ? 1 : 0;
        is_free[i] = allocated ? 0 : 1;
    }
}

int init_page(void *p, int pgcount) {
    int r, off, remaining;

    if (pgcount <= 0 || p == NULLP) return -EINVAL;

    g_base = p;
    g_pgcount = pgcount;
    g_total = next_pow2i(pgcount);
    g_top_rank = ilog2i(g_total) + 1;

    if (block_rank) free(block_rank);
    if (is_allocated) free(is_allocated);
    if (is_free) free(is_free);
    if (free_prev) free(free_prev);
    if (free_next) free(free_next);

    block_rank = (int *)malloc(sizeof(int) * g_total);
    is_allocated = (unsigned char *)malloc(g_total);
    is_free = (unsigned char *)malloc(g_total);
    free_prev = (int *)malloc(sizeof(int) * g_total);
    free_next = (int *)malloc(sizeof(int) * g_total);

    memset(is_allocated, 0, g_total);
    memset(is_free, 0, g_total);
    memset(block_rank, 0, sizeof(int) * g_total);

    for (r = 0; r <= MAXRANK; r++) {
        free_head[r] = -1;
        free_count_arr[r] = 0;
    }

    /* decompose [0, pgcount) into maximal aligned power-of-two free blocks */
    off = 0;
    remaining = pgcount;
    while (remaining > 0) {
        int size = 1;
        while ((size * 2 <= remaining) && (off % (size * 2) == 0)) size *= 2;
        int rank = ilog2i(size) + 1;
        mark_block(off, size, rank, 0);
        list_push(off, rank);
        off += size;
        remaining -= size;
    }

    return OK;
}

void *alloc_pages(int rank) {
    int size, r, off;

    if (rank < 1 || rank > MAXRANK) return ERR_PTR(-EINVAL);
    if (g_base == NULLP) return ERR_PTR(-EINVAL);

    size = 1 << (rank - 1);

    r = rank;
    while (r <= MAXRANK && free_count_arr[r] == 0) r++;
    if (r > MAXRANK) return ERR_PTR(-ENOSPC);

    off = free_head[r];
    list_remove(off, r);

    while (r > rank) {
        r--;
        {
            int half = 1 << (r - 1);
            int right_off = off + half;
            mark_block(right_off, half, r, 0);
            list_push(right_off, r);
        }
    }

    mark_block(off, size, rank, 1);
    return (void *)((char *)g_base + (long)off * PAGE_SIZE);
}

int return_pages(void *p) {
    long diff, offL;
    int off, rank;

    if (g_base == NULLP || p == NULLP) return -EINVAL;

    diff = (char *)p - (char *)g_base;
    if (diff < 0) return -EINVAL;
    if (diff % PAGE_SIZE != 0) return -EINVAL;

    offL = diff / PAGE_SIZE;
    if (offL < 0 || offL >= g_pgcount) return -EINVAL;

    off = (int)offL;
    if (!is_allocated[off]) return -EINVAL;

    rank = block_rank[off];
    {
        int size = 1 << (rank - 1);
        if (off % size != 0) return -EINVAL;
    }

    while (rank < g_top_rank) {
        int size2 = 1 << (rank - 1);
        int buddy = off ^ size2;
        if (buddy < 0 || buddy >= g_total) break;
        if (!is_free[buddy] || block_rank[buddy] != rank) break;
        list_remove(buddy, rank);
        off = (off < buddy) ? off : buddy;
        rank += 1;
    }

    mark_block(off, 1 << (rank - 1), rank, 0);
    list_push(off, rank);

    return OK;
}

int query_ranks(void *p) {
    long diff, offL;

    if (g_base == NULLP || p == NULLP) return -EINVAL;

    diff = (char *)p - (char *)g_base;
    if (diff < 0 || diff % PAGE_SIZE != 0) return -EINVAL;

    offL = diff / PAGE_SIZE;
    if (offL < 0 || offL >= g_pgcount) return -EINVAL;

    return block_rank[(int)offL];
}

int query_page_counts(int rank) {
    if (rank < 1 || rank > MAXRANK) return -EINVAL;
    return free_count_arr[rank];
}
