/* JNI falso: una JavaVM y un JNIEnv minimos. Todo lo no implementado cae en un stub
 * que escribe el NOMBRE de la funcion JNI en el log y detiene el programa. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdt.h"

typedef int32_t jint;
typedef struct { uint32_t magic; char name[100]; } JCls;
typedef struct { uint32_t magic; char *utf; } JStr;
typedef struct { uint32_t magic; JCls *cls; char name[64]; char sig[96]; } JMeth;
typedef struct { const char *name; const char *sig; void *fn; } JNativeMethod;

static void jni_stub_hit(int i);
#include "jni_names.inc"
#include "jni_stubs.inc"

static void *g_fn[JNI_N];
static const void *g_env_obj;
static void *g_vmfn[8];
static const void *g_vm_obj;

static JCls g_cls[512]; static int g_ncls;
static struct { char cls[100]; char name[64]; char sig[96]; void *fn; JCls *c; } g_nat[1024]; static int g_nnat;

static void jni_stub_hit(int i) {
    LOG("!! JNI sin implementar: %s (indice %d)\n", jni_names[i], i);
    fatal_wait("funcion JNI sin implementar");
}

static JCls *cls_get(const char *n) {
    for (int i = 0; i < g_ncls; i++) if (!strcmp(g_cls[i].name, n)) return &g_cls[i];
    if (g_ncls >= 512) return &g_cls[0];
    JCls *c = &g_cls[g_ncls++]; c->magic = 0x434c5331; snprintf(c->name, sizeof c->name, "%s", n);
    return c;
}

/* ---- JavaVM ---- */
static jint vm_destroy(void *vm) { (void)vm; return 0; }
static jint vm_attach(void *vm, void **penv, void *args) { (void)vm; (void)args; *penv = (void *)&g_env_obj; LOG("JNI AttachCurrentThread\n"); return 0; }
static jint vm_detach(void *vm) { (void)vm; return 0; }
static jint vm_getenv(void *vm, void **penv, jint ver) { (void)vm; LOG("JNI GetEnv(version 0x%x)\n", ver); *penv = (void *)&g_env_obj; return 0; }

/* ---- JNIEnv (solo lo imprescindible por ahora) ---- */
static jint   j_GetVersion(void *e) { (void)e; return 0x10006; }
static void  *j_FindClass(void *e, const char *n) { (void)e; LOG("JNI FindClass(\"%s\")\n", n); return cls_get(n); }
static void  *j_GetObjectClass(void *e, void *o) {
    (void)e; uint32_t mg = o ? *(uint32_t *)o : 0;
    if (mg == 0x4a4f424au) return ((JCls **)((char *)o + 8))[0];
    if (mg == 0x53545231u) return cls_get("java/lang/String");
    return cls_get("java/lang/Object");
}
static void  *j_NewRef(void *e, void *o) { (void)e; return o; }
static void   j_DelRef(void *e, void *o) { (void)e; (void)o; }
static uint8_t j_IsSame(void *e, void *a, void *b) { (void)e; return a == b; }
static jint   j_Zero(void) { return 0; }
static void  *j_Null(void) { return NULL; }
static void   j_Void(void) {}
static void  *j_PopLocalFrame(void *e, void *o) { (void)e; return o; }
static uint8_t j_ExceptionCheck(void *e) { (void)e; return 0; }
static void  *j_GetMethodID(void *e, JCls *c, const char *n, const char *s) {
    (void)e; LOG("JNI GetMethodID(%s, %s, %s)\n", c->name, n, s);
    JMeth *m = calloc(1, sizeof *m); m->magic = 0x4d455448; m->cls = c;
    snprintf(m->name, sizeof m->name, "%s", n); snprintf(m->sig, sizeof m->sig, "%s", s); return m;
}
static void  *j_GetStaticMethodID(void *e, JCls *c, const char *n, const char *s) {
    LOG("JNI GetStaticMethodID(%s, %s, %s)\n", c->name, n, s); return j_GetMethodID(e, c, n, s);
}
static void  *j_GetFieldID(void *e, JCls *c, const char *n, const char *s) {
    (void)e; LOG("JNI GetFieldID/GetStaticFieldID(%s, %s, %s)\n", c->name, n, s);
    JMeth *m = calloc(1, sizeof *m); m->magic = 0x4d455448; m->cls = c;
    snprintf(m->name, sizeof m->name, "%s", n); snprintf(m->sig, sizeof m->sig, "%s", s); return m;
}
void *jni_new_string(const char *utf) { JStr *s = calloc(1, sizeof *s); s->magic = 0x53545231; s->utf = strdup(utf); return s; }
static void  *j_NewStringUTF(void *e, const char *u) { (void)e; return jni_new_string(u); }
static jint   j_GetStringUTFLength(void *e, JStr *s) { (void)e; return (jint)strlen(s->utf); }
static const char *j_GetStringUTFChars(void *e, JStr *s, uint8_t *copy) { (void)e; if (copy) *copy = 0; return s->utf; }
static void   j_ReleaseStringUTFChars(void *e, JStr *s, const char *c) { (void)e; (void)s; (void)c; }
static jint   j_GetJavaVM(void *e, void **vm) { (void)e; *vm = (void *)&g_vm_obj; return 0; }
static jint   j_RegisterNatives(void *e, JCls *c, const JNativeMethod *m, jint n) {
    (void)e; LOG("JNI RegisterNatives(%s, %d metodos)\n", c->name, n);
    for (int i = 0; i < n; i++) {
        LOG("   nativo %s %s -> %p\n", m[i].name, m[i].sig, m[i].fn);
        if (g_nnat < 1024) {
            snprintf(g_nat[g_nnat].cls, sizeof g_nat[0].cls, "%s", c->name);
            snprintf(g_nat[g_nnat].name, sizeof g_nat[0].name, "%s", m[i].name);
            snprintf(g_nat[g_nnat].sig, sizeof g_nat[0].sig, "%s", m[i].sig);
            g_nat[g_nnat].fn = m[i].fn; g_nat[g_nnat].c = c; g_nnat++;
        }
    }
    return 0;
}

int jni_find_native(const char *cls, const char *name, void **fn, void **clsobj) {
    for (int i = 0; i < g_nnat; i++)
        if (!strcmp(g_nat[i].cls, cls) && !strcmp(g_nat[i].name, name)) { *fn = g_nat[i].fn; *clsobj = g_nat[i].c; return 0; }
    return -1;
}


/* ================= objetos, llamadas a metodos Java y arrays (hito 5a) =================
 * Politica: una llamada a un metodo Java NUNCA detiene el programa. Se anota (clase.metodo(firma)) y se
 * devuelve algo razonable: rutas para los metodos conocidos, un objeto NO nulo para los que devuelven
 * objetos, y 0 para el resto. Asi vemos todo lo que Unity pregunta sin que se caiga por un NULL. */
typedef struct { uint32_t magic; JCls *cls; char tag[256]; } JObj;
typedef struct { uint32_t magic; int kind; int len; void *data; } JArr;      /* kind: 'B','I','F','L' */
#define MAGIC_OBJ 0x4a4f424au
#define MAGIC_ARR 0x4a415252u
#define MAGIC_STR 0x53545231u
#define MAGIC_MET 0x4d455448u

static JObj *obj_new(const char *cls, const char *tag) {
    JObj *o = (JObj *)calloc(1, sizeof *o); o->magic = MAGIC_OBJ; o->cls = cls_get(cls);
    if (tag) snprintf(o->tag, sizeof o->tag, "%s", tag);
    return o;
}
void *jni_new_object(const char *cls) { return obj_new(cls, NULL); }

static JMeth *as_meth(void *m) { JMeth *x = (JMeth *)m; return (x && x->magic == MAGIC_MET) ? x : NULL; }
static const char *ret_of(const char *sig) { const char *r = strrchr(sig, ')'); return r ? r + 1 : "V"; }
/* "Landroid/content/res/Resources;" -> "android/content/res/Resources" */
static void cls_of_sig(const char *r, char *out, size_t n) {
    if (*r == 'L') { snprintf(out, n, "%s", r + 1); char *s = strchr(out, ';'); if (s) *s = 0; }
    else snprintf(out, n, "java/lang/Object");
}
static void *str_or_obj(const char *sig, const char *s_value) {
    if (!strcmp(sig, "Ljava/lang/String;")) return jni_new_string(s_value ? s_value : "");
    char c[128]; cls_of_sig(sig, c, sizeof c); return obj_new(c, s_value);
}
/* respuesta a un metodo Java: devuelve un jobject/jstring, o un entero */
static intptr_t java_answer(JMeth *m, void *self) {
    const char *n = m->name, *rt = ret_of(m->sig);
    JObj *o = (self && ((JObj *)self)->magic == MAGIC_OBJ) ? (JObj *)self : NULL;
    if (!strcmp(n, "getPackageName") || !strcmp(n, "getCallingPackage")) return (intptr_t)jni_new_string("com.sdt.port");
    if (!strcmp(n, "getPackageCodePath")) return (intptr_t)jni_new_string(SDT_APK);
    if (!strcmp(n, "getFilesDir") || !strcmp(n, "getNoBackupFilesDir") || !strcmp(n, "getDataDir")) return (intptr_t)obj_new("java/io/File", SDT_FILES);
    if (!strcmp(n, "getExternalFilesDir")) return (intptr_t)obj_new("java/io/File", SDT_FILES);
    if (!strcmp(n, "getCacheDir") || !strcmp(n, "getExternalCacheDir") || !strcmp(n, "getCodeCacheDir")) return (intptr_t)obj_new("java/io/File", SDT_BASE "/cache");
    if (!strcmp(n, "getObbDir") || !strcmp(n, "getExternalStorageDirectory")) return (intptr_t)obj_new("java/io/File", SDT_BASE);
    if (!strcmp(n, "getAbsolutePath") || !strcmp(n, "getPath") || !strcmp(n, "getCanonicalPath"))
        return (intptr_t)jni_new_string(o && o->tag[0] ? o->tag : SDT_BASE);
    if (!strcmp(n, "getLanguage")) return (intptr_t)jni_new_string("es");
    if (!strcmp(n, "getCountry")) return (intptr_t)jni_new_string("ES");
    if (*rt == 'L' || *rt == '[') return (intptr_t)str_or_obj(rt, NULL);
    return 0;                                                  /* Z, I, J, B, C, S: 0 */
}
static intptr_t call_any(const char *kind, void *obj, void *mid) {
    JMeth *m = as_meth(mid);
    if (!m) { LOG("JNI %s mid=%p (desconocido)\n", kind, mid); return 0; }
    intptr_t r = java_answer(m, obj);
    LOG("JNI %s %s.%s%s -> %lx\n", kind, m->cls->name, m->name, m->sig, (unsigned long)r);
    return r;
}
static long  j_callany(void *e, void *obj, void *mid) { (void)e; return (long)call_any("call", obj, mid); }
static long  j_callstatic(void *e, void *cls, void *mid) { (void)e; return (long)call_any("static", cls, mid); }
static long  j_callnv(void *e, void *obj, void *cls, void *mid) { (void)e; (void)cls; return (long)call_any("nonvirt", obj, mid); }
static void  j_callvoid(void *e, void *obj, void *mid) { (void)e; call_any("void", obj, mid); }
static void  j_callvoid_nv(void *e, void *obj, void *cls, void *mid) { (void)e; (void)cls; call_any("void-nv", obj, mid); }
static float j_callf(void *e, void *obj, void *mid) { (void)e; call_any("float", obj, mid); return 0.0f; }
static double j_calld(void *e, void *obj, void *mid) { (void)e; call_any("double", obj, mid); return 0.0; }
static float j_callf_nv(void *e, void *o, void *c, void *mid) { (void)e; (void)c; call_any("float-nv", o, mid); return 0.0f; }
static double j_calld_nv(void *e, void *o, void *c, void *mid) { (void)e; (void)c; call_any("double-nv", o, mid); return 0.0; }
static void *j_newobject(void *e, JCls *c, void *mid) { (void)e; (void)mid; LOG("JNI NewObject %s\n", c->name); return obj_new(c->name, NULL); }

/* campos */
static long j_getfield(void *e, void *obj, void *fid) {
    (void)e; JMeth *m = as_meth(fid); if (!m) return 0;
    const char *n = m->name, *s = m->sig; (void)obj;
    LOG("JNI GetField %s.%s %s\n", m->cls->name, n, s);
    if (!strcmp(n, "nativeLibraryDir")) return (long)jni_new_string(SDT_LIB);
    if (!strcmp(n, "sourceDir") || !strcmp(n, "publicSourceDir")) return (long)jni_new_string(SDT_APK);
    if (!strcmp(n, "dataDir")) return (long)jni_new_string(SDT_FILES);
    if (!strcmp(n, "SDK_INT")) return 30;
    if (*s == 'L' || *s == '[') return (long)str_or_obj(s, NULL);
    return 0;
}
static float  j_getfield_f(void *e, void *o, void *f) { (void)e; (void)o; (void)f; return 0.0f; }
static double j_getfield_d(void *e, void *o, void *f) { (void)e; (void)o; (void)f; return 0.0; }
static void   j_setfield(void *e, void *o, void *f, long v) { (void)e; (void)o; (void)f; (void)v; }

/* strings UTF-16 (solo ASCII) */
static int j_GetStringLength(void *e, JStr *s) { (void)e; return (int)strlen(s->utf); }
static const uint16_t *j_GetStringChars(void *e, JStr *s, uint8_t *c) {
    (void)e; if (c) *c = 1; size_t n = strlen(s->utf); uint16_t *w = (uint16_t *)malloc((n + 1) * 2);
    for (size_t i = 0; i <= n; i++) w[i] = (uint8_t)s->utf[i];
    return w;
}
static void j_ReleaseStringChars(void *e, JStr *s, const uint16_t *w) { (void)e; (void)s; free((void *)w); }
static void *j_NewString(void *e, const uint16_t *w, int n) { (void)e; char *b = (char *)malloc((size_t)n + 1); for (int i = 0; i < n; i++) b[i] = (char)w[i]; b[n] = 0; void *s = jni_new_string(b); free(b); return s; }

/* arrays */
static int esize(int k) { return k == 'B' ? 1 : (k == 'I' || k == 'F') ? 4 : 8; }
static JArr *arr_new(int kind, int len) { JArr *a = (JArr *)calloc(1, sizeof *a); a->magic = MAGIC_ARR; a->kind = kind; a->len = len; a->data = calloc((size_t)len + 1, (size_t)esize(kind)); return a; }
static void *j_NewByteArray(void *e, int n) { (void)e; return arr_new('B', n); }
static void *j_NewIntArray(void *e, int n) { (void)e; return arr_new('I', n); }
static void *j_NewFloatArray(void *e, int n) { (void)e; return arr_new('F', n); }
static void *j_NewObjectArray(void *e, int n, void *c, void *init) { (void)e; (void)c; JArr *a = arr_new('L', n); for (int i = 0; i < n; i++) ((void **)a->data)[i] = init; return a; }
static int   j_GetArrayLength(void *e, JArr *a) { (void)e; return a ? a->len : 0; }
static void *j_GetObjectArrayElement(void *e, JArr *a, int i) { (void)e; return (a && i >= 0 && i < a->len) ? ((void **)a->data)[i] : NULL; }
static void  j_SetObjectArrayElement(void *e, JArr *a, int i, void *v) { (void)e; if (a && i >= 0 && i < a->len) ((void **)a->data)[i] = v; }
static void *j_GetArrayElements(void *e, JArr *a, uint8_t *c) { (void)e; if (c) *c = 0; return a->data; }
static void  j_ReleaseArrayElements(void *e, JArr *a, void *p, int m) { (void)e; (void)a; (void)p; (void)m; }
static void  j_GetArrayRegion(void *e, JArr *a, int s, int n, void *buf) { (void)e; memcpy(buf, (char *)a->data + (size_t)s * esize(a->kind), (size_t)n * esize(a->kind)); }
static void  j_SetArrayRegion(void *e, JArr *a, int s, int n, const void *buf) { (void)e; memcpy((char *)a->data + (size_t)s * esize(a->kind), buf, (size_t)n * esize(a->kind)); }
static uint8_t j_IsInstanceOf(void *e, void *o, void *c) { (void)e; (void)o; (void)c; return 1; }

static void setfn(const char *name, void *fn) {
    for (int i = 0; i < JNI_N; i++) if (!strcmp(jni_names[i], name)) { g_fn[i] = fn; return; }
    LOG("jni: nombre desconocido %s\n", name);
}
static void jni_install_calls(void) {
    static const char *const T[] = { "Object", "Boolean", "Byte", "Char", "Short", "Int", "Long", NULL };
    char b[64];
    for (int i = 0; T[i]; i++) {
        snprintf(b, sizeof b, "Call%sMethod", T[i]); setfn(b, j_callany); snprintf(b, sizeof b, "Call%sMethodV", T[i]); setfn(b, j_callany); snprintf(b, sizeof b, "Call%sMethodA", T[i]); setfn(b, j_callany);
        snprintf(b, sizeof b, "CallStatic%sMethod", T[i]); setfn(b, j_callstatic); snprintf(b, sizeof b, "CallStatic%sMethodV", T[i]); setfn(b, j_callstatic); snprintf(b, sizeof b, "CallStatic%sMethodA", T[i]); setfn(b, j_callstatic);
        snprintf(b, sizeof b, "CallNonvirtual%sMethod", T[i]); setfn(b, j_callnv); snprintf(b, sizeof b, "CallNonvirtual%sMethodV", T[i]); setfn(b, j_callnv); snprintf(b, sizeof b, "CallNonvirtual%sMethodA", T[i]); setfn(b, j_callnv);
    }
    static const char *const sfx[] = { "", "V", "A", NULL };
    for (int i = 0; sfx[i]; i++) {
        snprintf(b, sizeof b, "CallVoidMethod%s", sfx[i]); setfn(b, j_callvoid);
        snprintf(b, sizeof b, "CallStaticVoidMethod%s", sfx[i]); setfn(b, j_callvoid);
        snprintf(b, sizeof b, "CallNonvirtualVoidMethod%s", sfx[i]); setfn(b, j_callvoid_nv);
        snprintf(b, sizeof b, "CallFloatMethod%s", sfx[i]); setfn(b, j_callf); snprintf(b, sizeof b, "CallStaticFloatMethod%s", sfx[i]); setfn(b, j_callf);
        snprintf(b, sizeof b, "CallDoubleMethod%s", sfx[i]); setfn(b, j_calld); snprintf(b, sizeof b, "CallStaticDoubleMethod%s", sfx[i]); setfn(b, j_calld);
        snprintf(b, sizeof b, "CallNonvirtualFloatMethod%s", sfx[i]); setfn(b, j_callf_nv); snprintf(b, sizeof b, "CallNonvirtualDoubleMethod%s", sfx[i]); setfn(b, j_calld_nv);
    }
    setfn("NewObject", j_newobject); setfn("NewObjectV", j_newobject); setfn("NewObjectA", j_newobject);
    static const char *const F[] = { "Object", "Boolean", "Byte", "Char", "Short", "Int", "Long", NULL };
    for (int i = 0; F[i]; i++) {
        snprintf(b, sizeof b, "Get%sField", F[i]); setfn(b, j_getfield); snprintf(b, sizeof b, "GetStatic%sField", F[i]); setfn(b, j_getfield);
        snprintf(b, sizeof b, "Set%sField", F[i]); setfn(b, j_setfield); snprintf(b, sizeof b, "SetStatic%sField", F[i]); setfn(b, j_setfield);
    }
    setfn("GetFloatField", j_getfield_f); setfn("GetStaticFloatField", j_getfield_f); setfn("GetDoubleField", j_getfield_d); setfn("GetStaticDoubleField", j_getfield_d);
    setfn("SetFloatField", j_setfield); setfn("SetStaticFloatField", j_setfield); setfn("SetDoubleField", j_setfield); setfn("SetStaticDoubleField", j_setfield);
    setfn("GetStringLength", j_GetStringLength); setfn("GetStringChars", j_GetStringChars); setfn("ReleaseStringChars", j_ReleaseStringChars); setfn("NewString", j_NewString);
    setfn("NewByteArray", j_NewByteArray); setfn("NewIntArray", j_NewIntArray); setfn("NewFloatArray", j_NewFloatArray); setfn("NewObjectArray", j_NewObjectArray);
    setfn("GetArrayLength", j_GetArrayLength); setfn("GetObjectArrayElement", j_GetObjectArrayElement); setfn("SetObjectArrayElement", j_SetObjectArrayElement);
    static const char *const K[] = { "Byte", "Int", "Float", NULL };
    for (int i = 0; K[i]; i++) {
        snprintf(b, sizeof b, "Get%sArrayElements", K[i]); setfn(b, j_GetArrayElements); snprintf(b, sizeof b, "Release%sArrayElements", K[i]); setfn(b, j_ReleaseArrayElements);
        snprintf(b, sizeof b, "Get%sArrayRegion", K[i]); setfn(b, j_GetArrayRegion); snprintf(b, sizeof b, "Set%sArrayRegion", K[i]); setfn(b, j_SetArrayRegion);
    }
    setfn("GetPrimitiveArrayCritical", j_GetArrayElements); setfn("ReleasePrimitiveArrayCritical", j_ReleaseArrayElements);
    setfn("IsInstanceOf", j_IsInstanceOf);
}

void jni_init(void) {
    for (int i = 0; i < JNI_N; i++) g_fn[i] = jni_stub_tab[i];
    g_fn[0] = g_fn[1] = g_fn[2] = g_fn[3] = NULL;
    g_fn[4] = (void *)j_GetVersion;
    g_fn[6] = (void *)j_FindClass;
    g_fn[15] = (void *)j_Null;                 /* ExceptionOccurred */
    g_fn[16] = (void *)j_Void;                 /* ExceptionDescribe */
    g_fn[17] = (void *)j_Void;                 /* ExceptionClear    */
    g_fn[19] = (void *)j_Zero;                 /* PushLocalFrame    */
    g_fn[20] = (void *)j_PopLocalFrame;
    g_fn[21] = (void *)j_NewRef;               /* NewGlobalRef      */
    g_fn[22] = (void *)j_DelRef;               /* DeleteGlobalRef   */
    g_fn[23] = (void *)j_DelRef;               /* DeleteLocalRef    */
    g_fn[24] = (void *)j_IsSame;
    g_fn[25] = (void *)j_NewRef;               /* NewLocalRef       */
    g_fn[26] = (void *)j_Zero;                 /* EnsureLocalCapacity */
    g_fn[31] = (void *)j_GetObjectClass;
    g_fn[33] = (void *)j_GetMethodID;
    g_fn[94] = (void *)j_GetFieldID;
    g_fn[113] = (void *)j_GetStaticMethodID;
    g_fn[144] = (void *)j_GetFieldID;          /* GetStaticFieldID  */
    g_fn[167] = (void *)j_NewStringUTF;
    g_fn[168] = (void *)j_GetStringUTFLength;
    g_fn[169] = (void *)j_GetStringUTFChars;
    g_fn[170] = (void *)j_ReleaseStringUTFChars;
    g_fn[215] = (void *)j_RegisterNatives;
    g_fn[216] = (void *)j_Zero;                /* UnregisterNatives */
    g_fn[217] = (void *)j_Zero;                /* MonitorEnter      */
    g_fn[218] = (void *)j_Zero;                /* MonitorExit       */
    g_fn[219] = (void *)j_GetJavaVM;
    g_fn[227] = (void *)j_DelRef;              /* DeleteWeakGlobalRef */
    g_fn[228] = (void *)j_ExceptionCheck;
    jni_install_calls();
    g_env_obj = g_fn;
    g_vmfn[0] = g_vmfn[1] = g_vmfn[2] = NULL;
    g_vmfn[3] = (void *)vm_destroy; g_vmfn[4] = (void *)vm_attach; g_vmfn[5] = (void *)vm_detach;
    g_vmfn[6] = (void *)vm_getenv;  g_vmfn[7] = (void *)vm_attach;
    g_vm_obj = g_vmfn;
}
void *jni_vm(void)  { return (void *)&g_vm_obj; }
void *jni_env(void) { return (void *)&g_env_obj; }
