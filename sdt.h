#ifndef SDT_H
#define SDT_H
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include "elfload.h"

/* log + utilidades (main.c) */
void LOG(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void canary_fix(void);                 /* restaura TP+0x28 tras cualquier IPC (ver notas) */
void fatal_wait(const char *why);      /* muestra el motivo y espera '+'; no vuelve        */
extern int g_guest;                    /* 1 mientras se ejecuta codigo de las .so          */

/* ficheros virtuales / funopen (cfile.c) */
typedef struct CFile CFile;
CFile *cf_of(const void *f);
void  *cf_open(const void *cookie, int (*rd)(void *, char *, int), int (*wr)(void *, const char *, int), long (*sk)(void *, long, int), int (*cl)(void *));
int    cf_getc(CFile *c);
int    cf_ungetc(CFile *c, int ch);
size_t cf_fread(CFile *c, void *p, size_t sz, size_t n);
size_t cf_fwrite(CFile *c, const void *p, size_t sz, size_t n);
char  *cf_fgets(CFile *c, char *b, int n);
int    cf_fseek(CFile *c, long off, int wh);
long   cf_ftell(CFile *c);
int    cf_feof(CFile *c);
int    cf_ferror(CFile *c);
void   cf_clearerr(CFile *c);
int    cf_close(CFile *c);

/* hilos y TLS del codigo cargado (pthr.c) */
void     guest_tls_install(void);      /* da bloque TLS (TPIDR_EL0) al hilo actual */
uint32_t guest_tid(void);              /* id del hilo actual (1 = principal)       */

/* tabla de shims */
typedef struct { const char *n; void *p; } Shim;
extern const Shim g_shims_pthr[];
extern const Shim g_shims_sys[];
extern const Shim g_shims_vfs[];

/* memoria virtual del codigo cargado (vm.c) */
void *vm_mmap(void *addr, size_t len, int prot, int flags, int fd, long off);
int   vm_munmap(void *addr, size_t len);
int   vm_mprotect(void *addr, size_t len, int prot);
long  vfs_pread(int fd, void *buf, size_t n, long off);

/* stubs por simbolo (stubs.c) */
void *stub_make(const char *name);

/* plataforma Switch (nxplat.c) */
void nx_platform(ElfPlatform *p);

/* shims (shims.c / dl.c) */
void *shim_lookup(const char *name);
void *sdt_dlopen(const char *path, int flags);
void *sdt_dlsym(void *h, const char *name);
int   sdt_dlclose(void *h);
char *sdt_dlerror(void);
int   mod_load(const char *dir, const char *name, const ElfPlatform *pl);
ElfModule *mod_find(const char *name);
void *mod_export(const char *sym);

/* JNI falso (jni.c) */
/* rutas del port en la SD (las devuelve el JNI falso a Unity) */
#define SDT_BASE  "sdmc:/switch/sdt"
#define SDT_LIB   SDT_BASE "/lib"
#define SDT_FILES SDT_BASE "/files"
#define SDT_APK   SDT_BASE "/game.apk"
void  jni_init(void);
void *jni_new_object(const char *cls);
void *jni_vm(void);
void *jni_env(void);
void *jni_new_string(const char *utf);
int   jni_find_native(const char *cls, const char *name, void **fn, void **clsobj);
#endif
