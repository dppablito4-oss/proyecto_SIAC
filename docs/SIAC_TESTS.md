# Pruebas y resultados

Fecha de la validación: 2026-10-07.

## Ampliación SIAC v0.2

El commit `dd790aad` conserva los 15 casos de comportamiento del controlador/planificador y añade 8 métodos para el protocolo de portapapeles: framing fragmentado, tamaños numéricos inválidos, rutas inseguras, deduplicación, autorización/pinning, código simétrico, restricción LAN y colisiones del manifiesto. Son 23 métodos de prueba, más inicialización y limpieza de Qt Test.

## Automatización incluida

`tests/siac/tst_sessionswitchplanner.cpp` cubre:

1. el equipo local nunca se devuelve como destino remoto;
2. el orden configurado se respeta;
3. equipos no disponibles se omiten;
4. después del último remoto se vuelve al local;
5. el planificador puro admite un local sintético, identificado expresamente como capacidad no integrada en el MVP;
6. cualquiera de cuatro equipos puede ser el local;
7. Ctrl+Alt+F9/F10 se clasifica como acción local y combinaciones distintas se reenvían;
8. F9 repetido durante creación/conexión produce una sola transición;
9. F10 invalida un lanzamiento diferido y una sesión creada todavía no activa;
10. un fallo de conexión o creación deja reintentar;
11. atajos iguales o fuera de F1-F24 se rechazan;
12. la selección de aplicación prioriza sesión activa, nombre configurado y `directLaunch`;
13. hosts sin aplicación utilizable se omiten y la ausencia total recupera el estado local.

## Resultados ejecutados

| Comprobación | Resultado | Evidencia |
|---|---|---|
| Repositorio y submódulos | Aprobada | `git submodule update --init --recursive` terminó correctamente. |
| Espacios/blancos del parche | Aprobada | `git diff --check` sin errores. |
| Integración estática SIAC | Aprobada | `tests/siac/source-integration.tests.ps1`: 16 aserciones aprobadas. |
| Herramientas de compilación local | Bloqueada por entorno | No existen qmake, MSVC, MSBuild, Windows SDK, CMake, Ninja ni compilador C++ en el equipo local. |
| Qt Test SIAC | Aprobada en CI | Ejecutada dentro del build x64 Windows del workflow `37707269326`. |
| Compilación Windows x64 y ARM64 | Aprobada en CI | Commit `ee7cdec2`, Windows 2025 y Qt 6.12; empaquetado y artefactos completados. |
| Workflow completo | Aprobado | Windows/macOS, AppImage y Steam Link finalizaron correctamente en `37707269326`. |
| Qt Test SIAC v0.2 | Aprobada en CI | 23 métodos ejecutados con código 0 dentro del build x64 del workflow `37717294591`. |
| Compilación v0.2 | Aprobada en CI | Commit `dd790aad`; Windows x64/ARM64, macOS, AppImage y Steam Link completados con artefactos. |

La aprobación automatizada de v0.2 no equivale a una prueba real de Ctrl+C/Ctrl+V entre dos PC; esa matriz permanece en `SIAC_CLIPBOARD_TESTS.md`.

No se declara aprobada ninguna prueba no ejecutada.

La prueba estática se puede repetir sin Qt:

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\siac\source-integration.tests.ps1
```

## Matriz pendiente con cuatro PC físicos

| Caso | Estado |
|---|---|
| Rotación PC01 -> PC02 -> PC03 -> PC04 -> local | Pendiente de hardware |
| Mismo ciclo comenzando físicamente en PC02, PC03 y PC04 | Pendiente de hardware |
| Host apagado durante el ciclo | Pendiente de hardware |
| F9/F10 no aparecen en el host remoto | Pendiente de captura de input en hardware |
| Retorno local conserva aplicaciones locales | Pendiente de hardware |
| Persistencia tras reiniciar SIAC y Windows | Pendiente de ejecutable |
| Inicio automático y menú de bandeja | Pendiente de ejecutable Windows |
| Emparejamiento, audio, vídeo e input Moonlight sin regresión | Pendiente de prueba de integración |
| Latencia y tiempo de transición | Pendiente de medición LAN |

## Procedimiento recomendado

Ejecute primero las pruebas Qt Test, luego una prueba de humo con dos PC, y finalmente la matriz de cuatro PC descrita en `SIAC_INSTALLATION.md`. Registre por cambio el tiempo desde F9 hasta el primer fotograma interactivo y los errores de Sunshine/Moonlight.
