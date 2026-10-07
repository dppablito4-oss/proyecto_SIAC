# Limitaciones conocidas del MVP

- Solo existe una sesión activa. Cada cambio cierra limpiamente la conexión actual antes de iniciar la siguiente.
- No hay sesiones calientes, simultáneas ni decodificadores en espera. Deben medirse CPU, GPU, RAM, red y latencia antes de considerar esa optimización.
- El cambio incluye negociación, lanzamiento/reanudación y creación de ventana; no es instantáneo.
- La detección automática del PC local usa nombre y direcciones locales. Si es ambigua o falla, SIAC exige selección manual y no cambia de host.
- La aplicación de Sunshine se llama `Desktop` por defecto. Un nombre diferente debe configurarse en SIAC.
- Si Sunshine ya tiene otra aplicación activa, SIAC la reanuda para no terminar procesos remotos. La vista inicial puede no ser el escritorio limpio.
- La disponibilidad procede del sondeo de Moonlight y puede quedar obsoleta entre sondeo y conexión. El error original de Moonlight se muestra y el usuario puede volver a pulsar F9/F10.
- Los hotkeys globales nativos se implementan solo en Windows. Dentro del streaming, SDL consume los atajos en todas las plataformas compatibles, pero el objetivo soportado de SIAC es Windows.
- Si otra aplicación registra la misma combinación, SIAC informa el conflicto; el usuario debe escoger otras teclas F1-F24.
- El ejecutable, icono y varios textos siguen usando la identidad Moonlight upstream en este primer incremento.
- No se ha compilado ni ejecutado en este entorno por falta total de Qt/MSVC/Windows SDK.
- No se ha validado todavía en cuatro equipos, con HDR, varios monitores, suspensión/reanudación ni cambios de red.
- El inicio automático escribe solo el valor `SIAC` del registro del usuario actual; no instala tareas ni servicios adicionales.
- No hay administración central, sincronización de archivos, telemetría ni dependencia de nube.
