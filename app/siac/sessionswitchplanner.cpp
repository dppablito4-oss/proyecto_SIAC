#include "sessionswitchplanner.h"

SessionSwitchPlanner::Result SessionSwitchPlanner::next(
        const QStringList& orderedComputerUuids,
        const QString& localComputerUuid,
        const QString& activeComputerUuid,
        const QSet<QString>& availableComputerUuids)
{
    if (orderedComputerUuids.isEmpty()) {
        return {};
    }

    int activeIndex = orderedComputerUuids.indexOf(activeComputerUuid);
    if (activeIndex < 0 && !localComputerUuid.isEmpty()) {
        activeIndex = orderedComputerUuids.indexOf(localComputerUuid);
    }

    // A local computer that isn't present in the host list is a synthetic
    // element immediately after the final configured remote host.
    if (activeIndex < 0 && activeComputerUuid == localComputerUuid &&
            !localComputerUuid.isEmpty()) {
        activeIndex = -1;
    }

    const bool syntheticLocal = localComputerUuid.isEmpty();
    const int candidatesToCheck = syntheticLocal && activeIndex >= 0
            ? orderedComputerUuids.size() - activeIndex - 1
            : orderedComputerUuids.size();

    for (int offset = 1; offset <= candidatesToCheck; offset++) {
        const int index = (activeIndex + offset) % orderedComputerUuids.size();
        const QString& candidate = orderedComputerUuids.at(index);

        if (!localComputerUuid.isEmpty() && candidate == localComputerUuid) {
            if (activeComputerUuid != localComputerUuid) {
                return {Action::ReturnLocal, {}};
            }
            continue;
        }

        if (availableComputerUuids.contains(candidate)) {
            return {Action::ConnectRemote, candidate};
        }
    }

    // When the local machine isn't represented by a discovered Sunshine host,
    // completing one pass over the remote list returns to the physical desktop.
    if (!activeComputerUuid.isEmpty() && syntheticLocal) {
        return {Action::ReturnLocal, {}};
    }

    return {};
}

SessionSwitchPlanner::ShortcutAction SessionSwitchPlanner::matchShortcut(
        int functionKey, bool control, bool alt, bool shift, bool meta,
        int nextFunctionKey, int localFunctionKey)
{
    if (!control || !alt || shift || meta || functionKey <= 0) {
        return ShortcutAction::Forward;
    }
    if (functionKey == nextFunctionKey) {
        return ShortcutAction::NextComputer;
    }
    if (functionKey == localFunctionKey) {
        return ShortcutAction::ReturnLocal;
    }
    return ShortcutAction::Forward;
}

bool SessionSwitchPlanner::validHotkeys(int nextFunctionKey, int localFunctionKey)
{
    return nextFunctionKey >= 1 && nextFunctionKey <= 24 &&
            localFunctionKey >= 1 && localFunctionKey <= 24 &&
            nextFunctionKey != localFunctionKey;
}

int SessionSwitchPlanner::selectApplication(
        const QVector<ApplicationCandidate>& applications,
        int currentGameId, const QString& preferredName)
{
    if (currentGameId != 0) {
        for (int i = 0; i < applications.size(); ++i) {
            if (applications.at(i).valid && applications.at(i).id == currentGameId) {
                return i;
            }
        }
    }

    for (int i = 0; i < applications.size(); ++i) {
        if (applications.at(i).valid &&
                applications.at(i).name.compare(preferredName, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }

    for (int i = 0; i < applications.size(); ++i) {
        if (applications.at(i).valid && applications.at(i).directLaunch) {
            return i;
        }
    }
    return -1;
}
