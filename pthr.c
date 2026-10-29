/* pthread / semaforos / TLS para el codigo de Android (ABI bionic arm64).
 * Los objetos de bionic (mutex=40 bytes, cond=48, rwlock=56, sem=16...) son del tamano del
 * codigo cargado; usamos esos mismos bytes con NUESTRA disposicion. Un objeto a cero
 * (PTHREAD_MUTEX_INITIALIZER) es un mutex normal libre. */
#include <switch.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <time.h>
#include "sdt.h"

/* ---------- TLS por hilo ---------- */
#define TLS_BYTES 0x2000
#define TLS_KEYS  32            /* indice (en u64) donde empiezan las claves: 0x100 */
#define MAX_KEYS  256
static const uint64_t CANARY = 0x5344544e58434b31ULL;
static volatile uint32_t g_next_tid = 2;

static inline uint64_t *tp0(void) {
#if defined(__aarch64__)
    uint64_t x; __asm__ volatile("mrs %0, tpidr_el0" : "=r"(x)); return (uint64_t *)x;
#else
    return NULL;
#endif
}
static uint64_t *tls_new(uint32_t tid) {
    uint64_t *b = (uint64_t *)memalign(0x1000, TLS_BYTES);
    memset(b, 0, TLS_BYTES);
    b[0] = (uint64_t)(uintptr_t)b;     /* TLS_SLOT_SELF        */
    b[1] = tid;                        /* TLS_SLOT_THREAD_ID   */
    b[5] = CANARY;                     /* TLS_SLOT_STACK_GUARD (TP+0x28) */
    return b;
}
static void tls_set(uint64_t *b) {
#if defined(__aarch64__)
    __asm__ volatile("msr tpidr_el0, %0" :: "r"(b));
#else
    (void)b;
#endif
}
void guest_tls_install(void) { tls_set(tls_new(1)); }
uint32_t guest_tid(void) { uint64_t *t = tp0(); return t ? (uint32_t)t[1] : 0; }

static void nap(void) { svcSleepThread(50000); }     /* 50 us */
static void backoff(int *n) { if (++*n < 64) { __asm__ volatile("" ::: "memory"); } else nap(); }

/* ---------- mutex ---------- */
typedef struct { int32_t w[10]; } BMutex;   /* w0: tipo<<14 (bionic) | w1: bloqueo | w2: dueno | w3: cuenta */
static int  mtype(BMutex *m) { return (m->w[0] >> 14) & 3; }
static int  m_lock(BMutex *m) {
    uint32_t me = guest_tid(); int n = 0;
    int t = mtype(m);
    if (t && __atomic_load_n(&m->w[1], __ATOMIC_ACQUIRE) && (uint32_t)m->w[2] == me) {
        if (t == 1) { m->w[3]++; return 0; }
        return 35;                                    /* EDEADLK (errorcheck) */
    }
    for (;;) {
        int e = 0;
        if (__atomic_compare_exchange_n(&m->w[1], &e, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) { m->w[2] = (int32_t)me; m->w[3] = 1; return 0; }
        backoff(&n);
    }
}
static int  m_trylock(BMutex *m) {
    uint32_t me = guest_tid(); int t = mtype(m);
    if (t == 1 && __atomic_load_n(&m->w[1], __ATOMIC_ACQUIRE) && (uint32_t)m->w[2] == me) { m->w[3]++; return 0; }
    int e = 0;
    if (__atomic_compare_exchange_n(&m->w[1], &e, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) { m->w[2] = (int32_t)me; m->w[3] = 1; return 0; }
    return 16;                                        /* EBUSY */
}
static int  m_unlock(BMutex *m) {
    int t = mtype(m);
    if (t == 1 && m->w[3] > 1) { m->w[3]--; return 0; }
    if (t == 2 && (!__atomic_load_n(&m->w[1], __ATOMIC_ACQUIRE) || (uint32_t)m->w[2] != guest_tid())) return 1; /* EPERM */
    m->w[2] = 0; m->w[3] = 0;
    __atomic_store_n(&m->w[1], 0, __ATOMIC_RELEASE);
    return 0;
}
static int sh_mutex_init(BMutex *m, const long *attr) { memset(m, 0, sizeof *m); if (attr) m->w[0] = (int32_t)((*attr & 3) << 14); return 0; }
static int sh_mutex_destroy(BMutex *m) { (void)m; return 0; }
static int sh_mutexattr_init(long *a) { *a = 0; return 0; }
static int sh_mutexattr_destroy(long *a) { (void)a; return 0; }
static int sh_mutexattr_settype(long *a, int t) { *a = (*a & ~3L) | (t & 3); return 0; }

/* ---------- variables de condicion ---------- */
typedef struct { int32_t w[12]; } BCond;    /* w0: secuencia | w1: reloj (0 realtime, 1 monotonic) */
static uint64_t now_ns(int clk) {
    uint64_t mono = armTicksToNs(armGetSystemTick());
    if (clk == 1) return mono;
    return (uint64_t)time(NULL) * 1000000000ULL + (mono % 1000000000ULL);
}
static int sh_cond_init(BCond *c, const int *attr) { memset(c, 0, sizeof *c); if (attr) c->w[1] = (*attr >> 1) & 1; return 0; }
static int sh_cond_destroy(BCond *c) { (void)c; return 0; }
static int sh_cond_signal(BCond *c) { __atomic_fetch_add(&c->w[0], 1, __ATOMIC_RELEASE); return 0; }
static int sh_cond_wait(BCond *c, BMutex *m) {
    int32_t seq = __atomic_load_n(&c->w[0], __ATOMIC_ACQUIRE);
    m_unlock(m);
    while (__atomic_load_n(&c->w[0], __ATOMIC_ACQUIRE) == seq) nap();
    return m_lock(m);
}
static int sh_cond_timedwait(BCond *c, BMutex *m, const long *ts) {
    uint64_t dl = (uint64_t)ts[0] * 1000000000ULL + (uint64_t)ts[1];
    int32_t seq = __atomic_load_n(&c->w[0], __ATOMIC_ACQUIRE);
    m_unlock(m);
    int rc = 0;
    while (__atomic_load_n(&c->w[0], __ATOMIC_ACQUIRE) == seq) {
        if (now_ns(c->w[1]) >= dl) { rc = 110; break; }   /* ETIMEDOUT */
        nap();
    }
    m_lock(m);
    return rc;
}
static int sh_condattr_init(int *a) { *a = 0; return 0; }
static int sh_condattr_destroy(int *a) { (void)a; return 0; }
static int sh_condattr_setclock(int *a, int clk) { *a = (*a & 1) | ((clk == 1 ? 1 : 0) << 1); return 0; }

/* ---------- rwlock (56 bytes): w0 lectores, w1 escritor, w2 cerrojo interno ---------- */
typedef struct { int32_t w[14]; } BRw;
static void rw_g(BRw *r) { int n = 0; for (;;) { int e = 0; if (__atomic_compare_exchange_n(&r->w[2], &e, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return; backoff(&n); } }
static void rw_u(BRw *r) { __atomic_store_n(&r->w[2], 0, __ATOMIC_RELEASE); }
static int sh_rw_init(BRw *r, const void *a) { (void)a; memset(r, 0, sizeof *r); return 0; }
static int sh_rw_rd(BRw *r) { for (;;) { rw_g(r); if (!r->w[1]) { r->w[0]++; rw_u(r); return 0; } rw_u(r); nap(); } }
static int sh_rw_wr(BRw *r) { for (;;) { rw_g(r); if (!r->w[1] && !r->w[0]) { r->w[1] = 1; rw_u(r); return 0; } rw_u(r); nap(); } }
static int sh_rw_un(BRw *r) { rw_g(r); if (r->w[1]) r->w[1] = 0; else if (r->w[0] > 0) r->w[0]--; rw_u(r); return 0; }

/* ---------- semaforos (16 bytes: w0 = cuenta) ---------- */
static int sh_sem_init(uint32_t *s, int pshared, unsigned v) { (void)pshared; memset(s, 0, 16); s[0] = v; return 0; }
static int sh_sem_destroy(uint32_t *s) { (void)s; return 0; }
static int sh_sem_post(uint32_t *s) { __atomic_fetch_add(&s[0], 1, __ATOMIC_RELEASE); return 0; }
static int sh_sem_wait(uint32_t *s) {
    for (;;) {
        uint32_t v = __atomic_load_n(&s[0], __ATOMIC_ACQUIRE);
        if (v && __atomic_compare_exchange_n(&s[0], &v, v - 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return 0;
        nap();
    }
}

/* ---------- claves y once ---------- */
static uint8_t g_key_used[MAX_KEYS]; static void (*g_key_dtor[MAX_KEYS])(void *);
static int sh_key_create(unsigned *key, void (*d)(void *)) {
    for (unsigned i = 1; i < MAX_KEYS; i++) {
        uint8_t e = 0;
        if (__atomic_compare_exchange_n(&g_key_used[i], &e, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) { g_key_dtor[i] = d; *key = i; return 0; }
    }
    return 11;                                       /* EAGAIN */
}
static int sh_key_delete(unsigned k) { if (k < MAX_KEYS) g_key_used[k] = 0; return 0; }
static void *sh_getspecific(unsigned k) { uint64_t *t = tp0(); return (t && k < MAX_KEYS) ? (void *)t[TLS_KEYS + k] : NULL; }
static int sh_setspecific(unsigned k, const void *v) { uint64_t *t = tp0(); if (!t || k >= MAX_KEYS) return 22; t[TLS_KEYS + k] = (uint64_t)(uintptr_t)v; return 0; }
static int sh_once(int *o, void (*fn)(void)) {
    if (__atomic_load_n(o, __ATOMIC_ACQUIRE) == 2) return 0;
    int e = 0;
    if (__atomic_compare_exchange_n(o, &e, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
        fn();
        __atomic_store_n(o, 2, __ATOMIC_RELEASE);
    } else {
        while (__atomic_load_n(o, __ATOMIC_ACQUIRE) != 2) nap();
    }
    return 0;
}

/* ---------- hilos ---------- */
typedef struct ThreadRec { Thread t; void *(*fn)(void *); void *arg; void *ret; volatile int done; int detached; uint32_t tid; } ThreadRec;
static ThreadRec g_main_rec = { .tid = 1 };
static void thread_entry(void *p) {
    ThreadRec *r = (ThreadRec *)p;
    uint64_t *b = tls_new(r->tid); b[2] = (uint64_t)(uintptr_t)r; tls_set(b);
    r->ret = r->fn(r->arg);
    __atomic_store_n(&r->done, 1, __ATOMIC_RELEASE);
}
/* pthread_attr_t (56 bytes): +0 flags, +8 stack_base, +16 stack_size, +24 guard, +32 policy, +36 prio */
static int sh_attr_init(char *a) { memset(a, 0, 56); *(size_t *)(a + 16) = 1 << 20; *(size_t *)(a + 24) = 4096; return 0; }
static int sh_attr_destroy(char *a) { (void)a; return 0; }
static int sh_attr_setdetach(char *a, int s) { if (s) *(uint32_t *)a |= 1; else *(uint32_t *)a &= ~1u; return 0; }
static int sh_attr_setstack(char *a, size_t s) { *(size_t *)(a + 16) = s; return 0; }
static int sh_attr_getstack(const char *a, void **base, size_t *size) { *base = *(void **)(a + 8); *size = *(const size_t *)(a + 16); return 0; }
static int sh_getattr_np(void *th, char *a) { (void)th; sh_attr_init(a); return 0; }
static int sh_create(uintptr_t *th, const char *attr, void *(*fn)(void *), void *arg) {
    ThreadRec *r = (ThreadRec *)calloc(1, sizeof *r);
    if (!r) return 11;
    r->fn = fn; r->arg = arg; r->tid = __atomic_fetch_add(&g_next_tid, 1, __ATOMIC_RELAXED);
    size_t stack = attr ? *(const size_t *)(attr + 16) : (1 << 20);
    if (stack < 0x10000) stack = 0x10000;
    stack = (stack + 0xfff) & ~(size_t)0xfff;
    if (attr && (*(const uint32_t *)attr & 1)) r->detached = 1;
    Result rc = threadCreate(&r->t, thread_entry, r, NULL, stack, 0x2C, -2);
    if (R_FAILED(rc)) { LOG("pthread_create: threadCreate fallo rc=0x%x (stack %zu)\n", rc, stack); free(r); return 11; }
    rc = threadStart(&r->t);
    if (R_FAILED(rc)) { LOG("pthread_create: threadStart fallo rc=0x%x\n", rc); return 11; }
    *th = (uintptr_t)r;
    LOG("pthread_create: hilo %u (stack %zu KB)\n", r->tid, stack >> 10);
    return 0;
}
static int sh_join(uintptr_t th, void **ret) {
    ThreadRec *r = (ThreadRec *)th;
    while (!__atomic_load_n(&r->done, __ATOMIC_ACQUIRE)) svcSleepThread(100000);
    threadWaitForExit(&r->t); threadClose(&r->t);
    if (ret) *ret = r->ret;
    return 0;
}
static int sh_detach(uintptr_t th) { ((ThreadRec *)th)->detached = 1; return 0; }
static void sh_exit_thread(void *ret) {
    uint64_t *t = tp0(); ThreadRec *r = t ? (ThreadRec *)(uintptr_t)t[2] : NULL;
    if (!r || r == &g_main_rec) { LOG("!! pthread_exit en el hilo principal\n"); fatal_wait("pthread_exit principal"); }
    r->ret = ret; __atomic_store_n(&r->done, 1, __ATOMIC_RELEASE); svcExitThread();
}
static uintptr_t sh_self(void) { uint64_t *t = tp0(); ThreadRec *r = t ? (ThreadRec *)(uintptr_t)t[2] : NULL; return (uintptr_t)(r ? r : &g_main_rec); }
static int sh_equal(uintptr_t a, uintptr_t b) { return a == b; }
static int sh_setname(uintptr_t th, const char *n) { (void)th; (void)n; return 0; }
static int sh_gettid_np(uintptr_t th) { ThreadRec *r = (ThreadRec *)th; return r ? (int)r->tid : 1; }

const Shim g_shims_pthr[] = {
    {"pthread_mutex_init", (void *)sh_mutex_init}, {"pthread_mutex_destroy", (void *)sh_mutex_destroy},
    {"pthread_mutex_lock", (void *)m_lock}, {"pthread_mutex_trylock", (void *)m_trylock}, {"pthread_mutex_unlock", (void *)m_unlock},
    {"pthread_mutexattr_init", (void *)sh_mutexattr_init}, {"pthread_mutexattr_destroy", (void *)sh_mutexattr_destroy},
    {"pthread_mutexattr_settype", (void *)sh_mutexattr_settype},
    {"pthread_cond_init", (void *)sh_cond_init}, {"pthread_cond_destroy", (void *)sh_cond_destroy},
    {"pthread_cond_signal", (void *)sh_cond_signal}, {"pthread_cond_broadcast", (void *)sh_cond_signal},
    {"pthread_cond_wait", (void *)sh_cond_wait}, {"pthread_cond_timedwait", (void *)sh_cond_timedwait},
    {"pthread_condattr_init", (void *)sh_condattr_init}, {"pthread_condattr_destroy", (void *)sh_condattr_destroy},
    {"pthread_condattr_setclock", (void *)sh_condattr_setclock},
    {"pthread_rwlock_init", (void *)sh_rw_init}, {"pthread_rwlock_rdlock", (void *)sh_rw_rd},
    {"pthread_rwlock_wrlock", (void *)sh_rw_wr}, {"pthread_rwlock_unlock", (void *)sh_rw_un},
    {"sem_init", (void *)sh_sem_init}, {"sem_destroy", (void *)sh_sem_destroy}, {"sem_post", (void *)sh_sem_post}, {"sem_wait", (void *)sh_sem_wait},
    {"pthread_key_create", (void *)sh_key_create}, {"pthread_key_delete", (void *)sh_key_delete},
    {"pthread_getspecific", (void *)sh_getspecific}, {"pthread_setspecific", (void *)sh_setspecific},
    {"pthread_once", (void *)sh_once},
    {"pthread_attr_init", (void *)sh_attr_init}, {"pthread_attr_destroy", (void *)sh_attr_destroy},
    {"pthread_attr_setdetachstate", (void *)sh_attr_setdetach}, {"pthread_attr_setstacksize", (void *)sh_attr_setstack},
    {"pthread_attr_getstack", (void *)sh_attr_getstack}, {"pthread_getattr_np", (void *)sh_getattr_np},
    {"pthread_create", (void *)sh_create}, {"pthread_join", (void *)sh_join}, {"pthread_detach", (void *)sh_detach},
    {"pthread_exit", (void *)sh_exit_thread}, {"pthread_self", (void *)sh_self}, {"pthread_equal", (void *)sh_equal},
    {"pthread_setname_np", (void *)sh_setname}, {"pthread_gettid_np", (void *)sh_gettid_np},
    {NULL, NULL}
};
