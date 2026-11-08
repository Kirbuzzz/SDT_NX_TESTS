# Reaweaken NX

**Rayman Adventures (Reawaken)** para Nintendo Switch. Carga de forma nativa `libuaf.so` (el motor UAF de Ubisoft,
Android arm64) con un so-loader basado en [gdash_nx](https://github.com/), emula la capa de Android que le falta
(JNI, OpenSL ES, AAssetManager) y usa OpenGL ES 2 a traves de mesa.

> **Estado: v0.1, sin probar en consola.** Todo el codigo portable esta probado en PC (ver abajo), pero la parte que
> habla con libnx/mesa/SDL2 y el arranque del motor aun no se han ejecutado en una Switch real. Es normal que la primera
> prueba falle: el proyecto esta hecho para que el fallo deje un `log.txt` util. Lee `docs/PRIMERA_PRUEBA.md`.

## Que hace

1. **Pantalla de arranque** (con el arte "Rayman Adventures REAWAKEN"): extrae `libuaf.so` y `assets/` del apk,
   baja el manifest del servidor de Reawaken y descarga/verifica el **obb** (normal 424 MB o HD 506 MB), con
   barra de progreso, pausa/reanudacion y MD5.
2. **Arranca el motor**: replica la secuencia de la capa Java de la app (`InitGlobal`, `setupDeviceInfo`,
   `initNativeEngine`, `init`, ...) y entra al bucle `step()` + entrada + `eglSwapBuffers`.

## Lo que necesitas

- Una Switch con homebrew. **Abre el `.nro` manteniendo `R` pulsado sobre un juego** (modo "game override"): el cargador
  necesita casi toda la memoria y llamadas de sistema que el modo applet (album) no da. Sin eso solo funciona la pantalla
  de descarga y te avisa.
- Tu propio `Reawaken.apk` (mismo que usa el proyecto Reawaken). **No se distribuye con este repositorio.**
- Internet solo para bajar el obb la primera vez.

## Instalacion en la SD

```
sdmc:/switch/Reaweaken/
├── Reaweaken.nro
└── Reawaken.apk        <- tu apk
```
El resto se crea solo: `lib/libuaf.so`, `assets/`, `obb/`, `data/` (partidas y `log.txt`), `cacert.pem`, `config.txt`.
(La carpeta se puede llamar como quieras: las rutas salen de donde esta el `.nro`.)

## Controles

| | Pantalla de descarga | Juego |
|---|---|---|
| Tactil | | toques directos al motor |
| Stick izq. / cruceta | | movimiento (el motor lo consulta como un mando Android) |
| A / B / X / Y | A: jugar o descargar normal, Y: HD o cambiar version, X: verificar | por posicion: B = boton inferior |
| Stick derecho + R3 | | cursor y "toque" (para jugar acoplado sin tactil) |
| B | pausa la descarga | |
| Minus | | "atras" |
| Plus + Minus | salir | salir |

## `config.txt`

| clave | valor | que hace |
|---|---|---|
| `screen_width`, `screen_height` | `-1` | resolucion; `-1` = 1280x720 portatil, 1920x1080 acoplado |
| `autoplay` | `0` | `1` = si todo esta listo, entra al juego sin pulsar A |
| `log_jni` | `1` | anota en el log cada llamada JNI desconocida (poner `0` cuando funcione) |
| `log_imports` | `1` | anota `dlopen` y assets que faltan |
| `log_engine` | `1` | vuelca al log lo que el motor imprime (UbiServices, etc.; con tope de lineas) |
| `log_net` | `1` | anota a que servidores intenta conectar el motor (`getaddrinfo` / `connect`) |
| `net_block` | `0` | `1` = los hosts de Ubisoft (`*.ubi.com`...) fallan al instante en vez de colgarse |
| `connection_status` | `1` | lo que se le informa al motor: `0` sin red, `1` wifi, `2` datos moviles |

## `patches.txt` (parches por nombre de funcion)

Se crea una plantilla la primera vez. Permite hacer que una funcion de `libuaf.so` devuelva un valor (`ret 0 <nombre>`)
o no haga nada (`void <nombre>`) **sin recompilar**; se aplican justo antes de que el codigo pase a ejecutable.
Pensado para sortear el login de UbiServices (los servidores de Ubisoft ya no existen). Mira `docs/MOD_MENU.md`.

## Compilar (GitHub Actions)

1. Crea un repositorio nuevo y sube **el contenido** de esta carpeta (`.github/` incluida: es una carpeta oculta, si la
   web no la sube, crea `.github/workflows/build.yml` a mano con *Add file → Create new file*).
2. Se compila solo en cada push (o *Actions → Build NRO → Run workflow*) y deja el artefacto **Reaweaken**
   (un zip con `Reaweaken.nro`).
3. Localmente: `docker run --rm -v "$PWD":/w -w /w devkitpro/devkita64 sh -c "dkp-pacman -Sy --noconfirm switch-portlibs && make"`.

Dependencias (portlibs): `switch-sdl2 switch-mesa switch-libdrm_nouveau switch-zlib switch-curl switch-mbedtls`.

## Arquitectura

```
main.c        heap (so + newlib) -> EGL -> lanzador -> game_boot -> bucle
ui.c          EGL + pantalla por software subida como textura GLES2 (sin framebuffer de libnx)
launcher.c    apk -> manifest -> obb, con la pantalla de rdl_screen
rdl/          descargador (libcurl+mbedTLS), apk (zip), PNG propio, texto, pantalla, MD5   <- probado en PC
game.c        so_load/relocate/resolve -> JNI_OnLoad -> secuencia de arranque UAFJNILib -> step()
jni_fake.c    JNIEnv/JavaVM falsos; despacho por nombre de metodo (lista sacada del apk, ver abajo)
imports.c     tabla de simbolos que se le dan a libuaf.so (375 de 375 cubiertos)
opensl*.c     OpenSL ES sobre SDL2 para Wwise (mezclador con colas)                       <- probado en PC
aasset.c      AAssetManager sobre la carpeta assets/
input.c       tactil + mando; el motor consulta el mando por JNI (isGamePadKeyPressed...)
so_util pthr libc_shim net_shim util prefs      heredados de gdash_nx
```

Como se averiguo lo que el motor espera de Java: se cruzaron los nombres de metodo de `com.ubisoft.*` del dex con las
cadenas de `libuaf.so`, y se desensamblaron `GameActivity.initEngine`, `GL2JNIView.init`, `Renderer.onSurfaceCreated` y
`onTouchEvent` para copiar la secuencia y los argumentos. Detalle en `docs/DESARROLLO.md`.

## Pruebas en PC

```
sh tests/run.sh                    # descargador, apk, manifest + OpenSL ES   (170 comprobaciones)
sh tests/run_gfx.sh <logorc.png>   # PNG (57 casos), texto, pantallas           (407 comprobaciones)
```
Con ASan + UBSan. No existen pruebas del cargador/JNI/EGL/entrada: dependen de libnx y del hardware.

## Licencia

MIT (ver `LICENSE`), con los creditos de `CREDITS.md`. Rayman Adventures es de Ubisoft; este repositorio no contiene
ninguno de sus archivos.
