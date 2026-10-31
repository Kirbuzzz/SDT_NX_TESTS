/* mmap/mprotect del codigo cargado, con memoria EJECUTABLE real.
 *
 * Regla de Horizon (aprendida de gdash_nx): en memoria de codigo el kernel NUNCA permite pasar de
 * escritura (RW) a ejecucion (RX). Un permiso de ejecucion solo se puede dar "desde recien mapeado".
 * Por eso:
 *   - fichero + RX: se rellena el respaldo ANTES de mapear y se da RX directamente.
 *   - RW -> RX (mprotect): se deja el trozo en RW, se desmapea, se vuelve a mapear (el contenido se
 *     conserva) y se da RX desde recien mapeado. Antes de desmapear, todo debe estar en RW.
 */
#include <switch.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include "sdt.h"

#define PAGE 0x1000
/* Estado por pagina. Los valores 1/3/5 SON los permisos de Horizon (R, RW, RX) y se pasan tal cual al kernel;
 * PS_FRESH es un valor interno (nunca se envia al kernel). */
enum { PS_NONE = 0, PS_R = 1, PS_RW = 3, PS_RX = 5, PS_FRESH = 0x10 };
typedef struct { uintptr_t dst; void *src; size_t size; } Chunk;
typedef struct Region { uintptr_t base; size_t size; uint8_t *ps; Chunk *ch; int nch, cap; VirtmemReservation *rv; } Region;
static Region **g_reg; static int g_nreg, g_capreg;
static volatile int g_lock;
static void lock(void) { while (__atomic_exchange_n(&g_lock, 1, __ATOMIC_ACQUIRE)) svcSleepThread(10000); }
static void unlock(void) { __atomic_store_n(&g_lock, 0, __ATOMIC_RELEASE); }
static size_t up(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }
static Handle H(void) { return envGetOwnProcessHandle(); }

static Region *find(uintptr_t a) {
    for (int i = 0; i < g_nreg; i++) if (a >= g_reg[i]->base && a < g_reg[i]->base + g_reg[i]->size) return g_reg[i];
    return NULL;
}
static Region *reserve(size_t size) {
    size = up(size, PAGE);
    virtmemLock();
    void *a = virtmemFindAslr(size + 0x10000, 0x1000);
    void *r = a ? (void *)up((uintptr_t)a, 0x10000) : NULL;
    VirtmemReservation *rv = r ? virtmemAddReservation(r, size) : NULL;
    virtmemUnlock();
    if (!r) return NULL;
    Region *g = (Region *)calloc(1, sizeof *g);
    g->base = (uintptr_t)r; g->size = size; g->ps = (uint8_t *)calloc(size / PAGE, 1); g->rv = rv;
    if (g_nreg == g_capreg) { g_capreg = g_capreg ? g_capreg * 2 : 256; g_reg = (Region **)realloc(g_reg, g_capreg * sizeof *g_reg); }
    g_reg[g_nreg++] = g;
    return g;
}

static Chunk *chunk_at(Region *r, size_t page) {
    uintptr_t a = r->base + page * PAGE;
    for (int i = 0; i < r->nch; i++) if (a >= r->ch[i].dst && a < r->ch[i].dst + r->ch[i].size) return &r->ch[i];
    return NULL;
}
/* fija el permiso de Horizon a un rango de paginas, por tramos que comparten estado previo.
 * Un tramo NUNCA cruza el limite entre dos mapeos (chunks): el kernel exige que todo el rango sea del mismo bloque. */
static int set_runs(Region *r, size_t p0, size_t p1, int hp) {
    static int warned_rw_r;
    for (size_t p = p0; p < p1; ) {
        if (r->ps[p] == PS_NONE) { p++; continue; }              /* paginas sin mapear: nada que hacer */
        Chunk *c = chunk_at(r, p);
        size_t cend = c ? (c->dst - r->base + c->size) / PAGE : p1;
        size_t q = p + 1;
        while (q < p1 && q < cend && r->ps[q] == r->ps[p]) q++;
        if (r->ps[p] != hp) {
            Result rc = svcSetProcessMemoryPermission(H(), r->base + p * PAGE, (q - p) * PAGE, hp);
            if (R_FAILED(rc)) {
                if (hp == PS_R && r->ps[p] == PS_RW) {           /* RW -> R puede no estar permitido: se deja en RW (inofensivo) */
                    if (!warned_rw_r) { warned_rw_r = 1; LOG("vm: RW->R rechazado por el kernel (rc=0x%x); se deja en RW\n", rc); }
                } else {
                    LOG("vm: permiso %d fallo en %lx +%zx (estado previo %d) rc=0x%x\n", hp, (unsigned long)(r->base + p * PAGE), (q - p) * PAGE, r->ps[p], rc);
                    return -1;
                }
            } else memset(r->ps + p, hp, q - p);
        }
        p = q;
    }
    return 0;
}
/* mapea un trozo NUEVO (paginas p0..p0+np) con respaldo de heap; opcionalmente lo rellena desde un fichero ANTES de mapear */
static int map_new(Region *r, size_t p0, size_t np, int fd, long off, size_t flen) {
    size_t n = np * PAGE;
    void *src = memalign(PAGE, n);
    if (!src) { LOG("vm: sin memoria para %zu KB\n", n >> 10); return -1; }
    memset(src, 0, n);
    if (fd >= 0 && flen) { if (vfs_pread(fd, src, flen > n ? n : flen, off) < 0) { free(src); LOG("vm: no se pudo leer fd=%d off=%ld\n", fd, off); return -1; } }
    armDCacheFlush(src, n);
    Result rc = svcMapProcessCodeMemory(H(), r->base + p0 * PAGE, (u64)src, n);
    if (R_FAILED(rc)) { LOG("vm: MapProcessCodeMemory fallo dst=%lx n=%zx rc=0x%x\n", (unsigned long)(r->base + p0 * PAGE), n, rc); free(src); return -1; }
    if (r->nch == r->cap) { r->cap = r->cap ? r->cap * 2 : 4; r->ch = (Chunk *)realloc(r->ch, r->cap * sizeof *r->ch); }
    r->ch[r->nch++] = (Chunk){ r->base + p0 * PAGE, src, n };
    memset(r->ps + p0, PS_FRESH, np);
    return 0;
}
static int commit_zero(Region *r, size_t p0, size_t p1) {
    for (size_t p = p0; p < p1; ) {
        if (r->ps[p]) { p++; continue; }
        size_t q = p; while (q < p1 && !r->ps[q]) q++;
        if (map_new(r, p, q - p, -1, 0, 0)) return -1;
        p = q;
    }
    return 0;
}
/* da RX al rango [p0,p1): remapea los trozos que ya no estan "recien mapeados" */
static int make_exec(Region *r, size_t p0, size_t p1) {
    for (int i = 0; i < r->nch; i++) {
        Chunk *c = &r->ch[i];
        size_t cp0 = (c->dst - r->base) / PAGE, cp1 = cp0 + c->size / PAGE;
        size_t a = p0 > cp0 ? p0 : cp0, b = p1 < cp1 ? p1 : cp1;
        int need = 0;
        for (size_t p = a; p < b; p++) if (r->ps[p] != PS_FRESH && r->ps[p] != PS_RX) need = 1;
        if (!need) continue;
        uint8_t *save = (uint8_t *)malloc(cp1 - cp0);
        memcpy(save, r->ps + cp0, cp1 - cp0);
        if (set_runs(r, cp0, cp1, PS_RW)) { free(save); return -1; }               /* todo a RW para poder desmapear */
        armDCacheFlush((void *)c->dst, c->size);
        Result rc = svcUnmapProcessCodeMemory(H(), c->dst, (u64)c->src, c->size);
        if (R_FAILED(rc)) { LOG("vm: UnmapProcessCodeMemory fallo rc=0x%x\n", rc); free(save); return -1; }
        rc = svcMapProcessCodeMemory(H(), c->dst, (u64)c->src, c->size);
        if (R_FAILED(rc)) { LOG("vm: remapeo fallo rc=0x%x\n", rc); free(save); return -1; }
        memset(r->ps + cp0, PS_FRESH, cp1 - cp0);
        for (size_t p = cp0; p < cp1; ) {                                          /* restaurar permisos; el rango pedido va a RX */
            int t = (p >= p0 && p < p1) ? PS_RX : save[p - cp0];
            size_t q = p + 1; while (q < cp1 && (((q >= p0 && q < p1) ? PS_RX : save[q - cp0]) == t)) q++;
            if (t != PS_FRESH && t != PS_NONE && set_runs(r, p, q, t)) { free(save); return -1; }
            p = q;
        }
        free(save);
    }
    return 0;
}
static int apply_prot(Region *r, size_t off, size_t len, int prot) {
    int hp = prot & 7;
    if (!hp) return 0;                                         /* PROT_NONE: se deja tal cual */
    if ((hp & 2) && (hp & 4)) { LOG("vm: pidio W+X; se concede RW\n"); hp = 3; }
    if (hp == 2) hp = 3;
    size_t p0 = off / PAGE, p1 = (off + len + PAGE - 1) / PAGE;
    if (commit_zero(r, p0, p1)) return -1;
    if (hp == PS_RX) {
        if (make_exec(r, p0, p1) || set_runs(r, p0, p1, PS_RX)) return -1;
        armICacheInvalidate((void *)(r->base + p0 * PAGE), (p1 - p0) * PAGE);
        return 0;
    }
    return set_runs(r, p0, p1, hp == 1 ? PS_R : PS_RW);
}

void *vm_mmap(void *addr, size_t len, int prot, int flags, int fd, long off) {
    if (!len) { errno = EINVAL; return (void *)-1; }
    size_t sz = up(len, PAGE), roff = 0; Region *r;
    int anon = (flags & 0x20) != 0, fixed = (flags & 0x10) != 0;
    lock();
    if (fixed) {
        r = find((uintptr_t)addr);
        if (!r || (uintptr_t)addr + sz > r->base + r->size) { unlock(); LOG("vm: mmap FIXED fuera de una region propia (%p)\n", addr); errno = ENOMEM; return (void *)-1; }
        roff = (uintptr_t)addr - r->base;
    } else {
        r = reserve(sz);
        if (!r) { unlock(); errno = ENOMEM; return (void *)-1; }
    }
    size_t p0 = roff / PAGE, np = sz / PAGE; int any = 0;
    for (size_t p = p0; p < p0 + np; p++) if (r->ps[p]) any = 1;
    if (!anon && !prot) { /* ventana de fichero sin permisos: solo reserva */ }
    else if (!any) {
        if (!anon) { if (map_new(r, p0, np, fd, off, len)) goto fail; }
        else if (prot) { if (map_new(r, p0, np, -1, 0, 0)) goto fail; }
    } else if (anon || prot) {                                  /* sobrescribir paginas ya mapeadas: dejarlas en RW y rellenar */
        if (commit_zero(r, p0, p0 + np) || set_runs(r, p0, p0 + np, PS_RW)) goto fail;
        uint8_t *p = (uint8_t *)(r->base + roff);
        memset(p, 0, sz);
        if (!anon && vfs_pread(fd, p, len, off) < 0) goto fail;
    }
    if (prot && apply_prot(r, roff, sz, prot)) goto fail;
    { void *res = (void *)(r->base + roff); unlock(); return res; }
fail:
    unlock(); errno = ENOMEM; return (void *)-1;
}
int vm_munmap(void *addr, size_t len) {
    lock();
    Region *r = find((uintptr_t)addr);
    if (r && (uintptr_t)addr == r->base && up(len, PAGE) >= r->size) {
        set_runs(r, 0, r->size / PAGE, PS_RW);                  /* hay que dejar todo en RW para desmapear */
        for (int i = 0; i < r->nch; i++) { svcUnmapProcessCodeMemory(H(), r->ch[i].dst, (u64)r->ch[i].src, r->ch[i].size); free(r->ch[i].src); }
        virtmemLock(); if (r->rv) virtmemRemoveReservation(r->rv); virtmemUnlock();
        for (int i = 0; i < g_nreg; i++) if (g_reg[i] == r) { g_reg[i] = g_reg[--g_nreg]; break; }
        free(r->ch); free(r->ps); free(r);
    }
    unlock();
    return 0;
}
int vm_mprotect(void *addr, size_t len, int prot) {
    lock();
    Region *r = find((uintptr_t)addr);
    int rc = 0;
    if (r && (uintptr_t)addr + len <= r->base + r->size) rc = apply_prot(r, (uintptr_t)addr - r->base, up(len, PAGE), prot);
    else LOG("vm: mprotect sobre memoria no gestionada %p +%zx prot=%d (ignorado)\n", addr, len, prot);
    unlock();
    if (rc) errno = ENOMEM;
    return rc;
}
