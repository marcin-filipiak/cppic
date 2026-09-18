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
#include "cppic_runtime.h"

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
/* String                                                               */
/* ------------------------------------------------------------------- */

static unsigned short cppic_strlen(const char* s) {
    unsigned short n = 0;
    if (s) {
        while (s[n]) ++n;
    }
    return n;
}

static void cppic_strset(char* d, const char* s, unsigned short n) {
    unsigned short i;
    for (i = 0; i < n; ++i) d[i] = s[i];
    d[n] = 0;
}

static void cppic_string_release(CppicString* s) {
    if (s && s->own) {
        cppic_free(s->data);
        s->data = 0;
        s->own = 0;
    }
}

/* Make sure `s` owns a buffer of at least `needed` writable bytes.
 * Returns 0 when the heap is exhausted (s is left untouched). */
static unsigned char cppic_string_reserve(CppicString* s, unsigned short needed) {
    char* nb;
    if (s->own && s->cap >= needed) return 1;
    nb = (char*)cppic_malloc((unsigned int)needed + 1u);
    if (!nb) return 0;
    if (s->data && s->len) cppic_strset(nb, s->data, s->len);
    nb[s->len] = 0;
    cppic_string_release(s);
    s->data = nb;
    s->cap = needed;
    s->own = 1;
    return 1;
}

void cppic_string_set(CppicString* s, const char* lit) {
    unsigned short n = cppic_strlen(lit);
    if (!s) return;
    if (n == 0) {
        cppic_string_release(s);
        s->len = 0;
        return;
    }
    if (!cppic_string_reserve(s, n)) return;
    cppic_strset(s->data, lit, n);
    s->len = n;
}

void cppic_string_copy(CppicString* d, const CppicString* src) {
    if (!d || !src) return;
    if (d == src) return;
    if (d->data == src->data && d->own == src->own) return;
    if (src->len == 0) {
        cppic_string_set(d, "");
        return;
    }
    if (!cppic_string_reserve(d, src->len)) return;
    cppic_strset(d->data, src->data, src->len);
    d->len = src->len;
}

void cppic_string_append(CppicString* d, const CppicString* src) {
    unsigned short sn, need, dn;
    char* tmp;
    if (!d || !src || src->len == 0) return;
    sn = src->len;
    dn = d->len;
    need = (unsigned short)(dn + sn);
    if (!(d->own && d->cap >= need)) {
        /* guard against src aliasing d's buffer */
        if (src->data >= d->data && src->data < d->data + dn) {
            tmp = (char*)cppic_malloc((unsigned int)sn + 1u);
            if (!tmp) return;
            cppic_strset(tmp, src->data, sn);
            cppic_string_reserve(d, need);
            if (!(d->own && d->cap >= need)) { cppic_free(tmp); return; }
        } else {
            if (!cppic_string_reserve(d, need)) return;
            tmp = 0;
        }
    } else {
        tmp = 0;
    }
    cppic_strset(d->data + dn, tmp ? tmp : src->data, sn);
    d->len = need;
    if (tmp) cppic_free(tmp);
}

void cppic_string_append_lit(CppicString* d, const char* lit) {
    unsigned short sn = cppic_strlen(lit);
    unsigned short dn, need;
    if (!d || sn == 0) return;
    dn = d->len;
    need = (unsigned short)(dn + sn);
    if (!cppic_string_reserve(d, need)) return;
    cppic_strset(d->data + dn, lit, sn);
    d->len = need;
}

void cppic_string_append_char(CppicString* d, unsigned char c) {
    unsigned short dn = d ? d->len : 0;
    if (!d) return;
    if (!cppic_string_reserve(d, (unsigned short)(dn + 1u))) return;
    d->data[dn] = (char)c;
    d->data[dn + 1u] = 0;
    d->len = (unsigned short)(dn + 1u);
}

static void cppic_string_concat_buf(CppicString* d,
                                    const char* a, unsigned short an,
                                    const char* b, unsigned short bn) {
    char* nb;
    unsigned short len = (unsigned short)(an + bn);
    if (!d) return;
    nb = (char*)cppic_malloc((unsigned int)len + 1u);
    if (!nb) return;
    if (an) cppic_strset(nb, a, an);
    if (bn) cppic_strset(nb + an, b, bn);
    nb[len] = 0;
    cppic_string_release(d);
    d->data = nb;
    d->len = len;
    d->cap = len;
    d->own = 1;
}

void cppic_string_concat(CppicString* d, const CppicString* a, const CppicString* b) {
    if (!d || !a || !b) return;
    cppic_string_concat_buf(d, a->data, a->len, b->data, b->len);
}

void cppic_string_concat_lit(CppicString* d, const CppicString* a, const char* lit) {
    if (!d || !a) return;
    cppic_string_concat_buf(d, a->data, a->len, lit, cppic_strlen(lit));
}

void cppic_string_concat_llit(CppicString* d, const char* lit, const CppicString* b) {
    if (!d || !b) return;
    cppic_string_concat_buf(d, lit, cppic_strlen(lit), b->data, b->len);
}

unsigned short cppic_string_length(const CppicString* s) {
    return s ? s->len : 0;
}

unsigned char cppic_string_char_at(const CppicString* s, unsigned short i) {
    return (s && s->data && i < s->len) ? (unsigned char)s->data[i] : 0;
}

const char* cppic_string_c_str(const CppicString* s) {
    return (s && s->data) ? s->data : "";
}

unsigned char cppic_string_is_empty(const CppicString* s) {
    return (!s || s->len == 0) ? 1 : 0;
}

signed char cppic_string_compare(const CppicString* a, const CppicString* b) {
    const char* pa = a ? a->data : "";
    const char* pb = b ? b->data : "";
    unsigned short i = 0;
    for (;;) {
        unsigned char ca = (unsigned char)pa[i];
        unsigned char cb = (unsigned char)pb[i];
        if (ca != cb) return (signed char)(ca < cb ? -1 : 1);
        if (ca == 0) return 0;
        ++i;
    }
}

signed char cppic_string_compare_lit(const CppicString* a, const char* b) {
    const char* pa = a ? a->data : "";
    const char* pb = b ? b : "";
    unsigned short i = 0;
    for (;;) {
        unsigned char ca = (unsigned char)pa[i];
        unsigned char cb = (unsigned char)pb[i];
        if (ca != cb) return (signed char)(ca < cb ? -1 : 1);
        if (ca == 0) return 0;
        ++i;
    }
}

signed short cppic_string_index_of(const CppicString* s, const char* needle) {
    unsigned short sn = cppic_strlen(needle);
    unsigned short i, j;
    if (!s || sn == 0 || s->len < sn) return -1;
    if (sn == 1) return (signed short)cppic_string_index_of_char(s, (unsigned char)needle[0]);
    for (i = 0; i + sn <= s->len; ++i) {
        for (j = 0; j < sn; ++j) {
            if (s->data[i + j] != needle[j]) break;
        }
        if (j == sn) return (signed short)i;
    }
    return -1;
}

signed short cppic_string_index_of_char(const CppicString* s, unsigned char c) {
    unsigned short i;
    if (!s || !s->data) return -1;
    for (i = 0; i < s->len; ++i) {
        if ((unsigned char)s->data[i] == c) return (signed short)i;
    }
    return -1;
}

unsigned char cppic_string_starts_with(const CppicString* s, const char* pre) {
    unsigned short i = 0;
    if (!pre || !pre[0]) return 1;
    if (!s || !s->data) return 0;
    while (pre[i]) {
        if (i >= s->len || s->data[i] != pre[i]) return 0;
        ++i;
    }
    return 1;
}

unsigned char cppic_string_ends_with(const CppicString* s, const char* suf) {
    unsigned short sn, i;
    if (!suf || !suf[0]) return 1;
    sn = cppic_strlen(suf);
    if (!s || !s->data || s->len < sn) return 0;
    for (i = 0; i < sn; ++i) {
        if (s->data[s->len - sn + i] != suf[i]) return 0;
    }
    return 1;
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