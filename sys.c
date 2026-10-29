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
    if (is_std(f)) { char b[1024]; int n = vsnprintf(b, sizeof b, fmt, ap); LOG("%s", b); return n; }
    return vfprintf(f, fmt, ap);
}
static int sh_fprintf(FILE *f, const char *fmt, ...) { va_list ap; va_start(ap, fmt); int r = sh_vfprintf(f, fmt, ap); va_end(ap); return r; }
static int sh_vprintf(const char *fmt, va_list ap) { char b[1024]; int n = vsnprintf(b, sizeof b, fmt, ap); LOG("%s", b); return n; }
static int sh_printf(const char *fmt, ...) { va_list ap; va_start(ap, fmt); int r = sh_vprintf(fmt, ap); va_end(ap); return r; }
static int sh_puts(const char *s) { LOG("%s\n", s); return 1; }
static int sh_fputs(const char *s, FILE *f) { if (is_std(f)) { LOG("%s", s); return 1; } return fputs(s, f); }
static int sh_fputc(int c, FILE *f) { if (is_std(f)) { LOG("%c", c); return c; } return fputc(c, f); }
static size_t sh_fwrite(const void *p, size_t s, size_t n, FILE *f) { if (is_std(f)) { LOG("%.*s", (int)(s * n), (const char *)p); return n; } return fwrite(p, s, n, f); }
static int sh_fflush(FILE *f) { return (!f || is_std(f)) ? 0 : fflush(f); }
static int sh_fclose(FILE *f) { return is_std(f) ? 0 : fclose(f); }
static int sh_fileno(FILE *f) { return is_std(f) ? (int)(((char *)f - g_sF) / 152) : fileno(f); }
static char *sh_strcasestr(const char *h, const char *n) {
    size_t nl = strlen(n);
    if (!nl) return (char *)h;
    for (; *h; h++) if (!strncasecmp(h, n, nl)) return (char *)h;
    return NULL;
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

const Shim g_shims_sys[] = {
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
    {"fopen", (void *)fopen}, {"fread", (void *)fread}, {"fseek", (void *)fseek}, {"ftell", (void *)ftell}, {"fgets", (void *)fgets},
    {"feof", (void *)feof}, {"ferror", (void *)ferror}, {"clearerr", (void *)clearerr}, {"setvbuf", (void *)setvbuf},
    {"sprintf", (void *)sprintf}, {"vsprintf", (void *)vsprintf}, {"sscanf", (void *)sscanf}, {"remove", (void *)remove}, {"rename", (void *)rename},
    {"strerror", (void *)strerror}, {"strerror_r", (void *)sh_strerror_r}, {"strndup", (void *)strndup}, {"strtok", (void *)strtok},
    {"memalign", (void *)memalign}, {"posix_memalign", (void *)posix_memalign},
    {"sincos", (void *)sh_sincos}, {"sincosf", (void *)sh_sincosf}, {"lldiv", (void *)sh_lldiv},
    {"syslog", (void *)sh_syslog}, {"openlog", (void *)sh_zero}, {"closelog", (void *)sh_zero},
    {"newlocale", (void *)sh_newlocale}, {"freelocale", (void *)sh_freelocale}, {"uselocale", (void *)sh_uselocale},
    {"__ctype_get_mb_cur_max", (void *)sh_mbcur}, {"_ctype_", (void *)g_ctype},
    {"setjmp", (void *)setjmp}, {"longjmp", (void *)longjmp}, {"sigsetjmp", (void *)setjmp}, {"siglongjmp", (void *)longjmp},
    {NULL, NULL}
};

__attribute__((constructor)) static void sys_init(void) { ctype_init(); }
