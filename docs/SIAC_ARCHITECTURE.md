# Arquitectura de SIAC

## Base auditada

La rama `siac/mvp` parte de `moonlight-stream/moonlight-qt` en el commit `de2467e433821664cdd2224aad8c89a625be1ad9` (2026-10-03). La rama `main` original del repositorio SIAC permanece intacta. Los submódulos `moonlight-common-c`, `qmdnsengine` y `SDL_GameControllerDB` están inicializados.

Componentes relevantes comprobados en el código:

- `ComputerManager` descubre por mDNS, agrega hosts manuales, sondea estado, empareja y serializa la lista en `QSettings` (`hosts` y `hostsbackup`).
- `NvComputer` contiene UUID, nombres, direcciones, certificado del servidor, estado de emparejamiento, disponibilidad y aplicaciones persistidas.
- `ComputerModel` y `AppModel` exponen hosts y aplicaciones a QML. `AppModel::createSessionForApp()` crea la sesión normal de Moonlight.
- `Session` valida el decodificador, inicia Limelight en un hilo auxiliar, crea la ventana SDL, ejecuta el bucle de streaming y limpia de forma diferida. `s_ActiveSessionSemaphore` impide dos sesiones activas.
- `SdlInputHandler::handleKeyEvent()` recibe el teclado de la ventana de streaming antes de llamar a `LiSendKeyboardEvent()`.
- `StreamSegue.qml` une el ciclo de vida C++ de `Session` con el `StackView` de Qt Quick.
- `StreamingPreferences` usa `QSettings` para resolución, modo de ventana, entrada y demás preferencias Moonlight.
- La pantalla completa real o de escritorio se implementa en `Session` mediante las banderas SDL, sin cambios al decodificador.

## Componentes SIAC

### `SessionSwitchPlanner`

Componente puro y sin dependencias de streaming. Dada una lista ordenada, el UUID local, el UUID activo y el conjunto disponible, devuelve una de tres acciones: conectar un remoto, regresar al local o no hacer nada. Es la unidad cubierta por pruebas Qt Test.

Reglas principales:

1. El UUID local nunca puede producir una conexión.
2. Los hosts no disponibles se omiten.
3. Encontrar el UUID local al recorrer el anillo significa regresar al escritorio físico.
4. Si no hay remotos disponibles desde el local, no se inicia una sesión.

### `SessionSwitcher`

Es propiedad de `ComputerManager` y reutiliza sus objetos `NvComputer`; no crea un registro paralelo de hosts ni un backend central. Sus responsabilidades son:

- cargar y guardar el grupo `siac` en `QSettings`;
- exigir que el usuario seleccione explícitamente el registro Sunshine del equipo físico local;
- sincronizar y reordenar UUID de hosts;
- registrar `Ctrl+Alt+F<n>` mediante `RegisterHotKey()` cuando Qt controla la interfaz;
- coordinar el cambio secuencial cuando hay una sesión;
- seleccionar la aplicación Sunshine configurada (por defecto `Desktop`);
- crear sesiones SIAC en pantalla completa y sin cerrar la aplicación del host al desconectar;
- administrar el icono y menú de bandeja;
- configurar el inicio de sesión en `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`.

Solo se persisten identificadores, orden y preferencias. Los certificados y el material de emparejamiento continúan bajo la implementación original de Moonlight.

## Flujo del atajo

### Interfaz local visible u oculta

1. Windows entrega `WM_HOTKEY` al hilo Qt.
2. `SessionSwitcher::nativeEventFilter()` consume el mensaje.
3. El planificador selecciona el próximo host disponible.
4. `sessionRequested` crea `StreamSegue.qml` y una `Session` normal de Moonlight.

Si Windows rechaza el registro global porque otra aplicación usa la combinación, SIAC informa el conflicto y permite elegir otras teclas F1-F24.

### Streaming en pantalla completa

1. El bucle Qt está bloqueado dentro de `Session::exec()`; por eso `WM_HOTKEY` no es suficiente.
2. `SdlInputHandler::handleKeyEvent()` reconoce exactamente Ctrl+Alt más la tecla configurada.
3. La pulsación y liberación se consumen localmente. Se liberan los modificadores ya enviados para evitar teclas atascadas.
4. `SessionSwitcher` guarda la acción pendiente e interrumpe la sesión actual.
5. `StreamSegue` espera `readyForDeletion`, cuando Limelight y SDL ya terminaron la limpieza.
6. Si el destino es remoto, inicia la siguiente sesión; si es local, minimiza la ventana SIAC y deja visible el escritorio físico.

El atajo no sustituye ni sintetiza `Win+Tab`.

## Flujo de conexión

`Host ordenado -> comprobar online/emparejado/aplicación utilizable -> elegir activa, Desktop o directLaunch -> Session::initialize -> Limelight -> ventana SDL a pantalla completa -> input/audio/vídeo existentes`

`SessionTransitionState` mantiene un token monotónico durante cierre, lanzamiento diferido, creación QML, inicialización y conexión. F9 se ignora mientras la transición no termine. F10 incrementa el token, detiene el temporizador cancelable y cancela también una `Session` creada que todavía no alcanzó `Session::start()`.

El MVP desconecta antes de conectar. Mantener sesiones preparadas no es seguro con el singleton global de Moonlight Common C, `Session::s_ActiveSession`, el semáforo de sesión y recursos únicos de SDL/decodificador.

## Dependencias y compilación

- Qt Core, Quick, Quick Controls 2, Network, SVG y Widgets (Widgets se usa solo para la bandeja).
- SDL/SDL_ttf, Moonlight Common C, OpenSSL, FFmpeg, Opus y libplacebo ya exigidos por upstream.
- Windows: Qt 6.11+ con MSVC, Visual Studio 2026 y dependencias precompiladas descargadas por `setup-deps.ps1`.
- Sunshine se ejecuta en cada host; no es una dependencia enlazada de SIAC.

## Riesgos y decisiones

- La transición incluye cierre y nueva negociación; no se promete que sea instantánea.
- El nombre `Desktop` debe coincidir con una aplicación Sunshine o cambiarse en Configuración.
- El MVP requiere un registro Sunshine emparejado para el equipo local y su selección explícita. El planificador puro conserva soporte para un local sintético, pero esa capacidad no está conectada de extremo a extremo.
- Estado online significa el último sondeo de Moonlight; un host puede caer entre el sondeo y la conexión. La ruta de error original permanece activa y el siguiente atajo sigue disponible.
- La bandeja requiere un entorno Windows con bandeja disponible.
- No se modificaron protocolo, decodificación, transporte ni autenticación.

## Licencias

Moonlight Qt y las modificaciones SIAC se mantienen bajo GPLv3. Los avisos, la licencia upstream y las licencias de submódulos se conservan. Sunshine se instala por separado desde su distribución oficial.
