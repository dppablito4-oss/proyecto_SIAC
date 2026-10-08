# Registro de cambios SIAC

## 2026-10-07 - SIAC v0.2 portapapeles e interfaz de productividad

- Creada y publicada la rama `siac/clipboard-ui` desde `siac/mvp`, sin cambios en `main`.
- Agregado un agente de portapapeles residente en el proceso de bandeja, independiente del ciclo de vida del streaming.
- Agregado un canal TLS directo en LAN con autorización separada de Sunshine, identidad persistente, pinning de certificado y código de verificación simétrico.
- Implementada sincronización bidireccional de texto Unicode, imágenes PNG y archivos/carpetas mediante transporte TLS y preparación automática en caché privado.
- Agregados streaming de archivos por bloques, SHA-256 obligatorio, límites, caché privado, cancelación, limpieza y defensas de rutas.
- Conservadas transferencias iniciadas al usar F10 y agregado reenvío controlado PC 02 → PC 03 sin bucles.
- Rediseñada la vista principal para mostrar computadoras y estados de trabajo, con acceso directo a pantalla completa.
- Agregada la vista **Portapapeles** para estado, autorización, revocación, progreso, cancelación y reconexión.
- Español establecido como idioma inicial cuando no existe una preferencia previa.
- Ampliadas las pruebas Qt del protocolo y las comprobaciones de integración de fuentes.
- Documentadas arquitectura, actualización, límites y matriz de pruebas físicas. La transferencia bajo demanda con OLE/IStream queda aplazada y no se presenta como terminada.

## 2026-10-07 - Primer incremento MVP

- Preparada la rama local `siac/mvp` desde Moonlight Qt upstream `de2467e` sin modificar la rama `main` original.
- Inicializados todos los submódulos oficiales.
- Agregados `SessionSwitcher` y `SessionSwitchPlanner`.
- Agregada rotación ordenada con exclusión segura del equipo local y omisión de hosts no disponibles.
- Agregados Ctrl+Alt+F9 y Ctrl+Alt+F10 configurables entre F1 y F24.
- Implementada captura de atajos dentro de SDL antes del envío al host y registro global de Windows para la interfaz local.
- Integrado el cambio secuencial con el ciclo real de `Session`/`StreamSegue`.
- Agregada pantalla completa forzada solo para sesiones SIAC.
- Evitado que un cambio SIAC termine la aplicación Sunshine remota aunque la preferencia normal de Moonlight solicite cerrar al salir.
- Agregada persistencia con `QSettings`, selección local, orden de hosts y nombre de aplicación Sunshine.
- Agregados menú de bandeja e inicio automático por usuario en Windows.
- Agregada configuración SIAC a la interfaz y opción contextual para marcar el PC local.
- Agregadas pruebas Qt Test para planificación y clasificación de hotkeys.
- Agregadas guías de arquitectura, instalación, compilación, pruebas y limitaciones.

No se crearon commits ni se publicó contenido al remoto.
