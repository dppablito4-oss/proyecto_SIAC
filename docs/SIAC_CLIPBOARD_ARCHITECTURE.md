# Arquitectura del portapapeles compartido SIAC v0.2

## Decisión de proceso

El agente se integra en el proceso SIAC que ya permanece en la bandeja de la sesión interactiva. No se crea por ahora un servicio de Windows ni un segundo ejecutable: un servicio no tiene acceso directo al portapapeles del escritorio del usuario y otro proceso por usuario añadiría IPC, instalación y actualización sin aportar aislamiento necesario en este incremento.

El agente se inicia con `ComputerManager` y continúa activo aunque termine una `Session` de Moonlight. El canal de portapapeles no comparte ciclo de vida, sockets ni mensajes con Limelight.

## Componentes

### `ClipboardManager`

- observa `QClipboard::dataChanged()` en la sesión interactiva;
- clasifica texto Unicode, imagen y rutas de archivos de Explorer;
- conserva un identificador UUID y el origen de cada evento;
- mantiene el servidor TLS LAN y la conexión con el host remoto activo;
- aplica contenido remoto al portapapeles nativo;
- prepara archivos recibidos en un caché privado y publica sus rutas como `CF_HDROP` mediante `QMimeData::setUrls()`;
- expone estado, autorización, progreso, cancelación y errores a QML.

Qt usa el portapapeles nativo en Windows. La arquitectura deja aislada la captura para poder sustituirla por una ventana Win32 con `AddClipboardFormatListener` si una prueba física descubre formatos que Qt no notifica correctamente. Win32 publica `WM_CLIPBOARDUPDATE` a las ventanas registradas; no se usa sondeo.

### `ClipboardPeer`

Encapsula una conexión `QSslSocket`. El framing no interpreta datos de streaming: cada mensaje contiene longitud de cabecera, JSON UTF-8 y una carga binaria opcional. Se aplican límites antes de reservar memoria.

### `ClipboardProtocol`

Contiene validaciones puras y probables mediante Qt Test:

- versión de protocolo;
- tamaño máximo de cabecera y bloque;
- normalización de rutas relativas;
- rechazo de rutas absolutas, `..`, nombres vacíos y componentes peligrosos;
- clasificación y deduplicación de eventos;
- límites de texto, archivos y transferencia.

## Identidad, TLS y autorización

SIAC reutiliza el par RSA/certificado persistente de `IdentityManager` solo como identidad criptográfica de instalación. Esto **no** hereda confianza del emparejamiento Sunshine.

El canal aplica una autorización separada:

1. ambos extremos presentan certificado durante TLS;
2. cada extremo deriva el mismo código corto a partir de las dos huellas SHA-256; además muestra nombre y UUID Sunshine del peer;
3. el usuario autoriza o rechaza desde SIAC;
4. las huellas autorizadas se guardan en el grupo `siacClipboard` de `QSettings`;
5. un cambio de certificado para el mismo identificador se rechaza;
6. no se aceptan mensajes de portapapeles hasta que la huella esté autorizada;
7. el cliente comprueba que el UUID local declarado por el peer coincide con el host Moonlight al que pretendía conectarse.

La comparación visual del código simétrico en los dos PC durante el primer emparejamiento mitiga suplantación en esa primera conexión. No se usa descubrimiento por Internet. El servidor escucha en el puerto TCP fijo `48219`, rechaza direcciones que no sean privadas o link-local y el cliente usa exclusivamente la dirección LAN registrada; el firewall de Windows también debe limitarlo al perfil privado.

## Selección del peer

Solo se sincroniza con el host SIAC activo:

- al iniciar una transición remota, `activeComputerUuid` selecciona dirección y peer;
- el cliente envía `activate` y el agente remoto selecciona esa conexión;
- F10 envía `deactivate`, con lo que nuevos cambios dejan de sincronizarse;
- la conexión y una transferencia ya iniciada sobreviven al fin del streaming;
- una ventana de gracia de cinco segundos acepta los últimos datos ya puestos en tránsito antes de F10; después, solo continúan eventos de archivo cuyo manifiesto ya fue aceptado;
- al cambiar de PC se desactiva el peer anterior antes de activar el nuevo.

No existe difusión a los demás equipos autorizados.

Al pasar PC 02 → PC 03, SIAC puede reenviar el contenido que ya está en el portapapeles local. Conserva `originId` y `eventId`; el rastreador de eventos en todos los extremos corta un posible retorno al origen y evita crear un evento artificial nuevo.

## Protocolo v1

Todos los mensajes contienen `type`, `version`, `originId` y, cuando corresponde, `eventId`.

| Mensaje | Uso |
|---|---|
| `hello` | Identidad, UUID local, nombre y versión. |
| `activate` / `deactivate` | Selección exclusiva del canal. |
| `text` | Texto UTF-8 y multilínea. |
| `image` | Imagen codificada como PNG. |
| `fileManifest` | Árbol relativo, tipo, tamaño y suma total. |
| `fileChunk` | Bloque binario con ruta, desplazamiento y tamaño. |
| `fileComplete` | Confirma que la preparación terminó. |
| `cancel` | Cancela y elimina una preparación incompleta. |
| `error` | Código estable y mensaje comprensible. |

El receptor ignora `eventId` vistos y añade `application/x-siac-clipboard-event` al contenido aplicado. Así, la notificación local posterior no vuelve a enviarse y se corta el ciclo PC1 → PC2 → PC1.

## Texto e imágenes

- Texto: UTF-8 en la carga, máximo inicial de 16 MiB.
- Imagen: PNG en la carga, máximo inicial de 64 MiB.
- Un PNG seleccionado en Explorer se trata como archivo; una imagen copiada desde un editor se trata como datos gráficos.

## Archivos y carpetas: implementación inicial

La implementación v0.2 usa **preparación automática cifrada**, no OLE virtual bajo demanda:

1. Explorer publica rutas locales (`CF_HDROP`, visibles en Qt como URLs locales).
2. El origen recorre archivos y directorios sin seguir enlaces simbólicos.
3. Envía manifiesto y bloques de 256 KiB sin cargar archivos completos en RAM.
4. El destino valida cada ruta y escribe bajo su directorio privado de caché.
5. Cada archivo debe completar su tamaño declarado y superar SHA-256; solo entonces, y cuando todos estén verificados, se publican las rutas raíz en el portapapeles Windows.
6. Explorer puede pegarlas en cualquier carpeta con Ctrl+V.

La conexión no se destruye con F10, por lo que una preparación iniciada puede terminar después del retorno local. El usuario ve progreso y puede cancelarla. El caché tiene cuota, antigüedad máxima y limpieza de transferencias incompletas.

Esta estrategia cumple la experiencia integrada, pero empieza a transferir al copiar y usa espacio temporal. No se presenta como transferencia definitiva bajo demanda.

## Evolución OLE pendiente

La versión final para archivos grandes debe ofrecer un `IDataObject` en un hilo STA con:

- `CFSTR_FILEDESCRIPTORW` para metadatos;
- un `IStream` por `CFSTR_FILECONTENTS`;
- lecturas remotas por índice, desplazamiento y longitud;
- vida del canal ligada al propietario OLE, no a la sesión Moonlight;
- manejo de `WM_DESTROYCLIPBOARD` y referencias COM.

Windows permite delayed rendering, pero el propietario debe responder mientras conserve el portapapeles y una operación costosa dentro del mensaje puede bloquear la interfaz. Por ello no se implementa una capa OLE parcial sin pruebas específicas con Explorer.

## Límites y defensas

- 10 000 entradas por copia.
- 20 GiB por evento y cuota de caché inicial de 40 GiB.
- bloques máximos de 512 KiB en protocolo; emisor usa 256 KiB.
- no se siguen enlaces simbólicos ni junctions.
- se rechazan rutas absolutas, traversal, ADS (`:`), nombres reservados y componentes que terminan en punto/espacio.
- se rechazan rutas duplicadas sin distinguir mayúsculas, colisiones de capitalización y árboles que intenten usar un archivo como directorio.
- los archivos se escriben únicamente dentro del directorio resuelto del evento.
- `Ctrl+X`/movimiento no se sincroniza; el origen nunca se elimina.
- desconexión o error deja el evento incompleto fuera del portapapeles y lo elimina.
- nombres raíz repetidos reciben un sufijo estable dentro del evento.

## Análisis de RustDesk y licencia

Se revisó su diseño como referencia conceptual: usa anuncios de formato, solicitudes de datos y, en Windows, `IDataObject`/`IStream` para transformar lecturas del shell en RPC. También mantiene identificación de listas y evita realimentación. RustDesk indica que la lógica de bajo nivel procede de FreeRDP y su repositorio se distribuye bajo AGPL-3.0.

SIAC no incorpora ni adapta archivos de esa biblioteca. El protocolo, clases y pruebas de SIAC se escriben desde cero sobre Qt y continúan bajo GPLv3.

## Pruebas

Automatizables sin dos PC:

- framing fragmentado y múltiples frames;
- límites de tamaños;
- rutas válidas y traversal;
- deduplicación de eventos;
- selección exclusiva del peer;
- autorización por huella y cambio de certificado;
- código de emparejamiento simétrico y restricción a direcciones LAN;
- manifiestos, colisiones de rutas y progreso;
- cancelación y recuperación.

Pendientes de hardware:

- Explorer real PC2 → F10 → Explorer PC1;
- dirección inversa y PC2 → PC3;
- imágenes entre aplicaciones;
- archivos grandes, bloqueo y pérdida de red;
- SHA-256 de originales y copias;
- ausencia de regresiones en streaming, audio, teclado y F9/F10.

## Fuentes técnicas

- Microsoft Learn: `AddClipboardFormatListener`, `WM_CLIPBOARDUPDATE` y operaciones de portapapeles.
- Microsoft Learn: Shell Clipboard Formats, `CFSTR_FILEDESCRIPTOR` y `CFSTR_FILECONTENTS`.
- Repositorio RustDesk: `libs/clipboard/README.md` y arquitectura Windows, consultados únicamente como referencia.
