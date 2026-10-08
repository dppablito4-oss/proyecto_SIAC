# Limitaciones conocidas del MVP

## Portapapeles v0.2

- La transferencia de archivos implementada prepara contenido automáticamente en un caché privado antes de publicar `CF_HDROP`; todavía no usa `CFSTR_FILEDESCRIPTORW`/`CFSTR_FILECONTENTS` con `IStream` bajo demanda.
- El puerto TCP 48219 debe estar permitido solo en el perfil de red privada de Windows.
- La autorización SIAC es independiente del emparejamiento Sunshine y debe confirmarse en ambas instalaciones.
- El caché admite hasta 40 GiB y elimina entradas antiguas; cada evento admite como máximo 20 GiB y 10 000 elementos.
- Ctrl+X no se implementa como movimiento remoto y nunca debe borrar el original.
- Texto, archivos, carpetas e imágenes requieren todavía validación física en dos PC Windows antes de declararse funcionales.

- Solo existe una sesión activa. Cada cambio cierra limpiamente la conexión actual antes de iniciar la siguiente.
- No hay sesiones calientes, simultáneas ni decodificadores en espera. Deben medirse CPU, GPU, RAM, red y latencia antes de considerar esa optimización.
- El cambio incluye negociación, lanzamiento/reanudación y creación de ventana; no es instantáneo.
- El MVP exige que cada PC tenga su propio registro Sunshine emparejado y que se seleccione explícitamente como equipo local. El modo del planificador sin registro Sunshine no está disponible de extremo a extremo.
- La aplicación de Sunshine se llama `Desktop` por defecto. Un nombre diferente debe configurarse en SIAC.
- Si Sunshine ya tiene otra aplicación activa, SIAC la reanuda para no terminar procesos remotos. La vista inicial puede no ser el escritorio limpio.
- La disponibilidad procede del sondeo de Moonlight y puede quedar obsoleta entre sondeo y conexión. El error original de Moonlight se muestra y el usuario puede volver a pulsar F9/F10.
- Los hotkeys globales nativos se implementan solo en Windows. Dentro del streaming, SDL consume los atajos en todas las plataformas compatibles, pero el objetivo soportado de SIAC es Windows.
- Si otra aplicación registra la misma combinación, SIAC informa el conflicto; el usuario debe escoger otras teclas F1-F24.
- El ejecutable, icono y varios textos siguen usando la identidad Moonlight upstream en este primer incremento.
- El entorno local no dispone de Qt/MSVC/Windows SDK. La compilación y Qt Test se validan mediante GitHub Actions; las pruebas físicas siguen pendientes.
- No se ha validado todavía en cuatro equipos, con HDR, varios monitores, suspensión/reanudación ni cambios de red.
- El inicio automático escribe solo el valor `SIAC` del registro del usuario actual; no instala tareas ni servicios adicionales.
- No hay administración central, sincronización de archivos, telemetría ni dependencia de nube.
