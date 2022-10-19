/*
 * Tests for the Itty-Bitty Garbage Collector
 *
 * Copyright (c) 2022 Robbert Haarman
 *
 * SPDX-License-Identifier: MIT
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

typedef int32_t cell_t;
typedef uint16_t addr_t;

#define ADDR_MASK 0xffff
#define CELL_SZ sizeof(cell_t)

#include "ibgc.c"

static void show_freelist() {
  addr_t l, n = 0, p = freeptr;
  char *sep = "";

  for (; p < alloc_top; p = nextfree(p) & ADDR_MASK) {
    l = freelen(p);
    n += l;
    printf("%s%04x(%u)", sep, p, l);
    sep = ",";
  }
  printf(" total: %lu\n", (unsigned long) n);
}

#define SETPTR(A, V) do {                       \
    M(A) = (cell_t) (V);                        \
    settag(A, gettag(A) | PTR_MASK);            \
  } while (0)

void reset_ibgc() {
  freeptr = ALLOC_BASE;
  mark_tag = 0;
  ibgc_init();
}

int main(int argc, char *argv[]) {
  addr_t a, b, c, d;

  printf("init\n");
  ibgc_init();
  show_freelist();

  printf("\nalloc 1\n");
  reset_ibgc();
  alloc(1, 0);
  show_freelist();

  printf("\nreclaim none\n");
  reset_ibgc();
  a = alloc(2, 0);
  b = alloc(1, 0);
  c = alloc(1, 0);
  d = alloc(1, 0);
  SETPTR(a, b);
  SETPTR(b, c);
  SETPTR(a + CELL_SZ, d);
  printf("tags: %02x %02x %02x %02x %02x\n",
         gettag(a), gettag(a + CELL_SZ), gettag(b), gettag(c), gettag(d));
  gc_trace(a);
  printf("tags: %02x %02x %02x %02x %02x\n",
         gettag(a), gettag(a + CELL_SZ), gettag(b), gettag(c), gettag(d));
  gc_reclaim();
  show_freelist();

  printf("\nreclaim mid\n");
  reset_ibgc();
  a = alloc(2, 0);
  b = alloc(1, 0);
  c = alloc(1, 0);
  d = alloc(1, 0);
  SETPTR(a, b);
  SETPTR(a + CELL_SZ, d);
  printf("tags: %02x %02x %02x %02x %02x\n",
         gettag(a), gettag(a + CELL_SZ), gettag(b), gettag(c), gettag(d));
  gc_trace(a);
  printf("tags: %02x %02x %02x %02x %02x\n",
         gettag(a), gettag(a + CELL_SZ), gettag(b), gettag(c), gettag(d));
  gc_reclaim();
  show_freelist();

  printf("\nreclaim coalesce after\n");
  reset_ibgc();
  a = alloc(2, 0);
  b = alloc(1, 0);
  c = alloc(1, 0);
  d = alloc(1, 0);
  SETPTR(a, b);
  SETPTR(b, c);
  printf("tags: %02x %02x %02x %02x %02x\n",
         gettag(a), gettag(a + CELL_SZ), gettag(b), gettag(c), gettag(d));
  gc_trace(a);
  printf("tags: %02x %02x %02x %02x %02x\n",
         gettag(a), gettag(a + CELL_SZ), gettag(b), gettag(c), gettag(d));
  gc_reclaim();
  show_freelist();

  printf("\nreclaim coalesce before\n");
  reset_ibgc();
  a = alloc(2, 0);
  b = alloc(1, 0);
  c = alloc(1, 0);
  d = alloc(1, 0);
  SETPTR(a, b);
  SETPTR(b, c);
  SETPTR(c, d);
  printf("tags: %02x %02x %02x %02x %02x\n",
         gettag(a), gettag(a + CELL_SZ), gettag(b), gettag(c), gettag(d));
  gc_trace(b);
  printf("tags: %02x %02x %02x %02x %02x\n",
         gettag(a), gettag(a + CELL_SZ), gettag(b), gettag(c), gettag(d));
  show_freelist();
  gc_reclaim();
  mark_tag ^= MARK_MASK;
  show_freelist();
  gc_trace(c);
  printf("tags: %02x %02x %02x %02x %02x\n",
         gettag(a), gettag(a + CELL_SZ), gettag(b), gettag(c), gettag(d));
  gc_reclaim();
  show_freelist();

  printf("\nreclaim coalesce both\n");
  reset_ibgc();
  a = alloc(2, 0);
  b = alloc(1, 0);
  c = alloc(1, 0);
  SETPTR(a, b);
  gc_trace(b);
  printf("tags: %02x %02x %02x %02x\n",
         gettag(a), gettag(a + CELL_SZ), gettag(b), gettag(c));
  gc_reclaim();
  mark_tag ^= MARK_MASK;
  show_freelist();
  gc_reclaim();
  show_freelist();

  return 0;
}
