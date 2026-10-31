/* Funciones de sistema/libc de Android (bionic) que el codigo cargado necesita:
 * tiempo, memoria, stdio (con __sF), locale, setjmp, variantes _chk, etc. */
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <malloc.h>
#include <setjmp.h>
#include <fcntl.h>
#include <wchar.h>
#include <wctype.h>
#include <ctype.h>
#include <strings.h>
#include "sdt.h"

int *__errno(void);   /* newlib lo declara en errno.h; se repite por claridad */

/* ---------------- variantes _chk (fortify): se ignoran los limites ---------------- */
static void  *sh_memcpy_chk(void *d, const void *s, size_t n, size_t dl) { (void)dl; return memcpy(d, s, n); }
static void  *sh_memmove_chk(void *d, const void *s, size_t n, size_t dl) { (void)dl; return memmove(d, s, n); }
static size_t sh_strlen_chk(const char *s, size_t sl) { (void)sl; return strlen(s); }
static char  *sh_strchr_chk(const char *s, int c, size_t sl) { (void)sl; return strchr(s, c); }
static char  *sh_strrchr_chk(const char *s, int c, size_t sl) { (void)sl; return strrchr(s, c); }
static char  *sh_strncpy_chk2(char *d, const char *s, size_t n, size_t dl, size_t sl) { (void)dl; (void)sl; return strncpy(d, s, n); }
static int    sh_vsnprintf_chk(char *b, size_t n, int fl, size_t bl, const char *f, va_list ap) { (void)fl; (void)bl; return vsnprintf(b, n, f, ap); }

/* ---------------- tiempo ---------------- */
static uint64_t mono_ns(void) { return armTicksToNs(armGetSystemTick()); }
static int sh_clock_gettime(int id, long *ts) {
    uint64_t m = mono_ns();
    if (id == 0) { uint64_t sec = (uint64_t)time(NULL); ts[0] = (long)sec; ts[1] = (long)(m % 1000000000ULL); }   /* REALTIME */
    else { ts[0] = (long)(m / 1000000000ULL); ts[1] = (long)(m % 1000000000ULL); }                               /* MONOTONIC y demas */
    return 0;
}
static int sh_gettimeofday(long *tv, void *tz) {
    (void)tz; long ts[2]; sh_clock_gettime(0, ts); tv[0] = ts[0]; tv[1] = ts[1] / 1000; return 0;
}
static int sh_nanosleep(const long *req, long *rem) { (void)rem; svcSleepThread((s64)req[0] * 1000000000LL + req[1]); return 0; }
static int sh_usleep(unsigned us) { svcSleepThread((s64)us * 1000LL); return 0; }
static unsigned sh_sleep(unsigned s) { svcSleepThread((s64)s * 1000000000LL); return 0; }
static int sh_sched_yield(void) { svcSleepThread(0); return 0; }
static int sh_sched_getcpu(void) { return 0; }
static long sh_time(long *t) { long v = (long)time(NULL); if (t) *t = v; return v; }

typedef struct { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; long tm_gmtoff; const char *tm_zone; } BTm;
static void tm_b(const struct tm *s, BTm *d) {
    d->tm_sec = s->tm_sec; d->tm_min = s->tm_min; d->tm_hour = s->tm_hour; d->tm_mday = s->tm_mday; d->tm_mon = s->tm_mon;
    d->tm_year = s->tm_year; d->tm_wday = s->tm_wday; d->tm_yday = s->tm_yday; d->tm_isdst = s->tm_isdst; d->tm_gmtoff = 0; d->tm_zone = "UTC";
}
static void tm_n(const BTm *s, struct tm *d) {
    memset(d, 0, sizeof *d);
    d->tm_sec = s->tm_sec; d->tm_min = s->tm_min; d->tm_hour = s->tm_hour; d->tm_mday = s->tm_mday; d->tm_mon = s->tm_mon;
    d->tm_year = s->tm_year; d->tm_wday = s->tm_wday; d->tm_yday = s->tm_yday; d->tm_isdst = s->tm_isdst;
}
static BTm *sh_localtime_r(const time_t *t, BTm *o) { struct tm x; if (!localtime_r(t, &x)) return NULL; tm_b(&x, o); return o; }
static BTm *sh_gmtime_r(const time_t *t, BTm *o) { struct tm x; if (!gmtime_r(t, &x)) return NULL; tm_b(&x, o); return o; }
static BTm *sh_localtime(const time_t *t) { static BTm b; return sh_localtime_r(t, &b); }
static BTm *sh_gmtime(const time_t *t) { static BTm b; return sh_gmtime_r(t, &b); }
static long sh_mktime(BTm *b) { struct tm x; tm_n(b, &x); return (long)mktime(&x); }
static size_t sh_strftime(char *s, size_t n, const char *f, const BTm *b) { struct tm x; tm_n(b, &x); return strftime(s, n, f, &x); }

/* ---------------- memoria ---------------- */
static void *g_maps[1024];
static void *sh_mmap(void *addr, size_t len, int prot, int flags, int fd, long off) {
    (void)addr; (void)prot; (void)off;
    if (!(flags & 0x20) || fd != -1) { LOG("mmap de fichero no soportado (fd=%d len=%zu)\n", fd, len); errno = ENOSYS; return (void *)-1; }
    size_t sz = (len + 0xfff) & ~(size_t)0xfff;
    void *p = memalign(0x1000, sz);
    if (!p) { errno = ENOMEM; return (void *)-1; }
    memset(p, 0, sz);
    for (int i = 0; i < 1024; i++) if (!g_maps[i]) { g_maps[i] = p; break; }
    return p;
}
static int sh_munmap(void *a, size_t l) { (void)l; for (int i = 0; i < 1024; i++) if (g_maps[i] == a) { g_maps[i] = NULL; free(a); break; } return 0; }
static int sh_zero(void) { return 0; }
static int sh_getpagesize(void) { return 4096; }
static long sh_sysconf(int n) {
    switch (n) {
        case 39: case 40: return 4096;   /* _SC_PAGESIZE y _SC_PAGE_SIZE */
        case 96: case 97: return 3;      /* NPROCESSORS_CONF/ONLN: una aplicacion de Switch usa 3 nucleos */
        case 98: return (long)(3000000000ULL / 4096);   /* PHYS_PAGES  */
        case 99: return (long)(1500000000ULL / 4096);   /* AVPHYS_PAGES */
        default: return -1;
    }
}
/* 0 = "sin extensiones": el codigo usa rutas portables en vez de sondear crypto ARMv8 (como gdash_nx) */
static unsigned long sh_getauxval(unsigned long t) { return t == 6 ? 4096 : 0; }
static long sh_prctl(int o, ...) { (void)o; return 0; }
static long sh_syscall(long n, ...) { if (n == 178) return (long)guest_tid(); errno = ENOSYS; return -1; }
static int sh_getrlimit(int r, unsigned long *lim) { (void)r; lim[0] = lim[1] = (unsigned long)-1; return 0; }
static int sh_sched_getaffinity(int pid, size_t sz, unsigned char *m) { (void)pid; memset(m, 0, sz); m[0] = 0x07; return 0; }
static int sh_sprop_get(const char *n, char *v) { (void)n; v[0] = 0; return 0; }
static void *sh_null(void) { return NULL; }
static int sh_getpid(void) { return 1; }
static int sh_gettid(void) { return (int)guest_tid(); }
static int sh_getuid(void) { return 0; }
static int sh_minus1(void) { errno = ENOSYS; return -1; }

/* ---------------- stdio con __sF (bionic: FILE de 152 bytes, 3 estandar) ---------------- */
static char g_sF[3 * 152];
static void *g_stdin_p = &g_sF[0], *g_stdout_p = &g_sF[152], *g_stderr_p = &g_sF[304];
static int is_std(const void *f) { return (const char *)f >= g_sF && (const char *)f < g_sF + sizeof g_sF; }
static int sh_vfprintf(FILE *f, const char *fmt, va_list ap) {
    if (cf_of(f)) {
        va_list ap2; va_copy(ap2, ap);
        char b[1024]; int n = vsnprintf(b, sizeof b, fmt, ap);
        if (n >= (int)sizeof b) { char *big = (char *)malloc((size_t)n + 1); vsnprintf(big, (size_t)n + 1, fmt, ap2); cf_fwrite(cf_of(f), big, 1, (size_t)n); free(big); }
        else if (n > 0) cf_fwrite(cf_of(f), b, 1, (size_t)n);
        va_end(ap2); return n;
    }
    if (is_std(f)) { char b[1024]; int n = vsnprintf(b, sizeof b, fmt, ap); LOG("%s", b); return n; }
    return vfprintf(f, fmt, ap);
}
static int sh_fprintf(FILE *f, const char *fmt, ...) { va_list ap; va_start(ap, fmt); int r = sh_vfprintf(f, fmt, ap); va_end(ap); return r; }
static int sh_vprintf(const char *fmt, va_list ap) { char b[1024]; int n = vsnprintf(b, sizeof b, fmt, ap); LOG("%s", b); return n; }
static int sh_printf(const char *fmt, ...) { va_list ap; va_start(ap, fmt); int r = sh_vprintf(fmt, ap); va_end(ap); return r; }
static int sh_puts(const char *s) { LOG("%s\n", s); return 1; }
static int sh_fputs(const char *s, FILE *f) { if (cf_of(f)) return (int)cf_fwrite(cf_of(f), s, 1, strlen(s)); if (is_std(f)) { LOG("%s", s); return 1; } return fputs(s, f); }
static int sh_fputc(int c, FILE *f) { if (cf_of(f)) { unsigned char ch = (unsigned char)c; return cf_fwrite(cf_of(f), &ch, 1, 1) == 1 ? c : -1; } if (is_std(f)) { LOG("%c", c); return c; } return fputc(c, f); }
static size_t sh_fwrite(const void *p, size_t s, size_t n, FILE *f) { if (cf_of(f)) return cf_fwrite(cf_of(f), p, s, n); if (is_std(f)) { LOG("%.*s", (int)(s * n), (const char *)p); return n; } return fwrite(p, s, n, f); }
static int sh_fflush(FILE *f) { return (!f || is_std(f) || cf_of(f)) ? 0 : fflush(f); }
static int sh_fclose(FILE *f) { if (cf_of(f)) return cf_close(cf_of(f)); return is_std(f) ? 0 : fclose(f); }
static int sh_fileno(FILE *f) { if (cf_of(f)) return -1; return is_std(f) ? (int)(((char *)f - g_sF) / 152) : fileno(f); }
static char *sh_strcasestr(const char *h, const char *n) {
    size_t nl = strlen(n);
    if (!nl) return (char *)h;
    for (; *h; h++) if (!strncasecmp(h, n, nl)) return (char *)h;
    return NULL;
}
/* newlib de la Switch no trae posix_memalign: se hace con memalign */
static int sh_posix_memalign(void **o, size_t a, size_t n) {
    void *p = memalign(a < sizeof(void *) ? sizeof(void *) : a, n);
    if (!p) return ENOMEM;
    *o = p; return 0;
}
static void sh_perror(const char *m) { LOG("perror: %s: %s\n", m ? m : "", strerror(errno)); }
static int sh_strerror_r(int e, char *b, size_t n) { snprintf(b, n, "%s", strerror(e)); return 0; }

/* ---------------- locale / ctype / setjmp ---------------- */
static char g_ctype[257];
static void ctype_init(void) {
    if (g_ctype[1] || g_ctype[2]) return;
    for (int c = 0; c < 256; c++) {
        int f = 0;
        if (c >= 'A' && c <= 'Z') f |= 0x01;           /* _U */
        if (c >= 'a' && c <= 'z') f |= 0x02;           /* _L */
        if (c >= '0' && c <= '9') f |= 0x04;           /* _N */
        if (c == ' ' || (c >= 9 && c <= 13)) f |= 0x08;/* _S */
        if (c > 32 && c < 127 && !((f & 7))) f |= 0x10;/* _P */
        if (c < 32 || c == 127) f |= 0x20;             /* _C */
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) f |= 0x40; /* _X */
        if (c == ' ') f |= 0x80;                       /* _B */
        g_ctype[c + 1] = (char)f;
    }
    g_ctype[0] = 0;
}
static void *sh_newlocale(int m, const char *l, void *b) { (void)m; (void)l; (void)b; return (void *)1; }
static void  sh_freelocale(void *l) { (void)l; }
static void *sh_uselocale(void *l) { (void)l; return (void *)(intptr_t)-1; }
static int   sh_mbcur(void) { return 4; }

/* ---------------- varios ---------------- */
static void sh_assert2(const char *f, int l, const char *fn, const char *m) { LOG("!! assert: %s:%d %s: %s\n", f, l, fn, m); fatal_wait("assert()"); }
static int  sh_raise(int s) { LOG("!! raise(%d)\n", s); fatal_wait("raise()"); return 0; }
static void sh_set_abort_message(const char *m) { LOG("!! abort message: %s\n", m ? m : "(null)"); }
static int  sh_log_buf_write(int b, int p, const char *t, const char *m) { (void)b; LOG("[android %d %s] %s\n", p, t ? t : "", m ? m : ""); return 0; }
static void sh_sincos(double x, double *s, double *c) { *s = sin(x); *c = cos(x); }
static void sh_sincosf(float x, float *s, float *c) { *s = sinf(x); *c = cosf(x); }
typedef struct { long long quot, rem; } LLDiv;
static LLDiv sh_lldiv(long long a, long long b) { LLDiv r = { a / b, a % b }; return r; }
static int sh_sigempty(unsigned long *s) { *s = 0; return 0; }
static int sh_sigfill(unsigned long *s) { *s = ~0UL; return 0; }
static int sh_register_atfork(void *a, void *b, void *c, void *d) { (void)a; (void)b; (void)c; (void)d; return 0; }
static void sh_syslog(int p, const char *f, ...) { (void)p; (void)f; }


/* ---- lectura/posicion de stdio con ficheros virtuales (funopen) ---- */
static size_t sh_fread(void *p, size_t sz, size_t n, FILE *f) { if (cf_of(f)) return cf_fread(cf_of(f), p, sz, n); if (is_std(f)) return 0; return fread(p, sz, n, f); }
static char  *sh_fgets(char *b, int n, FILE *f) { if (cf_of(f)) return cf_fgets(cf_of(f), b, n); if (is_std(f)) return NULL; return fgets(b, n, f); }
static int    sh_fgetc(FILE *f) { if (cf_of(f)) return cf_getc(cf_of(f)); if (is_std(f)) return -1; return fgetc(f); }
static int    sh_ungetc(int c, FILE *f) { if (cf_of(f)) return cf_ungetc(cf_of(f), c); if (is_std(f)) return -1; return ungetc(c, f); }
static int    sh_fseek(FILE *f, long o, int w) { if (cf_of(f)) return cf_fseek(cf_of(f), o, w); if (is_std(f)) return -1; return fseek(f, o, w); }
static long   sh_ftell(FILE *f) { if (cf_of(f)) return cf_ftell(cf_of(f)); if (is_std(f)) return -1; return ftell(f); }
static void   sh_rewind(FILE *f) { sh_fseek(f, 0, 0); }
static int    sh_feof(FILE *f) { if (cf_of(f)) return cf_feof(cf_of(f)); if (is_std(f)) return 1; return feof(f); }
static int    sh_ferror(FILE *f) { if (cf_of(f)) return cf_ferror(cf_of(f)); if (is_std(f)) return 0; return ferror(f); }
static void   sh_clearerr(FILE *f) { if (cf_of(f)) cf_clearerr(cf_of(f)); else if (!is_std(f)) clearerr(f); }
static int    sh_setvbuf(FILE *f, char *b, int m, size_t n) { if (cf_of(f) || is_std(f)) return 0; return setvbuf(f, b, m, n); }
static FILE  *sh_fdopen(int fd, const char *m) { return fdopen(fd, m); }
static int    sh_fscanf(FILE *f, const char *fmt, ...) { (void)f; (void)fmt; return -1; }   /* EOF: no se necesita aun */
static int    sh_vasprintf(char **out, const char *fmt, va_list ap) {
    va_list a2; va_copy(a2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    if (n < 0) { va_end(a2); return -1; }
    *out = (char *)malloc((size_t)n + 1);
    if (!*out) { va_end(a2); return -1; }
    vsnprintf(*out, (size_t)n + 1, fmt, a2); va_end(a2); return n;
}

/* ---- variantes con locale (_l): se ignora el locale ---- */
static int sh_isdigit_l(int c, void *l) { (void)l; return isdigit(c); }
static int sh_islower_l(int c, void *l) { (void)l; return islower(c); }
static int sh_isupper_l(int c, void *l) { (void)l; return isupper(c); }
static int sh_isxdigit_l(int c, void *l) { (void)l; return isxdigit(c); }
static int sh_iswlower_l(wint_t c, void *l) { (void)l; return iswlower(c); }
static long long sh_strtoll_l(const char *s, char **e, int b, void *l) { (void)l; return strtoll(s, e, b); }
static unsigned long long sh_strtoull_l(const char *s, char **e, int b, void *l) { (void)l; return strtoull(s, e, b); }
static long double sh_strtold_l(const char *s, char **e, void *l) { (void)l; return strtold(s, e); }
static size_t sh_strftime_l(char *s, size_t n, const char *f, const BTm *b, void *l) { (void)l; return sh_strftime(s, n, f, b); }

/* ---- funciones que la capa de iOS (facade) busca por nombre en bionic ---- */
static void *sh_memset_chk(void *d, int c, size_t n, size_t dl) { (void)dl; return memset(d, c, n); }
static void  sh_arc4random_buf(void *b, size_t n) {
    static uint64_t st; if (!st) st = armGetSystemTick() | 1;
    unsigned char *p = (unsigned char *)b;
    for (size_t i = 0; i < n; i++) { st ^= st << 13; st ^= st >> 7; st ^= st << 17; p[i] = (unsigned char)(st >> 24); }
}
static int   sh_gethostname(char *b, size_t n) { snprintf(b, n, "switch"); return 0; }
static int   sh_mkstemp(char *t) {
    size_t l = strlen(t);
    if (l < 6 || strcmp(t + l - 6, "XXXXXX")) { errno = EINVAL; return -1; }
    static unsigned c;
    for (int k = 0; k < 100; k++) {
        snprintf(t + l - 6, 7, "%06x", (unsigned)((mono_ns() >> 8) + c++) & 0xffffff);
        int fd = open(t, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) return fd;
    }
    return -1;
}
static const char *sh_setlocale(int c, const char *l) { (void)c; (void)l; return "C"; }
static size_t sh_strlcpy(char *d, const char *s, size_t n) { size_t l = strlen(s); if (n) { size_t k = l >= n ? n - 1 : l; memcpy(d, s, k); d[k] = 0; } return l; }
static size_t sh_strlcat(char *d, const char *s, size_t n) { size_t dl = strnlen(d, n), sl = strlen(s); if (dl == n) return n + sl; size_t k = sl < n - dl - 1 ? sl : n - dl - 1; memcpy(d + dl, s, k); d[dl + k] = 0; return dl + sl; }

/* ---- varios ---- */
static char *sh_basename(char *p) { char *b = strrchr(p, '/'); return b ? b + 1 : p; }
static long  sh_clock(void) { return (long)(mono_ns() / 1000); }          /* CLOCKS_PER_SEC de Android = 1000000 */
static void *sh_memrchr(const void *s, int c, size_t n) { const unsigned char *p = (const unsigned char *)s + n; while (n--) if (*--p == (unsigned char)c) return (void *)p; return NULL; }
static int   sh_fnmatch(const char *p, const char *s, int f) { (void)p; (void)s; (void)f; return 1; }   /* FNM_NOMATCH */

/* ---- datos del NDK de medios: son punteros a cadenas, no funciones ---- */
#define AMF(n, s) static const char *g_amf_##n = s;
AMF(WIDTH, "width") AMF(HEIGHT, "height") AMF(MIME, "mime") AMF(DURATION, "durationUs") AMF(FRAME_RATE, "frame-rate")
AMF(COLOR_FORMAT, "color-format") AMF(COLOR_RANGE, "color-range") AMF(COLOR_STANDARD, "color-standard") AMF(STRIDE, "stride")
AMF(SLICE_HEIGHT, "slice-height") AMF(CHANNEL_COUNT, "channel-count") AMF(SAMPLE_RATE, "sample-rate") AMF(LANGUAGE, "language")
AMF(ROTATION, "rotation-degrees") AMF(ENCODER_DELAY, "encoder-delay")
#define AMFE(n) {"AMEDIAFORMAT_KEY_" #n, (void *)&g_amf_##n}

const Shim g_shims_sys[] = {
    {"__memset_chk", (void *)sh_memset_chk}, {"arc4random_buf", (void *)sh_arc4random_buf}, {"gethostname", (void *)sh_gethostname},
    {"mkstemp", (void *)sh_mkstemp}, {"setlocale", (void *)sh_setlocale}, {"strlcpy", (void *)sh_strlcpy}, {"strlcat", (void *)sh_strlcat},
    {"getegid", (void *)sh_getuid}, {"pthread_atfork", (void *)sh_zero},
    {"dup", (void *)sh_minus1}, {"dup2", (void *)sh_minus1}, {"pipe", (void *)sh_minus1}, {"poll", (void *)sh_minus1},
    {"select", (void *)sh_minus1}, {"truncate", (void *)sh_minus1},
    {"asin", (void *)asin}, {"atan", (void *)atan}, {"hypot", (void *)hypot}, {"log10", (void *)log10},
    {"bsearch", (void *)bsearch}, {"div", (void *)div},
    {"funopen", (void *)cf_open}, {"fread", (void *)sh_fread}, {"fgets", (void *)sh_fgets}, {"fgetc", (void *)sh_fgetc}, {"getc", (void *)sh_fgetc},
    {"ungetc", (void *)sh_ungetc}, {"fseek", (void *)sh_fseek}, {"fseeko", (void *)sh_fseek}, {"ftell", (void *)sh_ftell}, {"ftello", (void *)sh_ftell},
    {"rewind", (void *)sh_rewind}, {"feof", (void *)sh_feof}, {"ferror", (void *)sh_ferror}, {"clearerr", (void *)sh_clearerr},
    {"setvbuf", (void *)sh_setvbuf}, {"fdopen", (void *)sh_fdopen}, {"fscanf", (void *)sh_fscanf}, {"vasprintf", (void *)sh_vasprintf},
    {"vsscanf", (void *)vsscanf},
    {"mbrlen", (void *)mbrlen}, {"mbrtowc", (void *)mbrtowc}, {"mbtowc", (void *)mbtowc}, {"mbsrtowcs", (void *)mbsrtowcs}, {"mbsnrtowcs", (void *)mbsnrtowcs},
    {"wcrtomb", (void *)wcrtomb}, {"wcsnrtombs", (void *)wcsnrtombs}, {"wcslen", (void *)wcslen}, {"wmemchr", (void *)wmemchr},
    {"wmemcpy", (void *)wmemcpy}, {"wmemmove", (void *)wmemmove}, {"wmemset", (void *)wmemset},
    {"isdigit_l", (void *)sh_isdigit_l}, {"islower_l", (void *)sh_islower_l}, {"isupper_l", (void *)sh_isupper_l}, {"isxdigit_l", (void *)sh_isxdigit_l},
    {"iswlower_l", (void *)sh_iswlower_l}, {"strtoll_l", (void *)sh_strtoll_l}, {"strtoull_l", (void *)sh_strtoull_l}, {"strtold_l", (void *)sh_strtold_l},
    {"strftime_l", (void *)sh_strftime_l},
    {"basename", (void *)sh_basename}, {"clock", (void *)sh_clock}, {"memrchr", (void *)sh_memrchr}, {"fnmatch", (void *)sh_fnmatch},
    AMFE(WIDTH), AMFE(HEIGHT), AMFE(MIME), AMFE(DURATION), AMFE(FRAME_RATE), AMFE(COLOR_FORMAT), AMFE(COLOR_RANGE), AMFE(COLOR_STANDARD),
    AMFE(STRIDE), AMFE(SLICE_HEIGHT), AMFE(CHANNEL_COUNT), AMFE(SAMPLE_RATE), AMFE(LANGUAGE), AMFE(ROTATION), AMFE(ENCODER_DELAY),
    {"__memcpy_chk", (void *)sh_memcpy_chk}, {"__memmove_chk", (void *)sh_memmove_chk}, {"__strlen_chk", (void *)sh_strlen_chk},
    {"__strchr_chk", (void *)sh_strchr_chk}, {"__strrchr_chk", (void *)sh_strrchr_chk}, {"__strncpy_chk2", (void *)sh_strncpy_chk2},
    {"__vsnprintf_chk", (void *)sh_vsnprintf_chk},
    {"clock_gettime", (void *)sh_clock_gettime}, {"gettimeofday", (void *)sh_gettimeofday}, {"nanosleep", (void *)sh_nanosleep},
    {"usleep", (void *)sh_usleep}, {"sleep", (void *)sh_sleep}, {"sched_yield", (void *)sh_sched_yield}, {"sched_getcpu", (void *)sh_sched_getcpu},
    {"time", (void *)sh_time}, {"difftime", (void *)difftime},
    {"localtime_r", (void *)sh_localtime_r}, {"gmtime_r", (void *)sh_gmtime_r}, {"localtime", (void *)sh_localtime},
    {"gmtime", (void *)sh_gmtime}, {"mktime", (void *)sh_mktime}, {"strftime", (void *)sh_strftime},
    {"mmap", (void *)sh_mmap}, {"munmap", (void *)sh_munmap}, {"mprotect", (void *)sh_zero}, {"madvise", (void *)sh_zero},
    {"getpagesize", (void *)sh_getpagesize}, {"sysconf", (void *)sh_sysconf}, {"getauxval", (void *)sh_getauxval},
    {"prctl", (void *)sh_prctl}, {"syscall", (void *)sh_syscall}, {"getrlimit", (void *)sh_getrlimit},
    {"sched_getaffinity", (void *)sh_sched_getaffinity}, {"sched_setaffinity", (void *)sh_zero},
    {"__system_property_get", (void *)sh_sprop_get}, {"__system_property_find", (void *)sh_null}, {"__system_property_read", (void *)sh_zero},
    {"dl_iterate_phdr", (void *)sh_zero}, {"dladdr", (void *)sh_zero},
    {"getpid", (void *)sh_getpid}, {"gettid", (void *)sh_gettid}, {"getuid", (void *)sh_getuid}, {"geteuid", (void *)sh_getuid},
    {"getpriority", (void *)sh_zero}, {"setpriority", (void *)sh_zero}, {"getrusage", (void *)sh_minus1}, {"ptrace", (void *)sh_minus1},
    {"sigaltstack", (void *)sh_zero}, {"sigemptyset", (void *)sh_sigempty}, {"sigfillset", (void *)sh_sigfill},
    {"__errno", (void *)__errno}, {"__assert2", (void *)sh_assert2}, {"raise", (void *)sh_raise},
    {"__register_atfork", (void *)sh_register_atfork}, {"android_set_abort_message", (void *)sh_set_abort_message},
    {"__android_log_buf_write", (void *)sh_log_buf_write},
    {"__sF", (void *)g_sF}, {"stdin", (void *)&g_stdin_p}, {"stdout", (void *)&g_stdout_p}, {"stderr", (void *)&g_stderr_p},
    {"printf", (void *)sh_printf}, {"vprintf", (void *)sh_vprintf}, {"puts", (void *)sh_puts}, {"fprintf", (void *)sh_fprintf},
    {"vfprintf", (void *)sh_vfprintf}, {"fputs", (void *)sh_fputs}, {"fputc", (void *)sh_fputc}, {"fwrite", (void *)sh_fwrite},
    {"fflush", (void *)sh_fflush}, {"fileno", (void *)sh_fileno}, {"strcasestr", (void *)sh_strcasestr}, {"fclose", (void *)sh_fclose}, {"perror", (void *)sh_perror},
    {"fopen", (void *)fopen}, {"sprintf", (void *)sprintf}, {"vsprintf", (void *)vsprintf}, {"sscanf", (void *)sscanf}, {"remove", (void *)remove}, {"rename", (void *)rename},
    {"strerror", (void *)strerror}, {"strerror_r", (void *)sh_strerror_r}, {"strndup", (void *)strndup}, {"strtok", (void *)strtok},
    {"memalign", (void *)memalign}, {"posix_memalign", (void *)sh_posix_memalign},
    {"sincos", (void *)sh_sincos}, {"sincosf", (void *)sh_sincosf}, {"lldiv", (void *)sh_lldiv},
    {"syslog", (void *)sh_syslog}, {"openlog", (void *)sh_zero}, {"closelog", (void *)sh_zero},
    {"newlocale", (void *)sh_newlocale}, {"freelocale", (void *)sh_freelocale}, {"uselocale", (void *)sh_uselocale},
    {"__ctype_get_mb_cur_max", (void *)sh_mbcur}, {"_ctype_", (void *)g_ctype},
    {"setjmp", (void *)setjmp}, {"longjmp", (void *)longjmp}, {"sigsetjmp", (void *)setjmp}, {"siglongjmp", (void *)longjmp},
    {NULL, NULL}
};

__attribute__((constructor)) static void sys_init(void) { ctype_init(); }
