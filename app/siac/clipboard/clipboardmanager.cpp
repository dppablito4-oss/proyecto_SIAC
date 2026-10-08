#include "clipboardmanager.h"

#include "clipboardpeer.h"
#include "backend/computermanager.h"
#include "backend/identitymanager.h"
#include "backend/nvcomputer.h"
#include "siac/sessionswitcher.h"

#include <QClipboard>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHostAddress>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QPixmap>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QSysInfo>
#include <QTcpServer>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QtEndian>

#include <cmath>
#include <functional>
#include <utility>

namespace {
constexpr auto SettingsGroup = "siacClipboard";
constexpr auto EventMimeType = "application/x-siac-clipboard-event";
constexpr quint64 MaxCacheSize = 40ull * 1024 * 1024 * 1024;
constexpr qint64 MaxCacheAgeMs = 24ll * 60 * 60 * 1000;
constexpr quint64 MaxDecodedImagePixels = 64ull * 1024 * 1024;

class ClipboardTcpServer : public QTcpServer
{
public:
    std::function<void(qintptr)> descriptorAccepted;

protected:
    void incomingConnection(qintptr descriptor) override
    {
        if (descriptorAccepted) {
            descriptorAccepted(descriptor);
        }
    }
};

QString shortFingerprint(const QString& fingerprint)
{
    QStringList groups;
    for (int i = 0; i < qMin(12, fingerprint.size()); i += 4) {
        groups.append(fingerprint.mid(i, 4).toUpper());
    }
    return groups.join(QLatin1Char('-'));
}

QString localCertificateFingerprint()
{
    const QSslCertificate certificate(IdentityManager::get()->getCertificate());
    return QString::fromLatin1(
                certificate.digest(QCryptographicHash::Sha256).toHex());
}

quint64 directorySize(const QString& path)
{
    quint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += quint64(qMax<qint64>(0, it.fileInfo().size()));
    }
    return total;
}

bool readRemoteMarker(const QMimeData* mimeData, QString* originId,
                      QString* eventId)
{
    if (!mimeData || !mimeData->hasFormat(EventMimeType)) return false;
    const QString marker = QString::fromUtf8(mimeData->data(EventMimeType));
    const int separator = marker.indexOf(QLatin1Char(':'));
    if (separator <= 0 || separator >= marker.size() - 1) return false;
    const QString origin = marker.left(separator);
    const QString event = marker.mid(separator + 1);
    if (origin.size() > 256 || event.size() > 128) return false;
    if (originId) *originId = origin;
    if (eventId) *eventId = event;
    return true;
}
}

ClipboardManager::ClipboardManager(ComputerManager* computerManager,
                                   SessionSwitcher* switcher,
                                   QObject* parent)
    : QObject(parent),
      m_ComputerManager(computerManager),
      m_Switcher(switcher),
      m_Clipboard(QGuiApplication::clipboard()),
      m_Server(new ClipboardTcpServer()),
      m_ReconnectTimer(new QTimer(this)),
      m_SendTimer(new QTimer(this)),
      m_RecentPeerTimer(new QTimer(this)),
      m_EventTracker(512)
{
    m_Server->setParent(this);
    static_cast<ClipboardTcpServer*>(m_Server)->descriptorAccepted =
            [this](qintptr descriptor) { acceptDescriptor(descriptor); };

    m_ReconnectTimer->setSingleShot(true);
    m_ReconnectTimer->setInterval(5000);
    connect(m_ReconnectTimer, &QTimer::timeout,
            this, &ClipboardManager::connectActivePeer);

    m_SendTimer->setSingleShot(true);
    connect(m_SendTimer, &QTimer::timeout,
            this, &ClipboardManager::pumpFileSend);

    m_RecentPeerTimer->setSingleShot(true);
    m_RecentPeerTimer->setInterval(5000);
    connect(m_RecentPeerTimer, &QTimer::timeout, this, [this]() {
        m_RecentPeer = nullptr;
    });

    connect(m_Clipboard, &QClipboard::dataChanged,
            this, &ClipboardManager::handleLocalClipboardChanged);
    connect(m_Switcher, &SessionSwitcher::activeComputerChanged,
            this, &ClipboardManager::handleActiveComputerChanged);

    loadSettings();
    cleanupCache();
    if (m_Enabled) {
        startServer();
    }
    setStatus(m_Enabled ? tr("Esperando una conexión SIAC autorizada")
                        : tr("Portapapeles compartido desactivado"));
}

ClipboardManager::~ClipboardManager()
{
    stopServer();
    delete m_OutgoingFile;
    delete m_OutgoingHash;
}

void ClipboardManager::loadSettings()
{
    QSettings settings;
    settings.beginGroup(SettingsGroup);
    m_Enabled = settings.value(QStringLiteral("enabled"), true).toBool();
    const int count = settings.beginReadArray(QStringLiteral("authorizedPeers"));
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        AuthorizedPeer peer;
        peer.installationId = settings.value(QStringLiteral("installationId")).toString();
        peer.computerUuid = settings.value(QStringLiteral("computerUuid")).toString();
        peer.displayName = settings.value(QStringLiteral("displayName")).toString();
        peer.fingerprint = settings.value(QStringLiteral("fingerprint")).toString();
        if (!peer.installationId.isEmpty() && peer.fingerprint.size() == 64) {
            m_AuthorizedPeers.append(peer);
        }
    }
    settings.endArray();
    settings.endGroup();
}

void ClipboardManager::saveSettings()
{
    QSettings settings;
    settings.beginGroup(SettingsGroup);
    settings.setValue(QStringLiteral("enabled"), m_Enabled);
    settings.beginWriteArray(QStringLiteral("authorizedPeers"));
    for (int i = 0; i < m_AuthorizedPeers.size(); ++i) {
        settings.setArrayIndex(i);
        const auto& peer = m_AuthorizedPeers.at(i);
        settings.setValue(QStringLiteral("installationId"), peer.installationId);
        settings.setValue(QStringLiteral("computerUuid"), peer.computerUuid);
        settings.setValue(QStringLiteral("displayName"), peer.displayName);
        settings.setValue(QStringLiteral("fingerprint"), peer.fingerprint);
    }
    settings.endArray();
    settings.endGroup();
    settings.sync();
}

QStringList ClipboardManager::pendingPeers() const
{
    QStringList result;
    for (ClipboardPeer* peer : m_PendingPeers) {
        result.append(tr("%1 — código %2 — UUID %3")
                      .arg(peer->displayName().isEmpty() ? tr("Equipo sin nombre") : peer->displayName(),
                           SiacClipboardProtocol::pairingCode(
                               localCertificateFingerprint(),
                               peer->certificateFingerprint()),
                           peer->computerUuid().isEmpty() ? tr("sin configurar") : peer->computerUuid()));
    }
    return result;
}

QStringList ClipboardManager::authorizedPeers() const
{
    QStringList result;
    for (const auto& peer : m_AuthorizedPeers) {
        result.append(tr("%1 — código %2")
                      .arg(peer.displayName.isEmpty() ? peer.installationId : peer.displayName,
                           shortFingerprint(peer.fingerprint)));
    }
    return result;
}

int ClipboardManager::transferProgress() const
{
    if (!m_TransferActive || m_TransferTotal == 0) {
        return m_TransferActive ? 0 : 100;
    }
    return int(qMin<quint64>(100, (m_TransferCompleted * 100) / m_TransferTotal));
}

void ClipboardManager::setEnabled(bool enabled)
{
    if (m_Enabled == enabled) return;
    m_Enabled = enabled;
    saveSettings();
    if (enabled) {
        startServer();
        handleActiveComputerChanged();
    }
    else {
        cancelTransfer();
        stopServer();
        m_RecentPeerTimer->stop();
        m_RecentPeer = nullptr;
        for (ClipboardPeer* peer : std::as_const(m_Peers)) peer->close();
        setSelectedPeer(nullptr);
        setStatus(tr("Portapapeles compartido desactivado"));
    }
    emit enabledChanged();
}

void ClipboardManager::startServer()
{
    if (m_Server->isListening()) return;
    if (!m_Server->listen(QHostAddress::AnyIPv4,
                          SiacClipboardProtocol::DefaultPort)) {
        setError(tr("No se pudo abrir el canal de portapapeles en el puerto %1: %2")
                 .arg(SiacClipboardProtocol::DefaultPort)
                 .arg(m_Server->errorString()));
        setStatus(tr("Canal de portapapeles no disponible"));
    }
}

void ClipboardManager::stopServer()
{
    m_ReconnectTimer->stop();
    if (m_Server->isListening()) m_Server->close();
}

void ClipboardManager::acceptDescriptor(qintptr descriptor)
{
    auto* peer = new ClipboardPeer(this);
    if (!peer->adoptIncomingSocket(descriptor)) {
        peer->deleteLater();
        return;
    }
    if (!m_Enabled) {
        peer->close();
        peer->deleteLater();
        return;
    }
    if (!SiacClipboardProtocol::isLanAddress(peer->peerAddress())) {
        setError(tr("Se rechazó una conexión de portapapeles fuera de la red local."));
        peer->close();
        peer->deleteLater();
        return;
    }
    attachPeer(peer);
}

void ClipboardManager::attachPeer(ClipboardPeer* peer)
{
    m_Peers.append(peer);
    connect(peer, &ClipboardPeer::encrypted,
            this, &ClipboardManager::handlePeerEncrypted);
    connect(peer, &ClipboardPeer::frameReceived,
            this, &ClipboardManager::handleFrame);
    connect(peer, &ClipboardPeer::disconnected,
            this, &ClipboardManager::handlePeerDisconnected);
    connect(peer, &ClipboardPeer::peerError, this,
            [this, peer](ClipboardPeer*, const QString& error) {
                if (peer == m_ActiveOutgoingPeer) {
                    setStatus(tr("Agente remoto no disponible; el escritorio continúa operativo"));
                    setError(tr("Canal de portapapeles: %1").arg(error));
                }
            });
    connect(peer, &ClipboardPeer::protocolError, this,
            [this](ClipboardPeer*, const QString& error) {
                setError(tr("Mensaje de portapapeles rechazado: %1").arg(error));
            });
}

QJsonObject ClipboardManager::baseHeader(const QString& type) const
{
    return {{QStringLiteral("type"), type},
            {QStringLiteral("version"), SiacClipboardProtocol::Version},
            {QStringLiteral("originId"), IdentityManager::get()->getUniqueId()}};
}

void ClipboardManager::sendHello(ClipboardPeer* peer)
{
    QJsonObject hello = baseHeader(QStringLiteral("hello"));
    hello.insert(QStringLiteral("installationId"), IdentityManager::get()->getUniqueId());
    hello.insert(QStringLiteral("computerUuid"), m_Switcher->localComputerUuid());
    hello.insert(QStringLiteral("displayName"), QSysInfo::machineHostName());
    peer->send(hello);
}

void ClipboardManager::handlePeerEncrypted(ClipboardPeer* peer)
{
    sendHello(peer);
    if (peer == m_ActiveOutgoingPeer && !m_ActiveComputerUuid.isEmpty()) {
        QJsonObject activate = baseHeader(QStringLiteral("activate"));
        activate.insert(QStringLiteral("computerUuid"), m_Switcher->localComputerUuid());
        peer->send(activate);
    }
}

bool ClipboardManager::validateHello(ClipboardPeer* peer, const QJsonObject& header)
{
    const QString installationId = header.value(QStringLiteral("installationId")).toString();
    const QString computerUuid = header.value(QStringLiteral("computerUuid")).toString();
    const QString displayName = header.value(QStringLiteral("displayName")).toString();
    if (installationId.isEmpty() || computerUuid.isEmpty() || displayName.size() > 256) {
        setError(tr("Se rechazó una identidad de portapapeles incompleta."));
        peer->close();
        return false;
    }
    if (!peer->expectedComputerUuid().isEmpty() &&
            peer->expectedComputerUuid() != computerUuid) {
        setError(tr("La identidad SIAC remota no coincide con la computadora seleccionada."));
        peer->close();
        return false;
    }

    peer->setRemoteIdentity(installationId, computerUuid, displayName);
    const int index = authorizedIndex(installationId);
    if (index >= 0) {
        const auto& authorized = m_AuthorizedPeers.at(index);
        const auto decision = SiacClipboardProtocol::authorizePeer(
                    authorized.installationId, authorized.computerUuid,
                    authorized.fingerprint, installationId, computerUuid,
                    peer->certificateFingerprint());
        if (decision != SiacClipboardProtocol::AuthorizationDecision::Authorized) {
            setError(tr("El certificado o UUID del equipo autorizado cambió. Se bloqueó el canal."));
            peer->close();
            return false;
        }
        peer->setAuthorized(true);
        activatePeerIfAllowed(peer);
    }
    else {
        addPending(peer);
        setStatus(tr("Autorización de portapapeles pendiente"));
    }
    return true;
}

int ClipboardManager::authorizedIndex(const QString& installationId) const
{
    for (int i = 0; i < m_AuthorizedPeers.size(); ++i) {
        if (m_AuthorizedPeers.at(i).installationId == installationId) return i;
    }
    return -1;
}

bool ClipboardManager::isAuthorized(ClipboardPeer* peer) const
{
    return peer && peer->isAuthorized() &&
            authorizedIndex(peer->installationId()) >= 0;
}

void ClipboardManager::addPending(ClipboardPeer* peer)
{
    if (!m_PendingPeers.contains(peer)) {
        m_PendingPeers.append(peer);
        emit peersChanged();
    }
}

void ClipboardManager::authorizePending(int index)
{
    if (index < 0 || index >= m_PendingPeers.size()) return;
    ClipboardPeer* peer = m_PendingPeers.takeAt(index);
    if (peer->installationId().isEmpty() || peer->certificateFingerprint().size() != 64) {
        setError(tr("La solicitud todavía no presentó una identidad válida."));
        emit peersChanged();
        return;
    }
    AuthorizedPeer authorized{peer->installationId(), peer->computerUuid(),
                              peer->displayName(), peer->certificateFingerprint()};
    m_AuthorizedPeers.append(authorized);
    peer->setAuthorized(true);
    saveSettings();
    emit peersChanged();
    activatePeerIfAllowed(peer);
}

void ClipboardManager::rejectPending(int index)
{
    if (index < 0 || index >= m_PendingPeers.size()) return;
    ClipboardPeer* peer = m_PendingPeers.takeAt(index);
    m_ActivationRequested.remove(peer);
    peer->close();
    emit peersChanged();
}

void ClipboardManager::revokeAuthorized(int index)
{
    if (index < 0 || index >= m_AuthorizedPeers.size()) return;
    const QString id = m_AuthorizedPeers.at(index).installationId;
    m_AuthorizedPeers.removeAt(index);
    for (ClipboardPeer* peer : std::as_const(m_Peers)) {
        if (peer->installationId() == id) {
            peer->setAuthorized(false);
            if (peer == m_SelectedPeer) setSelectedPeer(nullptr);
            peer->close();
        }
    }
    saveSettings();
    emit peersChanged();
}

void ClipboardManager::activatePeerIfAllowed(ClipboardPeer* peer)
{
    if (!isAuthorized(peer)) return;
    const bool requested = m_ActivationRequested.contains(peer);
    const bool outgoingActive = peer == m_ActiveOutgoingPeer &&
            !m_ActiveComputerUuid.isEmpty();
    if (requested || outgoingActive) {
        setSelectedPeer(peer);
        setError({});
        setStatus(tr("Portapapeles conectado con %1").arg(peer->displayName()));
        // The connector publishes its current clipboard. The listening peer
        // waits for an actual clipboard change, avoiding a stale two-way swap.
        if (outgoingActive) sendCurrentClipboard(true);
    }
}

void ClipboardManager::handleFrame(ClipboardPeer* peer, const QJsonObject& header,
                                   const QByteArray& payload)
{
    const int version = header.value(QStringLiteral("version")).toInt(-1);
    const QString type = header.value(QStringLiteral("type")).toString();
    if (version != SiacClipboardProtocol::Version || type.isEmpty()) {
        peer->close();
        return;
    }
    if (type == QStringLiteral("hello")) {
        validateHello(peer, header);
        return;
    }
    if (peer->installationId().isEmpty()) {
        peer->close();
        return;
    }
    if (type == QStringLiteral("activate")) {
        m_ActivationRequested.insert(peer);
        activatePeerIfAllowed(peer);
        return;
    }
    if (type == QStringLiteral("deactivate")) {
        m_ActivationRequested.remove(peer);
        if (peer == m_SelectedPeer) {
            m_RecentPeer = peer;
            m_RecentPeerTimer->start();
            setSelectedPeer(nullptr);
        }
        return;
    }
    const QString eventId = header.value(QStringLiteral("eventId")).toString();
    const bool existingTransfer = m_IncomingTransfers.contains(eventId) &&
            m_IncomingTransfers.value(eventId).peer == peer;
    if (!SiacClipboardProtocol::shouldAcceptContent(
                isAuthorized(peer),
                peer == m_SelectedPeer || peer == m_RecentPeer,
                existingTransfer)) {
        return;
    }

    if (type == QStringLiteral("text")) receiveText(peer, header, payload);
    else if (type == QStringLiteral("image")) receiveImage(peer, header, payload);
    else if (type == QStringLiteral("fileManifest")) receiveManifest(peer, header, payload);
    else if (type == QStringLiteral("fileChunk")) receiveFileChunk(peer, header, payload);
    else if (type == QStringLiteral("fileEnd")) receiveFileEnd(peer, header);
    else if (type == QStringLiteral("fileComplete")) receiveTransferComplete(peer, header);
    else if (type == QStringLiteral("cancel")) receiveCancel(peer, header);
    else if (type == QStringLiteral("error")) {
        QString message = tr("El equipo remoto informó: %1")
                .arg(header.value(QStringLiteral("message")).toString());
        if (peer == m_OutgoingPeer &&
                header.value(QStringLiteral("eventId")).toString() == m_OutgoingEventId) {
            finishOutgoingFile(&message);
        }
        else {
            setError(message);
        }
    }
}

void ClipboardManager::handlePeerDisconnected(ClipboardPeer* peer)
{
    const bool wasOutgoing = peer == m_ActiveOutgoingPeer;
    if (peer == m_SelectedPeer) setSelectedPeer(nullptr);
    if (peer == m_RecentPeer) {
        m_RecentPeer = nullptr;
        m_RecentPeerTimer->stop();
    }
    if (wasOutgoing) m_ActiveOutgoingPeer = nullptr;
    m_ActivationRequested.remove(peer);
    m_PendingPeers.removeAll(peer);
    m_Peers.removeAll(peer);

    QStringList incomplete;
    for (auto it = m_IncomingTransfers.cbegin(); it != m_IncomingTransfers.cend(); ++it) {
        if (it.value().peer == peer) incomplete.append(it.key());
    }
    for (const QString& eventId : std::as_const(incomplete)) {
        abortIncoming(eventId, tr("La conexión se perdió durante la transferencia."), false);
    }
    if (m_OutgoingPeer == peer) {
        QString error = tr("La conexión se perdió durante el envío.");
        finishOutgoingFile(&error);
    }

    emit peersChanged();
    peer->deleteLater();
    if (wasOutgoing && m_Enabled && !m_ActiveComputerUuid.isEmpty()) {
        m_ReconnectTimer->start();
    }
}

void ClipboardManager::handleActiveComputerChanged()
{
    const QString nextUuid = m_Switcher->activeComputerUuid();
    if (nextUuid == m_ActiveComputerUuid) return;

    if (m_ActiveOutgoingPeer) {
        ClipboardPeer* previousPeer = m_ActiveOutgoingPeer;
        if (previousPeer->isEncrypted()) {
            previousPeer->send(baseHeader(QStringLiteral("deactivate")));
        }
        else {
            previousPeer->close();
        }
        // Keep an encrypted socket alive for an in-flight transfer, but it is
        // no longer the selected outgoing peer and must not block PC 03.
        m_ActiveOutgoingPeer = nullptr;
    }
    if (m_SelectedPeer) {
        m_RecentPeer = m_SelectedPeer;
        m_RecentPeerTimer->start();
    }
    setSelectedPeer(nullptr);
    m_ActiveComputerUuid = nextUuid;
    m_ReconnectTimer->stop();
    if (m_Enabled && !m_ActiveComputerUuid.isEmpty()) {
        connectActivePeer();
    }
    else if (m_Enabled) {
        setStatus(m_TransferActive ? tr("Finalizando transferencia; sincronización pausada")
                                   : tr("Escritorio local; sincronización en espera"));
    }
}

void ClipboardManager::connectActivePeer()
{
    if (!m_Enabled || m_ActiveComputerUuid.isEmpty() ||
            m_ActiveOutgoingPeer) {
        return;
    }

    NvComputer* destination = nullptr;
    for (NvComputer* computer : m_ComputerManager->getComputers()) {
        QReadLocker lock(&computer->lock);
        if (computer->uuid == m_ActiveComputerUuid) {
            destination = computer;
            break;
        }
    }
    if (!destination) {
        setStatus(tr("No se encontró el agente de la computadora activa"));
        return;
    }

    for (ClipboardPeer* peer : std::as_const(m_Peers)) {
        if (!peer->isIncoming() && peer->isEncrypted() && isAuthorized(peer) &&
                peer->computerUuid() == m_ActiveComputerUuid) {
            m_ActiveOutgoingPeer = peer;
            QJsonObject activate = baseHeader(QStringLiteral("activate"));
            activate.insert(QStringLiteral("computerUuid"),
                            m_Switcher->localComputerUuid());
            if (!peer->send(activate)) return;
            activatePeerIfAllowed(peer);
            return;
        }
    }

    QString address;
    {
        QReadLocker lock(&destination->lock);
        address = destination->localAddress.address();
    }
    const QHostAddress hostAddress(address);
    if (!SiacClipboardProtocol::isLanAddress(hostAddress)) {
        setStatus(tr("La computadora activa no tiene una dirección LAN privada válida"));
        return;
    }

    auto* peer = new ClipboardPeer(this);
    attachPeer(peer);
    m_ActiveOutgoingPeer = peer;
    setStatus(tr("Conectando el portapapeles con el equipo activo…"));
    peer->connectToHost(hostAddress, SiacClipboardProtocol::DefaultPort,
                        m_ActiveComputerUuid);
}

void ClipboardManager::reconnect()
{
    setError({});
    if (m_ActiveOutgoingPeer) {
        setStatus(tr("Reconectando el canal de portapapeles…"));
        m_ActiveOutgoingPeer->close();
    }
    else {
        connectActivePeer();
    }
}

void ClipboardManager::setSelectedPeer(ClipboardPeer* peer)
{
    m_SelectedPeer = peer;
}

void ClipboardManager::handleLocalClipboardChanged()
{
    if (m_ApplyingRemoteClipboard || !m_Enabled || !isAuthorized(m_SelectedPeer)) return;
    const QMimeData* mime = m_Clipboard->mimeData();
    if (!mime || mime->hasFormat(EventMimeType)) return;
    sendCurrentClipboard();
}

void ClipboardManager::sendCurrentClipboard(bool allowForward)
{
    if (!m_Enabled || !isAuthorized(m_SelectedPeer)) return;
    const QMimeData* mime = m_Clipboard->mimeData();
    if (!mime) return;

    QString originId;
    QString eventId;
    const bool forwarding = readRemoteMarker(mime, &originId, &eventId);
    if (forwarding && (!allowForward || !m_EventTracker.contains(eventId) ||
                       originId == IdentityManager::get()->getUniqueId() ||
                       originId == m_SelectedPeer->installationId())) {
        return;
    }

    if (mime->hasUrls()) {
        QList<QUrl> localUrls;
        for (const QUrl& url : mime->urls()) {
            if (url.isLocalFile()) localUrls.append(url);
        }
        if (!localUrls.isEmpty()) {
#ifdef Q_OS_WIN32
            const QByteArray effect = mime->data(QStringLiteral("Preferred DropEffect"));
            if (effect.size() >= 4 &&
                    (qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(effect.constData())) & 2)) {
                setError(tr("Ctrl+X no se sincroniza. Use Ctrl+C para conservar los archivos originales."));
                return;
            }
#endif
            beginFileSend(localUrls, eventId, originId);
            return;
        }
    }
    if (mime->hasImage()) {
        sendImage(eventId, originId);
        return;
    }
    if (mime->hasText()) {
        sendText(mime->text(), eventId, originId);
    }
}

void ClipboardManager::sendText(const QString& text, const QString& forwardedEventId,
                                const QString& forwardedOriginId)
{
    const QByteArray payload = text.toUtf8();
    if (quint64(payload.size()) > SiacClipboardProtocol::MaxTextSize) {
        setError(tr("El texto supera el límite de 16 MiB."));
        return;
    }
    const QString eventId = forwardedEventId.isEmpty()
            ? QUuid::createUuid().toString(QUuid::WithoutBraces)
            : forwardedEventId;
    const QString originId = forwardedOriginId.isEmpty()
            ? IdentityManager::get()->getUniqueId() : forwardedOriginId;
    if (forwardedEventId.isEmpty()) m_EventTracker.remember(eventId);
    QJsonObject header = baseHeader(QStringLiteral("text"));
    header.insert(QStringLiteral("eventId"), eventId);
    header.insert(QStringLiteral("originId"), originId);
    if (!m_SelectedPeer->send(header, payload)) {
        setError(tr("No se pudo enviar el texto al equipo activo."));
    }
}

void ClipboardManager::sendImage(const QString& forwardedEventId,
                                 const QString& forwardedOriginId)
{
    const QVariant imageData = m_Clipboard->mimeData()->imageData();
    QImage image = qvariant_cast<QImage>(imageData);
    if (image.isNull() && imageData.canConvert<QPixmap>()) {
        image = qvariant_cast<QPixmap>(imageData).toImage();
    }
    if (image.isNull()) return;
    if (quint64(image.width()) * quint64(image.height()) > MaxDecodedImagePixels) {
        setError(tr("La imagen supera el límite de 64 megapíxeles."));
        return;
    }
    QByteArray payload;
    QBuffer buffer(&payload);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG") ||
            quint64(payload.size()) > SiacClipboardProtocol::MaxImageSize) {
        setError(tr("La imagen no pudo codificarse o supera el límite de 64 MiB."));
        return;
    }
    const QString eventId = forwardedEventId.isEmpty()
            ? QUuid::createUuid().toString(QUuid::WithoutBraces)
            : forwardedEventId;
    const QString originId = forwardedOriginId.isEmpty()
            ? IdentityManager::get()->getUniqueId() : forwardedOriginId;
    if (forwardedEventId.isEmpty()) m_EventTracker.remember(eventId);
    QJsonObject header = baseHeader(QStringLiteral("image"));
    header.insert(QStringLiteral("eventId"), eventId);
    header.insert(QStringLiteral("originId"), originId);
    if (!m_SelectedPeer->send(header, payload)) {
        setError(tr("No se pudo enviar la imagen al equipo activo."));
    }
}

bool ClipboardManager::buildFileManifest(const QList<QUrl>& urls,
                                         QVector<FileEntry>* entries,
                                         quint64* totalSize,
                                         QString* error) const
{
    entries->clear();
    *totalSize = 0;
    QSet<QString> roots;

    for (const QUrl& url : urls) {
        const QFileInfo rootInfo(url.toLocalFile());
        if (!rootInfo.exists() || rootInfo.isSymLink()) {
            if (error) *error = tr("Un archivo ya no existe o es un enlace: %1").arg(rootInfo.filePath());
            return false;
        }
        const QString rootName = SiacClipboardProtocol::safeRootName(rootInfo.fileName(), roots);
        roots.insert(rootName.toCaseFolded());

        auto appendEntry = [&](const QFileInfo& info, const QString& relative) -> bool {
            if (entries->size() >= SiacClipboardProtocol::MaxTransferEntries ||
                    !SiacClipboardProtocol::isSafeRelativePath(relative)) {
                if (error) *error = tr("La selección contiene demasiados elementos o una ruta no válida.");
                return false;
            }
            FileEntry entry;
            entry.relativePath = relative;
            entry.sourcePath = info.absoluteFilePath();
            entry.directory = info.isDir();
            entry.size = entry.directory ? 0 : quint64(qMax<qint64>(0, info.size()));
            if (*totalSize > SiacClipboardProtocol::MaxTransferSize - entry.size) {
                if (error) *error = tr("La selección supera el límite de 20 GiB por copia.");
                return false;
            }
            *totalSize += entry.size;
            entries->append(entry);
            return true;
        };

        if (!appendEntry(rootInfo, rootName)) return false;
        if (rootInfo.isDir()) {
            QDir root(rootInfo.absoluteFilePath());
            QDirIterator it(rootInfo.absoluteFilePath(),
                            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                const QFileInfo info = it.fileInfo();
                if (info.isSymLink()) continue;
                const QString relative = rootName + QLatin1Char('/') +
                        root.relativeFilePath(info.absoluteFilePath()).replace(QLatin1Char('\\'), QLatin1Char('/'));
                if (!appendEntry(info, relative)) return false;
            }
        }
    }
    return !entries->isEmpty();
}

void ClipboardManager::beginFileSend(const QList<QUrl>& urls,
                                     const QString& forwardedEventId,
                                     const QString& forwardedOriginId)
{
    if (m_OutgoingPeer) cancelTransfer();
    QString error;
    if (!buildFileManifest(urls, &m_OutgoingEntries, &m_OutgoingTotal, &error)) {
        setError(error);
        return;
    }

    m_OutgoingEventId = forwardedEventId.isEmpty()
            ? QUuid::createUuid().toString(QUuid::WithoutBraces)
            : forwardedEventId;
    m_OutgoingOriginId = forwardedOriginId.isEmpty()
            ? IdentityManager::get()->getUniqueId() : forwardedOriginId;
    if (forwardedEventId.isEmpty()) m_EventTracker.remember(m_OutgoingEventId);
    m_OutgoingEntryIndex = 0;
    m_OutgoingOffset = 0;
    m_OutgoingSent = 0;
    m_OutgoingPeer = m_SelectedPeer;

    QJsonArray manifest;
    for (const auto& entry : std::as_const(m_OutgoingEntries)) {
        manifest.append(QJsonObject{{QStringLiteral("path"), entry.relativePath},
                                    {QStringLiteral("directory"), entry.directory},
                                    {QStringLiteral("size"), double(entry.size)}});
    }
    QJsonObject header = baseHeader(QStringLiteral("fileManifest"));
    header.insert(QStringLiteral("eventId"), m_OutgoingEventId);
    header.insert(QStringLiteral("originId"), m_OutgoingOriginId);
    header.insert(QStringLiteral("totalSize"), double(m_OutgoingTotal));
    const QByteArray payload = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    if (!m_OutgoingPeer->send(header, payload)) {
        error = tr("No se pudo iniciar la transferencia de archivos.");
        finishOutgoingFile(&error);
        return;
    }

    setTransferState(true, tr("Enviando %1 elemento(s)").arg(m_OutgoingEntries.size()),
                     0, m_OutgoingTotal);
    m_SendTimer->start(0);
}

void ClipboardManager::pumpFileSend()
{
    if (!m_OutgoingPeer || !m_OutgoingPeer->isEncrypted()) {
        QString error = tr("El canal se cerró durante la transferencia.");
        finishOutgoingFile(&error);
        return;
    }
    if (m_OutgoingPeer->bytesToWrite() > 2 * 1024 * 1024) {
        m_SendTimer->start(20);
        return;
    }

    while (m_OutgoingEntryIndex < m_OutgoingEntries.size()) {
        const FileEntry& entry = m_OutgoingEntries.at(m_OutgoingEntryIndex);
        if (entry.directory) {
            ++m_OutgoingEntryIndex;
            continue;
        }

        if (!m_OutgoingFile) {
            m_OutgoingFile = new QFile(entry.sourcePath);
            if (!m_OutgoingFile->open(QIODevice::ReadOnly)) {
                QString error = tr("No se pudo leer %1: %2")
                        .arg(entry.sourcePath, m_OutgoingFile->errorString());
                finishOutgoingFile(&error);
                return;
            }
            m_OutgoingHash = new QCryptographicHash(QCryptographicHash::Sha256);
            m_OutgoingOffset = 0;
        }

        const QByteArray chunk = m_OutgoingFile->read(SiacClipboardProtocol::FileChunkSize);
        if (!chunk.isEmpty()) {
            QJsonObject header = baseHeader(QStringLiteral("fileChunk"));
            header.insert(QStringLiteral("eventId"), m_OutgoingEventId);
            header.insert(QStringLiteral("originId"), m_OutgoingOriginId);
            header.insert(QStringLiteral("path"), entry.relativePath);
            header.insert(QStringLiteral("offset"), double(m_OutgoingOffset));
            if (!m_OutgoingPeer->send(header, chunk)) {
                QString error = tr("No se pudo enviar un bloque de %1.").arg(entry.relativePath);
                finishOutgoingFile(&error);
                return;
            }
            m_OutgoingHash->addData(chunk);
            m_OutgoingOffset += quint64(chunk.size());
            m_OutgoingSent += quint64(chunk.size());
            setTransferState(true, m_TransferDescription, m_OutgoingSent, m_OutgoingTotal);
            m_SendTimer->start(0);
            return;
        }

        if (m_OutgoingFile->error() != QFile::NoError ||
                m_OutgoingOffset != entry.size) {
            QString error = tr("El archivo cambió o no pudo leerse completamente: %1")
                    .arg(entry.sourcePath);
            finishOutgoingFile(&error);
            return;
        }

        QJsonObject end = baseHeader(QStringLiteral("fileEnd"));
        end.insert(QStringLiteral("eventId"), m_OutgoingEventId);
        end.insert(QStringLiteral("originId"), m_OutgoingOriginId);
        end.insert(QStringLiteral("path"), entry.relativePath);
        end.insert(QStringLiteral("sha256"), QString::fromLatin1(m_OutgoingHash->result().toHex()));
        if (!m_OutgoingPeer->send(end)) {
            QString error = tr("No se pudo confirmar la integridad de %1.")
                    .arg(entry.relativePath);
            finishOutgoingFile(&error);
            return;
        }
        delete m_OutgoingFile;
        m_OutgoingFile = nullptr;
        delete m_OutgoingHash;
        m_OutgoingHash = nullptr;
        m_OutgoingOffset = 0;
        ++m_OutgoingEntryIndex;
    }

    QJsonObject complete = baseHeader(QStringLiteral("fileComplete"));
    complete.insert(QStringLiteral("eventId"), m_OutgoingEventId);
    complete.insert(QStringLiteral("originId"), m_OutgoingOriginId);
    if (!m_OutgoingPeer->send(complete)) {
        QString error = tr("No se pudo confirmar el final de la transferencia.");
        finishOutgoingFile(&error);
        return;
    }
    finishOutgoingFile();
}

void ClipboardManager::finishOutgoingFile(QString* error)
{
    m_SendTimer->stop();
    if (error && m_OutgoingPeer && m_OutgoingPeer->isEncrypted() &&
            !m_OutgoingEventId.isEmpty()) {
        QJsonObject cancel = baseHeader(QStringLiteral("cancel"));
        cancel.insert(QStringLiteral("eventId"), m_OutgoingEventId);
        cancel.insert(QStringLiteral("originId"), m_OutgoingOriginId);
        m_OutgoingPeer->send(cancel);
    }
    delete m_OutgoingFile;
    m_OutgoingFile = nullptr;
    delete m_OutgoingHash;
    m_OutgoingHash = nullptr;
    m_OutgoingEntries.clear();
    m_OutgoingEventId.clear();
    m_OutgoingOriginId.clear();
    m_OutgoingPeer = nullptr;
    m_OutgoingEntryIndex = 0;
    m_OutgoingOffset = 0;
    m_OutgoingTotal = 0;
    m_OutgoingSent = 0;
    setTransferState(false, error ? *error : tr("Transferencia enviada"));
    if (error) setError(*error);
}

bool ClipboardManager::acceptEvent(const QJsonObject& header, QString* eventId)
{
    *eventId = header.value(QStringLiteral("eventId")).toString();
    const QString origin = header.value(QStringLiteral("originId")).toString();
    if (eventId->isEmpty() || origin.isEmpty() ||
            origin == IdentityManager::get()->getUniqueId()) {
        return false;
    }
    return m_EventTracker.remember(*eventId);
}

void ClipboardManager::applyRemoteMarker(QMimeData* mimeData,
                                         const QString& eventId,
                                         const QString& originId)
{
    mimeData->setData(EventMimeType, (originId + QLatin1Char(':') + eventId).toUtf8());
    m_ApplyingRemoteClipboard = true;
    m_Clipboard->setMimeData(mimeData);
    m_ApplyingRemoteClipboard = false;
}

void ClipboardManager::receiveText(ClipboardPeer*, const QJsonObject& header,
                                   const QByteArray& payload)
{
    QString eventId;
    if (!acceptEvent(header, &eventId) ||
            quint64(payload.size()) > SiacClipboardProtocol::MaxTextSize) return;
    auto* mime = new QMimeData();
    mime->setText(QString::fromUtf8(payload));
    applyRemoteMarker(mime, eventId, header.value(QStringLiteral("originId")).toString());
    setStatus(tr("Texto recibido de %1").arg(m_SelectedPeer->displayName()));
}

void ClipboardManager::receiveImage(ClipboardPeer*, const QJsonObject& header,
                                    const QByteArray& payload)
{
    QString eventId;
    if (!acceptEvent(header, &eventId) ||
            quint64(payload.size()) > SiacClipboardProtocol::MaxImageSize) return;
    QBuffer encodedImage;
    encodedImage.setData(payload);
    encodedImage.open(QIODevice::ReadOnly);
    QImageReader reader(&encodedImage, "PNG");
    const QSize dimensions = reader.size();
    if (!dimensions.isValid() ||
            quint64(dimensions.width()) * quint64(dimensions.height()) > MaxDecodedImagePixels) {
        setError(tr("La imagen remota tiene dimensiones no válidas o excesivas."));
        return;
    }
    const QImage image = reader.read();
    if (image.isNull()) {
        setError(tr("La imagen remota no es un PNG válido."));
        return;
    }
    auto* mime = new QMimeData();
    mime->setImageData(image);
    applyRemoteMarker(mime, eventId, header.value(QStringLiteral("originId")).toString());
    setStatus(tr("Imagen recibida de %1").arg(m_SelectedPeer->displayName()));
}

QString ClipboardManager::cacheRoot() const
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
            .absoluteFilePath(QStringLiteral("siac-clipboard"));
}

QString ClipboardManager::transferPath(const QString& eventId) const
{
    const QString safeId = QString::fromLatin1(
                QCryptographicHash::hash(eventId.toUtf8(), QCryptographicHash::Sha256).toHex());
    return QDir(cacheRoot()).absoluteFilePath(safeId);
}

void ClipboardManager::receiveManifest(ClipboardPeer* peer,
                                       const QJsonObject& header,
                                       const QByteArray& payload)
{
    QString eventId;
    if (!acceptEvent(header, &eventId)) return;
    const double declaredValue = header.value(QStringLiteral("totalSize")).toDouble(-1);
    if (!std::isfinite(declaredValue) || declaredValue < 0 ||
            declaredValue > double(SiacClipboardProtocol::MaxTransferSize) ||
            std::floor(declaredValue) != declaredValue) return;
    const quint64 declaredTotal = quint64(declaredValue);

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray() ||
            document.array().isEmpty() ||
            document.array().size() > SiacClipboardProtocol::MaxTransferEntries) {
        setError(tr("El manifiesto de archivos remoto no es válido."));
        return;
    }

    IncomingTransfer transfer;
    transfer.eventId = eventId;
    transfer.basePath = transferPath(eventId);
    transfer.peer = peer;
    quint64 calculatedTotal = 0;
    QVector<SiacClipboardProtocol::ManifestPath> manifestPaths;

    for (const QJsonValue& value : document.array()) {
        if (!value.isObject()) return;
        const QJsonObject object = value.toObject();
        const QString relative = object.value(QStringLiteral("path")).toString();
        const bool directory = object.value(QStringLiteral("directory")).toBool();
        const double sizeValue = object.value(QStringLiteral("size")).toDouble(-1);
        if (!std::isfinite(sizeValue) || sizeValue < 0 ||
                sizeValue > double(SiacClipboardProtocol::MaxTransferSize) ||
                std::floor(sizeValue) != sizeValue) return;
        const quint64 size = quint64(sizeValue);
        if (directory && size != 0) return;
        if (calculatedTotal > SiacClipboardProtocol::MaxTransferSize - size) return;
        calculatedTotal += size;
        transfer.entries.append({relative, {}, size, directory});
        manifestPaths.append({relative, directory});
    }

    QStringList normalizedPaths;
    if (!SiacClipboardProtocol::validateManifestPaths(
                manifestPaths, &normalizedPaths)) {
        setError(tr("Se bloqueó un manifiesto con rutas inseguras o en conflicto."));
        return;
    }
    for (int i = 0; i < transfer.entries.size(); ++i) {
        transfer.entries[i].relativePath = normalizedPaths.at(i);
        transfer.rootNames.insert(normalizedPaths.at(i).section(QLatin1Char('/'), 0, 0));
    }
    const QStorageInfo storage(cacheRoot());
    const quint64 cachedSize = directorySize(cacheRoot());
    if (calculatedTotal != declaredTotal || !storage.isValid() || !storage.isReady() ||
            storage.bytesAvailable() < qint64(declaredTotal) ||
            cachedSize > MaxCacheSize - declaredTotal) {
        setError(tr("No hay espacio suficiente o el tamaño del manifiesto no coincide."));
        return;
    }

    removeTransferDirectory(transfer.basePath);
    if (!QDir().mkpath(transfer.basePath)) {
        setError(tr("No se pudo crear el caché privado del portapapeles."));
        return;
    }
    for (const FileEntry& entry : std::as_const(transfer.entries)) {
        const QString destination = QDir(transfer.basePath).absoluteFilePath(entry.relativePath);
        const QString parent = entry.directory ? destination : QFileInfo(destination).absolutePath();
        if (!QDir().mkpath(parent)) {
            removeTransferDirectory(transfer.basePath);
            setError(tr("No se pudo preparar una carpeta de destino."));
            return;
        }
        if (!entry.directory) {
            QFile file(destination);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                removeTransferDirectory(transfer.basePath);
                setError(tr("No se pudo crear un archivo de destino."));
                return;
            }
            transfer.hashes.insert(entry.relativePath,
                                   QSharedPointer<QCryptographicHash>::create(
                                       QCryptographicHash::Sha256));
        }
        transfer.received.insert(entry.relativePath, 0);
    }
    transfer.totalSize = calculatedTotal;
    m_IncomingTransfers.insert(eventId, transfer);
    setTransferState(true, tr("Recibiendo %1 elemento(s)").arg(transfer.entries.size()),
                     0, calculatedTotal);
}

void ClipboardManager::receiveFileChunk(ClipboardPeer*, const QJsonObject& header,
                                        const QByteArray& payload)
{
    const QString eventId = header.value(QStringLiteral("eventId")).toString();
    auto it = m_IncomingTransfers.find(eventId);
    if (it == m_IncomingTransfers.end() ||
            quint64(payload.size()) > SiacClipboardProtocol::MaxFileChunkSize) return;
    QString relative;
    if (!SiacClipboardProtocol::isSafeRelativePath(
                header.value(QStringLiteral("path")).toString(), &relative) ||
            !it->received.contains(relative)) {
        abortIncoming(eventId, tr("Se recibió una ruta de bloque no autorizada."), true);
        return;
    }
    const double offsetValue = header.value(QStringLiteral("offset")).toDouble(-1);
    if (!std::isfinite(offsetValue) || offsetValue < 0 ||
            offsetValue > double(SiacClipboardProtocol::MaxTransferSize) ||
            std::floor(offsetValue) != offsetValue) {
        abortIncoming(eventId, tr("El desplazamiento de un bloque no es válido."), true);
        return;
    }
    const quint64 offset = quint64(offsetValue);
    if (offset != it->received.value(relative)) {
        abortIncoming(eventId, tr("Los bloques llegaron fuera de orden."), true);
        return;
    }

    quint64 expectedSize = 0;
    bool isDirectory = false;
    for (const FileEntry& entry : std::as_const(it->entries)) {
        if (entry.relativePath == relative) {
            expectedSize = entry.size;
            isDirectory = entry.directory;
            break;
        }
    }
    if (isDirectory || offset > expectedSize ||
            quint64(payload.size()) > expectedSize - offset) {
        abortIncoming(eventId, tr("Un bloque excede el tamaño declarado."), true);
        return;
    }

    QFile file(QDir(it->basePath).absoluteFilePath(relative));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append) ||
            quint64(file.size()) != offset || file.write(payload) != payload.size()) {
        abortIncoming(eventId, tr("No se pudo escribir un archivo recibido."), true);
        return;
    }
    it->received[relative] += quint64(payload.size());
    it->receivedSize += quint64(payload.size());
    it->hashes.value(relative)->addData(payload);
    setTransferState(true, m_TransferDescription, it->receivedSize, it->totalSize);
}

void ClipboardManager::receiveFileEnd(ClipboardPeer*, const QJsonObject& header)
{
    const QString eventId = header.value(QStringLiteral("eventId")).toString();
    auto it = m_IncomingTransfers.find(eventId);
    if (it == m_IncomingTransfers.end()) return;
    QString relative;
    if (!SiacClipboardProtocol::isSafeRelativePath(
                header.value(QStringLiteral("path")).toString(), &relative)) return;

    bool declaredFile = false;
    quint64 expectedSize = 0;
    for (const FileEntry& entry : std::as_const(it->entries)) {
        if (entry.relativePath == relative && !entry.directory) {
            declaredFile = true;
            expectedSize = entry.size;
            break;
        }
    }
    if (!declaredFile || it->received.value(relative) != expectedSize) {
        abortIncoming(eventId, tr("Se intentó verificar un archivo incompleto o no declarado."), true);
        return;
    }

    const auto hash = it->hashes.value(relative);
    if (hash.isNull()) {
        abortIncoming(eventId, tr("No se pudo verificar un archivo recibido."), true);
        return;
    }
    const QString actual = QString::fromLatin1(hash->result().toHex());
    if (actual != header.value(QStringLiteral("sha256")).toString()) {
        abortIncoming(eventId, tr("La verificación SHA-256 del archivo falló."), true);
        return;
    }
    it->hashes.remove(relative);
    it->verified.insert(relative);
}

void ClipboardManager::receiveTransferComplete(ClipboardPeer*, const QJsonObject& header)
{
    const QString eventId = header.value(QStringLiteral("eventId")).toString();
    auto it = m_IncomingTransfers.find(eventId);
    if (it == m_IncomingTransfers.end()) return;
    for (const FileEntry& entry : std::as_const(it->entries)) {
        if (!entry.directory &&
                (it->received.value(entry.relativePath) != entry.size ||
                 !it->verified.contains(entry.relativePath))) {
            abortIncoming(eventId, tr("La transferencia terminó con archivos incompletos."), true);
            return;
        }
    }

    QList<QUrl> urls;
    for (const QString& root : std::as_const(it->rootNames)) {
        urls.append(QUrl::fromLocalFile(QDir(it->basePath).absoluteFilePath(root)));
    }
    auto* mime = new QMimeData();
    mime->setUrls(urls);
    applyRemoteMarker(mime, eventId, header.value(QStringLiteral("originId")).toString());
    m_IncomingTransfers.erase(it);
    setTransferState(false, tr("Archivos listos para pegar con Ctrl+V"));
    setStatus(tr("Archivos remotos preparados en el portapapeles local"));
}

void ClipboardManager::receiveCancel(ClipboardPeer* peer, const QJsonObject& header)
{
    const QString eventId = header.value(QStringLiteral("eventId")).toString();
    QString message = tr("La transferencia fue cancelada por el otro equipo.");
    if (peer == m_OutgoingPeer && eventId == m_OutgoingEventId) {
        finishOutgoingFile(&message);
        return;
    }
    abortIncoming(eventId, message, false);
}

void ClipboardManager::abortIncoming(const QString& eventId,
                                     const QString& message,
                                     bool notifyPeer)
{
    auto it = m_IncomingTransfers.find(eventId);
    if (it == m_IncomingTransfers.end()) return;
    ClipboardPeer* peer = it->peer;
    const QString path = it->basePath;
    m_IncomingTransfers.erase(it);
    removeTransferDirectory(path);
    if (notifyPeer && peer && peer->isEncrypted()) {
        QJsonObject error = baseHeader(QStringLiteral("error"));
        error.insert(QStringLiteral("eventId"), eventId);
        error.insert(QStringLiteral("message"), message);
        peer->send(error);
    }
    setTransferState(false, message);
    setError(message);
}

void ClipboardManager::cancelTransfer()
{
    if (m_OutgoingPeer && !m_OutgoingEventId.isEmpty()) {
        QString message = tr("Transferencia cancelada.");
        finishOutgoingFile(&message);
    }

    const QStringList incoming = m_IncomingTransfers.keys();
    for (const QString& eventId : incoming) {
        ClipboardPeer* peer = m_IncomingTransfers.value(eventId).peer;
        if (peer && peer->isEncrypted()) {
            QJsonObject cancel = baseHeader(QStringLiteral("cancel"));
            cancel.insert(QStringLiteral("eventId"), eventId);
            peer->send(cancel);
        }
        abortIncoming(eventId, tr("Transferencia cancelada."), false);
    }
}

bool ClipboardManager::removeTransferDirectory(const QString& path)
{
    const QString root = QDir(cacheRoot()).absolutePath();
    const QString target = QDir(path).absolutePath();
    if (target == root ||
            !target.startsWith(root + QDir::separator(), Qt::CaseInsensitive)) {
        return false;
    }
    QDir directory(target);
    return !directory.exists() || directory.removeRecursively();
}

void ClipboardManager::cleanupCache()
{
    QDir root(cacheRoot());
    if (!root.exists()) root.mkpath(QStringLiteral("."));
    QFileInfoList directories = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                    QDir::Time | QDir::Reversed);
    quint64 total = 0;
    struct CachedDirectory { QFileInfo info; quint64 size; };
    QVector<CachedDirectory> retained;
    for (const QFileInfo& info : std::as_const(directories)) {
        const QDateTime timestamp = info.lastModified();
        if (timestamp.isValid() && timestamp.msecsTo(QDateTime::currentDateTime()) > MaxCacheAgeMs) {
            removeTransferDirectory(info.absoluteFilePath());
            continue;
        }
        const quint64 size = directorySize(info.absoluteFilePath());
        total += size;
        retained.append({info, size});
    }
    for (const CachedDirectory& cached : std::as_const(retained)) {
        if (total <= MaxCacheSize) break;
        if (removeTransferDirectory(cached.info.absoluteFilePath())) total -= cached.size;
    }
}

void ClipboardManager::setStatus(const QString& status)
{
    if (m_Status == status) return;
    m_Status = status;
    emit statusChanged();
}

void ClipboardManager::setError(const QString& error)
{
    if (m_LastError == error) return;
    m_LastError = error;
    emit lastErrorChanged();
}

void ClipboardManager::setTransferState(bool active, const QString& description,
                                        quint64 completed, quint64 total)
{
    m_TransferActive = active;
    m_TransferDescription = description;
    m_TransferCompleted = completed;
    m_TransferTotal = total;
    emit transferChanged();
}
