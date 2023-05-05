#include "uthread.h"

#include <unistd.h>
#include <stdint.h>
#include <string.h>

#define PAGE_SIZE 4096
#define HEADER_SIZE 8
#define MIN_HOLE_SIZE 16

#define SIZE_MASK 0xFFFFFFFFFFFFFFF8UL
#define ALLOC_BIT 0x0000000000000001UL
#define MINI_HOLE_BIT 0x0000000000000004UL
#define PREDECESSOR_ALLOC_BIT 0x0000000000000002UL

typedef struct Hole Hole;
struct Hole {
    uint64_t header;
    union {
        struct {
            Hole *next;
            Hole *prev;
        };
        char payload[0];
    };
};

/**
 * An global array of 9 lists of free holes
 * Each list contains all holes that fall
 * into a particular size class/ group:
 *
 * The holes, by their sizes, are grouped
 * as follows:
 *
 * class index |    size        |     Capacity (Payload)
 *       0     |    32          |     1-24
 *       1     |    33-64       |     25-56
 *       2     |    65-128      |     57-120
 *       3     |    129-256     |     121-248
 *       4     |    257-512     |     249-504
 *       5     |    513-1024    |     505-1016
 *       6     |    1025-2048   |     1017-2040
 *       7     |    2049-4096   |     2041-4088
 *       8     |    4097-inf    |     4089-inf
 *
 * Note that `size` column is somewhat misleading since
 * it groups the hole sizes into ranges of continuous
 * integer values while the size of the hole can only be
 * a multiple of 32 (the minnimal hole size)
 *
 */

#define FREE_LISTS_SIZE 10

Hole *free_lists[FREE_LISTS_SIZE] = {0};

// This pointer points to the last mini hole (the one before the head) in the
// circular list of free mini holes
Hole *tail = 0;

uint64_t heap_base = 0;

/**************************************************************************************
 *                                                                                    *
 *                                                                                    *
 *                                                                                    *
 *                                                                                    *
 *                                                                                    *
 *                                  Function Dec's                                    *
 *                                                                                    *
 *                                                                                    *
 *                                                                                    *
 *                                                                                    *
 *                                                                                    *
 **************************************************************************************
 */

uint64_t get_size(Hole *h);

void write_size(Hole *h, size_t size);

uint64_t get_footer_addr(Hole *h);

uint64_t get_successor_addr(Hole *h);

int is_alloc(Hole *h);

void set_alloc_bit(Hole *h);

void unset_alloc_bit(Hole *h);

int predecessor_is_alloc(Hole *h);

void set_predecessor_alloc_bit(Hole *h);

void unset_predecessor_alloc_bit(Hole *h);

void set_mini_hole_bit(Hole *h);

void unset_mini_hole_bit(Hole *h);

int is_mini_hole(Hole *h);

void announce_alloc_to_successor(Hole *h);

void announce_free_to_successor(Hole *h);

void copy_header_to_footer(Hole *h);

size_t align16(size_t to_be_aligned);

size_t align4k(size_t to_be_aligned);

void create_hole(Hole *h, size_t size, uint64_t flags);

int get_class_index(size_t size);

void free_list_insert(Hole *h);

void free_list_delete(Hole *h);

Hole *pack(Hole *h);

Hole *unpack(Hole *h);

void free_list_insert_mini(Hole *h);

void free_list_delete_mini(Hole *h);

Hole *find_fit(size_t size);

Hole *merge(Hole *h);

int malloc_init(void);

void *malloc(size_t size_req);

void free(void *ptr);

void *realloc(void *ptr, size_t size);

void *calloc(size_t nmemb, size_t size);

void check_hole(Hole *h, int flag);

size_t check_free_list(int flag);

void check_heap(int flag);

int mm_checkheap(int line);

/**************************************************************************************
 *                                                                                    *
 *                                                                                    *
 *                                                                                    *
 *                                                                                    *
 *                                                                                    *
 *                                  Implementation *
 *                                                                                    *
 *                                                                                    *
 *                                                                                    *
 *                                                                                    *
 *                                                                                    *
 **************************************************************************************
 */

int get_class_index(size_t size) {
    if (size <= 16)
        return 0;
    else if (size <= 32)
        return 1;
    else if (size <= 64)
        return 2;
    else if (size <= 128)
        return 3;
    else if (size <= 256)
        return 4;
    else if (size <= 512)
        return 5;
    else if (size <= 1024)
        return 6;
    else if (size <= 2048)
        return 7;
    else if (size <= 4096)
        return 8;
    else
        return 9;
}

Hole *pack(Hole *h) {
    return (Hole *)((uint64_t)h | MINI_HOLE_BIT);
}

Hole *unpack(Hole *h) {
    return (Hole *)((uint64_t)h->next & SIZE_MASK);
}

void free_list_insert_mini(Hole *h) {
    Hole *head = free_lists[0];
    if (!head) {
        tail = free_lists[0] = h;
        h->next =
            pack(h); // Create circular structure by having h points to itself
        tail = h;    // Keep track of the hole at the tail
    } else {
        h->next = pack(head);
        tail->next = pack(h);
        free_lists[0] = h;
    }
}

void free_list_delete_mini(Hole *h) {
    Hole *head = free_lists[0];
    if (unpack(h) == h)
        tail = free_lists[0] = NULL;
    else {
        Hole *pre = unpack(head);
        for (; pre != head && unpack(pre) != h; pre = unpack(pre))
            ;
        pre = (unpack(head) == h) ? head : pre;

        pre->next = pack(unpack(h));
        free_lists[0] = unpack(h); // Relocate head
        tail = pre;                // Relocate tail
    }
}

void free_list_insert(Hole *h) {
    int i = get_class_index(get_size(h));
    if (!i) {
        free_list_insert_mini(h);
        return;
    }
    Hole *head = free_lists[i];
    if (!head) {
        free_lists[i] = h;
        h->prev = h;
        h->next = h;
    } else {
        h->prev = head->prev;
        h->next = head;
        h->prev->next = h;
        h->next->prev = h;
        free_lists[i] = h;
    }
}

Hole *find_fit(size_t size) {
    // Search through all lists where hole size >= size
    // Fit must be found after searching through the next free list
    // if its is non-empty
    for (int i = get_class_index(size); i < FREE_LISTS_SIZE; i++)
        if (free_lists[i]) {
            if (!i) { // Mini hole list (zero-indexed)
                for (Hole *h = unpack(free_lists[i]); h != free_lists[i];
                     h = unpack(h))
                    if (get_size(h) >= size)
                        return h;
                if (get_size(free_lists[i]) >= size)
                    return free_lists[i];
            } else { // Normal hole lists
                for (Hole *h = free_lists[i]; h != free_lists[i]->prev;
                     h = h->next)
                    if (get_size(h) >= size)
                        return h;
                if (get_size(free_lists[i]->prev) >= size)
                    return free_lists[i]->prev;
            }
        }
    return NULL;
}

void free_list_delete(Hole *h) {
    int i = get_class_index(get_size(h));
    if (!i) {
        free_list_delete_mini(h);
        return;
    }
    if (h->next == h)
        free_lists[i] = NULL;
    else {
        h->prev->next = h->next;
        h->next->prev = h->prev;
        free_lists[i] = h->next; // Relocate head to the next hole
    }
}

Hole *merge(Hole *h) {
    if (!predecessor_is_alloc(h)) {
        uint64_t predecessor_footer = *(uint64_t *)((uint64_t)h - HEADER_SIZE);
        size_t predecessor_size = (predecessor_footer & MINI_HOLE_BIT)
                                      ? MIN_HOLE_SIZE
                                      : predecessor_footer & SIZE_MASK;
        Hole *predecessor = (Hole *)((uint64_t)h - predecessor_size);
        free_list_delete(predecessor);
        create_hole(predecessor, predecessor_size + get_size(h),
                    PREDECESSOR_ALLOC_BIT);
        h = predecessor;
    }
    Hole *successor = (Hole *)get_successor_addr(h);
    if (!is_alloc(successor)) {
        free_list_delete(successor);
        create_hole(h, get_size(successor) + get_size(h),
                    PREDECESSOR_ALLOC_BIT);
    }
    announce_free_to_successor(h);
    return h;
}

int malloc_init(void) {
    memset(free_lists, 0, sizeof(free_lists));

    // Grow the heap by one page
    heap_base = (uint64_t)sbrk(PAGE_SIZE);
    if (heap_base == (uint64_t)-1)
        return 0;

    // Create sentinal holes
    create_hole((Hole *)heap_base, 8, ALLOC_BIT | PREDECESSOR_ALLOC_BIT);
    create_hole((Hole *)(heap_base + PAGE_SIZE - HEADER_SIZE), 8, ALLOC_BIT);

    // Create the first hole, which speads across the entire heap area
    Hole *first_Hole = (Hole *)(heap_base + HEADER_SIZE);
    create_hole(
        first_Hole, PAGE_SIZE - HEADER_SIZE * 2,
        PREDECESSOR_ALLOC_BIT); // Subtract the size taken by two sentinals

    free_list_insert(first_Hole);

    return 1;
}

void *malloc(size_t size_req) {
    // Ignore zero-sized allocation requests
    if (!size_req)
        return NULL;

    // Initialzie the heap if uninitialized
    if (!heap_base)
        if (!malloc_init())
            return NULL;

    size_t size =
        align16(size_req + HEADER_SIZE); // Hole size should be 16-byte aligned
    // size = size < (size_t)32 ? (size_t)32 : size; // Minimal hole size is 32
    // bytes
    Hole *h = find_fit(size);

    // Not found (out of heap memory, should expend the heap)
    if (!h) {
        // Grow the heap by a multiple of 4K based on the required block size
        uint64_t break_addr = (uint64_t)sbrk((intptr_t)align4k(size));
        if (break_addr == (uint64_t)-1)
            return NULL;

        // Create a new sential hole at the top of the heap
        create_hole((Hole *)(break_addr + align4k(size) - HEADER_SIZE), 8,
                    ALLOC_BIT);

        // Overwrite the old sentinal hole
        Hole *old_sentinal = (Hole *)(break_addr - HEADER_SIZE);
        write_size(old_sentinal, align4k(size));
        unset_alloc_bit(old_sentinal);

        // Merge if the predessor of the sentinal block is free
        h = merge(old_sentinal);

        free_list_insert(h);
    }

    free_list_delete(h);

    // If the size left in the hole is sufficient for the creation of another
    // hole, we create a new hole to take over the space left
    size_t size_left = get_size(h) - size;
    if (get_size(h) - size >= MIN_HOLE_SIZE) {
        Hole *new_hole = (Hole *)((uint64_t)h + size);
        // Create a new hole and set its MINI_HOLE_BIT if its size equals 16
        // bytes
        create_hole(new_hole, size_left, 0);
        write_size(h, size); // Update the hole size after split
        // Add the induced hole into the free list
        free_list_insert(new_hole);
    }

    if (size == MIN_HOLE_SIZE)
        set_mini_hole_bit(h);
    set_alloc_bit(h);
    // Inform the successor that it has been allocated
    announce_alloc_to_successor(h);

    return h->payload;
}

void free(void *ptr) {
    if (!ptr)
        return;
    Hole *h = (Hole *)((uint64_t)ptr - HEADER_SIZE);
    if (!is_alloc(h))
        return;
    unset_alloc_bit(h);
    // Recreate footer
    copy_header_to_footer(h);
    h = merge(h);
    free_list_insert(h);
}

void *realloc(void *ptr, size_t size) {
    // If size == 0, then free block and return NULL
    if (!size) {
        free(ptr);
        return NULL;
    }

    // If ptr is NULL, then equivalent to malloc
    if (ptr == NULL) {
        return malloc(size);
    }

    // Pointer to the header
    Hole *h = (Hole *)((uint64_t)ptr - HEADER_SIZE);
    // Otherwise, proceed with reallocation
    void *new = malloc(size);
    // If malloc fails, the original block is left untouched
    if (!new)
        return NULL;

    // Relocate
    size_t payload_size = get_size(h) - HEADER_SIZE;
    // Size to copy should be the smaller one between the
    // old payload size and the payload size of the hole
    // to relocate data to
    memcpy(new, ptr, size < payload_size ? size : payload_size);

    // Free the old block
    free(ptr);

    return new;
}

void *calloc(size_t elements, size_t size) {
    if (!elements)
        return NULL;

    size_t asize = elements * size;

    if (asize / elements != size) {
        // Multiplication overflowed
        return NULL;
    }

    void *p = malloc(asize);
    if (!p)
        return NULL;

    memset(p, 0, asize);

    return p;
}

void check_hole(Hole *h, int flag) {
    size_t size = get_size(h);
    // Check payload alignment
    if ((uintptr_t)h->payload % 16 != 0 && size != 8)
        uprintf("Payload misaligned: %p\n", (void *)h->payload);
    // Check hole size
    if (size < MIN_HOLE_SIZE != 0 && size != 8)
        uprintf("Hole size too small: %ld\n", size);
    if (size % 16 != 0 && size != 8)
        uprintf("Hole size not a multiple of 16 bytes: %ld\n", size);
    // Check header-footer consistency for free holes
    uint64_t footer = *((uint64_t *)get_footer_addr(h));
    if (!is_alloc(h) && !is_mini_hole(h)) { // mini hole's footer is ptr!
        uint64_t size1 = (footer >> 4) << 4;
        if (size1 != size)
            uprintf("Header and footer inconsistent\n");
    }
    // Check mini hole validity
    if (is_mini_hole(h)) {
        if (size != MIN_HOLE_SIZE)
            uprintf("Incorrect mini hole size\n");
        if (!(footer & MINI_HOLE_BIT))
            uprintf("Mini hole footer bit unset\n");
    }
    // Print hole info if flag is set
    if (flag)
        uprintf("%s at %p size %ld pre status %d my status %d %s\n",
               get_size(h) != 8 ? "Hole" : "Sentinal", (void *)h, get_size(h),
               predecessor_is_alloc(h), is_alloc(h),
               is_mini_hole(h) ? "(mini_hole)" : "");
}

size_t check_free_list(int flag) {
    size_t free = 0;
    for (int i = 0; i < FREE_LISTS_SIZE; i++) {
        if (flag)
            uprintf("Class %d:", i);
        if (free_lists[i]) {
            if (!i) {
                for (Hole *h = unpack(free_lists[i]); h != free_lists[i];
                     h = unpack(h), free++)
                    if (flag)
                        uprintf("%ld ", get_size(h));
                if (flag)
                    uprintf("%ld", get_size(free_lists[i]));
            } else {
                for (Hole *h = free_lists[i]; h != free_lists[i]->prev;
                     h = h->next, free++)
                    if (flag)
                        uprintf("%ld ", get_size(h));
                if (flag)
                    uprintf("%ld", get_size(free_lists[i]->prev));
            }
            free++;
        }
        if (flag)
            uprintf("\n");
    }
    return free;
}

void check_heap(int flag) {
    Hole *h = (Hole *)heap_base;
    size_t sum = 0;
    size_t total = 0;
    size_t alloc = 0;
    size_t free = 0;
    for (size_t i = 0;;
         sum += get_size(h), h = (Hole *)get_successor_addr(h), i++, total++) {
        check_hole(h, flag);
        if (get_size(h) == 8 && i != 0) {
            sum += get_size(h);
            total++;
            alloc++;
            break;
        }
        if (is_alloc(h))
            alloc++;
        else
            free++;
    }
    if (flag) {
        uprintf("--------Heap Stat--------\n");
        uprintf("Summary:\n");
        uprintf("Hole size sum:  %lu\n", sum);
        uprintf("Heap size:      %lu\n", (size_t)sbrk(0) - heap_base);
        uprintf("Details:\n");
        uprintf("    Type        Count\n");
        uprintf("-------------------------\n");
        uprintf("    Total       %lu\n", total);
        uprintf("    Free        %lu\n", free);
        uprintf("  Allocated     %lu\n", alloc);
    }
    size_t n = check_free_list(flag);
    if (n != free)
        uprintf("Confliting free block count %ld(free lists) vs %ld(heap)\n",
               free, n);
}

int mm_checkheap(int line) {
    if (!heap_base)
        return 1;
    check_heap(line);
    return 1;
}

/**************************************************************************************
 *                                                                                    *
 *                                                                                    *
 *                                                                                    *
 *                                                                                    *
 *                                                                                    *
 *                           Helper Function Def's *
 *                                                                                    *
 *                                                                                    *
 *                                                                                    *
 *                                                                                    *
 *                                                                                    *
 **************************************************************************************
 */

uint64_t get_size(Hole *h) {
    return (uint64_t)(h->header & SIZE_MASK);
}

void write_size(Hole *h, size_t size) {
    // Clear the old szie
    h->header &= ~SIZE_MASK;
    // Write the new size
    h->header |= (size & SIZE_MASK);
}

uint64_t get_footer_addr(Hole *h) {
    return (uint64_t)((uint64_t)h + get_size(h) - HEADER_SIZE);
}

uint64_t get_successor_addr(Hole *h) {
    return (uint64_t)((uint64_t)h + get_size(h));
}

int is_alloc(Hole *h) {
    return (int)(h->header & ALLOC_BIT);
}

void set_alloc_bit(Hole *h) {
    h->header |= ALLOC_BIT;
}

void unset_alloc_bit(Hole *h) {
    h->header &= ~ALLOC_BIT;
}

int predecessor_is_alloc(Hole *h) {
    return (int)(h->header & PREDECESSOR_ALLOC_BIT);
}

void set_predecessor_alloc_bit(Hole *h) {
    h->header |= PREDECESSOR_ALLOC_BIT;
}

void unset_predecessor_alloc_bit(Hole *h) {
    h->header &= ~PREDECESSOR_ALLOC_BIT;
}

void set_mini_hole_bit(Hole *h) {
    h->header |= MINI_HOLE_BIT;
    uint64_t *footer = (uint64_t *)get_footer_addr(h);
    *footer |= MINI_HOLE_BIT;
}

void unset_mini_hole_bit(Hole *h) {
    h->header &= ~MINI_HOLE_BIT;
}

int is_mini_hole(Hole *h) {
    return h->header & MINI_HOLE_BIT;
}

void announce_alloc_to_successor(Hole *h) {
    Hole *suc = (Hole *)((uint64_t)h + get_size(h));
    suc->header |= PREDECESSOR_ALLOC_BIT;
}

void announce_free_to_successor(Hole *h) {
    Hole *suc = (Hole *)((uint64_t)h + get_size(h));
    suc->header &= ~PREDECESSOR_ALLOC_BIT;
}

void copy_header_to_footer(Hole *h) {
    *(uint64_t *)get_footer_addr(h) = h->header;
}

void create_hole(Hole *h, size_t size, uint64_t flags) {
    h->header = size;
    h->header = h->header | flags;
    if (size == MIN_HOLE_SIZE)
        set_mini_hole_bit(h);
    *(uint64_t *)get_footer_addr(h) = h->header;
}

size_t align16(size_t to_be_aligned) {
    return to_be_aligned + 15 & 0xFFFFFFFFFFFFFFF0;
}

size_t align4k(size_t to_be_aligned) {
    return to_be_aligned + 4095 & 0xFFFFFFFFFFFFF000;
}
