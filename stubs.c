/* Un stub distinto por cada simbolo no resuelto: si el codigo cargado lo llama,
 * se escribe su NOMBRE en el log y el programa se detiene (asi sabemos que shim falta). */
#include <string.h>
#include <stdlib.h>
#include "sdt.h"

#define NSTUB 1024
static const char *g_stub_names[NSTUB];
static int g_nstub;
static void stub_hit(int i);
#include "stubs.inc"

/* Funciones del NDK de Android y de EGL: no detienen el programa. Se anota la llamada y se
 * devuelve 0 (error/NULL), para que Unity siga y veamos TODO lo que intenta usar. */
#define NSOFT 256
static const char *g_soft_names[NSOFT];
static int g_nsoft;
static void soft_hit(int i);
#include "soft.inc"
static int is_soft(const char *n) {
    static const char *const pre[] = { "AAudio", "AMedia", "AImage", "AHardware", "ASensor", "ALooper", "ANativeWindow",
        "AAsset", "AConfiguration", "AInput", "ASharedMemory", "AChoreographer", "ATrace", "ASurface", "egl", NULL };
    for (int i = 0; pre[i]; i++) if (!strncmp(n, pre[i], strlen(pre[i]))) return 1;
    return 0;
}
static void soft_hit(int i) { LOG("[ndk] %s() -> 0\n", g_soft_names[i]); }

void *stub_make(const char *name) {
    if (is_soft(name)) {
        for (int i = 0; i < g_nsoft; i++) if (!strcmp(g_soft_names[i], name)) return g_soft_fns[i];
        if (g_nsoft < NSOFT) { g_soft_names[g_nsoft] = strdup(name); return g_soft_fns[g_nsoft++]; }
    }
    for (int i = 0; i < g_nstub; i++) if (!strcmp(g_stub_names[i], name)) return (void *)g_stub_fns[i];
    if (g_nstub >= NSTUB) return (void *)g_stub_fns[NSTUB - 1];
    g_stub_names[g_nstub] = strdup(name);
    return (void *)g_stub_fns[g_nstub++];
}
static void stub_hit(int i) {
    LOG("!! FALTA SHIM: se llamo a '%s' (stub %d)\n", i < g_nstub ? g_stub_names[i] : "?", i);
    fatal_wait("simbolo sin implementar");
}
