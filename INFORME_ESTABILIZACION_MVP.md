# Informe de estabilización del MVP SIAC

Fecha: 2026-10-07  
Rama: `siac/mvp`  
Repositorio: `dppablito4-oss/proyecto_SIAC`

## Alcance

Este incremento estabiliza el cambio secuencial de escritorios antes de las pruebas físicas en Windows. No modifica `main`, no cambia el protocolo de streaming de Moonlight/Limelight, no introduce conexiones concurrentes y no aborda identidad gráfica ni instalador.

## Hallazgos confirmados y correcciones

### 1. Solicitudes duplicadas durante el arranque

**Causa confirmada.** `SessionSwitcher::launchComputer()` liberaba `m_SwitchInProgress` antes de emitir `sessionRequested`. Entre la creación de `Session`, la construcción QML, `Session::initialize()` y `Session::start()` el controlador aceptaba nuevos F9. Cada pulsación podía crear otro objeto `Session`; después, todos competían por `Session::s_ActiveSessionSemaphore`.

**Solución.** Se extrajo `SessionTransitionState`, con fases explícitas y un token monotónico. La transición permanece ocupada durante cierre, lanzamiento diferido, creación, inicialización y conexión. Solo vuelve a aceptar F9 cuando la sesión notificó `connectionStarted` o cuando el fallo restauró el estado local. F10 se mantiene disponible en todas las fases.

### 2. Lanzamiento diferido no cancelable

**Causa confirmada.** `sessionEnded()` usaba `QTimer::singleShot()` con un lambda que capturaba el UUID. `returnLocal()` no podía retirar ese callback, por lo que una conexión remota podía comenzar después de F10.

**Solución.** El lambda se sustituyó por un `QTimer` miembro cancelable. El callback exige que coincidan el token y la fase actuales. F10 detiene el temporizador e invalida el token anterior. También se conserva la referencia a la `Session` administrada: si existe pero `Session::get()` todavía es nulo, se cancela antes de `start()`, se libera SDL si ya fue inicializado y se elimina el segue QML sin adquirir el semáforo.

### 3. Contradicción del computador local

**Causa confirmada.** El planificador puro admite un local sintético, pero el controlador necesita un UUID local para excluir con certeza el propio equipo. La detección por nombre o IP podía además seleccionar implícitamente un registro equivocado.

**Solución.** El MVP exige seleccionar explícitamente un host Sunshine emparejado que represente al equipo físico local. Ya no se autodetecta ni se sustituye silenciosamente. La interfaz y la documentación explican el requisito. La capacidad de local sintético permanece únicamente en el planificador y está marcada como no integrada de extremo a extremo. El UUID local nunca entra al conjunto de destinos remotos.

### 4. Atajos inválidos o iguales

**Causa confirmada.** Los setters limitaban el rango con `qBound`, pero aceptaban la misma tecla para avanzar y volver. Los valores persistidos tampoco se validaban antes de `RegisterHotKey`; SDL recibía la misma configuración ambigua.

**Solución.** Una validación común exige teclas distintas entre F1 y F24. Se aplica al cargar preferencias, editar la interfaz y registrar atajos. Una configuración persistida inválida vuelve a F9/F10 y muestra un aviso. Un conflicto de `RegisterHotKey` mantiene un mensaje corregible. Los SpinBox regresan al valor aceptado si el setter rechaza el cambio; SDL y Windows leen los mismos valores ya validados.

### 5. Hosts con aplicaciones no utilizables

**Causa confirmada.** La elegibilidad comprobaba solamente que `appList` no estuviera vacía, mientras el lanzamiento exigía una aplicación activa, el nombre configurado o `directLaunch`. Un host podía resultar “disponible” y fallar inmediatamente.

**Solución.** La política se centralizó en `SessionSwitchPlanner::selectApplication()` y se usa tanto al formar candidatos como al lanzar. Se conserva el orden: reanudar la aplicación activa, buscar `Desktop` (o el nombre configurado) y, por último, usar una aplicación `directLaunch`. Las entradas sin ID/nombre válidos no cuentan. Si el host cambia entre selección y lanzamiento, se marca como intentado y se busca otro mediante un recorrido acotado; no hay bucles. Si ninguno sirve, se restaura el estado local y se informa la razón.

### 6. Fallos de creación QML

**Causa confirmada.** `launchSiacSession()` verificaba `Qt.createComponent()`, pero no el resultado de `createObject()`. Además, el error de componente no devolvía al controlador la `Session` recién creada, por lo que quedaban UUID activo y transición bloqueada.

**Solución.** Se comprueban ambos resultados. `sessionLaunchFailed()` elimina de forma diferida la sesión no iniciada, borra UUID y acciones pendientes, vuelve a local, muestra el error y permite reintentar.

## Pruebas añadidas

Las pruebas Qt ejercitan comportamiento del planificador y de la máquina de estados extraída:

- F9 repetido durante creación/conexión produce una sola solicitud;
- F10 invalida el lanzamiento diferido;
- F10 cancela una sesión creada pero todavía no activa;
- un fallo de conexión o creación permite otro intento;
- atajos iguales o fuera de rango son inválidos;
- selección de aplicación activa, preferida o directa;
- host sin aplicación utilizable se omite;
- ningún host utilizable devuelve un estado local recuperable;
- el computador local queda excluido.

Las verificaciones estáticas se conservan como control de integración, pero no se presentan como sustituto de Qt Test. `scripts/build-arch.bat` ahora compila y ejecuta Qt Test en el build nativo x64 de Windows; no intenta ejecutar binarios ARM64 cruzados.

## Archivos modificados

- `app/siac/sessiontransitionstate.{h,cpp}`: estado y token de transición.
- `app/siac/sessionswitcher.{h,cpp}`: cancelación, ciclo de sesión, selección local, aplicaciones y atajos.
- `app/siac/sessionswitchplanner.{h,cpp}`: validación pura de atajos y selección central de aplicación.
- `app/streaming/session.{h,cpp}`: cancelación segura antes de `start()`.
- `app/gui/main.qml`: manejo de errores de componente y `createObject()`.
- `app/gui/StreamSegue.qml`: notificaciones de conexión/fin y cancelación prearranque.
- `app/gui/SettingsView.qml`: requisito local explícito y edición coherente de atajos.
- `app/app.pro`: nuevas fuentes.
- `tests/siac/siac-tests.pro` y `tst_sessionswitchplanner.cpp`: cobertura de comportamiento.
- `scripts/build-arch.bat`: ejecución de Qt Test en Windows x64.
- `docs/SIAC_ARCHITECTURE.md`, `SIAC_INSTALLATION.md`, `SIAC_LIMITATIONS.md` y `SIAC_TESTS.md`: documentación alineada.

## Compilación y validación

- Workflow Windows previo al cambio, ejecución `37705228816`: completó correctamente x64, ARM64 y empaquetado; también terminaron correctamente macOS, AppImage y Steam Link.
- Comprobación estática local: 11/11 aserciones aprobadas.
- `git diff --check`: aprobado.
- Compilación local y Qt Test local: no disponibles porque este equipo no tiene Qt/qmake, MSVC ni Windows SDK.
- Workflow del código corregido, commit `ee7cdec2`, ejecución `37707269326`: aprobado. Windows 2025 con Qt 6.12 compiló x64, ejecutó Qt Test, compiló ARM64, generó paquetes y publicó artefactos. Los jobs macOS, AppImage y Steam Link también terminaron correctamente.
- Streaming, teclado real y topología de dos/cuatro PC: no ejecutados en hardware y, por tanto, no aprobados.

## Checklist breve para dos PC

1. En ambos PC, emparejar Sunshine y seleccionar explícitamente el registro del PC físico como local.
2. Pulsar F9 rápidamente varias veces durante el arranque: debe crearse una sola conexión.
3. Pulsar F10 mientras aparece el segue o mientras conecta: no debe abrirse ningún remoto después y debe quedar visible/utilizable el escritorio local.
4. Durante una sesión, retirar la red del remoto: debe mostrarse el fallo, regresar a un estado local recuperable y permitir otro F9/F10.
5. Restaurar la red, conectar y usar F10: debe volver al local sin cerrar la aplicación que estaba activa en Sunshine.
6. Escribir en ambos lados después de cada transición y comprobar que Ctrl, Alt, F9 y F10 no quedan presionadas ni llegan como entrada residual al remoto.

## Límites pendientes

Siguen pendientes las pruebas físicas con dos y cuatro PC, pérdida real de red, captura de teclado, audio/vídeo, varios monitores y medición de latencia. Este incremento no afirma resultados para esas pruebas.
