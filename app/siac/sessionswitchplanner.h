#pragma once

#include <QString>
#include <QStringList>
#include <QSet>
#include <QVector>

class SessionSwitchPlanner
{
public:
    enum class Action
    {
        None,
        ConnectRemote,
        ReturnLocal,
    };

    struct Result
    {
        Action action = Action::None;
        QString computerUuid;
    };

    enum class ShortcutAction
    {
        Forward,
        NextComputer,
        ReturnLocal,
    };

    struct ApplicationCandidate
    {
        int id = 0;
        QString name;
        bool directLaunch = false;
        bool valid = true;
    };

    struct HotkeyConfiguration
    {
        int nextFunctionKey = 9;
        int localFunctionKey = 10;
        bool corrected = false;
    };

    static Result next(const QStringList& orderedComputerUuids,
                       const QString& localComputerUuid,
                       const QString& activeComputerUuid,
                       const QSet<QString>& availableComputerUuids);

    static ShortcutAction matchShortcut(int functionKey, bool control, bool alt,
                                        bool shift, bool meta, int nextFunctionKey,
                                        int localFunctionKey);

    static bool validHotkeys(int nextFunctionKey, int localFunctionKey);
    static HotkeyConfiguration normalizeHotkeys(int nextFunctionKey,
                                                 int localFunctionKey);
    static int selectApplication(const QVector<ApplicationCandidate>& applications,
                                 int currentGameId, const QString& preferredName);
};
