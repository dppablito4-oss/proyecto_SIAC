#include "sessionswitcher.h"

#include "backend/computermanager.h"
#include "backend/nvapp.h"
#include "backend/nvcomputer.h"
#include "streaming/session.h"

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QHostAddress>
#include <QHostInfo>
#include <QIcon>
#include <QMenu>
#include <QNetworkInterface>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QTimer>

#ifdef Q_OS_WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace {
constexpr auto SettingsGroup = "siac";
// RegisterHotKey reserves 0x0000-0xBFFF for application-defined IDs.
constexpr int NextHotkeyId = 0x5349;
constexpr int LocalHotkeyId = 0x534A;
}

SessionSwitcher* SessionSwitcher::s_Instance = nullptr;

SessionSwitcher::SessionSwitcher(ComputerManager* computerManager, QObject* parent)
    : QObject(parent),
      m_ComputerManager(computerManager),
      m_DesktopAppName(QStringLiteral("Desktop")),
      m_PendingAction(PendingAction::None),
      m_NextFunctionKey(9),
      m_LocalFunctionKey(10),
      m_Enabled(true),
      m_ForceFullscreen(true),
      m_AutoStartEnabled(false),
      m_HotkeysRegistered(false),
      m_SwitchInProgress(false),
      m_TrayIcon(nullptr),
      m_TrayMenu(nullptr)
{
    Q_ASSERT(s_Instance == nullptr);
    s_Instance = this;

    loadSettings();
    synchronizeHostOrder();
    detectLocalComputer();

    connect(m_ComputerManager, &ComputerManager::computerStateChanged,
            this, &SessionSwitcher::handleComputerStateChanged);

    QCoreApplication::instance()->installNativeEventFilter(this);
    registerHotkeys();
    setupTrayIcon();
}

SessionSwitcher::~SessionSwitcher()
{
    unregisterHotkeys();
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->removeNativeEventFilter(this);
    }
    delete m_TrayIcon;
    m_TrayIcon = nullptr;
    delete m_TrayMenu;
    m_TrayMenu = nullptr;
    s_Instance = nullptr;
}

SessionSwitcher* SessionSwitcher::instance()
{
    return s_Instance;
}

void SessionSwitcher::loadSettings()
{
    QSettings settings;
    settings.beginGroup(SettingsGroup);
    m_Enabled = settings.value(QStringLiteral("enabled"), true).toBool();
    m_LocalComputerUuid = settings.value(QStringLiteral("localComputerUuid")).toString();
    m_HostOrder = settings.value(QStringLiteral("hostOrder")).toStringList();
    m_DesktopAppName = settings.value(QStringLiteral("desktopAppName"), QStringLiteral("Desktop")).toString();
    m_NextFunctionKey = settings.value(QStringLiteral("nextFunctionKey"), 9).toInt();
    m_LocalFunctionKey = settings.value(QStringLiteral("localFunctionKey"), 10).toInt();
    m_ForceFullscreen = settings.value(QStringLiteral("forceFullscreen"), true).toBool();
    m_AutoStartEnabled = settings.value(QStringLiteral("autoStart"), false).toBool();
    settings.endGroup();
}

void SessionSwitcher::saveSettings()
{
    QSettings settings;
    settings.beginGroup(SettingsGroup);
    settings.setValue(QStringLiteral("enabled"), m_Enabled);
    settings.setValue(QStringLiteral("localComputerUuid"), m_LocalComputerUuid);
    settings.setValue(QStringLiteral("hostOrder"), m_HostOrder);
    settings.setValue(QStringLiteral("desktopAppName"), m_DesktopAppName);
    settings.setValue(QStringLiteral("nextFunctionKey"), m_NextFunctionKey);
    settings.setValue(QStringLiteral("localFunctionKey"), m_LocalFunctionKey);
    settings.setValue(QStringLiteral("forceFullscreen"), m_ForceFullscreen);
    settings.setValue(QStringLiteral("autoStart"), m_AutoStartEnabled);
    settings.endGroup();
    settings.sync();
}

void SessionSwitcher::synchronizeHostOrder()
{
    const QVector<NvComputer*> computers = m_ComputerManager->getComputers();
    QSet<QString> known;
    for (NvComputer* computer : computers) {
        QReadLocker lock(&computer->lock);
        known.insert(computer->uuid);
    }

    for (int i = m_HostOrder.size() - 1; i >= 0; --i) {
        if (!known.contains(m_HostOrder.at(i))) {
            m_HostOrder.removeAt(i);
        }
    }
    for (NvComputer* computer : computers) {
        QReadLocker lock(&computer->lock);
        if (!m_HostOrder.contains(computer->uuid)) {
            m_HostOrder.append(computer->uuid);
        }
    }
}

void SessionSwitcher::detectLocalComputer()
{
    if (!m_LocalComputerUuid.isEmpty() && findComputer(m_LocalComputerUuid)) {
        return;
    }

    const QString previousUuid = m_LocalComputerUuid;
    m_LocalComputerUuid.clear();

    const QString localName = QHostInfo::localHostName();
    const QList<QHostAddress> localAddresses = QNetworkInterface::allAddresses();
    QString detectedUuid;

    for (NvComputer* computer : m_ComputerManager->getComputers()) {
        QReadLocker lock(&computer->lock);
        const bool nameMatches = computer->name.compare(localName, Qt::CaseInsensitive) == 0;
        const bool addressMatches = localAddresses.contains(QHostAddress(computer->localAddress.address())) ||
                localAddresses.contains(QHostAddress(computer->manualAddress.address()));
        if (nameMatches || addressMatches) {
            if (!detectedUuid.isEmpty()) {
                if (previousUuid != m_LocalComputerUuid) {
                    saveSettings();
                }
                return; // Ambiguous: require explicit selection.
            }
            detectedUuid = computer->uuid;
        }
    }

    if (!detectedUuid.isEmpty()) {
        m_LocalComputerUuid = detectedUuid;
    }
    if (previousUuid != m_LocalComputerUuid) {
        saveSettings();
    }
}

QStringList SessionSwitcher::orderedHostNames() const
{
    QStringList names;
    for (const QString& uuid : m_HostOrder) {
        NvComputer* computer = findComputer(uuid);
        if (computer) {
            QReadLocker lock(&computer->lock);
            names.append(computer->name);
        }
        else {
            names.append(uuid);
        }
    }
    return names;
}

NvComputer* SessionSwitcher::findComputer(const QString& uuid) const
{
    for (NvComputer* computer : m_ComputerManager->getComputers()) {
        QReadLocker lock(&computer->lock);
        if (computer->uuid == uuid) {
            return computer;
        }
    }
    return nullptr;
}

bool SessionSwitcher::isComputerAvailable(NvComputer* computer) const
{
    if (!computer) {
        return false;
    }
    QReadLocker lock(&computer->lock);
    return computer->state == NvComputer::CS_ONLINE &&
            computer->pairState == NvComputer::PS_PAIRED &&
            !computer->appList.isEmpty();
}

void SessionSwitcher::requestNext()
{
    if (!m_Enabled) {
        emit errorOccurred(tr("SIAC is disabled in Settings."));
        return;
    }
    if (m_SwitchInProgress) {
        return;
    }

    synchronizeHostOrder();
    detectLocalComputer();

    if (m_LocalComputerUuid.isEmpty()) {
        emit errorOccurred(tr("Select this physical computer as the local SIAC computer in Settings before switching."));
        return;
    }

    QSet<QString> available;
    for (const QString& uuid : m_HostOrder) {
        if (uuid != m_LocalComputerUuid && isComputerAvailable(findComputer(uuid))) {
            available.insert(uuid);
        }
    }

    const QString current = m_ActiveComputerUuid.isEmpty() ? m_LocalComputerUuid : m_ActiveComputerUuid;
    const auto result = SessionSwitchPlanner::next(m_HostOrder, m_LocalComputerUuid, current, available);
    if (result.action == SessionSwitchPlanner::Action::ReturnLocal) {
        returnLocal();
    }
    else if (result.action == SessionSwitchPlanner::Action::ConnectRemote) {
        requestComputer(result.computerUuid);
    }
    else {
        emit errorOccurred(tr("No paired and online SIAC computer is available."));
    }
}

void SessionSwitcher::returnLocal()
{
    m_PendingComputerUuid.clear();
    if (Session::get()) {
        m_PendingAction = PendingAction::ReturnLocal;
        m_SwitchInProgress = true;
        Session::get()->interrupt();
        return;
    }

    m_PendingAction = PendingAction::None;
    m_SwitchInProgress = false;
    if (!m_ActiveComputerUuid.isEmpty()) {
        m_ActiveComputerUuid.clear();
        emit activeComputerChanged();
    }
    emit returnedToLocal();
    emit showLocalDesktopRequested();
    updateTrayMenu();
}

void SessionSwitcher::requestComputer(const QString& uuid)
{
    if (uuid.isEmpty() || uuid == m_LocalComputerUuid) {
        returnLocal();
        return;
    }

    if (Session::get()) {
        m_PendingAction = PendingAction::ConnectRemote;
        m_PendingComputerUuid = uuid;
        m_SwitchInProgress = true;
        Session::get()->interrupt();
    }
    else {
        launchComputer(uuid);
    }
}

void SessionSwitcher::launchComputer(const QString& uuid)
{
    NvComputer* computer = findComputer(uuid);
    if (!isComputerAvailable(computer)) {
        m_SwitchInProgress = false;
        emit errorOccurred(tr("The selected SIAC computer is offline, unpaired, or has no applications."));
        return;
    }

    NvApp selectedApp;
    bool found = false;
    {
        QReadLocker lock(&computer->lock);

        if (computer->currentGameId != 0) {
            for (const NvApp& app : computer->appList) {
                if (app.id == computer->currentGameId) {
                    selectedApp = app;
                    found = true;
                    break;
                }
            }
        }

        if (!found) {
            for (const NvApp& app : computer->appList) {
                if (app.name.compare(m_DesktopAppName, Qt::CaseInsensitive) == 0) {
                    selectedApp = app;
                    found = true;
                    break;
                }
            }
        }

        if (!found) {
            for (const NvApp& app : computer->appList) {
                if (app.directLaunch) {
                    selectedApp = app;
                    found = true;
                    break;
                }
            }
        }
    }

    if (!found) {
        m_SwitchInProgress = false;
        emit errorOccurred(tr("Application '%1' was not found on the selected host.").arg(m_DesktopAppName));
        return;
    }

    m_ActiveComputerUuid = uuid;
    m_SwitchInProgress = false;
    emit activeComputerChanged();
    updateTrayMenu();
    emit sessionRequested(new Session(computer, selectedApp, nullptr, m_ForceFullscreen, false), selectedApp.name);
}

void SessionSwitcher::sessionEnded()
{
    const PendingAction pendingAction = m_PendingAction;
    const QString pendingComputer = m_PendingComputerUuid;
    m_PendingAction = PendingAction::None;
    m_PendingComputerUuid.clear();
    m_SwitchInProgress = false;

    if (pendingAction == PendingAction::ReturnLocal) {
        m_ActiveComputerUuid.clear();
        emit activeComputerChanged();
        emit returnedToLocal();
        emit showLocalDesktopRequested();
        updateTrayMenu();
    }
    else if (pendingAction == PendingAction::ConnectRemote) {
        QTimer::singleShot(0, this, [this, pendingComputer]() { launchComputer(pendingComputer); });
    }
    else {
        m_ActiveComputerUuid.clear();
        emit activeComputerChanged();
        updateTrayMenu();
    }
}

bool SessionSwitcher::hasPendingAction() const
{
    return m_PendingAction != PendingAction::None;
}

void SessionSwitcher::moveHost(int index, int offset)
{
    const int destination = index + offset;
    if (index < 0 || index >= m_HostOrder.size() || destination < 0 || destination >= m_HostOrder.size()) {
        return;
    }
    m_HostOrder.move(index, destination);
    saveSettings();
    emit configurationChanged();
}

void SessionSwitcher::setLocalComputerAt(int index)
{
    if (index >= 0 && index < m_HostOrder.size()) {
        setLocalComputerUuid(m_HostOrder.at(index));
    }
}

void SessionSwitcher::setEnabled(bool enabled)
{
    if (m_Enabled == enabled) return;
    m_Enabled = enabled;
    saveSettings();
    registerHotkeys();
    emit configurationChanged();
}

void SessionSwitcher::setLocalComputerUuid(const QString& uuid)
{
    if (m_LocalComputerUuid == uuid) return;
    m_LocalComputerUuid = uuid;
    saveSettings();
    emit configurationChanged();
}

void SessionSwitcher::setDesktopAppName(const QString& name)
{
    const QString value = name.trimmed().isEmpty() ? QStringLiteral("Desktop") : name.trimmed();
    if (m_DesktopAppName == value) return;
    m_DesktopAppName = value;
    saveSettings();
    emit configurationChanged();
}

void SessionSwitcher::setNextFunctionKey(int functionKey)
{
    functionKey = qBound(1, functionKey, 24);
    if (m_NextFunctionKey == functionKey) return;
    m_NextFunctionKey = functionKey;
    saveSettings();
    registerHotkeys();
    emit configurationChanged();
}

void SessionSwitcher::setLocalFunctionKey(int functionKey)
{
    functionKey = qBound(1, functionKey, 24);
    if (m_LocalFunctionKey == functionKey) return;
    m_LocalFunctionKey = functionKey;
    saveSettings();
    registerHotkeys();
    emit configurationChanged();
}

void SessionSwitcher::setForceFullscreen(bool forceFullscreen)
{
    if (m_ForceFullscreen == forceFullscreen) return;
    m_ForceFullscreen = forceFullscreen;
    saveSettings();
    emit configurationChanged();
}

void SessionSwitcher::setAutoStartEnabled(bool enabled)
{
#ifdef Q_OS_WIN32
    QSettings startup(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                      QSettings::NativeFormat);
    if (enabled) {
        startup.setValue(QStringLiteral("SIAC"),
                         QStringLiteral("\"") + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + QStringLiteral("\""));
    }
    else {
        startup.remove(QStringLiteral("SIAC"));
    }
    startup.sync();
    if (startup.status() != QSettings::NoError) {
        emit errorOccurred(tr("Windows startup configuration could not be updated."));
        return;
    }
#else
    if (enabled) {
        emit errorOccurred(tr("Automatic startup is only implemented for Windows in this MVP."));
        return;
    }
#endif
    m_AutoStartEnabled = enabled;
    saveSettings();
    emit configurationChanged();
}

void SessionSwitcher::handleComputerStateChanged(NvComputer*)
{
    const QStringList oldOrder = m_HostOrder;
    const QString oldLocalComputerUuid = m_LocalComputerUuid;
    synchronizeHostOrder();
    detectLocalComputer();
    if (oldOrder != m_HostOrder || oldLocalComputerUuid != m_LocalComputerUuid) {
        saveSettings();
        emit configurationChanged();
    }
    updateTrayMenu();
}

void SessionSwitcher::unregisterHotkeys()
{
#ifdef Q_OS_WIN32
    UnregisterHotKey(nullptr, NextHotkeyId);
    UnregisterHotKey(nullptr, LocalHotkeyId);
#endif
    m_HotkeysRegistered = false;
}

void SessionSwitcher::registerHotkeys()
{
    unregisterHotkeys();
    m_HotkeyError.clear();

#ifdef Q_OS_WIN32
    if (m_Enabled) {
        const bool nextRegistered = RegisterHotKey(nullptr, NextHotkeyId,
                                                    MOD_CONTROL | MOD_ALT | MOD_NOREPEAT,
                                                    VK_F1 + m_NextFunctionKey - 1);
        const bool localRegistered = RegisterHotKey(nullptr, LocalHotkeyId,
                                                     MOD_CONTROL | MOD_ALT | MOD_NOREPEAT,
                                                     VK_F1 + m_LocalFunctionKey - 1);
        m_HotkeysRegistered = nextRegistered && localRegistered;
        if (!m_HotkeysRegistered) {
            if (nextRegistered) UnregisterHotKey(nullptr, NextHotkeyId);
            if (localRegistered) UnregisterHotKey(nullptr, LocalHotkeyId);
            m_HotkeyError = tr("A SIAC global shortcut is already in use. Choose different function keys in Settings.");
        }
    }
#else
    m_HotkeyError = tr("Global SIAC shortcuts are only available on Windows; streaming shortcuts remain active.");
#endif
    emit hotkeyStatusChanged();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
bool SessionSwitcher::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
#else
bool SessionSwitcher::nativeEventFilter(const QByteArray& eventType, void* message, long* result)
#endif
{
    Q_UNUSED(eventType)
    Q_UNUSED(result)
#ifdef Q_OS_WIN32
    MSG* msg = static_cast<MSG*>(message);
    if (msg && msg->message == WM_HOTKEY) {
        if (msg->wParam == NextHotkeyId) {
            requestNext();
            return true;
        }
        if (msg->wParam == LocalHotkeyId) {
            returnLocal();
            return true;
        }
    }
#else
    Q_UNUSED(message)
#endif
    return false;
}

void SessionSwitcher::setupTrayIcon()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }

    m_TrayMenu = new QMenu();
    QAction* openAction = m_TrayMenu->addAction(tr("Open SIAC"));
    QAction* nextAction = m_TrayMenu->addAction(tr("Next computer"));
    QAction* localAction = m_TrayMenu->addAction(tr("Return to local desktop"));
    m_TrayMenu->addSeparator();
    QAction* settingsAction = m_TrayMenu->addAction(tr("Settings"));
    QAction* exitAction = m_TrayMenu->addAction(tr("Exit"));

    connect(openAction, &QAction::triggered, this, &SessionSwitcher::showUiRequested);
    connect(nextAction, &QAction::triggered, this, &SessionSwitcher::requestNext);
    connect(localAction, &QAction::triggered, this, &SessionSwitcher::returnLocal);
    connect(settingsAction, &QAction::triggered, this, &SessionSwitcher::openSettingsRequested);
    connect(exitAction, &QAction::triggered, QCoreApplication::instance(), &QCoreApplication::quit);

    m_TrayIcon = new QSystemTrayIcon(QIcon(QStringLiteral(":/res/moonlight.svg")), this);
    m_TrayIcon->setToolTip(tr("SIAC - Interconnected Desktops"));
    m_TrayIcon->setContextMenu(m_TrayMenu);
    connect(m_TrayIcon, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger) {
                    emit showUiRequested();
                }
            });
    m_TrayIcon->show();
    updateTrayMenu();
}

void SessionSwitcher::updateTrayMenu()
{
    if (m_TrayIcon) {
        const QString activeName = m_ActiveComputerUuid.isEmpty()
                ? tr("Local desktop")
                : orderedHostNames().value(m_HostOrder.indexOf(m_ActiveComputerUuid), tr("Remote desktop"));
        m_TrayIcon->setToolTip(tr("SIAC - %1").arg(activeName));
    }
}
