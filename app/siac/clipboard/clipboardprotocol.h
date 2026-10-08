#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QJsonObject>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace SiacClipboardProtocol {

constexpr int Version = 1;
constexpr quint32 MaxHeaderSize = 64 * 1024;
constexpr quint64 MaxPayloadSize = 64ull * 1024 * 1024;
constexpr quint64 FileChunkSize = 256ull * 1024;
constexpr quint64 MaxFileChunkSize = 512ull * 1024;
constexpr quint64 MaxTextSize = 16ull * 1024 * 1024;
constexpr quint64 MaxImageSize = 64ull * 1024 * 1024;
constexpr quint64 MaxTransferSize = 20ull * 1024 * 1024 * 1024;
constexpr int MaxTransferEntries = 10000;
constexpr quint16 DefaultPort = 48219;

struct Frame
{
    QJsonObject header;
    QByteArray payload;
};

enum class AuthorizationDecision
{
    Authorized,
    UnknownPeer,
    CertificateChanged,
    ComputerChanged,
};

AuthorizationDecision authorizePeer(const QString& storedInstallationId,
                                    const QString& storedComputerUuid,
                                    const QString& storedFingerprint,
                                    const QString& actualInstallationId,
                                    const QString& actualComputerUuid,
                                    const QString& actualFingerprint);
bool shouldAcceptContent(bool authorized, bool selectedPeer,
                         bool existingTransfer);
QString pairingCode(const QString& firstFingerprint,
                    const QString& secondFingerprint);
bool isLanAddress(const QHostAddress& address);

struct ManifestPath
{
    QString path;
    bool directory = false;
};

bool validateManifestPaths(const QVector<ManifestPath>& paths,
                           QStringList* normalizedPaths = nullptr,
                           QString* error = nullptr);

QByteArray encodeFrame(QJsonObject header, const QByteArray& payload,
                       QString* error = nullptr);
bool isSafeRelativePath(const QString& path, QString* normalized = nullptr);
QString safeRootName(const QString& requested, const QSet<QString>& usedNames);

class FrameParser
{
public:
    bool append(const QByteArray& data, QVector<Frame>* frames, QString* error);
    void clear() { m_Buffer.clear(); }
    qsizetype bufferedBytes() const { return m_Buffer.size(); }

private:
    QByteArray m_Buffer;
};

class EventTracker
{
public:
    explicit EventTracker(int capacity = 256) : m_Capacity(qMax(1, capacity)) {}
    bool remember(const QString& eventId);
    bool contains(const QString& eventId) const { return m_Seen.contains(eventId); }

private:
    int m_Capacity;
    QQueue<QString> m_Order;
    QSet<QString> m_Seen;
};

} // namespace SiacClipboardProtocol
