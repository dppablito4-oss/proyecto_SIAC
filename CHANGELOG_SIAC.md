# Registro de cambios SIAC

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
