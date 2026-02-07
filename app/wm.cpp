#include <QtGlobal>

#include "utils.h"

bool WMUtils::isRunningX11()
{
    return false;
}

bool WMUtils::isRunningNvidiaProprietaryDriverX11()
{
    return false;
}

bool WMUtils::supportsDesktopGLWithEGL()
{
    return true;
}

bool WMUtils::isRunningWayland()
{
    return false;
}

bool WMUtils::isRunningWindowManager()
{
    return true;
}

bool WMUtils::isRunningDesktopEnvironment()
{
    bool value;
    if (Utils::getEnvironmentVariableOverride("HAS_DESKTOP_ENVIRONMENT", &value)) {
        return value;
    }

    return true;
}

bool WMUtils::isGpuSlow()
{
    bool ret;

    if (!Utils::getEnvironmentVariableOverride("GL_IS_SLOW", &ret)) {
#ifdef GL_IS_SLOW
        ret = true;
#else
        ret = false;
#endif
    }

    return ret;
}

QString WMUtils::getDrmCardOverride()
{
    return QString();
}
