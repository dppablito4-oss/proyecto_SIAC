#pragma once

#include "clipboardprotocol.h"

#include <QObject>
#include <QHostAddress>
#include <QJsonObject>

class QSslSocket;

class ClipboardPeer : public QObject
{
    Q_OBJECT

public:
    explicit ClipboardPeer(QObject* parent = nullptr);
    ~ClipboardPeer() override;

    void connectToHost(const QHostAddress& address, quint16 port,
                       const QString& expectedComputerUuid);
    bool adoptIncomingSocket(qintptr descriptor);
    bool send(const QJsonObject& header, const QByteArray& payload = {});
    void close();

    QString certificateFingerprint() const { return m_CertificateFingerprint; }
    QString expectedComputerUuid() const { return m_ExpectedComputerUuid; }
    QString installationId() const { return m_InstallationId; }
    QString computerUuid() const { return m_ComputerUuid; }
    QString displayName() const { return m_DisplayName; }
    bool isIncoming() const { return m_Incoming; }
    bool isEncrypted() const;
    bool isAuthorized() const { return m_Authorized; }
    void setAuthorized(bool authorized) { m_Authorized = authorized; }
    QHostAddress peerAddress() const;
    qint64 bytesToWrite() const;

    void setRemoteIdentity(const QString& installationId,
                           const QString& computerUuid,
                           const QString& displayName);

signals:
    void encrypted(ClipboardPeer* peer);
    void frameReceived(ClipboardPeer* peer, QJsonObject header, QByteArray payload);
    void disconnected(ClipboardPeer* peer);
    void peerError(ClipboardPeer* peer, QString message);
    void protocolError(ClipboardPeer* peer, QString message);

private:
    void configureSocket();
    void handleReadyRead();
    void handleEncrypted();

    QSslSocket* m_Socket;
    SiacClipboardProtocol::FrameParser m_Parser;
    QString m_CertificateFingerprint;
    QString m_ExpectedComputerUuid;
    QString m_InstallationId;
    QString m_ComputerUuid;
    QString m_DisplayName;
    bool m_Incoming = false;
    bool m_Authorized = false;
};
