#include <QtTest>

#include "siac/sessionswitchplanner.h"
#include "siac/sessiontransitionstate.h"
#include "siac/clipboard/clipboardprotocol.h"

class SessionSwitchPlannerTest : public QObject
{
    Q_OBJECT

private slots:
    void neverConnectsToLocalComputer();
    void followsConfiguredOrder();
    void skipsUnavailableComputers();
    void returnsLocalAfterLastRemote();
    void plannerOnlySupportsSyntheticLocalComputer();
    void worksWithAnyComputerAsLocal();
    void consumesSiacShortcutBeforeForwarding();
    void repeatedNextDuringStartupCreatesOneRequest();
    void returnLocalCancelsDeferredLaunch();
    void returnLocalCancelsCreatedSessionBeforeActive();
    void launchFailureAllowsRetry();
    void rejectsInvalidOrIdenticalHotkeys();
    void selectsRunningPreferredOrDirectApplication();
    void skipsHostWithoutUsableApplication();
    void noUsableHostReturnsRecoverableLocalState();
    void clipboardFramesHandleFragmentation();
    void clipboardRejectsUnsafePaths();
    void clipboardEventsAreDeduplicated();
    void clipboardAuthorizationRejectsUnknownOrChangedPeers();
};

void SessionSwitchPlannerTest::neverConnectsToLocalComputer()
{
    const QStringList order{"pc1", "pc2", "pc3", "pc4"};
    const QSet<QString> available{"pc1"};
    const auto result = SessionSwitchPlanner::next(order, "pc1", "pc1", available);
    QCOMPARE(static_cast<int>(result.action), static_cast<int>(SessionSwitchPlanner::Action::None));
    QVERIFY(result.computerUuid.isEmpty());
}

void SessionSwitchPlannerTest::followsConfiguredOrder()
{
    const QStringList order{"pc1", "pc3", "pc2", "pc4"};
    const QSet<QString> available{"pc2", "pc3", "pc4"};

    auto result = SessionSwitchPlanner::next(order, "pc1", "pc1", available);
    QCOMPARE(static_cast<int>(result.action), static_cast<int>(SessionSwitchPlanner::Action::ConnectRemote));
    QCOMPARE(result.computerUuid, QString("pc3"));

    result = SessionSwitchPlanner::next(order, "pc1", "pc3", available);
    QCOMPARE(result.computerUuid, QString("pc2"));
}

void SessionSwitchPlannerTest::skipsUnavailableComputers()
{
    const QStringList order{"pc1", "pc2", "pc3", "pc4"};
    const QSet<QString> available{"pc3"};
    const auto result = SessionSwitchPlanner::next(order, "pc1", "pc1", available);
    QCOMPARE(static_cast<int>(result.action), static_cast<int>(SessionSwitchPlanner::Action::ConnectRemote));
    QCOMPARE(result.computerUuid, QString("pc3"));
}

void SessionSwitchPlannerTest::returnsLocalAfterLastRemote()
{
    const QStringList order{"pc1", "pc2", "pc3", "pc4"};
    const QSet<QString> available{"pc2", "pc3", "pc4"};
    const auto result = SessionSwitchPlanner::next(order, "pc1", "pc4", available);
    QCOMPARE(static_cast<int>(result.action), static_cast<int>(SessionSwitchPlanner::Action::ReturnLocal));
}

void SessionSwitchPlannerTest::plannerOnlySupportsSyntheticLocalComputer()
{
    // This capability is intentionally limited to the pure planner. The MVP
    // controller requires an explicitly selected Sunshine record for local.
    const QStringList order{"pc2", "pc3", "pc4"};
    const QSet<QString> available{"pc2", "pc3", "pc4"};

    auto result = SessionSwitchPlanner::next(order, {}, {}, available);
    QCOMPARE(result.computerUuid, QString("pc2"));

    result = SessionSwitchPlanner::next(order, {}, "pc4", available);
    QCOMPARE(static_cast<int>(result.action), static_cast<int>(SessionSwitchPlanner::Action::ReturnLocal));
}

void SessionSwitchPlannerTest::repeatedNextDuringStartupCreatesOneRequest()
{
    SessionTransitionState state;
    const quint64 first = state.beginRemoteSwitch(false);
    QVERIFY(first != 0);
    QCOMPARE(state.beginRemoteSwitch(false), quint64(0));
    QVERIFY(state.markSessionCreated(first));
    QCOMPARE(state.beginRemoteSwitch(false), quint64(0));
    QVERIFY(state.markConnected(first));
    QVERIFY(state.canRequestNext());
}

void SessionSwitchPlannerTest::returnLocalCancelsDeferredLaunch()
{
    SessionTransitionState state;
    const quint64 token = state.beginRemoteSwitch(true);
    QVERIFY(state.markLaunchDeferred(token));
    state.cancelToLocal();
    QVERIFY(!state.beginDeferredLaunch(token));
    QVERIFY(state.transitionInProgress());
    state.finishLocal();
    QVERIFY(state.canRequestNext());
}

void SessionSwitchPlannerTest::returnLocalCancelsCreatedSessionBeforeActive()
{
    SessionTransitionState state;
    const quint64 token = state.beginRemoteSwitch(false);
    QVERIFY(state.markSessionCreated(token));
    state.cancelToLocal();
    QVERIFY(!state.markConnected(token));
    state.finishLocal();
    QCOMPARE(static_cast<int>(state.phase()),
             static_cast<int>(SessionTransitionState::Phase::Local));
}

void SessionSwitchPlannerTest::launchFailureAllowsRetry()
{
    SessionTransitionState state;
    const quint64 failed = state.beginRemoteSwitch(false);
    QVERIFY(state.markSessionCreated(failed));
    QVERIFY(state.fail(failed));
    const quint64 retry = state.beginRemoteSwitch(false);
    QVERIFY(retry != 0);
    QVERIFY(retry != failed);
}

void SessionSwitchPlannerTest::rejectsInvalidOrIdenticalHotkeys()
{
    QVERIFY(SessionSwitchPlanner::validHotkeys(9, 10));
    QVERIFY(!SessionSwitchPlanner::validHotkeys(9, 9));
    QVERIFY(!SessionSwitchPlanner::validHotkeys(0, 10));
    QVERIFY(!SessionSwitchPlanner::validHotkeys(9, 25));
    const auto persisted = SessionSwitchPlanner::normalizeHotkeys(12, 12);
    QVERIFY(persisted.corrected);
    QCOMPARE(persisted.nextFunctionKey, 9);
    QCOMPARE(persisted.localFunctionKey, 10);

    const auto valid = SessionSwitchPlanner::normalizeHotkeys(8, 11);
    QVERIFY(!valid.corrected);
    QCOMPARE(valid.nextFunctionKey, 8);
    QCOMPARE(valid.localFunctionKey, 11);
}

void SessionSwitchPlannerTest::selectsRunningPreferredOrDirectApplication()
{
    using Candidate = SessionSwitchPlanner::ApplicationCandidate;
    const QVector<Candidate> applications{
        {1, "Other", false}, {2, "Desktop", false}, {3, "Direct", true}
    };
    QCOMPARE(SessionSwitchPlanner::selectApplication(applications, 1, "Desktop"), 0);
    QCOMPARE(SessionSwitchPlanner::selectApplication(applications, 0, "desktop"), 1);
    QCOMPARE(SessionSwitchPlanner::selectApplication(applications, 0, "Missing"), 2);
    const QVector<Candidate> invalid{{0, "Desktop", true, false}};
    QCOMPARE(SessionSwitchPlanner::selectApplication(invalid, 0, "Desktop"), -1);
}

void SessionSwitchPlannerTest::skipsHostWithoutUsableApplication()
{
    using Candidate = SessionSwitchPlanner::ApplicationCandidate;
    const QVector<Candidate> unusable{{1, "Other", false}};
    const QVector<Candidate> usable{{2, "Desktop", false}};
    QSet<QString> available;
    if (SessionSwitchPlanner::selectApplication(unusable, 0, "Desktop") >= 0)
        available.insert("pc2");
    if (SessionSwitchPlanner::selectApplication(usable, 0, "Desktop") >= 0)
        available.insert("pc3");

    const auto result = SessionSwitchPlanner::next(
                {"local", "pc2", "pc3"}, "local", "local", available);
    QCOMPARE(result.computerUuid, QString("pc3"));
}

void SessionSwitchPlannerTest::noUsableHostReturnsRecoverableLocalState()
{
    SessionTransitionState state;
    const quint64 token = state.beginRemoteSwitch(false);
    QVERIFY(token != 0);
    const auto result = SessionSwitchPlanner::next(
                {"local", "pc2"}, "local", "local", {});
    QCOMPARE(static_cast<int>(result.action),
             static_cast<int>(SessionSwitchPlanner::Action::None));
    QVERIFY(state.fail(token));
    QVERIFY(state.canRequestNext());
}

void SessionSwitchPlannerTest::clipboardFramesHandleFragmentation()
{
    using namespace SiacClipboardProtocol;
    QJsonObject header{{"type", "text"}, {"version", Version}, {"eventId", "event-1"}};
    const QByteArray encoded = encodeFrame(header, QByteArray("hola\nPeru"));
    QVERIFY(!encoded.isEmpty());

    FrameParser parser;
    QVector<Frame> frames;
    QString error;
    QVERIFY(parser.append(encoded.left(3), &frames, &error));
    QVERIFY(frames.isEmpty());
    QVERIFY(parser.append(encoded.mid(3, 7), &frames, &error));
    QVERIFY(frames.isEmpty());
    QVERIFY(parser.append(encoded.mid(10) + encoded, &frames, &error));
    QCOMPARE(frames.size(), 2);
    QCOMPARE(frames.at(0).header.value("type").toString(), QString("text"));
    QCOMPARE(frames.at(0).payload, QByteArray("hola\nPeru"));
    QCOMPARE(frames.at(1).payload, QByteArray("hola\nPeru"));
}

void SessionSwitchPlannerTest::clipboardRejectsUnsafePaths()
{
    using namespace SiacClipboardProtocol;
    QString normalized;
    QVERIFY(isSafeRelativePath("Carpeta/documento.txt", &normalized));
    QCOMPARE(normalized, QString("Carpeta/documento.txt"));
    QVERIFY(isSafeRelativePath(QString::fromUtf8("Diseño/Perú.txt")));
    QVERIFY(!isSafeRelativePath("../secreto.txt"));
    QVERIFY(!isSafeRelativePath("Carpeta/../../secreto.txt"));
    QVERIFY(!isSafeRelativePath("C:/Windows/system.ini"));
    QVERIFY(!isSafeRelativePath("Carpeta/archivo.txt:flujo"));
    QVERIFY(!isSafeRelativePath("CON"));
    QVERIFY(!isSafeRelativePath("Carpeta/nombre. "));

    QSet<QString> used{QStringLiteral("informe.pdf")};
    QCOMPARE(safeRootName("informe.pdf", used), QString("informe.pdf (2)"));
}

void SessionSwitchPlannerTest::clipboardEventsAreDeduplicated()
{
    using namespace SiacClipboardProtocol;
    EventTracker tracker(2);
    QVERIFY(tracker.remember("uno"));
    QVERIFY(!tracker.remember("uno"));
    QVERIFY(tracker.remember("dos"));
    QVERIFY(tracker.remember("tres"));
    QVERIFY(!tracker.contains("uno"));
    QVERIFY(tracker.remember("uno"));
}

void SessionSwitchPlannerTest::clipboardAuthorizationRejectsUnknownOrChangedPeers()
{
    using namespace SiacClipboardProtocol;
    QCOMPARE(static_cast<int>(authorizePeer("install-a", "pc-a", "cert-a",
                                            "install-a", "pc-a", "cert-a")),
             static_cast<int>(AuthorizationDecision::Authorized));
    QCOMPARE(static_cast<int>(authorizePeer("install-a", "pc-a", "cert-a",
                                            "install-b", "pc-a", "cert-a")),
             static_cast<int>(AuthorizationDecision::UnknownPeer));
    QCOMPARE(static_cast<int>(authorizePeer("install-a", "pc-a", "cert-a",
                                            "install-a", "pc-a", "cert-b")),
             static_cast<int>(AuthorizationDecision::CertificateChanged));
    QCOMPARE(static_cast<int>(authorizePeer("install-a", "pc-a", "cert-a",
                                            "install-a", "pc-b", "cert-a")),
             static_cast<int>(AuthorizationDecision::ComputerChanged));

    QVERIFY(!shouldAcceptContent(false, true, false));
    QVERIFY(!shouldAcceptContent(true, false, false));
    QVERIFY(shouldAcceptContent(true, true, false));
    QVERIFY(shouldAcceptContent(true, false, true));
}

void SessionSwitchPlannerTest::worksWithAnyComputerAsLocal()
{
    const QStringList order{"pc1", "pc2", "pc3", "pc4"};
    const QSet<QString> available{"pc1", "pc2", "pc4"};

    auto result = SessionSwitchPlanner::next(order, "pc3", "pc3", available);
    QCOMPARE(result.computerUuid, QString("pc4"));

    result = SessionSwitchPlanner::next(order, "pc3", "pc2", available);
    QCOMPARE(static_cast<int>(result.action), static_cast<int>(SessionSwitchPlanner::Action::ReturnLocal));
}

void SessionSwitchPlannerTest::consumesSiacShortcutBeforeForwarding()
{
    using ShortcutAction = SessionSwitchPlanner::ShortcutAction;

    QCOMPARE(static_cast<int>(SessionSwitchPlanner::matchShortcut(9, true, true, false, false, 9, 10)),
             static_cast<int>(ShortcutAction::NextComputer));
    QCOMPARE(static_cast<int>(SessionSwitchPlanner::matchShortcut(10, true, true, false, false, 9, 10)),
             static_cast<int>(ShortcutAction::ReturnLocal));
    QCOMPARE(static_cast<int>(SessionSwitchPlanner::matchShortcut(9, true, true, true, false, 9, 10)),
             static_cast<int>(ShortcutAction::Forward));
    QCOMPARE(static_cast<int>(SessionSwitchPlanner::matchShortcut(9, true, false, false, false, 9, 10)),
             static_cast<int>(ShortcutAction::Forward));
}

QTEST_APPLESS_MAIN(SessionSwitchPlannerTest)

#include "tst_sessionswitchplanner.moc"
