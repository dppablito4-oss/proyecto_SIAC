# Informe técnico detallado — Proyecto SIAC

| Campo | Valor |
|---|---|
| Proyecto | SIAC — Sistema de Escritorios Interconectados |
| Repositorio | `dppablito4-oss/proyecto_SIAC` |
| Rama de desarrollo | `siac/mvp` |
| Base técnica | Moonlight Qt + Sunshine |
| Fecha del trabajo | 7 de octubre de 2026 |
| Commit inicial del MVP | `39dfe73603b8f6c45a3860e87754c9d21b56768a` |

## 1. Resumen ejecutivo

Se preparó un primer incremento funcional de SIAC sobre el código real de Moonlight Qt. El objetivo del incremento es permitir que un usuario sentado frente a cualquiera de cuatro computadores Windows pueda recorrer los otros equipos mediante un atajo, ver cada escritorio remoto a pantalla completa y regresar inmediatamente al escritorio físico local.

El trabajo no reconstruye el protocolo de streaming. Reutiliza el descubrimiento, emparejamiento, autenticación, transporte, vídeo, audio, teclado, mouse y ciclo de sesiones ya existentes en Moonlight Qt y Sunshine.

El MVP incorporado proporciona:

- `Ctrl+Alt+F9` para pasar al siguiente computador disponible.
- `Ctrl+Alt+F10` para regresar directamente al escritorio físico local.
- Interceptación del atajo antes de que Moonlight lo envíe al host remoto.
- Identificación explícita del computador local para impedir conexiones a sí mismo.
- Orden de navegación persistente entre los hosts.
- Omisión de hosts desconectados, no emparejados o sin aplicaciones disponibles.
- Sesiones SIAC a pantalla completa.
- Cambio secuencial: se termina la conexión actual antes de iniciar la siguiente.
- Conservación de las aplicaciones locales y de la aplicación remota de Sunshine al cambiar.
- Configuración persistente mediante `QSettings`.
- Inicio automático con Windows.
- Icono y menú en la bandeja del sistema.
- Configuración de los atajos entre F1 y F24.
- Pruebas unitarias del planificador y del reconocimiento de atajos.
- Documentación de arquitectura, instalación, compilación, pruebas y limitaciones.

El código fue publicado en la rama `siac/mvp`. La rama `main` del repositorio personal no fue modificada.

## 2. Estado inicial encontrado

El directorio inicial contenía únicamente:

- un repositorio Git limpio;
- un archivo `README.md` corto del proyecto SIAC;
- una licencia MIT inicial;
- una rama `main` enlazada al repositorio personal.

No estaba presente el código fuente de Moonlight Qt. Tampoco había herramientas de compilación C++/Qt disponibles en el entorno.

Se comprobó específicamente la ausencia de:

- Qt y `qmake`/`qmake6`;
- Visual Studio y MSVC;
- MSBuild;
- Windows SDK;
- CMake;
- Ninja;
- compiladores `cl`, GCC o Clang;
- 7-Zip.

Esta carencia impidió producir y ejecutar un binario Windows en el entorno de trabajo.

## 3. Preparación de la base Moonlight Qt

Se añadió el repositorio oficial de Moonlight Qt como remoto `upstream`:

```text
https://github.com/moonlight-stream/moonlight-qt.git
```

Se obtuvo la rama oficial `master` y se creó la rama local independiente:

```text
siac/mvp
```

La base utilizada corresponde al commit:

```text
de2467e433821664cdd2224aad8c89a625be1ad9
```

También se inicializaron correctamente los submódulos requeridos:

- `app/SDL_GameControllerDB`;
- `moonlight-common-c/moonlight-common-c`;
- `moonlight-common-c/moonlight-common-c/enet`;
- `moonlight-common-c/moonlight-common-c/nanors`;
- `qmdnsengine/qmdnsengine`.

La rama `main` original quedó intacta. Todo el desarrollo se realizó en `siac/mvp`.

## 4. Auditoría de la arquitectura existente

Antes de modificar archivos se inspeccionaron los componentes reales de Moonlight Qt.

### 4.1 Administración de hosts

Se identificó `ComputerManager` como el componente responsable de:

- descubrimiento mDNS;
- adición manual de hosts;
- sondeo periódico de disponibilidad;
- emparejamiento;
- actualización de la lista de aplicaciones;
- persistencia de hosts mediante `QSettings`;
- eliminación y renombrado de equipos.

La información de cada host reside en `NvComputer`, que contiene:

- UUID del servidor;
- nombre;
- direcciones local, remota, IPv6 y manual;
- estado online/offline;
- estado de emparejamiento;
- certificado del servidor;
- dirección activa;
- lista de aplicaciones;
- aplicación actualmente activa;
- capacidades del servidor.

Esta infraestructura fue reutilizada directamente. SIAC no mantiene una segunda base de datos de equipos.

### 4.2 Ciclo de vida de las sesiones

Se determinó que `Session` realiza las siguientes etapas:

1. validación de preferencias y capacidades;
2. comprobación del decodificador;
3. inicio asíncrono de la conexión Limelight;
4. creación de la ventana SDL;
5. ejecución del bucle de vídeo, audio e input;
6. interrupción o cierre de la conexión;
7. liberación del decodificador y de SDL;
8. limpieza diferida de Moonlight Common C;
9. notificación a QML de que la sesión puede eliminarse.

Moonlight impide que existan dos sesiones simultáneas mediante:

- `Session::s_ActiveSession`;
- `Session::s_ActiveSessionSemaphore`;
- estado global de Moonlight Common C;
- recursos únicos de SDL y del decodificador.

Por esta razón, el MVP implementa cambios secuenciales y no intenta mantener cuatro conexiones concurrentes.

### 4.3 Entrada de teclado

El procesamiento de teclado de una sesión pasa por:

```text
SDL -> SdlInputHandler::handleKeyEvent() -> conversión de tecla -> LiSendKeyboardEvent()
```

Este punto es crítico porque permite consumir F9/F10 antes de que el evento sea enviado al escritorio remoto.

También se comprobó que durante `Session::exec()` el hilo principal ejecuta el bucle SDL de forma síncrona. En ese periodo el bucle Qt normal no procesa los hotkeys globales de Windows de forma fiable. Por ello, un diseño basado únicamente en `RegisterHotKey()` habría sido insuficiente.

### 4.4 Pantalla completa

Moonlight ya implementa pantalla completa mediante SDL y las preferencias `WM_FULLSCREEN`, `WM_FULLSCREEN_DESKTOP` y `WM_WINDOWED`.

SIAC reutiliza este mecanismo. No se modificaron el motor de vídeo, los decodificadores ni los renderizadores.

### 4.5 Persistencia

Moonlight usa `QSettings` tanto para hosts como para preferencias de streaming. SIAC añadió un grupo independiente llamado `siac` dentro del mismo sistema de configuración.

### 4.6 Interfaz QML

Se identificaron como puntos principales:

- `main.qml`: ventana y navegación general;
- `PcView.qml`: registro y selección de equipos;
- `AppView.qml`: selección de aplicaciones;
- `StreamSegue.qml`: puente entre QML y `Session`;
- `SettingsView.qml`: configuración persistente;
- `ComputerModel`: modelo QML de hosts;
- `AppModel`: modelo QML de aplicaciones.

## 5. Arquitectura SIAC implementada

La funcionalidad nueva se aisló principalmente en dos componentes.

### 5.1 `SessionSwitchPlanner`

Archivos:

```text
app/siac/sessionswitchplanner.h
app/siac/sessionswitchplanner.cpp
```

Es un componente puro encargado de decidir el próximo destino. Recibe:

- lista ordenada de UUID;
- UUID del computador local;
- UUID del computador remoto activo;
- conjunto de computadores disponibles.

Devuelve una acción:

- conectar a un remoto;
- regresar al local;
- no realizar ninguna acción.

Reglas implementadas:

1. El UUID local nunca se devuelve como conexión remota.
2. El orden configurado se respeta.
3. Los equipos no disponibles se omiten.
4. Al alcanzar el equipo local durante el recorrido se regresa al escritorio físico.
5. El ciclo funciona independientemente de cuál de los cuatro equipos sea el local.

El planificador también clasifica las combinaciones de teclado para distinguir:

- siguiente computador;
- retorno local;
- tecla que debe continuar hacia Moonlight.

### 5.2 `SessionSwitcher`

Archivos:

```text
app/siac/sessionswitcher.h
app/siac/sessionswitcher.cpp
```

`SessionSwitcher` pertenece a `ComputerManager`, lo que le permite reutilizar los objetos `NvComputer` existentes.

Responsabilidades implementadas:

- cargar y guardar configuración SIAC;
- sincronizar el orden con la lista de hosts Moonlight;
- detectar el computador local por nombre o dirección;
- exigir selección manual si la detección local es ambigua;
- conocer la sesión remota activa;
- evitar cambios duplicados mientras existe una transición;
- seleccionar el siguiente destino;
- crear sesiones Moonlight para el destino;
- esperar la limpieza completa antes de conectar al siguiente host;
- registrar y liberar hotkeys globales Windows;
- administrar el inicio automático;
- crear el menú de bandeja;
- informar errores y conflictos a QML.

## 6. Implementación de los atajos

### 6.1 Interfaz local

Cuando no existe una sesión SDL activa, SIAC usa `RegisterHotKey()` de Windows.

Combinaciones predeterminadas:

```text
Ctrl + Alt + F9  -> siguiente computador
Ctrl + Alt + F10 -> escritorio local
```

Se usan identificadores dentro del rango válido para aplicaciones de Windows. También se activa `MOD_NOREPEAT` para evitar repeticiones involuntarias al mantener la tecla presionada.

Si Windows rechaza el registro porque otra aplicación utiliza el mismo atajo:

- SIAC no asume que el registro funcionó;
- muestra un mensaje de conflicto;
- permite configurar otra tecla entre F1 y F24.

### 6.2 Sesión remota a pantalla completa

Durante el streaming, los atajos se reconocen en `SdlInputHandler::handleKeyEvent()`.

El flujo es:

1. SDL entrega el evento de teclado.
2. SIAC comprueba Ctrl, Alt y la tecla de función configurada.
3. El evento se clasifica antes de `LiSendKeyboardEvent()`.
4. La pulsación SIAC se consume localmente.
5. Se liberan Ctrl y Alt remotamente para prevenir modificadores atascados.
6. Se solicita la interrupción de la sesión actual.
7. La pulsación F9/F10 no se envía al host remoto.

No se intercepta ni reemplaza `Win+Tab`.

## 7. Flujo de cambio de computador

El flujo implementado para F9 es:

```text
Atajo
  -> obtener orden persistido
  -> excluir computador local
  -> comprobar online + paired + aplicaciones
  -> omitir hosts no disponibles
  -> elegir siguiente UUID
  -> interrumpir sesión actual, si existe
  -> esperar readyForDeletion
  -> crear nueva Session
  -> iniciar streaming a pantalla completa
```

Para F10:

```text
Atajo
  -> cancelar destino remoto pendiente
  -> interrumpir sesión actual
  -> esperar limpieza
  -> limpiar UUID activo
  -> minimizar SIAC
  -> mostrar el escritorio físico local
```

Durante un cambio remoto a remoto, la interfaz Qt permanece oculta para reducir parpadeos entre sesiones.

## 8. Selección de la aplicación Sunshine

SIAC utiliza de forma predeterminada una aplicación Sunshine llamada:

```text
Desktop
```

El nombre es configurable.

La selección sigue este orden:

1. si Sunshine ya tiene una aplicación activa, se reanuda para evitar terminar procesos remotos;
2. se busca una aplicación cuyo nombre coincida con la configuración SIAC;
3. se busca una aplicación marcada para lanzamiento directo;
4. si no se encuentra ninguna opción válida, se informa el error.

## 9. Pantalla completa sin afectar Moonlight normal

Se amplió el constructor de `Session` con opciones específicas para:

- forzar pantalla completa;
- permitir o impedir que Moonlight cierre la aplicación remota al salir.

Las llamadas originales conservan los valores predeterminados, por lo que el comportamiento normal de Moonlight permanece sin cambios.

Las sesiones creadas por SIAC usan:

```text
forceFullScreen = true
allowQuitApp = false
```

Esto evita cambiar globalmente la preferencia de ventana del usuario y evita que una rotación SIAC cierre la aplicación Sunshine remota.

## 10. Manejo del computador local

SIAC intenta detectar el equipo local mediante:

- nombre reportado por Sunshine frente al hostname Windows;
- dirección local o manual frente a las interfaces de red locales.

Si encuentra exactamente una coincidencia, persiste su UUID.

Si no encuentra una coincidencia segura o encuentra una situación ambigua:

- no inicia la rotación;
- solicita seleccionar explícitamente el computador local;
- evita el riesgo de conectarse al mismo equipo.

También se agregó al menú contextual de cada host:

```text
Set as local SIAC computer
```

## 11. Configuración persistente

Dentro del grupo `siac` de `QSettings` se guardan:

- habilitación de SIAC;
- UUID del computador local;
- orden de UUID de hosts;
- nombre de la aplicación Sunshine;
- tecla de función para avanzar;
- tecla de función para regresar;
- preferencia de pantalla completa;
- inicio automático.

No se almacenan contraseñas de Sunshine, PIN ni credenciales SIAC adicionales.

## 12. Interfaz de configuración

Se añadió el grupo **SIAC - Interconnected Desktops** a `SettingsView.qml`.

Controles disponibles:

- activar/desactivar SIAC;
- seleccionar el computador local;
- seleccionar un host y moverlo arriba o abajo;
- indicar el nombre de la aplicación Sunshine;
- configurar F1-F24 para siguiente equipo;
- configurar F1-F24 para retorno local;
- activar pantalla completa SIAC;
- activar inicio con Windows;
- revisar el estado del registro de hotkeys.

## 13. Bandeja del sistema

Se añadió `QSystemTrayIcon` y se incorporó el módulo Qt Widgets al proyecto.

El menú contiene:

- abrir SIAC;
- siguiente computador;
- regresar al escritorio local;
- configuración;
- salir.

El tooltip muestra el escritorio local o el host remoto activo.

## 14. Inicio automático con Windows

La opción de inicio automático administra exclusivamente el valor:

```text
HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run\SIAC
```

Se almacena la ruta completa y entre comillas del ejecutable actual. La operación se realiza para el usuario Windows actual y no instala servicios ni tareas programadas.

## 15. Manejo de hosts desconectados

Un host se considera elegible solamente si:

- está online según `ComputerManager`;
- está emparejado;
- tiene lista de aplicaciones disponible;
- no es el computador local.

Los hosts no elegibles se omiten durante la rotación. Si no existe ningún destino válido, SIAC muestra un error y no bloquea la interfaz.

Si un host cae después del sondeo y antes de conectarse, permanece activo el manejo de errores original de Moonlight. El usuario puede continuar con F9 o regresar mediante F10.

## 16. Archivos modificados

### Integración y compilación

- `app/app.pro`
- `app/main.cpp`

### Hosts y modelos

- `app/backend/computermanager.h`
- `app/backend/computermanager.cpp`
- `app/gui/computermodel.h`
- `app/gui/computermodel.cpp`

### Sesiones e input

- `app/streaming/session.h`
- `app/streaming/session.cpp`
- `app/streaming/input/input.h`
- `app/streaming/input/keyboard.cpp`

### Interfaz

- `app/gui/main.qml`
- `app/gui/PcView.qml`
- `app/gui/SettingsView.qml`
- `app/gui/StreamSegue.qml`

### Componentes nuevos

- `app/siac/sessionswitcher.h`
- `app/siac/sessionswitcher.cpp`
- `app/siac/sessionswitchplanner.h`
- `app/siac/sessionswitchplanner.cpp`

### Pruebas nuevas

- `tests/siac/siac-tests.pro`
- `tests/siac/tst_sessionswitchplanner.cpp`
- `tests/siac/source-integration.tests.ps1`

### Documentación nueva

- `docs/SIAC_ARCHITECTURE.md`
- `docs/SIAC_INSTALLATION.md`
- `docs/SIAC_BUILD.md`
- `docs/SIAC_TESTS.md`
- `docs/SIAC_LIMITATIONS.md`
- `CHANGELOG_SIAC.md`
- actualización de `README.md`.

## 17. Pruebas automatizadas incorporadas

Se creó un proyecto Qt Test independiente para `SessionSwitchPlanner`.

Casos cubiertos:

1. el computador local nunca se conecta a sí mismo;
2. se respeta un orden personalizado;
3. se omiten computadores no disponibles;
4. después del último remoto se regresa al local;
5. el planificador contempla un local sin registro Sunshine;
6. cualquiera de cuatro PC puede actuar como local;
7. Ctrl+Alt+F9 se consume como siguiente equipo;
8. Ctrl+Alt+F10 se consume como retorno local;
9. combinaciones incompletas o con Shift se reenvían normalmente.

Además se creó una prueba PowerShell de integración estática que comprueba:

- clasificación SIAC antes del envío de teclado;
- liberación de modificadores;
- protección contra cierre de aplicación remota;
- opción de pantalla completa;
- exclusión del UUID local;
- presencia de `RegisterHotKey()`;
- ausencia de sustitución de `Win+Tab`;
- inclusión de SIAC en `app.pro`;
- enlace de Qt Widgets;
- ausencia de errores de whitespace.

## 18. Resultados de validación ejecutados

Se ejecutaron y aprobaron:

- inicialización de submódulos;
- `git diff --check`;
- 11 aserciones de integración estática;
- comprobación de delimitadores balanceados en los QML modificados;
- comprobación de archivos de documentación y pruebas;
- verificación de ausencia de whitespace final en los archivos nuevos;
- comparación del commit local con la rama remota publicada.

El commit local y el remoto coincidieron en:

```text
39dfe73603b8f6c45a3860e87754c9d21b56768a
```

## 19. Pruebas que no pudieron ejecutarse

No se pudieron ejecutar:

- compilación C++ de Moonlight/SIAC;
- prueba Qt Test compilada;
- apertura real de la interfaz QML;
- registro real de hotkeys en un ejecutable SIAC;
- transmisión entre dos PC;
- recorrido entre cuatro PC;
- audio, vídeo, teclado y mouse reales;
- desconexión física de un host durante el cambio;
- inicio automático tras cerrar sesión de Windows;
- medición de latencia y tiempo de cambio;
- pruebas con varios monitores, HDR o suspensión.

La causa es la ausencia de Qt, MSVC, MSBuild y Windows SDK, además de no disponer de cuatro equipos Sunshine dentro del entorno.

Estas pruebas se documentaron explícitamente como pendientes y no se declararon aprobadas.

## 20. Requisitos actuales para compilar

Según el código upstream auditado, la compilación Windows requiere:

- Qt 6.11 o posterior con kit MSVC;
- Visual Studio 2026 con desarrollo de escritorio C++;
- 7-Zip para el paquete;
- submódulos Git;
- dependencias descargadas mediante `setup-deps.ps1`.

Comandos principales:

```powershell
git submodule update --init --recursive
powershell -ExecutionPolicy Bypass -File .\setup-deps.ps1
```

Desde un prompt Qt/MSVC:

```bat
scripts\build-arch.bat release
```

## 21. Procedimiento recomendado para cuatro PC

En cada computador:

1. instalar Sunshine mediante su MSI oficial;
2. confirmar que el servicio se inicia;
3. abrir `https://localhost:47990`;
4. crear credenciales Sunshine;
5. confirmar una aplicación llamada `Desktop`;
6. instalar la misma compilación SIAC;
7. descubrir o agregar los otros equipos;
8. emparejar cada host mediante PIN;
9. marcar el host correspondiente como computador local;
10. configurar el mismo orden en los cuatro equipos;
11. probar F9 y F10 desde cada computador físico.

La matriz mínima de aceptación debe cubrir:

- PC01 como local;
- PC02 como local;
- PC03 como local;
- PC04 como local;
- un host apagado;
- pérdida de red durante streaming;
- persistencia después de reiniciar;
- conflicto deliberado de hotkey;
- conservación de aplicaciones locales;
- comprobación de que F9/F10 no llegan al remoto.

## 22. Limitaciones conocidas

- Existe una única sesión activa.
- El cambio no es instantáneo porque requiere desconexión y nueva negociación.
- No se han implementado sesiones calientes o concurrentes.
- El ejecutable y varios recursos visuales siguen usando el nombre Moonlight.
- El nombre `Desktop` debe coincidir con Sunshine o configurarse manualmente.
- La disponibilidad depende del último sondeo de Moonlight.
- Los hotkeys globales fuera del streaming son específicos de Windows.
- La detección automática local puede requerir corrección manual.
- No se ha producido todavía un instalador SIAC firmado.
- No existe validación física de cuatro PC en esta fase.

## 23. Seguridad y privacidad

Se conservaron las garantías existentes de Moonlight/Sunshine:

- emparejamiento mediante PIN;
- certificados de servidor;
- autenticación original;
- no desactivación de controles de seguridad;
- no incorporación de credenciales en código;
- funcionamiento LAN sin backend externo;
- ausencia de telemetría SIAC;
- ausencia de servidor central o nube.

El inicio automático solo modifica una entrada específica del usuario actual. SIAC no cambia reglas de firewall, no habilita UPnP y no abre puertos del router.

## 24. Licenciamiento

Al utilizar Moonlight Qt como base, la rama de desarrollo conserva la licencia GPLv3 de upstream y su archivo `LICENSE`.

Se conservaron:

- avisos de Moonlight Qt;
- licencia GPLv3;
- submódulos y sus historiales;
- separación de Sunshine como software instalado externamente.

## 25. Publicación realizada

Se creó el commit:

```text
39dfe736 Implement SIAC desktop switching MVP
```

Se publicó en:

```text
https://github.com/dppablito4-oss/proyecto_SIAC/tree/siac/mvp
```

No se modificó `main`, no se creó un Pull Request automáticamente y no se publicó ningún cambio en el repositorio oficial de Moonlight.

## 26. Recomendaciones para la siguiente etapa

Prioridad inmediata:

1. instalar Qt/MSVC y compilar el proyecto;
2. resolver cualquier diferencia de API detectada por el compilador;
3. ejecutar Qt Test;
4. realizar una prueba de humo con dos PC;
5. completar la matriz con cuatro PC;
6. registrar tiempos reales de cambio;
7. revisar logs de Moonlight y Sunshine durante fallos;
8. preparar identidad gráfica y nombre de ejecutable SIAC;
9. crear un instalador Windows no firmado para pruebas internas;
10. firmar únicamente después de estabilizar el MVP.

La optimización de conexiones preparadas debe aplazarse hasta disponer de mediciones. Antes de implementarla se deben medir:

- CPU;
- GPU;
- memoria RAM;
- ancho de banda;
- tiempo de negociación;
- tiempo hasta el primer fotograma;
- impacto de sesiones suspendidas;
- límites de Moonlight Common C y SDL.

## 27. Conclusión

El repositorio pasó de ser un contenedor inicial sin código Moonlight a una rama SIAC basada en el cliente oficial, con una arquitectura de cambio de escritorios integrada en los puntos reales de host, sesión, input, pantalla completa y persistencia.

El diseño prioriza la experiencia central solicitada: pulsar un atajo local y cambiar de computador sin reimplementar streaming ni debilitar el emparejamiento. El código, las pruebas fuente y la documentación están publicados. La fase pendiente más importante es compilar y validar el comportamiento en hardware Windows real con Sunshine instalado en cuatro computadores.
