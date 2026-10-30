/* Ficheros "virtuales" (funopen de BSD/bionic): un FILE* cuyas lecturas/escrituras/seek las hacen
 * funciones del propio codigo cargado (p. ej. para exponer entradas del APK como si fueran ficheros). */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "sdt.h"

#define CF_MAGIC 0x43464c31u
#define CF_POOL 256
#define CF_BUF 4096
typedef struct CFile {
    uint32_t magic; void *cookie;
    int  (*rd)(void *, char *, int);
    int  (*wr)(void *, const char *, int);
    long (*sk)(void *, long, int);
    int  (*cl)(void *);
    int eof, err;
    int bpos, blen; unsigned char buf[CF_BUF];     /* buffer de lectura */
} CFile;
static CFile g_cf[CF_POOL];
static volatile int g_cflock;
static void lk(void) { while (__atomic_exchange_n(&g_cflock, 1, __ATOMIC_ACQUIRE)) { } }
static void ul(void) { __atomic_store_n(&g_cflock, 0, __ATOMIC_RELEASE); }

CFile *cf_of(const void *f) {
    const char *p = (const char *)f;
    if (p < (const char *)g_cf || p >= (const char *)(g_cf + CF_POOL)) return NULL;
    if (((size_t)(p - (const char *)g_cf)) % sizeof(CFile)) return NULL;
    return ((const CFile *)f)->magic == CF_MAGIC ? (CFile *)f : NULL;
}
void *cf_open(const void *cookie, int (*rd)(void *, char *, int), int (*wr)(void *, const char *, int),
              long (*sk)(void *, long, int), int (*cl)(void *)) {
    lk();
    for (int i = 0; i < CF_POOL; i++) if (!g_cf[i].magic) {
        CFile *c = &g_cf[i]; memset(c, 0, sizeof *c);
        c->magic = CF_MAGIC; c->cookie = (void *)cookie; c->rd = rd; c->wr = wr; c->sk = sk; c->cl = cl;
        ul(); return c;
    }
    ul(); return NULL;
}
static int fill(CFile *c) {                         /* rellena el buffer; devuelve bytes o <=0 */
    if (!c->rd) { c->err = 1; return -1; }
    int n = c->rd(c->cookie, (char *)c->buf, CF_BUF);
    if (n < 0) { c->err = 1; return -1; }
    if (n == 0) { c->eof = 1; return 0; }
    c->bpos = 0; c->blen = n; return n;
}
int cf_getc(CFile *c) {
    if (c->bpos >= c->blen && fill(c) <= 0) return -1;
    return c->buf[c->bpos++];
}
int cf_ungetc(CFile *c, int ch) {
    if (ch == -1) return -1;
    if (c->bpos > 0) c->buf[--c->bpos] = (unsigned char)ch;
    else { memmove(c->buf + 1, c->buf, (size_t)c->blen < CF_BUF - 1 ? (size_t)c->blen : CF_BUF - 1); c->buf[0] = (unsigned char)ch; if (c->blen < CF_BUF) c->blen++; c->bpos = 0; }
    c->eof = 0; return ch;
}
size_t cf_fread(CFile *c, void *p, size_t sz, size_t n) {
    size_t total = sz * n, got = 0; unsigned char *d = (unsigned char *)p;
    if (!total) return 0;
    while (got < total) {
        if (c->bpos >= c->blen && fill(c) <= 0) break;
        size_t k = (size_t)(c->blen - c->bpos); if (k > total - got) k = total - got;
        memcpy(d + got, c->buf + c->bpos, k); c->bpos += (int)k; got += k;
    }
    return got / sz;
}
size_t cf_fwrite(CFile *c, const void *p, size_t sz, size_t n) {
    size_t total = sz * n, done = 0;
    if (!c->wr) { c->err = 1; return 0; }
    while (done < total) {
        int r = c->wr(c->cookie, (const char *)p + done, (int)(total - done > 0x40000000 ? 0x40000000 : total - done));
        if (r <= 0) { c->err = 1; break; }
        done += (size_t)r;
    }
    return sz ? done / sz : 0;
}
char *cf_fgets(CFile *c, char *b, int n) {
    if (n <= 0) return NULL;
    int i = 0;
    while (i < n - 1) { int ch = cf_getc(c); if (ch < 0) break; b[i++] = (char)ch; if (ch == '\n') break; }
    if (!i) return NULL;
    b[i] = 0; return b;
}
int cf_fseek(CFile *c, long off, int wh) {
    if (!c->sk) return -1;
    if (wh == 1) off -= (c->blen - c->bpos);          /* lo ya leido al buffer no cuenta */
    long r = c->sk(c->cookie, off, wh);
    if (r < 0) return -1;
    c->bpos = c->blen = 0; c->eof = 0; return 0;
}
long cf_ftell(CFile *c) {
    if (!c->sk) return -1;
    long r = c->sk(c->cookie, 0, 1);
    return r < 0 ? -1 : r - (c->blen - c->bpos);
}
int cf_feof(CFile *c) { return c->eof; }
int cf_ferror(CFile *c) { return c->err; }
void cf_clearerr(CFile *c) { c->eof = c->err = 0; }
int cf_close(CFile *c) { int r = c->cl ? c->cl(c->cookie) : 0; lk(); c->magic = 0; ul(); return r; }
