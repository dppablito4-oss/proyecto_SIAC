#pragma once

#include <QtGlobal>

class SessionTransitionState
{
public:
    enum class Phase
    {
        Local,
        StoppingForRemote,
        LaunchDeferred,
        LaunchRequested,
        Connecting,
        Active,
        ReturningLocal,
    };

    quint64 beginRemoteSwitch(bool hasActiveSession);
    bool markLaunchDeferred(quint64 token);
    bool beginDeferredLaunch(quint64 token);
    bool markSessionCreated(quint64 token);
    bool markConnected(quint64 token);
    bool fail(quint64 token);
    void cancelToLocal();
    void finishLocal();

    bool canRequestNext() const;
    bool isCurrent(quint64 token) const;
    bool transitionInProgress() const;
    quint64 token() const { return m_Token; }
    Phase phase() const { return m_Phase; }

private:
    quint64 nextToken();

    quint64 m_Token = 0;
    Phase m_Phase = Phase::Local;
};
