$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

function Assert-True {
    param(
        [Parameter(Mandatory = $true)][bool]$Condition,
        [Parameter(Mandatory = $true)][string]$Message
    )
    if (-not $Condition) {
        throw $Message
    }
    Write-Host "PASS: $Message"
}

$keyboardPath = Join-Path $RepoRoot 'app\streaming\input\keyboard.cpp'
$keyboard = Get-Content -Raw -LiteralPath $keyboardPath
$shortcutIndex = $keyboard.IndexOf('SessionSwitchPlanner::matchShortcut')
$forwardIndex = $keyboard.IndexOf('LiSendKeyboardEvent')
Assert-True ($shortcutIndex -ge 0) 'The SDL input path invokes the SIAC shortcut classifier.'
Assert-True ($forwardIndex -gt $shortcutIndex) 'SIAC classification occurs before keyboard forwarding.'
Assert-True ($keyboard.Contains('raiseAllKeys()')) 'Consumed shortcuts release remote modifier state.'

$session = Get-Content -Raw -LiteralPath (Join-Path $RepoRoot 'app\streaming\session.cpp')
Assert-True ($session.Contains('m_AllowQuitApp')) 'SIAC sessions can disconnect without quitting the host application.'
Assert-True ($session.Contains('forceFullScreen')) 'SIAC can force fullscreen without changing the normal Moonlight preference.'

$switcher = Get-Content -Raw -LiteralPath (Join-Path $RepoRoot 'app\siac\sessionswitcher.cpp')
Assert-True ($switcher.Contains('uuid != m_LocalComputerUuid')) 'The local UUID is excluded from the available remote set.'
Assert-True ($switcher.Contains('RegisterHotKey')) 'The Windows UI path registers global hotkeys.'
Assert-True (-not $switcher.Contains('Win+Tab')) 'SIAC does not replace or synthesize Win+Tab.'

$project = Get-Content -Raw -LiteralPath (Join-Path $RepoRoot 'app\app.pro')
Assert-True ($project.Contains('siac/sessionswitcher.cpp')) 'The SIAC switcher is included in the application build.'
Assert-True ($project.Contains('widgets')) 'Qt Widgets is linked for the system tray implementation.'
Assert-True ($project.Contains('siac/clipboard/clipboardmanager.cpp')) 'The clipboard agent is included in the application build.'

$clipboard = Get-Content -Raw -LiteralPath (Join-Path $RepoRoot 'app\siac\clipboard\clipboardmanager.cpp')
Assert-True ($clipboard.Contains('QSslSocket') -or (Get-Content -Raw -LiteralPath (Join-Path $RepoRoot 'app\siac\clipboard\clipboardpeer.cpp')).Contains('QSslSocket')) 'The clipboard channel uses TLS sockets.'
Assert-True ($clipboard.Contains('MaxTransferSize')) 'Clipboard file transfers enforce a total-size limit.'
Assert-True ($clipboard.Contains('QCryptographicHash::Sha256')) 'Received files are verified with SHA-256.'
Assert-True ($clipboard.Contains('activeComputerUuid')) 'Clipboard peer selection follows the active SIAC computer.'

Push-Location $RepoRoot
try {
    & git diff --check
    Assert-True ($LASTEXITCODE -eq 0) 'Git reports no whitespace errors in the patch.'
}
finally {
    Pop-Location
}
