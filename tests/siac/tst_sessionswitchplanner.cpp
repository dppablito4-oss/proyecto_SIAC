#include <QtTest>

#include "siac/sessionswitchplanner.h"

class SessionSwitchPlannerTest : public QObject
{
    Q_OBJECT

private slots:
    void neverConnectsToLocalComputer();
    void followsConfiguredOrder();
    void skipsUnavailableComputers();
    void returnsLocalAfterLastRemote();
    void supportsLocalComputerWithoutSunshineRecord();
    void worksWithAnyComputerAsLocal();
    void consumesSiacShortcutBeforeForwarding();
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

void SessionSwitchPlannerTest::supportsLocalComputerWithoutSunshineRecord()
{
    const QStringList order{"pc2", "pc3", "pc4"};
    const QSet<QString> available{"pc2", "pc3", "pc4"};

    auto result = SessionSwitchPlanner::next(order, {}, {}, available);
    QCOMPARE(result.computerUuid, QString("pc2"));

    result = SessionSwitchPlanner::next(order, {}, "pc4", available);
    QCOMPARE(static_cast<int>(result.action), static_cast<int>(SessionSwitchPlanner::Action::ReturnLocal));
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
