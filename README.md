# SDT-NX (hito 4: diagnostico)

Cargador experimental para ejecutar en Nintendo Switch (homebrew, `.nro`) las librerias de Android
de un port de *Sonic Dream Team*. **Todavia no muestra el juego**: es una herramienta de diagnostico
que escribe un registro de lo que ocurre al cargar las librerias.

## Estado
- Carga `libmain.so`, `libil2cpp.so` y `libunity.so` (ARM64, Android) con un cargador ELF propio.
- Ejecuta su codigo en memoria ejecutable real (respetando la regla de Horizon: no se puede pasar de RW a RX).
- Emula la parte de Android/bionic que necesitan: hilos, mutex, tiempo, memoria, ficheros, JNI falso, etc.
- `libil2cpp.so` abre el APK y carga `UnityFramework.arm64` (Mach-O de iOS) con su propio cargador.
- **No hay graficos, audio ni mando.** Los shaders del juego son solo de Vulkan y el homebrew de Switch no tiene Vulkan.
  Eso es el principal obstaculo del proyecto y esta sin resolver.

## Que necesitas (tu copia legal del juego)
Este repositorio **no incluye nada del juego**. Del APK de la version que uses (probado con 1.4.1 y 1.10.0):

- `libmain.so`, `libil2cpp.so`, `libunity.so` de `lib/arm64-v8a/`
- El propio APK, con el nombre `game.apk`

**Las tres librerias y el APK deben ser de la misma version.**

## Estructura en la SD
```
sdmc:/switch/sdt/
  sdt_loader.nro          el programa
  game.apk                el APK del juego (renombrado)
  lib/
    libmain.so
    libil2cpp.so
    libunity.so
  files/                  lo crea el programa
  log.txt                 lo crea el programa (el registro; es lo importante)
  unresolved.tsv          lo crea el programa (simbolos que faltaban al cargar)
```

## Como se ejecuta
Hace falta toda la memoria de la consola, asi que **no funciona desde el album (modo applet)**.
Abrelo en modo aplicacion: con un forwarder, o manteniendo **R** al abrir un juego y eligiendolo desde el hbmenu.
Arriba debe poner "hito 4". Al terminar o detenerse, pulsa **+** para salir y revisa `log.txt`.

Si se detiene con `FALTA SHIM: <nombre>`, es una funcion de Android que aun no esta emulada.
Si se cierra solo, mira `atmosphere/crash_reports/`.

## Como compilar
Con devkitPro (`devkitA64` + `libnx`):
```
make
```
O sin instalar nada, con GitHub Actions: el flujo `.github/workflows/build.yml` compila con la imagen
`devkitpro/devkita64` y deja el `.nro` como artefacto (`sdt_loader`).

## Archivos
| Archivo | Funcion |
|---|---|
| `main.c` | Programa principal: entorno, carga de librerias, llamadas a JNI |
| `elfload.c/.h` | Cargador ELF (relocaciones, RELR, permisos por segmento) |
| `nxplat.c` | Memoria de la Switch para el cargador |
| `vm.c` | `mmap`/`mprotect` con memoria ejecutable |
| `vfs.c` | Ficheros y directorios con el formato de Android |
| `pthr.c` | Hilos, mutex, condiciones, semaforos y TLS por hilo |
| `sys.c`, `shims.c`, `shims_direct.inc` | Resto de funciones del sistema |
| `jni.c` | JavaVM/JNIEnv falsos |
| `dl.c` | `dlopen`/`dlsym` y registro de modulos |
| `stubs.c`, `stubs.inc`, `soft.inc` | Avisos de funciones sin implementar |

## Referencias
Tecnica conocida como *so-loader*, usada en otros ports (p. ej. los de PS Vita de TheOfficialFloW y,
para Switch, `gdash_nx`, de licencia MIT). Este codigo es independiente; esos proyectos se
consultaron como referencia.

## Aviso
Proyecto no oficial, sin relacion con SEGA ni con los autores del port. Usalo solo con una copia
del juego que poseas legalmente.
