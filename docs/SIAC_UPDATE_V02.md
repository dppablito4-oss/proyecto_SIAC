# Actualizar instalaciones SIAC MVP a v0.2

## Antes de actualizar

1. Cierre cualquier transferencia y vuelva al escritorio local con `Ctrl+Alt+F10`.
2. Conserve la configuración de SIAC/Moonlight. La actualización reutiliza hosts, certificados Sunshine, equipo local, orden, atajos y preferencias existentes.
3. Actualice ambos equipos antes de habilitar el portapapeles. Una instalación anterior no ofrece el agente y la interfaz mostrará el canal como no disponible sin impedir el streaming.

## Instalación portable

1. Descargue el artefacto portable Windows x64 del workflow correspondiente a `siac/clipboard-ui`.
2. Cierre SIAC en el icono de bandeja.
3. Extraiga el paquete nuevo en una carpeta distinta para conservar una reversión simple.
4. Inicie `Moonlight.exe`. Windows puede solicitar acceso de firewall para el puerto TCP 48219; autorícelo únicamente en redes privadas.
5. Confirme en **Configuración** que el PC local sigue seleccionado correctamente.

## Autorizar el portapapeles

La autorización Sunshine no concede acceso al canal SIAC.

1. Active **Portapapeles compartido** en ambos equipos.
2. Conecte PC 01 a PC 02 desde **Mis computadoras** o con `Ctrl+Alt+F9`.
3. Abra **Portapapeles** en ambos SIAC.
4. Compare el código corto de la solicitud en los dos equipos: el valor derivado de ambos certificados debe ser idéntico.
5. Autorice únicamente si nombre, UUID y código corresponden al equipo esperado.
6. Repita en sentido contrario si aparece una segunda solicitud. La comunicación es TLS mutua y ambas instalaciones mantienen su propia lista de autorizaciones.

Si cambia el certificado o el UUID de una instalación autorizada, SIAC bloquea el canal. Revoque el registro anterior, verifique físicamente el equipo y autorícelo de nuevo.

## Verificación inicial

1. Copie una línea de texto en PC 02 y péguela en PC 01 después de F10.
2. Repita en sentido inverso.
3. Copie un archivo pequeño desde Explorer; espere **Archivos listos para pegar**, vuelva con F10 y péguelo.
4. Compare SHA-256:

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath .\original.bin
Get-FileHash -Algorithm SHA256 -LiteralPath .\copia.bin
```

5. Si el agente remoto no está disponible, confirme que la sesión de escritorio, audio, teclado y F9/F10 siguen funcionando.

## Reversión

Cierre la versión nueva y ejecute el portable anterior. La configuración de portapapeles se guarda en un grupo separado (`siacClipboard`) y no altera el emparejamiento Sunshine ni el protocolo Moonlight.
