#include "sessionswitcher.h"

#include "backend/computermanager.h"
#include "backend/nvapp.h"
#include "backend/nvcomputer.h"
#include "streaming/session.h"

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QMenu>
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
      m_PendingTransitionToken(0),
      m_ManagedSession(nullptr),
      m_ManagedSessionToken(0),
      m_DeferredLaunchTimer(new QTimer(this)),
      m_TrayIcon(nullptr),
      m_TrayMenu(nullptr)
{
    Q_ASSERT(s_Instance == nullptr);
    s_Instance = this;

    loadSettings();
    synchronizeHostOrder();
    validateLocalComputerSelection();

    m_DeferredLaunchTimer->setSingleShot(true);
    connect(m_DeferredLaunchTimer, &QTimer::timeout, this, [this]() {
        const quint64 token = m_PendingTransitionToken;
        if (m_PendingAction != PendingAction::ConnectRemote ||
                !m_Transition.beginDeferredLaunch(token)) {
            return;
        }
        const QString destination = m_PendingComputerUuid;
        m_PendingAction = PendingAction::None;
        m_PendingComputerUuid.clear();
        launchComputer(destination, token);
    });

    connect(m_ComputerManager, &ComputerManager::computerStateChanged,
            this, &SessionSwitcher::handleComputerStateChanged);

    QCoreApplication::instance()->installNativeEventFilter(this);
    registerHotkeys();
    setupTrayIcon();
    if (!m_StartupWarning.isEmpty()) {
        QTimer::singleShot(0, this, [this]() { emit errorOccurred(m_StartupWarning); });
    }
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

    const auto hotkeys = SessionSwitchPlanner::normalizeHotkeys(
                m_NextFunctionKey, m_LocalFunctionKey);
    if (hotkeys.corrected) {
        m_NextFunctionKey = hotkeys.nextFunctionKey;
        m_LocalFunctionKey = hotkeys.localFunctionKey;
        m_StartupWarning = tr("The saved SIAC shortcuts were invalid or identical and were reset to Ctrl+Alt+F9 and Ctrl+Alt+F10. You can correct them in Settings.");
        saveSettings();
    }
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

void SessionSwitcher::validateLocalComputerSelection()
{
    if (!m_LocalComputerUuid.isEmpty() && findComputer(m_LocalComputerUuid)) {
        return;
    }

    if (m_LocalComputerUuid.isEmpty()) {
        return;
    }

    m_LocalComputerUuid.clear();
    saveSettings();
    m_StartupWarning = tr("The previously selected local computer no longer exists. Select this physical computer explicitly before using SIAC.");
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

bool SessionSwitcher::selectApplication(NvComputer* computer, NvApp* selectedApp,
                                        QString* failureReason) const
{
    if (!computer) {
        if (failureReason) *failureReason = tr("The host no longer exists.");
        return false;
    }
    QReadLocker lock(&computer->lock);
    if (computer->state != NvComputer::CS_ONLINE) {
        if (failureReason) *failureReason = tr("The host is offline.");
        return false;
    }
    if (computer->pairState != NvComputer::PS_PAIRED) {
        if (failureReason) *failureReason = tr("The host is not paired.");
        return false;
    }

    QVector<SessionSwitchPlanner::ApplicationCandidate> candidates;
    candidates.reserve(computer->appList.size());
    for (const NvApp& app : computer->appList) {
        candidates.append({app.id, app.name, app.directLaunch,
                           app.id != 0 && !app.name.isNull()});
    }
    const int index = SessionSwitchPlanner::selectApplication(
                candidates, computer->currentGameId, m_DesktopAppName);
    if (index < 0) {
        if (failureReason) {
            *failureReason = tr("The host has no running, '%1', or directly launchable application.")
                    .arg(m_DesktopAppName);
        }
        return false;
    }
    if (selectedApp) {
        *selectedApp = computer->appList.at(index);
    }
    return true;
}

void SessionSwitcher::requestNext()
{
    if (!m_Enabled) {
        emit errorOccurred(tr("SIAC is disabled in Settings."));
        return;
    }
    if (!m_Transition.canRequestNext()) {
        return;
    }

    synchronizeHostOrder();
    validateLocalComputerSelection();

    if (m_LocalComputerUuid.isEmpty()) {
        emit errorOccurred(tr("Select this physical computer as the local SIAC computer in Settings before switching."));
        return;
    }

    const QString current = m_ActiveComputerUuid.isEmpty() ? m_LocalComputerUuid : m_ActiveComputerUuid;
    const auto result = nextUsableComputer(current);
    if (result.action == SessionSwitchPlanner::Action::ReturnLocal) {
        returnLocal();
    }
    else if (result.action == SessionSwitchPlanner::Action::ConnectRemote) {
        requestComputer(result.computerUuid);
    }
    else {
        emit errorOccurred(tr("No remote SIAC computer has a usable application. The local desktop remains active."));
    }
}

void SessionSwitcher::returnLocal()
{
    m_DeferredLaunchTimer->stop();
    m_Transition.cancelToLocal();
    m_PendingTransitionToken = m_Transition.token();
    m_PendingComputerUuid.clear();
    m_AttemptedComputerUuids.clear();
    m_PendingAction = PendingAction::ReturnLocal;

    if (Session::get()) {
        Session::get()->interrupt();
        return;
    }

    if (m_ManagedSession) {
        m_ManagedSession->cancelBeforeStart();
        emit cancelPendingSessionRequested(m_ManagedSession);
        return;
    }

    finishReturnLocal();
}

void SessionSwitcher::finishReturnLocal()
{
    m_DeferredLaunchTimer->stop();
    m_PendingAction = PendingAction::None;
    m_PendingComputerUuid.clear();
    m_PendingTransitionToken = 0;
    m_ManagedSession = nullptr;
    m_ManagedSessionToken = 0;
    m_AttemptedComputerUuids.clear();
    m_Transition.finishLocal();
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

    const bool hasActiveSession = Session::get() != nullptr;
    const quint64 token = m_Transition.beginRemoteSwitch(hasActiveSession);
    if (token == 0) {
        return;
    }
    m_PendingTransitionToken = token;
    m_AttemptedComputerUuids.clear();

    if (hasActiveSession) {
        m_PendingAction = PendingAction::ConnectRemote;
        m_PendingComputerUuid = uuid;
        Session::get()->interrupt();
    }
    else {
        launchComputer(uuid, token);
    }
}

SessionSwitchPlanner::Result SessionSwitcher::nextUsableComputer(const QString& currentUuid) const
{
    QSet<QString> available;
    for (const QString& uuid : m_HostOrder) {
        if (uuid != m_LocalComputerUuid && !m_AttemptedComputerUuids.contains(uuid) &&
                selectApplication(findComputer(uuid), nullptr)) {
            available.insert(uuid);
        }
    }
    return SessionSwitchPlanner::next(m_HostOrder, m_LocalComputerUuid, currentUuid, available);
}

void SessionSwitcher::launchComputer(const QString& uuid, quint64 token)
{
    if (!m_Transition.isCurrent(token) ||
            m_Transition.phase() != SessionTransitionState::Phase::LaunchRequested) {
        return;
    }

    if (uuid.isEmpty() || uuid == m_LocalComputerUuid) {
        finishReturnLocal();
        return;
    }

    m_AttemptedComputerUuids.insert(uuid);
    NvComputer* computer = findComputer(uuid);
    NvApp selectedApp;
    QString failureReason;
    if (!selectApplication(computer, &selectedApp, &failureReason)) {
        const auto next = nextUsableComputer(uuid);
        if (next.action == SessionSwitchPlanner::Action::ConnectRemote) {
            launchComputer(next.computerUuid, token);
        }
        else if (next.action == SessionSwitchPlanner::Action::ReturnLocal) {
            finishReturnLocal();
        }
        else {
            failTransition(token, tr("No remote SIAC computer is usable (%1). The local desktop remains available.")
                           .arg(failureReason));
        }
        return;
    }

    Session* session = new Session(computer, selectedApp, nullptr, m_ForceFullscreen, false);
    if (!m_Transition.markSessionCreated(token)) {
        session->deleteLater();
        return;
    }
    m_ManagedSession = session;
    m_ManagedSessionToken = token;
    m_ActiveComputerUuid = uuid;
    emit activeComputerChanged();
    updateTrayMenu();
    emit sessionRequested(session, selectedApp.name);
}

void SessionSwitcher::sessionStarted(Session* session)
{
    if (session == m_ManagedSession) {
        m_Transition.markConnected(m_ManagedSessionToken);
    }
}

void SessionSwitcher::scheduleDeferredLaunch(quint64 token)
{
    if (!m_Transition.markLaunchDeferred(token)) {
        return;
    }
    m_DeferredLaunchTimer->start(0);
}

void SessionSwitcher::sessionEnded(Session* session)
{
    if (session == m_ManagedSession) {
        m_ManagedSession = nullptr;
        m_ManagedSessionToken = 0;
    }

    const PendingAction pendingAction = m_PendingAction;

    if (pendingAction == PendingAction::ReturnLocal) {
        finishReturnLocal();
    }
    else if (pendingAction == PendingAction::ConnectRemote) {
        scheduleDeferredLaunch(m_PendingTransitionToken);
    }
    else {
        const quint64 token = m_ManagedSessionToken != 0 ? m_ManagedSessionToken : m_Transition.token();
        failTransition(token, tr("The remote session ended. The local desktop is available and SIAC can be retried."));
    }
}

void SessionSwitcher::sessionLaunchFailed(Session* session, const QString& reason)
{
    if (session != m_ManagedSession) {
        if (session) session->deleteLater();
        return;
    }
    const quint64 token = m_ManagedSessionToken;
    m_ManagedSession = nullptr;
    m_ManagedSessionToken = 0;
    session->deleteLater();
    failTransition(token, reason);
}

void SessionSwitcher::failTransition(quint64 token, const QString& message)
{
    if (!m_Transition.fail(token)) {
        return;
    }
    m_DeferredLaunchTimer->stop();
    m_PendingAction = PendingAction::None;
    m_PendingComputerUuid.clear();
    m_PendingTransitionToken = 0;
    m_ManagedSession = nullptr;
    m_ManagedSessionToken = 0;
    m_AttemptedComputerUuids.clear();
    if (!m_ActiveComputerUuid.isEmpty()) {
        m_ActiveComputerUuid.clear();
        emit activeComputerChanged();
    }
    emit showLocalDesktopRequested();
    updateTrayMenu();
    if (!message.isEmpty()) {
        emit errorOccurred(message);
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
    if (uuid.isEmpty() || !findComputer(uuid)) {
        emit errorOccurred(tr("Select an existing paired Sunshine host as this physical computer."));
        return;
    }
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
    if (m_NextFunctionKey == functionKey) return;
    if (!SessionSwitchPlanner::validHotkeys(functionKey, m_LocalFunctionKey)) {
        m_HotkeyError = tr("The next and return shortcuts must use different function keys from F1 through F24.");
        emit hotkeyStatusChanged();
        emit errorOccurred(m_HotkeyError);
        emit configurationChanged();
        return;
    }
    m_NextFunctionKey = functionKey;
    saveSettings();
    registerHotkeys();
    emit configurationChanged();
}

void SessionSwitcher::setLocalFunctionKey(int functionKey)
{
    if (m_LocalFunctionKey == functionKey) return;
    if (!SessionSwitchPlanner::validHotkeys(m_NextFunctionKey, functionKey)) {
        m_HotkeyError = tr("The next and return shortcuts must use different function keys from F1 through F24.");
        emit hotkeyStatusChanged();
        emit errorOccurred(m_HotkeyError);
        emit configurationChanged();
        return;
    }
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
    validateLocalComputerSelection();
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

    if (!SessionSwitchPlanner::validHotkeys(m_NextFunctionKey, m_LocalFunctionKey)) {
        m_HotkeyError = tr("The SIAC shortcuts must use different function keys from F1 through F24. Correct them in Settings.");
        emit hotkeyStatusChanged();
        return;
    }

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
