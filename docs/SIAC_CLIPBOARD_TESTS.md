# Checklist de pruebas físicas del portapapeles SIAC v0.2

Estado inicial: pendiente de ejecución en dos PC Windows físicos.

## Preparación

- [ ] PC 01 y PC 02 usan el mismo commit y están en una LAN privada.
- [ ] Sunshine y el escritorio remoto funcionan antes de activar portapapeles.
- [ ] El PC local está seleccionado explícitamente en cada instalación.
- [ ] El código corto TLS es idéntico en ambos PC y fue comparado físicamente antes de autorizar.
- [ ] Se registran hora, tamaño, resultado y logs para cada caso.

## Texto

- [ ] PC 02 → PC 01: Unicode, tildes, emoji y varias líneas.
- [ ] PC 01 → PC 02: mismo conjunto.
- [ ] Diez copias rápidas: solo la última debe quedar en el portapapeles y no debe existir un bucle.
- [ ] Copia desde un peer autorizado pero no activo: debe ignorarse.

## Archivos y carpetas

- [ ] Un archivo PC 02 → F10 → Ctrl+V en cualquier carpeta de PC 01.
- [ ] Dirección PC 01 → PC 02.
- [ ] Varios archivos, nombres repetidos y nombres Unicode.
- [ ] Carpeta con subcarpetas y carpeta vacía.
- [ ] Archivo mayor a 4 GiB sin crecimiento equivalente de RAM.
- [ ] Archivo bloqueado o eliminado durante envío: error claro, sin publicación incompleta.
- [ ] Cancelación desde la interfaz: caché incompleto eliminado.
- [ ] Pérdida de red durante envío y posterior reconexión.
- [ ] Intentos con `..`, ruta absoluta, ADS y nombre reservado: rechazados.
- [ ] Ctrl+X: el original no debe eliminarse; la función no se anuncia como movimiento.

Para cada archivo transferido:

```powershell
(Get-FileHash -Algorithm SHA256 -LiteralPath 'C:\ruta\original').Hash
(Get-FileHash -Algorithm SHA256 -LiteralPath 'C:\ruta\copia').Hash
```

- [ ] Los dos hashes coinciden.

## Imágenes

- [ ] Captura de pantalla copiada y pegada en Paint.
- [ ] Imagen copiada desde un editor hacia otra aplicación.
- [ ] Un PNG seleccionado en Explorer continúa tratándose como archivo, no como bitmap.

## Sesiones y regresiones

- [ ] F9 rápido conserva una sola sesión.
- [ ] F10 durante conexión cancela el remoto.
- [ ] F10 después de copiar permite que la transferencia iniciada termine y luego pegar.
- [ ] Cambiar PC 01 → PC 02 → PC 03 selecciona solo el agente de PC 03.
- [ ] Equipo sin agente: mensaje claro; streaming, audio, teclado y mouse normales.
- [ ] Peer desconocido o con certificado cambiado no recibe ni envía contenido.
- [ ] Ctrl y Alt no quedan presionados después de F9/F10.

## Criterio de aprobación

No marcar archivos o imágenes como funcionales hasta completar sus casos con Explorer/aplicaciones reales. Una compilación verde y pruebas unitarias no sustituyen esta matriz.
