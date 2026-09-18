/**
 * cppic_runtime.c - backing store for the host-sim runtime plus a small
 * self-contained heap used to implement C++ `new` / `delete`.
 *
 * The heap allocator (cppic_malloc / cppic_free) is compiled in EVERY
 * build - host simulator and SDCC/PIC18 alike - so the C lowered by the
 * transpiler always links against it.  It is a first-fit free-list
 * allocator over a static buffer; no libc dependency, deterministic
 * behaviour, ideal for a bare-metal 8-bit target.
 *
 * The host simulator part (emulated SFR cells + a main() that drives the
 * emitted setup()/loop() pair) is only compiled when CPPIC_HOST_SIM is
 * defined.  The simulator executes loop() RUNS times (default 4) and
 * returns a status derived from whether the sketch ever wrote a non-zero
 * value to PORTB, so automated tests can assert the sketch actually
 * exercised its output.
 *
 * Compile the host simulator with:
 *   cc -DCPPIC_HOST_SIM [-DRUNS=<n>] -o app app.c cppic_runtime.c
 */

#include <stdint.h>

/* ------------------------------------------------------------------- */
/* Heap                                                                 */
/* ------------------------------------------------------------------- */

#ifndef CPPIC_HEAP_SIZE
#define CPPIC_HEAP_SIZE 512
#endif

typedef struct cppic_heap_block {
    unsigned int size;                 /* usable payload bytes (aligned) */
    struct cppic_heap_block* next;     /* free-list link (valid when free) */
} cppic_heap_block;

/* Aligned storage for the whole arena. */
static union {
    cppic_heap_block hdr;
    unsigned char data[CPPIC_HEAP_SIZE];
} cppic_heap;

static cppic_heap_block* cppic_free_list = 0;

/* Branchless-ish power-of-two alignment to 4 bytes (fits 8-bit targets). */
static unsigned int cppic_align4(unsigned int n) {
    return (n + 3u) & ~3u;
}

void* cppic_malloc(unsigned int size) {
    cppic_heap_block* b;
    cppic_heap_block* prev = 0;
    unsigned int need;

    if (cppic_free_list == 0) {
        cppic_free_list = &cppic_heap.hdr;
        cppic_free_list->size =
            sizeof(cppic_heap.data) - (unsigned int)sizeof(cppic_heap_block);
        cppic_free_list->next = 0;
    }

    need = cppic_align4(size);
    if (need < sizeof(cppic_heap_block) + 4u) need = sizeof(cppic_heap_block) + 4u;

    for (b = cppic_free_list; b; prev = b, b = b->next) {
        if (b->size >= need) {
            if (b->size >= need + sizeof(cppic_heap_block) + 4u) {
                cppic_heap_block* rest = (cppic_heap_block*)
                    ((unsigned char*)(b + 1) + need);
                rest->size = b->size - need - (unsigned int)sizeof(cppic_heap_block);
                rest->next = b->next;
                b->size = need;
                b->next = rest; /* rest keeps the tail of the free list */
            }
            if (prev) prev->next = b->next;
            else cppic_free_list = b->next;
            return (void*)(b + 1);
        }
    }
    return 0; /* out of memory */
}

void cppic_free(void* p) {
    cppic_heap_block* b;
    cppic_heap_block* n;
    cppic_heap_block* prev;

    if (!p) return;

    b = (cppic_heap_block*)p - 1;

    /* insert into the free list, keeping it sorted by address */
    prev = 0;
    n = cppic_free_list;
    while (n && n < b) { prev = n; n = n->next; }

    b->next = n;
    if (prev) prev->next = b;
    else cppic_free_list = b;

    /* coalesce with the next neighbour */
    if (n && (unsigned char*)b + sizeof *b + b->size == (unsigned char*)n) {
        b->size += sizeof *b + n->size;
        b->next = n->next;
    }
    /* coalesce with the previous neighbour */
    if (prev &&
        (unsigned char*)prev + sizeof *prev + prev->size == (unsigned char*)b) {
        prev->size += sizeof *prev + b->size;
        prev->next = b->next;
    }
}

/* ------------------------------------------------------------------- */
/* Host simulator (only when CPPIC_HOST_SIM is defined)                 */
/* ------------------------------------------------------------------- */

#if defined(CPPIC_HOST_SIM)

#include <stdlib.h> /* getenv */

/* Emulated SFR memory cells (see cppic_runtime.h for the macros that
 * map PORTB/TRISB/... onto these). */
uint8_t cppic_PORTB = 0;
uint8_t cppic_TRISB = 0xFF;
uint8_t cppic_PORTA = 0;
uint8_t cppic_TRISA = 0xFF;
uint8_t cppic_PORTC = 0;
uint8_t cppic_TRISC = 0xFF;

/* Number of loop() iterations the simulator should execute before
 * stopping.  Default conservatively to a few; --run / tests override. */
#ifndef RUNS
#define RUNS 4
#endif

/* Prototypes injected by the transpiler. */
void setup(void);
void loop(void);

int main(void) {
    int i;
    uint8_t touched = 0;

    setup();
    if (getenv("CPPIC_HOST_ONCE")) {
        /* Single-shot: call loop() once and report what it produced. */
        loop();
        return cppic_PORTB ? 0 : 1;
    }
    for (i = 0; i < RUNS; ++i) {
        loop();
        touched |= cppic_PORTB;
    }
    /* Test convention: exit 0 iff the sketch wrote a non-zero PORTB. */
    return touched ? 0 : 1;
}

#endif /* CPPIC_HOST_SIM */