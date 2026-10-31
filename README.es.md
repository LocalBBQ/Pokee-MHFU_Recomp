<p align="center"><a href="README.md">English</a> · <a href="README.ru.md">Русский</a> · <b>Español</b></p>

<p align="center">
  <a href="https://github.com/LocalBBQ/Pokee"><img src="https://img.shields.io/badge/source-LocalBBQ%2FPokee-b98335?style=flat-square&amp;labelColor=2a1a22" alt="Source: LocalBBQ/Pokee"></a>
  <a href="https://github.com/LocalBBQ/Pokee/actions/workflows/tests.yml?query=branch%3Amain"><img src="https://img.shields.io/github/actions/workflow/status/LocalBBQ/Pokee/tests.yml?branch=main&amp;event=push&amp;style=flat-square&amp;label=tests&amp;labelColor=2a1a22" alt="Unit tests on main"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-b98335?style=flat-square&amp;labelColor=2a1a22" alt="License: MIT"></a>
  <a href="docs/BUILDING.md"><img src="https://img.shields.io/badge/platforms-Windows%20%C2%B7%20Linux%20%C2%B7%20macOS%20%C2%B7%20Android-b98335?style=flat-square&amp;labelColor=2a1a22" alt="Build targets: Windows, Linux, macOS and Android"></a>
  <a href="https://github.com/LocalBBQ/Pokee/stargazers"><img src="https://img.shields.io/github/stars/LocalBBQ/Pokee?style=flat-square&amp;label=stars&amp;labelColor=2a1a22&amp;color=b98335" alt="GitHub stars"></a>
</p>

# Pokee

Pokee ejecuta **Monster Hunter Freedom Unite** (EE. UU., `ULUS-10391`, versión 1.01) en Windows, Linux, macOS y Android.

> **Este proyecto no incluye el juego.** Para instalar o compilar Pokee, usa los archivos de tu propia copia legal de esa edición.

> Esto es una traducción. Si difiere del [README en inglés](README.md), prevalece el inglés. La documentación detallada está en inglés.

## Aviso legal

**Pokee** es un proyecto independiente y de código abierto, y no está afiliado, autorizado, patrocinado ni respaldado por CAPCOM, Sony ni ninguna de sus filiales.

Monster Hunter, Monster Hunter Freedom Unite, CAPCOM, PlayStation, PSP y todas las marcas comerciales, recursos del juego, ilustraciones, audio, personajes y demás propiedad intelectual relacionados pertenecen a sus respectivos propietarios.

**Pokee** no incluye ningún recurso del juego ni archivos originales del juego: ni la imagen de disco, ni una copia del ejecutable o de los datos del juego, ni texturas, modelos, audio o vídeo del juego. Para instalar o compilar **Pokee** debes aportar los archivos de tu propia copia legal de Monster Hunter Freedom Unite (EE. UU.); el instalador comprueba esa copia y solo acepta `ULUS-10391` versión 1.01.

Para usar **Pokee**, los usuarios deben aportar los archivos necesarios a partir de su propia copia adquirida legalmente de Monster Hunter Freedom Unite (EE. UU.) para PSP.

Los usuarios son los únicos responsables de obtener, volcar, extraer y usar su copia del juego conforme a las leyes aplicables en su jurisdicción.

**Pokee** no admite, proporciona, enlaza ni fomenta el uso de copias no autorizadas o piratas del juego.

Cualquier referencia al juego original o a sus marcas comerciales se hace únicamente con fines de identificación, compatibilidad e interoperabilidad.

Las capturas de pantalla y otras representaciones del juego original solo pueden usarse para documentar o mostrar el funcionamiento de **Pokee**. Todo el contenido de terceros que aparezca sigue siendo propiedad de sus respectivos titulares.

La licencia de **Pokee** se aplica únicamente al código y los materiales originales del propio proyecto y no concede ningún derecho sobre la propiedad intelectual de terceros.

**Pokee** proporciona el software, no el juego. Debes aportar tu propia copia adquirida legalmente.

## Estado

Jugable: una partida nueva o una copiada de una PSP, la aldea y las cacerías, música, cinemáticas y guardado en el formato de la propia PSP. El juego corre a los 30 fotogramas por segundo de la PSP; la interpolación puede presentarlo hasta la frecuencia de la pantalla. El ejecutable (3095 funciones) y los 290 overlays de código se recompilan por adelantado; un intérprete cubre lo que ellos no alcanzan.

| Hecho | Sigue abierto |
| --- | --- |
| Arranque, menús, creación de personaje, la aldea y las cacerías | Los diálogos de guardado y de mensaje no dibujan nada y responden como si se confirmaran ([upstream #33](https://github.com/TeamGDB/Yakumo/issues/33)) |
| Partidas en formato PSP, incluidas partidas y misiones descargadas copiadas de una PSP; importar, exportar y copias de seguridad | |
| Gráficos Vulkan, interpolación de fotogramas, panorámico, escala y ocultación del HUD | |
| Cámara y apuntado analógicos, fijación y su marcador, armadura por capas | |
| Paquetes de texturas HD compatibles con PPSSPP | |
| Mods por reemplazo de archivos ([README del perfil](profiles/mhfu/README.md#mods)) | Mods de código |
| Efectos de sonido, música y cinemáticas | Modo de descarga (`sceHttp`, `sceNetInet`) |
| Controles reasignables de teclado, ratón y mando | |
| Ad hoc entre instancias de Pokee, en una red local o a través de un servidor ad hoc | Jugar con PPSSPP y en servidores públicos |

Los resultados de [`docs/COMPATIBILITY.md`](docs/COMPATIBILITY.md) son de Portable 3rd, de antes de redirigir este árbol; Freedom Unite todavía necesita su propia pasada en cada plataforma.

## Jugar

Las descargas para Windows y Linux (Steam Deck incluido) están en la [página de versiones](https://github.com/LocalBBQ/Pokee/releases). Compilar desde el código fuente sigue siendo totalmente compatible; los pasos están en [Primeros pasos](#primeros-pasos). La configuración inicial comprueba la imagen, prepara el juego a partir de ella y guarda los ajustes y las partidas en un directorio del usuario (o en una [carpeta portátil `data`](profiles/mhfu/README.md#portable-copy) junto al ejecutable).

Cuando exista una compilación terminada, [`docs/LINUX.md`](docs/LINUX.md) y [`docs/MACOS.md`](docs/MACOS.md) (en inglés) explican la instalación, el primer inicio y dónde viven las partidas. Android apunta a Android 10 o posterior de 64 bits con Vulkan 1.1; los controles táctiles se dibujan sobre el juego y los mandos también funcionan.

## Requisitos

Compilar desde el código fuente es una forma plenamente compatible de jugar. Se necesita:

- Tu propia copia del juego (ver arriba)
- CMake 3.20 o posterior, Ninja y un compilador de C++20
- Python 3
- SDL3, Vulkan y `glslangValidator`
- `make` y un compilador de C en macOS y Linux: la compilación hace su propio FFmpeg para la música y las cinemáticas. En Windows no hay que instalar nada para ello
- Varios gigabytes de memoria libre para compilar: el código recompilado es grande

## Primeros pasos

En resumen:

1. Prepara el ejecutable del juego a partir de tu imagen de disco: compila `Pokee` una vez sin código recompilado y ejecútalo con `--install /ruta/a/la/imagen.iso`. No hace falta ninguna herramienta de descifrado externa. Después enlaza la imagen y ese ejecutable con el perfil mediante `profiles/mhfu/scripts/prepare_game.sh`.
2. Configura, genera el código recompilado con `profiles/mhfu/scripts/generate.sh` y compila `Pokee`.
3. Recompila los overlays de código con `profiles/mhfu/scripts/build_overlays.sh` (unos 40 minutos la primera vez; si se interrumpe, continúa donde se quedó).
4. Ejecuta `out/mhfu/bin/Pokee`.

Las instrucciones completas, con todos los ajustes, están en [`profiles/mhfu/README.md`](profiles/mhfu/README.md). Todas las plataformas, Windows incluido, cuánto dura cada etapa y cómo trabajar en el código sin recompilarlo todo: [`docs/BUILDING.md`](docs/BUILDING.md).

## Controles

En un mando, los botones están donde esperas: en un mando de PlayStation el círculo confirma y la equis vuelve atrás, como indican los mensajes del juego, y el stick derecho mueve la cámara y el apuntado. Los preajustes de control (Default, Modern con los ataques en los gatillos, Left-handed, Classic keyboard y los tuyos propios) abarcan todas las acciones, y cualquiera puede asignarse a un solo botón o a una combinación como L1 + ○. El control con teclado y ratón es completo; de forma predeterminada, WASD mueve al personaje, el ratón controla la cámara y sus botones atacan. Las tablas completas están en el [README del perfil](profiles/mhfu/README.md#running).

Esc, o los dos sticks pulsados a la vez (L3+R3), abre el menú propio de Pokee. Reúne los ajustes de vídeo, audio, controles, red y partidas, incluidos la relación de aspecto, la frecuencia de fotogramas, la resolución interna y la importación de texturas HD. El primer arranque prepara el juego a partir de tu imagen de disco en la misma ventana, y basta con un mando para hacerlo.

## Hoja de ruta

- Diálogos de guardado que dibujen
- Juego ad hoc con PPSSPP y en los servidores públicos que usan los jugadores de PSP
- Android en teléfonos reales: pruebas en dispositivos, controladores gráficos y rendimiento ([upstream #127](https://github.com/TeamGDB/Yakumo/issues/127), [#17](https://github.com/TeamGDB/Yakumo/issues/17))
- Un estilo visual coherente en las pantallas de configuración, los menús y los overlays ([upstream #33](https://github.com/TeamGDB/Yakumo/issues/33))

## Cómo funciona

El ejecutable se analiza y cada instrucción de su código se convierte en C++, que se compila dentro del programa. Además, el juego carga durante la ejecución 290 overlays de código en unas pocas regiones de memoria compartidas; cada uno se recompila, los de una misma región se enlazan en una sola biblioteca, y cuando el juego carga uno, su código recompilado se instala entre fotogramas. Un intérprete ejecuta cualquier código que el conjunto recompilado no cubra, así que el juego nunca se detiene; solo va más lento en esas partes.

Alrededor de ese código hay una reimplementación del sistema de la PSP: un núcleo con hilos, semáforos, flags de eventos y temporizadores; lectura del disco directamente desde la imagen; un renderizador en Vulkan para el motor gráfico de la PSP; mezcla de voces por software para el audio; y entrada mediante SDL3.

[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) describe el modelo de ejecución y [`docs/DATA_BIN.md`](docs/DATA_BIN.md) el formato de archivo del juego. [`docs/TESTING.md`](docs/TESTING.md) contiene la prueba de humo y cómo informar de los resultados.

## Estructura del repositorio

```text
include/psprecomp/   Framework: interfaces del runtime, la memoria y el estado Allegrex
src/                 Framework: carga de ELF, decodificador, runtime, intérprete
tools/               Framework: analizador y generador de código C++
tests/               Pruebas de regresión del framework
configs/             Datos de NID de PSP y ejemplos genéricos
profiles/mhfu/     Todo lo específico de este juego: host, núcleo, renderizador,
                     audio, entrada, configuración y scripts de compilación
docs/                Arquitectura, formato de archivo, guía de perfiles, normas
```

El código recompilado se genera localmente a partir de tu copia del juego y nunca se sube al repositorio.

## Basado en PSPRecomp

**Pokee** está construido sobre [PSPRecomp](https://github.com/jessicanataliagta/PSPRecomp), un framework de recompilación estática para software de PSP. El framework no depende de ningún juego y se puede compilar por separado:

```bash
cmake -S . -B out/framework -DPSPRECOMP_PROFILE=""
cmake --build out/framework --config Release
ctest --test-dir out/framework -C Release --output-on-failure
```

Para dar soporte a otro juego, consulta [`docs/PROFILE_GUIDE.md`](docs/PROFILE_GUIDE.md). [`docs/SOURCE_PROVENANCE.md`](docs/SOURCE_PROVENANCE.md) recoge las normas sobre código escrito de forma independiente y código de terceros.

## Autores

[Pokee](https://github.com/LocalBBQ/Pokee) parte de [Yakumo](https://github.com/TeamGDB/Yakumo), el port de Portable 3rd. Ese proyecto y quienes contribuyeron a él conservan el crédito del host del que sale este árbol:

- [@MHunterG](https://github.com/MHunterG)
- [@mojitosunrise](https://github.com/mojitosunrise)

## Créditos

- [Yakumo](https://github.com/TeamGDB/Yakumo): el port original, de sus colaboradores
- [PSPRecomp](https://github.com/jessicanataliagta/PSPRecomp): el framework de recompilación sobre el que se construye el proyecto
- [SDL3](https://www.libsdl.org/): ventana, entrada y salida de audio
- [FFmpeg](https://ffmpeg.org/): decodificación de la música y el vídeo
- [Vulkan](https://www.vulkan.org/) y [MoltenVK](https://github.com/KhronosGroup/MoltenVK): renderizado
- [Dear ImGui](https://github.com/ocornut/imgui): menús y pantallas de configuración inicial de Pokee
- [tiny-AES-c](https://github.com/kokke/tiny-AES-c): instalador y compatibilidad con las partidas guardadas de PSP
- [stb_truetype](https://github.com/nothings/stb): rasterización de fuentes
- [svanheulen/mhef](https://github.com/svanheulen/mhef): documentación de la comunidad sobre el formato de archivo del juego
- [Cinzel](https://github.com/NDISCOVER/Cinzel) y [Shippori Mincho](https://github.com/fontdasu/ShipporiMincho): tipografías del logotipo, bajo la SIL Open Font License

## Licencia

El repositorio se distribuye bajo la licencia MIT; consulta [`LICENSE`](LICENSE). Los archivos de terceros conservan sus licencias junto a ellos; [`docs/SOURCE_PROVENANCE.md`](docs/SOURCE_PROVENANCE.md) enumera sus orígenes y licencias.
