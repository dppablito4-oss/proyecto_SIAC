#include "sessiontransitionstate.h"

quint64 SessionTransitionState::nextToken()
{
    return ++m_Token;
}

quint64 SessionTransitionState::beginRemoteSwitch(bool hasActiveSession)
{
    if (!canRequestNext()) {
        return 0;
    }

    const quint64 newToken = nextToken();
    m_Phase = hasActiveSession ? Phase::StoppingForRemote : Phase::LaunchRequested;
    return newToken;
}

bool SessionTransitionState::markLaunchDeferred(quint64 token)
{
    if (!isCurrent(token) || m_Phase != Phase::StoppingForRemote) {
        return false;
    }
    m_Phase = Phase::LaunchDeferred;
    return true;
}

bool SessionTransitionState::beginDeferredLaunch(quint64 token)
{
    if (!isCurrent(token) || m_Phase != Phase::LaunchDeferred) {
        return false;
    }
    m_Phase = Phase::LaunchRequested;
    return true;
}

bool SessionTransitionState::markSessionCreated(quint64 token)
{
    if (!isCurrent(token) || m_Phase != Phase::LaunchRequested) {
        return false;
    }
    m_Phase = Phase::Connecting;
    return true;
}

bool SessionTransitionState::markConnected(quint64 token)
{
    if (!isCurrent(token) || m_Phase != Phase::Connecting) {
        return false;
    }
    m_Phase = Phase::Active;
    return true;
}

bool SessionTransitionState::fail(quint64 token)
{
    if (!isCurrent(token)) {
        return false;
    }
    nextToken();
    m_Phase = Phase::Local;
    return true;
}

void SessionTransitionState::cancelToLocal()
{
    nextToken();
    m_Phase = Phase::ReturningLocal;
}

void SessionTransitionState::finishLocal()
{
    nextToken();
    m_Phase = Phase::Local;
}

bool SessionTransitionState::canRequestNext() const
{
    return m_Phase == Phase::Local || m_Phase == Phase::Active;
}

bool SessionTransitionState::isCurrent(quint64 token) const
{
    return token != 0 && token == m_Token;
}

bool SessionTransitionState::transitionInProgress() const
{
    return m_Phase != Phase::Local && m_Phase != Phase::Active;
}
