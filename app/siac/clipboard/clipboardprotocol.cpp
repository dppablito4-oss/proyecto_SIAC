#include "clipboardprotocol.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QHash>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QtEndian>

#include <utility>

namespace SiacClipboardProtocol {

AuthorizationDecision authorizePeer(const QString& storedInstallationId,
                                    const QString& storedComputerUuid,
                                    const QString& storedFingerprint,
                                    const QString& actualInstallationId,
                                    const QString& actualComputerUuid,
                                    const QString& actualFingerprint)
{
    if (storedInstallationId.isEmpty() || storedInstallationId != actualInstallationId) {
        return AuthorizationDecision::UnknownPeer;
    }
    if (storedFingerprint.isEmpty() || storedFingerprint != actualFingerprint) {
        return AuthorizationDecision::CertificateChanged;
    }
    if (!storedComputerUuid.isEmpty() && storedComputerUuid != actualComputerUuid) {
        return AuthorizationDecision::ComputerChanged;
    }
    return AuthorizationDecision::Authorized;
}

bool shouldAcceptContent(bool authorized, bool selectedPeer,
                         bool existingTransfer)
{
    return authorized && (selectedPeer || existingTransfer);
}

QString pairingCode(const QString& firstFingerprint,
                    const QString& secondFingerprint)
{
    if (firstFingerprint.isEmpty() || secondFingerprint.isEmpty()) {
        return {};
    }

    QString first = firstFingerprint.toLower();
    QString second = secondFingerprint.toLower();
    if (second < first) qSwap(first, second);
    const QByteArray digest = QCryptographicHash::hash(
                first.toLatin1() + '\0' + second.toLatin1(),
                QCryptographicHash::Sha256).toHex().left(12).toUpper();
    QStringList groups;
    for (int i = 0; i < digest.size(); i += 4) {
        groups.append(QString::fromLatin1(digest.mid(i, 4)));
    }
    return groups.join(QLatin1Char('-'));
}

bool isLanAddress(const QHostAddress& address)
{
    if (address.isNull() || address.isLoopback()) return false;

    bool isIpv4 = false;
    const quint32 ipv4 = address.toIPv4Address(&isIpv4);
    if (isIpv4) {
        return (ipv4 & 0xff000000u) == 0x0a000000u ||
                (ipv4 & 0xfff00000u) == 0xac100000u ||
                (ipv4 & 0xffff0000u) == 0xc0a80000u ||
                (ipv4 & 0xffff0000u) == 0xa9fe0000u;
    }

    const Q_IPV6ADDR ipv6 = address.toIPv6Address();
    return (ipv6.c[0] & 0xfeu) == 0xfcu ||
            (ipv6.c[0] == 0xfeu && (ipv6.c[1] & 0xc0u) == 0x80u);
}

QByteArray encodeFrame(QJsonObject header, const QByteArray& payload, QString* error)
{
    if (payload.size() < 0 || quint64(payload.size()) > MaxPayloadSize) {
        if (error) *error = QStringLiteral("payload-too-large");
        return {};
    }

    header.insert(QStringLiteral("payloadSize"), double(payload.size()));
    const QByteArray headerBytes = QJsonDocument(header).toJson(QJsonDocument::Compact);
    if (headerBytes.isEmpty() || quint32(headerBytes.size()) > MaxHeaderSize) {
        if (error) *error = QStringLiteral("header-too-large");
        return {};
    }

    QByteArray frame;
    frame.resize(4);
    qToBigEndian<quint32>(quint32(headerBytes.size()),
                          reinterpret_cast<uchar*>(frame.data()));
    frame.append(headerBytes);
    frame.append(payload);
    return frame;
}

static bool isReservedWindowsName(const QString& component)
{
    const QString base = component.section(QLatin1Char('.'), 0, 0).toUpper();
    if (base == QStringLiteral("CON") || base == QStringLiteral("PRN") ||
            base == QStringLiteral("AUX") || base == QStringLiteral("NUL")) {
        return true;
    }
    static const QRegularExpression devicePattern(
                QStringLiteral("^(COM|LPT)[1-9]$"),
                QRegularExpression::CaseInsensitiveOption);
    return devicePattern.match(base).hasMatch();
}

bool isSafeRelativePath(const QString& path, QString* normalized)
{
    if (path.isEmpty() || path.size() > 4096 || path.contains(QChar::Null)) {
        return false;
    }

    QString candidate = path;
    candidate.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (candidate.startsWith(QLatin1Char('/')) ||
            QRegularExpression(QStringLiteral("^[A-Za-z]:")).match(candidate).hasMatch() ||
            candidate.contains(QStringLiteral("//"))) {
        return false;
    }

    const QStringList components = candidate.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    static const QRegularExpression invalidCharacters(QStringLiteral("[<>:\"|?*]"));
    for (const QString& component : components) {
        if (component.isEmpty() || component == QStringLiteral(".") ||
                component == QStringLiteral("..") ||
                component.endsWith(QLatin1Char('.')) ||
                component.endsWith(QLatin1Char(' ')) ||
                invalidCharacters.match(component).hasMatch() ||
                isReservedWindowsName(component)) {
            return false;
        }
    }

    const QString clean = QDir::cleanPath(candidate);
    if (clean != candidate || clean == QStringLiteral("..") ||
            clean.startsWith(QStringLiteral("../"))) {
        return false;
    }
    if (normalized) *normalized = clean;
    return true;
}

bool validateManifestPaths(const QVector<ManifestPath>& paths,
                           QStringList* normalizedPaths,
                           QString* error)
{
    if (paths.isEmpty() || paths.size() > MaxTransferEntries) {
        if (error) *error = QStringLiteral("invalid-entry-count");
        return false;
    }

    QStringList normalized;
    QHash<QString, bool> kinds;
    QHash<QString, QString> componentSpellings;
    normalized.reserve(paths.size());
    for (const ManifestPath& entry : paths) {
        QString clean;
        if (!isSafeRelativePath(entry.path, &clean)) {
            if (error) *error = QStringLiteral("unsafe-path");
            return false;
        }
        const QString key = clean.toCaseFolded();
        if (kinds.contains(key)) {
            if (error) *error = QStringLiteral("duplicate-path");
            return false;
        }
        kinds.insert(key, entry.directory);
        normalized.append(clean);

        const QStringList components = clean.split(QLatin1Char('/'));
        QString originalPrefix;
        QString foldedPrefix;
        for (const QString& component : components) {
            if (!originalPrefix.isEmpty()) {
                originalPrefix += QLatin1Char('/');
                foldedPrefix += QLatin1Char('/');
            }
            originalPrefix += component;
            foldedPrefix += component.toCaseFolded();
            if (componentSpellings.contains(foldedPrefix) &&
                    componentSpellings.value(foldedPrefix) != originalPrefix) {
                if (error) *error = QStringLiteral("case-collision");
                return false;
            }
            componentSpellings.insert(foldedPrefix, originalPrefix);
        }
    }

    for (auto it = kinds.cbegin(); it != kinds.cend(); ++it) {
        QString ancestor = it.key();
        int separator = ancestor.lastIndexOf(QLatin1Char('/'));
        while (separator >= 0) {
            ancestor.truncate(separator);
            if (kinds.contains(ancestor) && !kinds.value(ancestor)) {
                if (error) *error = QStringLiteral("file-used-as-directory");
                return false;
            }
            separator = ancestor.lastIndexOf(QLatin1Char('/'));
        }
    }

    if (normalizedPaths) *normalizedPaths = normalized;
    return true;
}

QString safeRootName(const QString& requested, const QSet<QString>& usedNames)
{
    QString base = requested.trimmed();
    if (!isSafeRelativePath(base) || base.contains(QLatin1Char('/')) ||
            base.contains(QLatin1Char('\\'))) {
        base = QStringLiteral("Elemento");
    }

    QString candidate = base;
    int suffix = 2;
    while (usedNames.contains(candidate.toCaseFolded())) {
        candidate = QStringLiteral("%1 (%2)").arg(base).arg(suffix++);
    }
    return candidate;
}

bool FrameParser::append(const QByteArray& data, QVector<Frame>* frames, QString* error)
{
    if (!frames) {
        if (error) *error = QStringLiteral("missing-output");
        return false;
    }
    m_Buffer.append(data);

    while (m_Buffer.size() >= 4) {
        const quint32 headerSize = qFromBigEndian<quint32>(
                    reinterpret_cast<const uchar*>(m_Buffer.constData()));
        if (headerSize == 0 || headerSize > MaxHeaderSize) {
            if (error) *error = QStringLiteral("invalid-header-size");
            m_Buffer.clear();
            return false;
        }
        if (m_Buffer.size() < 4 + qsizetype(headerSize)) {
            return true;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(
                    m_Buffer.mid(4, headerSize), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            if (error) *error = QStringLiteral("invalid-json-header");
            m_Buffer.clear();
            return false;
        }

        const QJsonObject header = document.object();
        const QJsonValue payloadValue = header.value(QStringLiteral("payloadSize"));
        if (!payloadValue.isDouble()) {
            if (error) *error = QStringLiteral("missing-payload-size");
            m_Buffer.clear();
            return false;
        }
        const double payloadDouble = payloadValue.toDouble(-1);
        const quint64 payloadSize = payloadDouble < 0 ? MaxPayloadSize + 1
                                                       : quint64(payloadDouble);
        if (payloadDouble != double(payloadSize) || payloadSize > MaxPayloadSize) {
            if (error) *error = QStringLiteral("invalid-payload-size");
            m_Buffer.clear();
            return false;
        }

        const quint64 frameSize = 4ull + headerSize + payloadSize;
        if (quint64(m_Buffer.size()) < frameSize) {
            return true;
        }

        Frame frame;
        frame.header = header;
        frame.payload = m_Buffer.mid(4 + headerSize, qsizetype(payloadSize));
        frames->append(std::move(frame));
        m_Buffer.remove(0, qsizetype(frameSize));
    }
    return true;
}

bool EventTracker::remember(const QString& eventId)
{
    if (eventId.isEmpty() || m_Seen.contains(eventId)) {
        return false;
    }
    m_Seen.insert(eventId);
    m_Order.enqueue(eventId);
    while (m_Order.size() > m_Capacity) {
        m_Seen.remove(m_Order.dequeue());
    }
    return true;
}

} // namespace SiacClipboardProtocol
