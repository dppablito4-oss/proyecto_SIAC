#pragma once

#include "sessionswitchplanner.h"
#include "sessiontransitionstate.h"

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QStringList>

class ComputerManager;
class NvComputer;
class Session;
class QMenu;
class QSystemTrayIcon;
class QTimer;
class NvApp;

class SessionSwitcher : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY configurationChanged)
    Q_PROPERTY(QString localComputerUuid READ localComputerUuid WRITE setLocalComputerUuid NOTIFY configurationChanged)
    Q_PROPERTY(QString activeComputerUuid READ activeComputerUuid NOTIFY activeComputerChanged)
    Q_PROPERTY(QStringList orderedHostUuids READ orderedHostUuids NOTIFY configurationChanged)
    Q_PROPERTY(QStringList orderedHostNames READ orderedHostNames NOTIFY configurationChanged)
    Q_PROPERTY(QString desktopAppName READ desktopAppName WRITE setDesktopAppName NOTIFY configurationChanged)
    Q_PROPERTY(int nextFunctionKey READ nextFunctionKey WRITE setNextFunctionKey NOTIFY configurationChanged)
    Q_PROPERTY(int localFunctionKey READ localFunctionKey WRITE setLocalFunctionKey NOTIFY configurationChanged)
    Q_PROPERTY(bool forceFullscreen READ forceFullscreen WRITE setForceFullscreen NOTIFY configurationChanged)
    Q_PROPERTY(bool autoStartEnabled READ autoStartEnabled WRITE setAutoStartEnabled NOTIFY configurationChanged)
    Q_PROPERTY(bool hotkeysRegistered READ hotkeysRegistered NOTIFY hotkeyStatusChanged)
    Q_PROPERTY(QString hotkeyError READ hotkeyError NOTIFY hotkeyStatusChanged)

public:
    explicit SessionSwitcher(ComputerManager* computerManager, QObject* parent = nullptr);
    ~SessionSwitcher() override;

    static SessionSwitcher* instance();

    bool enabled() const { return m_Enabled; }
    QString localComputerUuid() const { return m_LocalComputerUuid; }
    QString activeComputerUuid() const { return m_ActiveComputerUuid; }
    QStringList orderedHostUuids() const { return m_HostOrder; }
    QStringList orderedHostNames() const;
    QString desktopAppName() const { return m_DesktopAppName; }
    int nextFunctionKey() const { return m_NextFunctionKey; }
    int localFunctionKey() const { return m_LocalFunctionKey; }
    bool forceFullscreen() const { return m_ForceFullscreen; }
    bool autoStartEnabled() const { return m_AutoStartEnabled; }
    bool hotkeysRegistered() const { return m_HotkeysRegistered; }
    QString hotkeyError() const { return m_HotkeyError; }

    Q_INVOKABLE void requestNext();
    Q_INVOKABLE void returnLocal();
    Q_INVOKABLE void sessionStarted(Session* session);
    Q_INVOKABLE void sessionEnded(Session* session);
    Q_INVOKABLE void sessionLaunchFailed(Session* session, const QString& reason);
    Q_INVOKABLE bool hasPendingAction() const;
    Q_INVOKABLE void moveHost(int index, int offset);
    Q_INVOKABLE void setLocalComputerAt(int index);
    Q_INVOKABLE void setEnabled(bool enabled);
    Q_INVOKABLE void setLocalComputerUuid(const QString& uuid);
    Q_INVOKABLE void setDesktopAppName(const QString& name);
    Q_INVOKABLE void setNextFunctionKey(int functionKey);
    Q_INVOKABLE void setLocalFunctionKey(int functionKey);
    Q_INVOKABLE void setForceFullscreen(bool forceFullscreen);
    Q_INVOKABLE void setAutoStartEnabled(bool enabled);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;
#else
    bool nativeEventFilter(const QByteArray& eventType, void* message, long* result) override;
#endif

signals:
    void sessionRequested(Session* session, QString appName);
    void cancelPendingSessionRequested(Session* pendingSession);
    void showUiRequested();
    void showLocalDesktopRequested();
    void openSettingsRequested();
    void returnedToLocal();
    void errorOccurred(QString message);
    void statusMessage(QString message);
    void configurationChanged();
    void activeComputerChanged();
    void hotkeyStatusChanged();

private slots:
    void handleComputerStateChanged(NvComputer* computer);

private:
    enum class PendingAction { None, ConnectRemote, ReturnLocal };

    void loadSettings();
    void saveSettings();
    void synchronizeHostOrder();
    void validateLocalComputerSelection();
    void requestComputer(const QString& uuid);
    void launchComputer(const QString& uuid, quint64 token);
    void scheduleDeferredLaunch(quint64 token);
    void finishReturnLocal();
    void failTransition(quint64 token, const QString& message);
    SessionSwitchPlanner::Result nextUsableComputer(const QString& currentUuid) const;
    NvComputer* findComputer(const QString& uuid) const;
    bool selectApplication(NvComputer* computer, NvApp* selectedApp,
                           QString* failureReason = nullptr) const;
    void registerHotkeys();
    void unregisterHotkeys();
    void setupTrayIcon();
    void updateTrayMenu();

    static SessionSwitcher* s_Instance;

    ComputerManager* m_ComputerManager;
    QStringList m_HostOrder;
    QString m_LocalComputerUuid;
    QString m_ActiveComputerUuid;
    QString m_DesktopAppName;
    QString m_HotkeyError;
    QString m_StartupWarning;
    QString m_PendingComputerUuid;
    PendingAction m_PendingAction;
    int m_NextFunctionKey;
    int m_LocalFunctionKey;
    bool m_Enabled;
    bool m_ForceFullscreen;
    bool m_AutoStartEnabled;
    bool m_HotkeysRegistered;
    SessionTransitionState m_Transition;
    quint64 m_PendingTransitionToken;
    Session* m_ManagedSession;
    quint64 m_ManagedSessionToken;
    QSet<QString> m_AttemptedComputerUuids;
    QTimer* m_DeferredLaunchTimer;
    QSystemTrayIcon* m_TrayIcon;
    QMenu* m_TrayMenu;
};
