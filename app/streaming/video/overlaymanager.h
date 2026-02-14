#pragma once

#include <QString>

#include "SDL_compat.h"
#include <SDL_ttf.h>

namespace Overlay {

enum OverlayType {
    OverlayDebug,
    OverlayStatusUpdate,
    OverlayMax
};

enum ToastType {
    ToastInfo,
    ToastWarning,
    ToastError
};

enum ToastCategory {
    ToastCategoryNone,
    ToastCategoryCaptureKeys,
    ToastCategoryGamepadMouse,
    ToastCategoryConnectionStatus
};

class IOverlayRenderer
{
public:
    virtual ~IOverlayRenderer() = default;

    virtual void notifyOverlayUpdated(OverlayType type) = 0;
};

class OverlayManager
{
public:
    OverlayManager();
    ~OverlayManager();

    bool isOverlayEnabled(OverlayType type);
    char* getOverlayText(OverlayType type);
    void updateOverlayText(OverlayType type, const char* text);
    int getOverlayMaxTextLength();
    void setOverlayTextUpdated(OverlayType type);
    void setOverlayState(OverlayType type, bool enabled);
    SDL_Color getOverlayColor(OverlayType type);
    int getOverlayFontSize(OverlayType type);
    SDL_Surface* getUpdatedOverlaySurface(OverlayType type);

    void setOverlayRenderer(IOverlayRenderer* renderer);

    void showToast(ToastType type, ToastCategory category, const char* text);
    float getToastOpacity();
    SDL_Color getToastColor();
    ToastType getToastType();

    void setMouseModeOverlayActive(bool active);
    bool isMouseModeOverlayActive();

private:
    void notifyOverlayUpdated(OverlayType type);
    SDL_Color getToastColor(ToastType type);

    struct {
        bool enabled;
        int fontSize;
        SDL_Color color;
        char text[1024];

        TTF_Font* font;
        SDL_Surface* surface;
    } m_Overlays[OverlayMax];
    IOverlayRenderer* m_Renderer;
    QByteArray m_FontData;

    Uint32 m_ToastStartTime;
    Uint32 m_ToastDuration;
    ToastType m_ToastType;
    ToastCategory m_ToastCategory;
    bool m_ToastActive;

    bool m_MouseModeOverlayActive;
    char m_MouseModeOverlayText[1024];
};

}
