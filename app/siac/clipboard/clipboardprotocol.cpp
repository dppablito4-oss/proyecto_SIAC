#include "clipboardprotocol.h"

#include <QDataStream>
#include <QDir>
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
