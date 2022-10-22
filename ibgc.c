/*
 * Itty-Bitty Garbage Collector
 *
 * Automatic memory management for small-memory computers
 *
 * Copyright (c) 2022 Robbert Haarman
 *
 * SPDX-License-Identifier: MIT
 *
 * There is a 1-byte tag for every 4-byte cell.
 * The tags are stored at the top of memory.
 */

#define MEM_BYTES 0xc000
#define TAG_BASE ((MEM_BYTES >> 2) * 3)
#define ALLOC_BASE 0x0400

/* Tags consist of four bits: mpci.
 *
 * m is the mark bit, used to indicate if an object has been marked as
 * reachable. The memory manager only uses this bit for the first cell
 * of an object.
 *
 * p is the pointer bit. It is 1 if the cell holds a pointer, 0 if not.
 *
 * c is the continuation bit. It is 1 if the cell is followed by another
 * cell belonging to the same object. It is 0 for the last cell of an
 * object.
 *
 * i is an info bit. It is not used by the memory manager, but can be used
 * by the program to store a bit of information about a cell.
 */
enum { INFO_MASK = 1, CONT_MASK = 2, PTR_MASK = 4, MARK_MASK = 8 };

char mem[MEM_BYTES];

#define M(P) (*((cell_t*) (mem + (P))))

uint8_t mark_tag = 0;
addr_t alloc_top = TAG_BASE, freeptr = ALLOC_BASE;

static addr_t tagaddr(addr_t p) { return (p >> 2) + TAG_BASE; }
static uint8_t gettag(addr_t p) { return mem[tagaddr(p)]; }
static void settag(addr_t p, uint8_t t) { mem[tagaddr(p)] = t; }
static void mark(addr_t p) { settag(p, (gettag(p) & ~MARK_MASK) | mark_tag); }
static void unmark(addr_t p) { settag(p, (gettag(p) | MARK_MASK) ^ mark_tag); }
static int isfree(addr_t p) { return (gettag(p) & MARK_MASK) != mark_tag; }
static int hascont(addr_t p) { return (gettag(p) & CONT_MASK) != 0; }
static addr_t nextfree(addr_t p) { return M(p); }
static addr_t freelen(addr_t p) { return hascont(p) ? M(p + CELL_SZ) : 1; }

/**
 * Allocates ncells cells of memory and tags them with the given tag.
 *
 * @return the address of the first cell, or ADDR_MASK if allocation
 *   failed (no large enough contiguous span of free cells was found).
 */
static addr_t alloc(addr_t ncells, uint8_t tag) {
  addr_t len, p, next, prev = ADDR_MASK;

  /* Find >= ncells of contiguous free memory. */
  for (p = freeptr; p != ADDR_MASK; p = nextfree(p) & ADDR_MASK) {
    len = freelen(p);
    if (len >= ncells) break;
    prev = p;
  }

  if (p == ADDR_MASK) return p; /* Out of memory. */

  /* Remove the cells we found from the free list. */
  if (len == 1) {
    next = nextfree(p);
  } else {
    next = p + ncells * CELL_SZ;
    M(next) = nextfree(p);
    if (len > ncells + 1) {
      settag(next, gettag(next) | CONT_MASK);
      M(next + CELL_SZ) = len - ncells;
    }
  }
  if (prev == ADDR_MASK) freeptr = next;
  else M(prev) = next;

  /* Set the tag bytes for the newly allocated object. */
  settag(p, (tag & INFO_MASK) |
         (ncells > 1 ? CONT_MASK : 0) | (mark_tag ^ MARK_MASK));
  for (next = p + CELL_SZ, --ncells; ncells != 0; next += CELL_SZ, --ncells) {
    settag(next, ncells == 1 ? 0 : CONT_MASK);
  }
  return p;
}

/*
 * Reachability tracing algorithm.
 */
void gc_trace(addr_t p) {
  addr_t back = ADDR_MASK, tmp;

  /* Only process object if it is not already marked. */
  if (!isfree(p)) return;

  /* Objects are arranged in a graph which may contain cycles.
   * We avoid infinite looping by marking an object as soon as we
   * reach it. When we see an object that is already marked, we
   * don't process it again.
   *
   * When processing an object, we visit each of the object's cells
   * in turn. If the cell contains a pointer, we follow the pointer
   * and visit the object it points to. This makes the algorithm
   * recursive. We want to minimize the amount of memory we use
   * for the recursion, because it takes away from the memory the
   * program can use.
   *
   * We distinguish two cases:
   *
   *  a. If the cell we are currently processing is the last cell
   *     of the current object, we will not have to do any additional
   *     processing of the current object, so we can simply replace p
   *     with the pointer read from the cell.
   *
   *  b. If the current object has additional cells after the current
   *     one, we must continue processing at the next cell of the
   *     current object after we are done processing the pointed-to
   *     object. To do this, we maintain a value called "back" that
   *     points back at the cell we came from when we follow a
   *     pointer. This value is initially a sentinel value, ADDR_MASK.
   *     When we find a pointer in the cell pointed to by p, we:
   *
   *     1. Store the pointer in a temporary variable "tmp".
   *
   *     2. Store the value of back in the cell pointed to by p.
   *
   *     3. Set back to p. This will later allow us to restore the
   *        old value of back.
   *
   *     4. Set p to tmp, the pointer we read from the cell.
   *
   *     5. Process p.
   *
   *     6. If back has the special value ADDR_MASK, exit. We are done.
   *
   *     7. Read the value of the cell pointed to by back into tmp.
   *        This is the old value of back.
   *
   *     8. Store p into the cell pointed to by back. This restores
   *        the original value of that cell.
   *
   *     9. Point p at the cell after the cell pointed to by back.
   *
   *     10. Set back to tmp. This restores the original value of back.
   */
  for (;;) {
    /* printf("tracing %04x. val %04x tag %02x\n", */
    /*        p, M(p), gettag(p)); */
    /* Mark the object now. */
    mark(p);

    /* If the cell contains a pointer to an unmarked object, follow it. */
    /* if ((gettag(p) & PTR_MASK)) printf("points to free obj? %d\n", isfree(M(p))); */
    if ((gettag(p) & PTR_MASK) && isfree(M(p))) {
      /* Special case for last cell of an object. */
      if ((gettag(p) & CONT_MASK) == 0) {
        p = M(p);
        continue;
      }

      tmp = M(p);             /* 1. copy the pointer to tmp */
      M(p) = back;            /* 2. save back at p */
      back = p;               /* 3. set back to p */
      p = tmp;                /* 4. set p to pointer */
      continue;               /* 5. process object at p */
    }

    /* 6. At this point, if back is ADDR_MASK, we're done. */
    if (back == ADDR_MASK) break;

    /* Otherwise, return to processing the previous object. */
    tmp = M(back);              /* 7. read old value of back */
    M(back) = p;                /* 8. restore old cell value */
    p = back + CELL_SZ;         /* 9. point p at next cell */
    back = tmp;                 /* 10. restore old value of back */
  }
}

/** Return all unmarked objects to the free list. */
void gc_reclaim() {
  addr_t end, p = ALLOC_BASE, next_free = freeptr, prev_free = ADDR_MASK;

  for (; p < alloc_top; p = end) {
    /* printf("p %04x\n", p); */
    if (p == next_free) {
      /* Skip memory that is already on the free list. */
      prev_free = next_free;
      next_free = nextfree(next_free);
      end = p + freelen(p) * CELL_SZ;
      continue;
    }

    /* Determine where p ends. If p is followed by another unreachable
     * object, coalesce their lengths. */
    end = p;
    do {
      for (; gettag(end) & CONT_MASK; end += CELL_SZ);
      end += CELL_SZ;
      /* printf("end %04x\n", end); */
    } while (end != next_free && isfree(end) && isfree(p));

    if (isfree(p)) {
      if (next_free == freeptr) freeptr = p;
      if (end == next_free) {
        /* p ends at next_free. Coalesce. */
        M(p) = nextfree(next_free);
        settag(p, gettag(p) | CONT_MASK);
        M(p + CELL_SZ) = freelen(next_free) + (end - p) / CELL_SZ;
        end = next_free = M(p);
        /* printf("coalesced: %04x %04x(%u) next: %04x\n", p, M(p), M(p + CELL_SZ), end); */
      } else {
        /* p ends before next_free, create new free span. */
        M(p) = next_free;
        if (end > p + CELL_SZ) {
          M(p + CELL_SZ) = (end - p) / CELL_SZ;
          settag(p, gettag(p) | CONT_MASK);
        }
        /* printf("new free span: %04x %04x(%u)\n", */
        /*        p, M(p), freelen(p)); */
      }

      /* If there is a previous free span, we need to either coalesce
       * with it (if the new span starts just after it), or make sure
       * the previous span points at the new span. */
      /* printf("prev_free + freelen(%04x): %04x\n", */
      /*        prev_free, prev_free + freelen(prev_free)); */
      if (prev_free != ADDR_MASK) {
        if (p == prev_free + freelen(prev_free) * CELL_SZ) {
          /* Coalesce. */
          /* printf("M(%04x) = M(%04x): %04x\n", prev_free, p, M(p)); */
          M(prev_free) = M(p);
          M(prev_free + CELL_SZ) = freelen(prev_free) + freelen(p);
          settag(prev_free, gettag(prev_free) | CONT_MASK);
          p = prev_free;        /* Point p at beginning of free span */
        } else {
          /* Point previous span at new span. */
          M(prev_free) = p;
        }
      }
      prev_free = p;
    }
  }
}

void ibgc_init() {
  unmark(freeptr);
  settag(freeptr, gettag(freeptr) | CONT_MASK);
  M(freeptr) = ADDR_MASK;
  M(freeptr + CELL_SZ) = (alloc_top - ALLOC_BASE) / CELL_SZ;
}
