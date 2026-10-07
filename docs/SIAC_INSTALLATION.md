# Instalación y configuración de SIAC en cuatro PC

## 1. Requisitos de red y equipo

- Cuatro PC en la misma LAN; Ethernet Cat 5e o superior es preferible para 4K y para minimizar latencia.
- Windows 11 o posterior para las versiones actuales de Sunshine.
- GPU con codificación de hardware compatible. Como referencia oficial actual: Intel Skylake/Quick Sync o posterior, NVIDIA con NVENC o AMD con VCE; 4 GB de RAM como mínimo.
- Una sesión gráfica iniciada en cada host y permisos de administrador para instalar Sunshine.

No abra puertos en el router ni active UPnP para este escenario LAN. Mantenga la interfaz web de Sunshine limitada a `pc` o `lan`, nunca a Internet.

Fuentes oficiales: [requisitos de Sunshine](https://github.com/LizardByte/Sunshine#system-requirements) e [inicio de Sunshine](https://github.com/LizardByte/Sunshine/blob/master/docs/getting_started.md).

## 2. Instalar Sunshine en cada PC

Repita estos pasos en PC 01, PC 02, PC 03 y PC 04:

1. Descargue el MSI AMD64/x64 estable desde [Releases de Sunshine](https://github.com/LizardByte/Sunshine/releases). El instalador MSI es el método recomendado; ARM64 es experimental.
2. Ejecute el instalador y seleccione conscientemente los componentes. Para escritorio, teclado y mouse no es necesario habilitar opciones de gamepad que no vaya a usar.
3. Confirme que el servicio Sunshine esté iniciado. El instalador de Windows lo ejecuta como servicio de forma predeterminada.
4. En ese mismo PC abra `https://localhost:47990`. El aviso del certificado autofirmado es esperado en la primera configuración.
5. Cree y conserve las credenciales de administración de Sunshine. SIAC no las solicita ni almacena.
6. En **Applications**, verifique que exista una aplicación de escritorio llamada `Desktop`. Si usa otro nombre, introdúzcalo después en la configuración SIAC de cada PC.
7. En **Configuration**, asigne un nombre inequívoco, por ejemplo `PC 01`, y conserve los valores LAN predeterminados salvo que su hardware requiera ajustes.
8. Compruebe en **Troubleshooting** que Sunshine eligió un codificador de hardware y no reporta errores de captura.

## 3. Instalar y emparejar SIAC

1. Instale o copie la misma compilación SIAC en los cuatro PC.
2. Inicie SIAC. Los hosts deberían aparecer por mDNS; si uno no aparece, use **Add PC manually** e introduzca su IP LAN.
3. Seleccione cada host remoto. SIAC/Moonlight mostrará un PIN.
4. En el host correspondiente, abra la interfaz Sunshine, vaya a **PIN**, escriba el PIN y asigne un nombre al cliente.
5. Espere a que SIAC muestre el equipo como `Online` y `Paired`.
6. Incluya también el Sunshine del PC físico actual en la lista. Abra su menú contextual y elija **Set as local SIAC computer**. Esta marca evita cualquier conexión al propio equipo.
7. Abra **Settings > SIAC - Interconnected Desktops**:
   - verifique el equipo local;
   - ordene PC 01 a PC 04 con las flechas;
   - confirme el nombre `Desktop`;
   - deje pantalla completa activa;
   - confirme F9 para avanzar y F10 para volver;
   - active el inicio con Windows si lo desea.
8. Repita la selección del equipo local en cada PC. El orden puede ser el mismo; el planificador comienza después de la posición local.

## 4. Prueba de aceptación en cada PC

1. Cierre documentos que puedan interpretar Ctrl+Alt+F9/F10 durante la prueba inicial.
2. Pulse `Ctrl+Alt+F9`: debe abrir el siguiente remoto disponible a pantalla completa.
3. Escriba en una aplicación inocua del host remoto para comprobar teclado y mouse.
4. Pulse de nuevo F9 hasta recorrer los tres remotos; la pulsación siguiente debe devolver al escritorio físico y minimizar la interfaz SIAC.
5. Desde cualquier remoto pulse `Ctrl+Alt+F10`: debe volver directamente al escritorio local.
6. Apague temporalmente uno de los hosts y repita: debe omitirse sin bloquear el ciclo.
7. Confirme que las aplicaciones que estaban abiertas en el PC local continúan ejecutándose.

Si un atajo global está ocupado, Configuración mostrará el conflicto. Elija otra tecla F1-F24 para esa acción en los cuatro PC si desea mantener combinaciones uniformes.

## 5. Bandeja del sistema

El menú SIAC ofrece abrir la interfaz, siguiente equipo, volver al escritorio local, configuración y salir. Si volvió al escritorio local y la ventana quedó minimizada, use la barra de tareas o el icono de bandeja para abrirla.
