# Informe técnico detallado — SIAC v0.2

| Campo | Resultado |
|---|---|
| Repositorio | `dppablito4-oss/proyecto_SIAC` |
| Rama base | `siac/mvp` (`30326ec9`) |
| Rama de trabajo | `siac/clipboard-ui` |
| Fecha | 7 de octubre de 2026 |
| Objetivo | Portapapeles compartido seguro en LAN e interfaz orientada a escritorios de trabajo |
| Estado de pruebas físicas de portapapeles | Pendientes; no se declara funcional en hardware hasta ejecutarlas |

## 1. Resultado ejecutivo

Se implementó SIAC v0.2 como una evolución incremental del MVP estable. No se modificó Sunshine, el protocolo Limelight/Moonlight, la codificación de video, el audio ni el transporte de input. Tampoco se modificó `main` ni se creó una release estable.

El resultado incorpora:

- agente de portapapeles dentro del proceso SIAC residente en bandeja;
- canal TCP/TLS separado del streaming, directo y restringido a LAN;
- autorización explícita separada del emparejamiento Sunshine;
- texto Unicode, imágenes PNG y archivos/carpetas en ambos sentidos;
- preparación automática de archivos en caché privado y pegado normal con Ctrl+V;
- continuidad de transferencias iniciadas al volver con `Ctrl+Alt+F10`;
- streaming de archivos por bloques, límites, cancelación e integridad SHA-256;
- prevención de bucles mediante `originId` y `eventId`;
- reenvío controlado del contenido PC 02 → PC 03 conservando esos identificadores;
- nueva interfaz QML en español centrada en computadoras, portapapeles y configuración;
- pruebas automatizadas de la lógica comprobable sin dos PC;
- documentación de arquitectura, actualización, límites y validación física.

La transferencia de archivos es real, pero su integración inicial usa preparación automática en caché. No se presenta como transferencia OLE bajo demanda: `IDataObject`, `CFSTR_FILEDESCRIPTORW`, `CFSTR_FILECONTENTS` e `IStream` quedan como evolución posterior.

## 2. Auditoría y decisiones confirmadas

### 2.1 Proceso del agente

El MVP ya mantiene SIAC en la bandeja de la sesión interactiva del usuario. Ese proceso tiene acceso a `QClipboard` y sobrevive al cierre de una sesión Moonlight, por lo que se reutilizó como agente. Un servicio Windows no sería apropiado para el portapapeles de la sesión interactiva y un ejecutable adicional habría introducido instalación, IPC y ciclo de actualización sin una ventaja necesaria para este incremento.

### 2.2 Separación del streaming

El portapapeles se implementó en `app/siac/clipboard/` y usa sus propios sockets, protocolo y ciclo de vida. `SessionSwitcher` solo informa qué computadora está activa. F10 termina el streaming, pero no destruye el canal ni una transferencia ya aceptada.

### 2.3 Qt Clipboard y estrategia de archivos

Qt ya traduce texto, imágenes y listas de URL al portapapeles nativo de Windows. Para archivos, la v0.2 recorre las rutas publicadas por Explorer, transmite manifiesto y contenido por bloques, prepara una copia privada en destino y publica las URLs recibidas para que Explorer ejecute el pegado normal.

Esta estrategia conserva Ctrl+C/F10/Ctrl+V, evita carpetas compartidas y no carga archivos grandes completos en RAM. Su costo es empezar a transferir al copiar y consumir temporalmente espacio local.

### 2.4 Referencia RustDesk y licencia

Se revisó la arquitectura de RustDesk únicamente como referencia conceptual: anuncio de formatos, identificación de listas y objetos OLE/streams para archivos virtuales. Su implementación Windows deriva de FreeRDP y el repositorio usa AGPL-3.0. No se copió ni adaptó código de RustDesk; la implementación SIAC fue escrita desde cero sobre Qt y conserva GPLv3.

## 3. Implementación del canal seguro

### 3.1 Protocolo

`ClipboardProtocol` implementa frames con:

1. longitud de cabecera de 32 bits en big-endian;
2. cabecera JSON UTF-8;
3. carga binaria con tamaño declarado y limitado.

Los mensajes v1 son `hello`, `activate`, `deactivate`, `text`, `image`, `fileManifest`, `fileChunk`, `fileEnd`, `fileComplete`, `cancel` y `error`.

### 3.2 Identidad y autorización

- Cada instalación conserva identidad y certificado persistentes.
- Sunshine no concede acceso automático al nuevo canal.
- TLS cifra el transporte; la aplicación fija la huella SHA-256 autorizada.
- El UUID declarado debe coincidir con la computadora Moonlight seleccionada.
- Un cambio de certificado o UUID bloquea el peer.
- El código visible de emparejamiento se deriva simétricamente de ambos certificados y debe ser idéntico en los dos PC.
- Los peers autorizados se guardan en el grupo `siacClipboard` de `QSettings` y pueden revocarse desde la interfaz.

### 3.3 Restricción LAN

El cliente usa exclusivamente `localAddress`, no la dirección WAN activa. Se admiten rangos IPv4 privados/link-local e IPv6 ULA/link-local; se rechazan loopback y direcciones públicas. El puerto TCP fijo es `48219` y la documentación exige permitirlo solo en el perfil privado del firewall.

### 3.4 Selección y F10

Solo el peer activo y autorizado acepta contenido. Al cambiar de equipo se desactiva el anterior antes de activar el siguiente. Una ventana de gracia de cinco segundos permite aceptar contenido ya puesto en tránsito inmediatamente antes de F10; una vez recibido el manifiesto, la transferencia puede terminar aunque el peer deje de estar seleccionado.

Al conectar el siguiente equipo, el contenido remoto que ya reside en el portapapeles local puede reenviarse manteniendo su origen e identificador originales. Esto permite PC 02 → PC 03 y evita que un retorno al origen forme un bucle.

## 4. Formatos implementados

### Texto

- UTF-8, Unicode, multilínea y caracteres especiales.
- Límite inicial: 16 MiB.
- Sincronización bidireccional con deduplicación.

### Imágenes

- Datos de imagen se codifican como PNG, hasta 64 MiB y 64 megapíxeles decodificados.
- Un PNG seleccionado en Explorer continúa siendo un archivo porque las URLs tienen prioridad sobre `hasImage()`.

### Archivos y carpetas

- Uno o varios archivos, carpetas, subcarpetas, carpetas vacías y nombres Unicode.
- Bloques de 256 KiB; el receptor rechaza bloques superiores a 512 KiB.
- Máximo 10 000 entradas, 20 GiB por evento y 40 GiB de caché.
- No se siguen enlaces simbólicos.
- Se rechazan rutas absolutas, traversal, ADS, nombres reservados, componentes problemáticos, duplicados sin distinguir mayúsculas, colisiones de capitalización y un archivo usado como directorio.
- Cada archivo debe completar el tamaño declarado y superar SHA-256 antes de publicar el conjunto en el portapapeles.
- Una desconexión, cancelación o error elimina el evento incompleto.
- Ctrl+X se rechaza con un mensaje; SIAC nunca elimina el original.

## 5. Interfaz

La pantalla principal se reorganizó como **SIAC — Escritorios interconectados**:

- tarjetas compactas de las computadoras realmente registradas;
- identificación explícita del equipo local;
- estados disponible, desconectado, emparejado y sesión activa;
- conexión directa a pantalla completa sin usar el catálogo como pantalla principal;
- aplicaciones Sunshine conservadas en un menú avanzado, sin modificar su catálogo;
- navegación principal a **Mis computadoras**, **Portapapeles** y **Configuración**;
- vista de portapapeles con activación, estado, error, solicitudes pendientes, autorización, revocación, progreso, cancelación y reconexión;
- español como valor inicial si el usuario no tenía idioma persistido; las preferencias existentes se conservan.

## 6. Pruebas automatizadas

Las pruebas Qt conservan todos los casos del MVP y agregan cobertura para:

- framing fragmentado y varios mensajes en una lectura;
- traversal y rutas Windows inseguras;
- deduplicación de eventos;
- autorización, certificado y UUID cambiados;
- contenido únicamente desde peer seleccionado o transferencia existente;
- código de emparejamiento simétrico;
- aceptación de rangos LAN y rechazo de IP públicas/loopback;
- colisiones de rutas del manifiesto y archivo usado como directorio.

También siguen cubiertos los estados de F9/F10, cancelación diferida, fallo recuperable, atajos inválidos, selección de aplicación y exclusión del equipo local.

Resultado del commit de código `dd790aad`: **23 métodos de prueba ejecutados con código de salida 0** dentro del build Windows x64. El script de compilación detiene el job ante cualquier fallo; el paso x64 terminó correctamente. Qt Test cuenta además sus casos de inicialización y limpieza.

La verificación estática de integración ejecuta 16 aserciones y `git diff --check`; es complementaria y no sustituye Qt Test ni hardware.

## 7. Compilación y artefactos

- Workflow de código: [GitHub Actions 37717294591](https://github.com/dppablito4-oss/proyecto_SIAC/actions/runs/37717294591) — aprobado.
- Commit validado: `dd790aad7eed9a460fc874f38d0004e8b39b9e90`.
- Windows x64 portable: [Moonlight-Windows-x64-dd790a (40,4 MiB)](https://github.com/dppablito4-oss/proyecto_SIAC/actions/runs/37717294591/artifacts/11524971741).
- Windows ARM64, macOS, Linux AppImage y Steam Link: compilación, empaquetado y artefactos aprobados en el mismo workflow.

Se corrigió durante CI una incompatibilidad confirmada con Qt 5.14: `QSslSocket::sslErrors` es un nombre sobrecargado y el compilador Steam Link no podía deducir la señal. La conexión ahora selecciona explícitamente la sobrecarga `QList<QSslError>` y ese objetivo volvió a compilar.

## 8. Archivos modificados

### Núcleo

- `app/siac/clipboard/clipboardmanager.{h,cpp}`
- `app/siac/clipboard/clipboardpeer.{h,cpp}`
- `app/siac/clipboard/clipboardprotocol.{h,cpp}`
- `app/backend/computermanager.{h,cpp}`
- `app/siac/sessionswitcher.{h,cpp}`
- `app/app.pro`

### QML y recursos

- `app/gui/ClipboardView.qml`
- `app/gui/PcView.qml`
- `app/gui/SettingsView.qml`
- `app/gui/main.qml`
- `app/res/siac_clipboard.svg`
- `app/qml.qrc`
- `app/resources.qrc`
- `app/settings/streamingpreferences.cpp`

### Pruebas

- `tests/siac/tst_sessionswitchplanner.cpp`
- `tests/siac/siac-tests.pro`
- `tests/siac/source-integration.tests.ps1`

### Documentación

- `README.md`
- `CHANGELOG_SIAC.md`
- `docs/SIAC_CLIPBOARD_ARCHITECTURE.md`
- `docs/SIAC_CLIPBOARD_TESTS.md`
- `docs/SIAC_LIMITATIONS.md`
- `docs/SIAC_UPDATE_V02.md`
- `INFORME_SIAC_V02.md`

## 9. Limitaciones y riesgos pendientes

- No se ejecutó todavía Ctrl+C/Ctrl+V entre dos PC Windows físicos; por tanto texto, imagen y archivos no se declaran aprobados en hardware.
- No se validó Explorer con archivos mayores a 4 GiB, archivos bloqueados, desconexión real ni nombres extremos.
- No se repitieron pruebas físicas de streaming, audio, teclado o cuatro PC. La estabilidad indicada para el MVP proviene de la prueba previa informada por el usuario.
- El caché automático no es OLE/IStream bajo demanda.
- El firewall de cada PC debe conservar el puerto 48219 en perfil privado.
- La dirección LAN guardada por Moonlight debe corresponder a una interfaz alcanzable entre los equipos.
- Steam/gamepad y componentes originales se conservaron para reducir regresiones, aunque no son parte de la experiencia principal SIAC.

## 10. Checklist mínimo para dos PC

1. Instalar el mismo commit portable en PC 01 y PC 02.
2. Confirmar el PC local explícito y que Sunshine/streaming funcionan antes de activar portapapeles.
3. Permitir TCP 48219 solo en red privada.
4. Activar portapapeles en ambos PC, comparar el mismo código corto y autorizar ambos sentidos.
5. Copiar texto Unicode PC 02 → F10 → pegar en PC 01; repetir al revés.
6. Copiar un archivo pequeño PC 02 → pulsar F10 inmediatamente → esperar el estado listo → pegar en cualquier carpeta de PC 01.
7. Repetir con varios archivos, una carpeta, una carpeta vacía y nombres Unicode.
8. Comparar `Get-FileHash -Algorithm SHA256` entre original y copia.
9. Copiar una captura y pegarla en Paint; comprobar por separado que un PNG de Explorer se trata como archivo.
10. Cortar la red durante una transferencia, confirmar error, caché incompleto eliminado y reconexión posible.
11. Pulsar F9 rápidamente y F10 durante conexión; comprobar una sola sesión, retorno local y ninguna tecla Ctrl/Alt atascada.
12. Desactivar o cerrar el agente remoto y confirmar que el streaming sigue operativo con un estado claro de portapapeles no disponible.

## 11. Commits del incremento

- `288dfec0` — diseño del protocolo seguro.
- `52e0149d` — agente autenticado y formatos de portapapeles.
- `9f4130ef` — interfaz SIAC y documentación de uso.
- `dc05e836` — compatibilidad de la señal SSL con Qt 5.14.
- `3773f6ce` — endurecimiento de pairing, LAN, manifiestos, F10 y SHA-256.
- `26e432c2` — dependencias explícitas del protocolo para Qt 5/6.
- `dd790aad` — continuidad de transferencias al cambiar de peer, hash incremental y límites adicionales.
- Commit documental posterior — informe final, resultados de CI y registro de cambios; no altera binarios.

Todos los commits se publicaron únicamente en `siac/clipboard-ui`. `main` y las ramas de release no fueron modificadas.
