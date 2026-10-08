#pragma once

#include "clipboardprotocol.h"

#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QSharedPointer>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <QUrl>

class ClipboardPeer;
class ComputerManager;
class SessionSwitcher;
class QClipboard;
class QCryptographicHash;
class QFile;
class QTcpServer;
class QTimer;

class ClipboardManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QStringList pendingPeers READ pendingPeers NOTIFY peersChanged)
    Q_PROPERTY(QStringList authorizedPeers READ authorizedPeers NOTIFY peersChanged)
    Q_PROPERTY(bool transferActive READ transferActive NOTIFY transferChanged)
    Q_PROPERTY(int transferProgress READ transferProgress NOTIFY transferChanged)
    Q_PROPERTY(QString transferDescription READ transferDescription NOTIFY transferChanged)

public:
    explicit ClipboardManager(ComputerManager* computerManager,
                              SessionSwitcher* switcher,
                              QObject* parent = nullptr);
    ~ClipboardManager() override;

    bool enabled() const { return m_Enabled; }
    QString status() const { return m_Status; }
    QString lastError() const { return m_LastError; }
    QStringList pendingPeers() const;
    QStringList authorizedPeers() const;
    bool transferActive() const { return m_TransferActive; }
    int transferProgress() const;
    QString transferDescription() const { return m_TransferDescription; }

    Q_INVOKABLE void setEnabled(bool enabled);
    Q_INVOKABLE void authorizePending(int index);
    Q_INVOKABLE void rejectPending(int index);
    Q_INVOKABLE void revokeAuthorized(int index);
    Q_INVOKABLE void cancelTransfer();
    Q_INVOKABLE void reconnect();

signals:
    void enabledChanged();
    void statusChanged();
    void lastErrorChanged();
    void peersChanged();
    void transferChanged();

private:
    struct AuthorizedPeer
    {
        QString installationId;
        QString computerUuid;
        QString displayName;
        QString fingerprint;
    };

    struct FileEntry
    {
        QString relativePath;
        QString sourcePath;
        quint64 size = 0;
        bool directory = false;
    };

    struct IncomingTransfer
    {
        QString eventId;
        QString basePath;
        QVector<FileEntry> entries;
        QHash<QString, quint64> received;
        QHash<QString, QSharedPointer<QCryptographicHash>> hashes;
        QSet<QString> verified;
        QSet<QString> rootNames;
        quint64 totalSize = 0;
        quint64 receivedSize = 0;
        ClipboardPeer* peer = nullptr;
    };

    void loadSettings();
    void saveSettings();
    void startServer();
    void stopServer();
    void acceptDescriptor(qintptr descriptor);
    void attachPeer(ClipboardPeer* peer);
    void handlePeerEncrypted(ClipboardPeer* peer);
    void handleFrame(ClipboardPeer* peer, const QJsonObject& header,
                     const QByteArray& payload);
    void handlePeerDisconnected(ClipboardPeer* peer);
    void sendHello(ClipboardPeer* peer);
    QJsonObject baseHeader(const QString& type) const;
    bool validateHello(ClipboardPeer* peer, const QJsonObject& header);
    bool isAuthorized(ClipboardPeer* peer) const;
    int authorizedIndex(const QString& installationId) const;
    void addPending(ClipboardPeer* peer);
    void activatePeerIfAllowed(ClipboardPeer* peer);
    void handleActiveComputerChanged();
    void connectActivePeer();
    void setSelectedPeer(ClipboardPeer* peer);

    void handleLocalClipboardChanged();
    void sendCurrentClipboard(bool allowForward = false);
    void sendText(const QString& text, const QString& eventId = {},
                  const QString& originId = {});
    void sendImage(const QString& eventId = {}, const QString& originId = {});
    void beginFileSend(const QList<QUrl>& urls, const QString& eventId = {},
                       const QString& originId = {});
    bool buildFileManifest(const QList<QUrl>& urls, QVector<FileEntry>* entries,
                           quint64* totalSize, QString* error) const;
    void pumpFileSend();
    void finishOutgoingFile(QString* error = nullptr);

    void receiveText(ClipboardPeer* peer, const QJsonObject& header,
                     const QByteArray& payload);
    void receiveImage(ClipboardPeer* peer, const QJsonObject& header,
                      const QByteArray& payload);
    void receiveManifest(ClipboardPeer* peer, const QJsonObject& header,
                         const QByteArray& payload);
    void receiveFileChunk(ClipboardPeer* peer, const QJsonObject& header,
                          const QByteArray& payload);
    void receiveFileEnd(ClipboardPeer* peer, const QJsonObject& header);
    void receiveTransferComplete(ClipboardPeer* peer, const QJsonObject& header);
    void receiveCancel(ClipboardPeer* peer, const QJsonObject& header);
    void applyRemoteMarker(class QMimeData* mimeData, const QString& eventId,
                           const QString& originId);
    bool acceptEvent(const QJsonObject& header, QString* eventId);
    void abortIncoming(const QString& eventId, const QString& message,
                       bool notifyPeer);

    QString cacheRoot() const;
    QString transferPath(const QString& eventId) const;
    bool removeTransferDirectory(const QString& path);
    void cleanupCache();
    void setStatus(const QString& status);
    void setError(const QString& error);
    void setTransferState(bool active, const QString& description,
                          quint64 completed = 0, quint64 total = 0);

    ComputerManager* m_ComputerManager;
    SessionSwitcher* m_Switcher;
    QClipboard* m_Clipboard;
    QTcpServer* m_Server;
    QTimer* m_ReconnectTimer;
    QTimer* m_SendTimer;
    QTimer* m_RecentPeerTimer;
    QVector<ClipboardPeer*> m_Peers;
    QVector<ClipboardPeer*> m_PendingPeers;
    QVector<AuthorizedPeer> m_AuthorizedPeers;
    QSet<ClipboardPeer*> m_ActivationRequested;
    ClipboardPeer* m_SelectedPeer = nullptr;
    ClipboardPeer* m_RecentPeer = nullptr;
    ClipboardPeer* m_ActiveOutgoingPeer = nullptr;
    QString m_ActiveComputerUuid;
    bool m_Enabled = true;
    bool m_ApplyingRemoteClipboard = false;
    QString m_Status;
    QString m_LastError;
    SiacClipboardProtocol::EventTracker m_EventTracker;

    QVector<FileEntry> m_OutgoingEntries;
    QString m_OutgoingEventId;
    QString m_OutgoingOriginId;
    int m_OutgoingEntryIndex = 0;
    quint64 m_OutgoingOffset = 0;
    quint64 m_OutgoingTotal = 0;
    quint64 m_OutgoingSent = 0;
    QFile* m_OutgoingFile = nullptr;
    QCryptographicHash* m_OutgoingHash = nullptr;
    ClipboardPeer* m_OutgoingPeer = nullptr;
    QHash<QString, IncomingTransfer> m_IncomingTransfers;

    bool m_TransferActive = false;
    quint64 m_TransferCompleted = 0;
    quint64 m_TransferTotal = 0;
    QString m_TransferDescription;
};
