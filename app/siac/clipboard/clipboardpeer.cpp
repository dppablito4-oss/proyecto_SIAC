#include "clipboardpeer.h"

#include "backend/identitymanager.h"

#include <QCryptographicHash>
#include <QSslCertificate>
#include <QSslError>
#include <QSslSocket>

#include <utility>

ClipboardPeer::ClipboardPeer(QObject* parent)
    : QObject(parent),
      m_Socket(new QSslSocket(this))
{
    configureSocket();
}

ClipboardPeer::~ClipboardPeer() = default;

void ClipboardPeer::configureSocket()
{
    QSslConfiguration configuration = IdentityManager::get()->getSslConfig();
    configuration.setProtocol(QSsl::TlsV1_2OrLater);
    m_Socket->setSslConfiguration(configuration);
    connect(m_Socket, &QSslSocket::encrypted,
            this, &ClipboardPeer::handleEncrypted);
    connect(m_Socket, &QSslSocket::readyRead,
            this, &ClipboardPeer::handleReadyRead);
    connect(m_Socket, &QSslSocket::disconnected, this,
            [this]() { emit disconnected(this); });
    connect(m_Socket,
            QOverload<const QList<QSslError>&>::of(&QSslSocket::sslErrors),
            this,
            [this](const QList<QSslError>&) {
                // Trust is established by explicit certificate pinning in
                // ClipboardManager, not by a public certificate authority.
                m_Socket->ignoreSslErrors();
            });
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(m_Socket, &QSslSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
                emit peerError(this, m_Socket->errorString());
            });
#else
    connect(m_Socket, QOverload<QAbstractSocket::SocketError>::of(&QSslSocket::error),
            this, [this](QAbstractSocket::SocketError) {
                emit peerError(this, m_Socket->errorString());
            });
#endif
}

void ClipboardPeer::connectToHost(const QHostAddress& address, quint16 port,
                                  const QString& expectedComputerUuid)
{
    m_Incoming = false;
    m_ExpectedComputerUuid = expectedComputerUuid;
    m_Socket->setPeerVerifyMode(QSslSocket::VerifyNone);
    m_Socket->connectToHostEncrypted(address.toString(), port);
}

bool ClipboardPeer::adoptIncomingSocket(qintptr descriptor)
{
    m_Incoming = true;
    m_Socket->setPeerVerifyMode(QSslSocket::QueryPeer);
    if (!m_Socket->setSocketDescriptor(descriptor)) {
        return false;
    }
    m_Socket->startServerEncryption();
    return true;
}

bool ClipboardPeer::send(const QJsonObject& header, const QByteArray& payload)
{
    if (!m_Socket->isEncrypted()) {
        return false;
    }
    QString error;
    const QByteArray frame = SiacClipboardProtocol::encodeFrame(header, payload, &error);
    if (frame.isEmpty()) {
        emit protocolError(this, error);
        return false;
    }
    const qint64 written = m_Socket->write(frame);
    if (written != frame.size()) {
        emit peerError(this, tr("No se pudo encolar un mensaje TLS completo."));
        close();
        return false;
    }
    return true;
}

void ClipboardPeer::close()
{
    m_Socket->disconnectFromHost();
}

bool ClipboardPeer::isEncrypted() const
{
    return m_Socket->isEncrypted();
}

QHostAddress ClipboardPeer::peerAddress() const
{
    return m_Socket->peerAddress();
}

qint64 ClipboardPeer::bytesToWrite() const
{
    return m_Socket->bytesToWrite();
}

void ClipboardPeer::setRemoteIdentity(const QString& installationId,
                                      const QString& computerUuid,
                                      const QString& displayName)
{
    m_InstallationId = installationId;
    m_ComputerUuid = computerUuid;
    m_DisplayName = displayName;
}

void ClipboardPeer::handleEncrypted()
{
    const QSslCertificate certificate = m_Socket->peerCertificate();
    if (certificate.isNull()) {
        emit peerError(this, tr("El peer no presentó un certificado TLS."));
        close();
        return;
    }
    m_CertificateFingerprint = QString::fromLatin1(
                certificate.digest(QCryptographicHash::Sha256).toHex());
    emit encrypted(this);
}

void ClipboardPeer::handleReadyRead()
{
    QVector<SiacClipboardProtocol::Frame> frames;
    QString error;
    if (!m_Parser.append(m_Socket->readAll(), &frames, &error)) {
        emit protocolError(this, error);
        close();
        return;
    }
    for (const auto& frame : std::as_const(frames)) {
        emit frameReceived(this, frame.header, frame.payload);
    }
}
