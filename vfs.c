/* Ficheros y directorios para el codigo de Android (ABI bionic -> newlib/libnx):
 * flags de open, struct stat (128 bytes), struct dirent (280 bytes), pread/pwrite. */
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include "sdt.h"

static volatile int g_iolock;
static void iolock(void) { while (__atomic_exchange_n(&g_iolock, 1, __ATOMIC_ACQUIRE)) svcSleepThread(10000); }
static void iounlock(void) { __atomic_store_n(&g_iolock, 0, __ATOMIC_RELEASE); }

static int path_ok(const char *p) { return p && strncmp(p, "/proc", 5) && strncmp(p, "/sys", 4) && strncmp(p, "/dev", 4); }
static int xflags(int f) {
    int r = f & 3;
    if (f & 0x40) r |= O_CREAT;
    if (f & 0x80) r |= O_EXCL;
    if (f & 0x200) r |= O_TRUNC;
    if (f & 0x400) r |= O_APPEND;
    return r;
}
static int sh_open(const char *p, int fl, ...) {
    unsigned mode = 0;
    if (fl & 0x40) { va_list ap; va_start(ap, fl); mode = va_arg(ap, unsigned); va_end(ap); }
    if (!path_ok(p)) { errno = ENOENT; return -1; }
    int fd = open(p, xflags(fl), mode);
    LOG("open(%s, 0x%x) -> %d\n", p, fl, fd);
    return fd;
}
static int sh_open_2(const char *p, int fl) { return sh_open(p, fl, 0); }

long vfs_pread(int fd, void *buf, size_t n, long off) {
    iolock();
    off_t cur = lseek(fd, 0, SEEK_CUR);
    long total = 0;
    if (lseek(fd, off, SEEK_SET) < 0) { iounlock(); return -1; }
    while ((size_t)total < n) {
        ssize_t r = read(fd, (char *)buf + total, n - total);
        if (r < 0) { total = -1; break; }
        if (r == 0) break;
        total += r;
    }
    lseek(fd, cur, SEEK_SET);
    iounlock();
    return total;
}
static long sh_pread_chk(int fd, void *b, size_t n, long off, size_t bl) { (void)bl; return vfs_pread(fd, b, n, off); }
static long sh_pwrite(int fd, const void *b, size_t n, long off) {
    iolock();
    off_t cur = lseek(fd, 0, SEEK_CUR);
    long r = -1;
    if (lseek(fd, off, SEEK_SET) >= 0) r = write(fd, b, n);
    lseek(fd, cur, SEEK_SET);
    iounlock();
    return r;
}
static long sh_pwrite_chk(int fd, const void *b, size_t n, long off, size_t bl) { (void)bl; return sh_pwrite(fd, b, n, off); }
static long sh_write_chk(int fd, const void *b, size_t n, size_t bl) { (void)bl; return write(fd, b, n); }

/* struct stat de bionic arm64 (128 bytes) */
typedef struct {
    uint64_t st_dev, st_ino; uint32_t st_mode, st_nlink, st_uid, st_gid;
    uint64_t st_rdev, pad1; int64_t st_size; int32_t st_blksize, pad2; int64_t st_blocks;
    int64_t at, atn, mt, mtn, ct, ctn; uint32_t u4, u5;
} BStat;
_Static_assert(sizeof(BStat) == 128, "BStat debe medir 128 bytes");
static void st_conv(const struct stat *s, BStat *b) {
    memset(b, 0, sizeof *b);
    b->st_dev = s->st_dev; b->st_ino = s->st_ino; b->st_mode = s->st_mode; b->st_nlink = s->st_nlink;
    b->st_uid = s->st_uid; b->st_gid = s->st_gid; b->st_rdev = s->st_rdev; b->st_size = s->st_size;
    b->st_blksize = 4096; b->st_blocks = (s->st_size + 511) / 512;
    b->at = s->st_atime; b->mt = s->st_mtime; b->ct = s->st_ctime;
}
static int sh_stat(const char *p, BStat *b) {
    struct stat s;
    if (!path_ok(p)) { errno = ENOENT; return -1; }
    if (stat(p, &s)) return -1;
    st_conv(&s, b); return 0;
}
static int sh_fstat(int fd, BStat *b) { struct stat s; if (fstat(fd, &s)) return -1; st_conv(&s, b); return 0; }
static int sh_access(const char *p, int mode) { (void)mode; struct stat s; if (!path_ok(p)) { errno = ENOENT; return -1; } return stat(p, &s) ? -1 : 0; }

/* struct dirent de bionic (280 bytes) */
typedef struct { uint64_t d_ino; int64_t d_off; uint16_t d_reclen; uint8_t d_type; char d_name[256]; } BDirent;
static void *sh_opendir(const char *p) { if (!path_ok(p)) { errno = ENOENT; return NULL; } return opendir(p); }
static BDirent *sh_readdir(DIR *d) {
    static BDirent b;
    struct dirent *e = readdir(d);
    if (!e) return NULL;
    memset(&b, 0, sizeof b);
    b.d_ino = e->d_ino; b.d_reclen = sizeof b; b.d_type = e->d_type;
    snprintf(b.d_name, sizeof b.d_name, "%s", e->d_name);
    return &b;
}
static int sh_closedir(DIR *d) { return closedir(d); }

/* varios */
static long  sh_lseek(int fd, long off, int wh) { return (long)lseek(fd, off, wh); }
static int   sh_fcntl(int fd, int cmd, ...) { (void)fd; (void)cmd; return 0; }
static char *sh_getcwd(char *b, size_t n) { snprintf(b, n, "sdmc:/switch/sdt"); return b; }
static char *sh_realpath(const char *p, char *o) { if (!o) o = (char *)malloc(1024); snprintf(o, 1024, "%s", p); return o; }
static int   sh_zero(void) { return 0; }
static long  sh_minus1(void) { errno = ENOSYS; return -1; }
static int   sh_isatty(int fd) { (void)fd; return 0; }
static int   sh_sigaction(int s, const void *a, void *o) { (void)s; (void)a; (void)o; return 0; }
static void *sh_signal(int s, void *h) { (void)s; (void)h; return NULL; }
static int   sh_rtmin(void) { return 34; }
typedef struct { size_t v[10]; } BMallinfo;
static BMallinfo sh_mallinfo(void) { BMallinfo m; memset(&m, 0, sizeof m); return m; }
static size_t sh_usable(void *p) { (void)p; return 0; }

const Shim g_shims_vfs[] = {
    {"mmap", (void *)vm_mmap}, {"mmap64", (void *)vm_mmap}, {"munmap", (void *)vm_munmap}, {"mprotect", (void *)vm_mprotect},
    {"open", (void *)sh_open}, {"open64", (void *)sh_open}, {"__open_2", (void *)sh_open_2},
    {"close", (void *)close}, {"read", (void *)read}, {"write", (void *)write}, {"__write_chk", (void *)sh_write_chk},
    {"pread", (void *)vfs_pread}, {"pread64", (void *)vfs_pread}, {"__pread_chk", (void *)sh_pread_chk},
    {"pwrite", (void *)sh_pwrite}, {"__pwrite_chk", (void *)sh_pwrite_chk}, {"pwrite64", (void *)sh_pwrite},
    {"lseek", (void *)sh_lseek}, {"lseek64", (void *)sh_lseek},
    {"stat", (void *)sh_stat}, {"lstat", (void *)sh_stat}, {"stat64", (void *)sh_stat}, {"fstat", (void *)sh_fstat}, {"fstat64", (void *)sh_fstat},
    {"access", (void *)sh_access},
    {"opendir", (void *)sh_opendir}, {"readdir", (void *)sh_readdir}, {"closedir", (void *)sh_closedir}, {"rewinddir", (void *)rewinddir},
    {"mkdir", (void *)mkdir}, {"unlink", (void *)unlink}, {"rmdir", (void *)rmdir},
    {"fsync", (void *)fsync}, {"ftruncate", (void *)ftruncate},
    {"fcntl", (void *)sh_fcntl}, {"getcwd", (void *)sh_getcwd}, {"realpath", (void *)sh_realpath},
    {"chmod", (void *)sh_zero}, {"fchmod", (void *)sh_zero}, {"utime", (void *)sh_zero}, {"utimes", (void *)sh_zero}, {"flock", (void *)sh_zero},
    {"readlink", (void *)sh_minus1}, {"symlink", (void *)sh_minus1}, {"link", (void *)sh_minus1}, {"isatty", (void *)sh_isatty},
    {"setenv", (void *)setenv}, {"unsetenv", (void *)unsetenv},
    {"sigaction", (void *)sh_sigaction}, {"signal", (void *)sh_signal}, {"__libc_current_sigrtmin", (void *)sh_rtmin},
    {"mallinfo", (void *)sh_mallinfo}, {"malloc_usable_size", (void *)sh_usable},
    {NULL, NULL}
};
