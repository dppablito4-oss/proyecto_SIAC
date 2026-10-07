#pragma once

#include <QString>
#include <QStringList>
#include <QSet>

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

    static Result next(const QStringList& orderedComputerUuids,
                       const QString& localComputerUuid,
                       const QString& activeComputerUuid,
                       const QSet<QString>& availableComputerUuids);

    static ShortcutAction matchShortcut(int functionKey, bool control, bool alt,
                                        bool shift, bool meta, int nextFunctionKey,
                                        int localFunctionKey);
};
