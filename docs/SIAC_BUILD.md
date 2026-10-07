# Compilar SIAC en Windows

## Dependencias

Según el `README.md` del upstream auditado:

- Qt 6.11 SDK o posterior, componente MSVC. MinGW no está soportado.
- Visual Studio 2026 Community o superior, carga de trabajo **Desktop development with C++**.
- Git y PowerShell.
- 7-Zip para el paquete/instalador.
- Windows Graphics Tools para depuración gráfica.

La referencia vigente es el [README oficial de Moonlight Qt](https://github.com/moonlight-stream/moonlight-qt#building).

## Preparación

Desde una consola Qt para MSVC, situada en la raíz:

```powershell
git submodule update --init --recursive
powershell -ExecutionPolicy Bypass -File .\setup-deps.ps1
```

`setup-deps.ps1` descarga las bibliotecas precompiladas x64 y ARM64 a `libs/windows`. Si ese directorio ya existe, el script reemplaza su contenido.

## Aplicación y paquete

Para una compilación x64 de desarrollo o distribución, abra el prompt Qt x64 correspondiente y ejecute:

```bat
scripts\build-arch.bat release
```

El script detecta la arquitectura del Qt en `PATH`, prepara MSVC, ejecuta qmake/jom o nmake, despliega las DLL y genera los artefactos bajo `build\`. El ejecutable conserva por ahora el nombre upstream `Moonlight.exe` y la identidad visual original; funcionalmente incluye SIAC.

Para depuración:

```bat
scripts\build-arch.bat debug
```

## Pruebas unitarias SIAC

En el mismo prompt Qt/MSVC:

```bat
mkdir build\siac-tests
cd build\siac-tests
qmake ..\..\tests\siac\siac-tests.pro
nmake
debug\tst_sessionswitchplanner.exe
```

Según la configuración de qmake, el binario puede quedar en el directorio actual en lugar de `debug`. También se puede abrir `tests/siac/siac-tests.pro` directamente en Qt Creator.

## Estado de este entorno

No fue posible generar un ejecutable el 2026-10-07 porque el equipo de trabajo no tiene Qt/qmake, Visual Studio/MSVC/MSBuild, Windows SDK, CMake, Ninja, 7-Zip ni compilador C++ instalados o visibles. Los submódulos sí se descargaron. No se ejecutó `setup-deps.ps1` porque las bibliotecas por sí solas no permiten compilar sin Qt y MSVC.
