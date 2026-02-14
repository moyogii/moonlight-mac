#include "overlaymanager.h"
#include "path.h"

using namespace Overlay;

#define TOAST_FADE_IN_MS    250
#define TOAST_FADE_OUT_MS   500
#define TOAST_DISPLAY_MS    2500

OverlayManager::OverlayManager() :
    m_Renderer(nullptr),
    m_FontData(Path::readDataFile("ModeSeven.ttf")),
    m_ToastStartTime(0),
    m_ToastDuration(0),
    m_ToastType(ToastInfo),
    m_ToastCategory(ToastCategoryNone),
    m_ToastActive(false),
    m_MouseModeOverlayActive(false)
{
    memset(m_Overlays, 0, sizeof(m_Overlays));

    m_Overlays[OverlayType::OverlayDebug].color = {0xFF, 0xFF, 0xFF, 0xFF};
    m_Overlays[OverlayType::OverlayDebug].fontSize = 20;

    m_Overlays[OverlayType::OverlayStatusUpdate].color = {0xFF, 0xFF, 0xFF, 0xFF};
    m_Overlays[OverlayType::OverlayStatusUpdate].fontSize = 28;

    m_MouseModeOverlayText[0] = '\0';

    if (TTF_Init() != 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "TTF_Init() failed: %s",
                    TTF_GetError());
        return;
    }
}

OverlayManager::~OverlayManager()
{
    for (int i = 0; i < OverlayType::OverlayMax; i++) {
        if (m_Overlays[i].surface != nullptr) {
            SDL_FreeSurface(m_Overlays[i].surface);
        }
        if (m_Overlays[i].font != nullptr) {
            TTF_CloseFont(m_Overlays[i].font);
        }
    }

    TTF_Quit();

    // For similar reasons to the comment in the constructor, this will usually,
    // but not always, deinitialize TTF. In the cases where Session objects overlap
    // in lifetime, there may be an additional reference on TTF for the new Session
    // that means it will not be cleaned up here.
    //SDL_assert(TTF_WasInit() == 0);
}

bool OverlayManager::isOverlayEnabled(OverlayType type)
{
    return m_Overlays[type].enabled;
}

char* OverlayManager::getOverlayText(OverlayType type)
{
    return m_Overlays[type].text;
}

void OverlayManager::updateOverlayText(OverlayType type, const char* text)
{
    strncpy(m_Overlays[type].text, text, sizeof(m_Overlays[0].text));
    m_Overlays[type].text[getOverlayMaxTextLength() - 1] = '\0';

    setOverlayTextUpdated(type);
}

int OverlayManager::getOverlayMaxTextLength()
{
    return sizeof(m_Overlays[0].text);
}

int OverlayManager::getOverlayFontSize(OverlayType type)
{
    return m_Overlays[type].fontSize;
}

SDL_Surface* OverlayManager::getUpdatedOverlaySurface(OverlayType type)
{
    // If a new surface is available, return it. If not, return nullptr.
    // Caller must free the surface on success.
    return (SDL_Surface*)SDL_AtomicSetPtr((void**)&m_Overlays[type].surface, nullptr);
}

void OverlayManager::setOverlayTextUpdated(OverlayType type)
{
    // Only update the overlay state if it's enabled. If it's not enabled,
    // the renderer has already been notified by setOverlayState().
    if (m_Overlays[type].enabled) {
        notifyOverlayUpdated(type);
    }
}

void OverlayManager::setOverlayState(OverlayType type, bool enabled)
{
    bool stateChanged = m_Overlays[type].enabled != enabled;

    m_Overlays[type].enabled = enabled;

    if (stateChanged) {
        if (!enabled) {
            // Set the text to empty string on disable
            m_Overlays[type].text[0] = 0;
        }

        notifyOverlayUpdated(type);
    }
}

SDL_Color OverlayManager::getOverlayColor(OverlayType type)
{
    return m_Overlays[type].color;
}

void OverlayManager::setOverlayRenderer(IOverlayRenderer* renderer)
{
    m_Renderer = renderer;
}

void OverlayManager::notifyOverlayUpdated(OverlayType type)
{
    if (m_Renderer == nullptr) {
        return;
    }

    // Construct the required font to render the overlay
    if (m_Overlays[type].font == nullptr) {
        if (m_FontData.isEmpty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "SDL overlay font failed to load");
            return;
        }

        // m_FontData must stay around until the font is closed
        m_Overlays[type].font = TTF_OpenFontRW(SDL_RWFromConstMem(m_FontData.constData(), m_FontData.size()),
                                               1,
                                               m_Overlays[type].fontSize);
        if (m_Overlays[type].font == nullptr) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "TTF_OpenFont() failed: %s",
                        TTF_GetError());

            // Can't proceed without a font
            return;
        }
    }

    // Exchange the old surface with the new one
    SDL_Surface* oldSurface = (SDL_Surface*)SDL_AtomicSetPtr(
        (void**)&m_Overlays[type].surface,
        m_Overlays[type].enabled ?
            // The _Wrapped variant is required for line breaks to work
            TTF_RenderText_Blended_Wrapped(m_Overlays[type].font,
                                           m_Overlays[type].text,
                                           m_Overlays[type].color,
                                           1024)
            : nullptr);

    // Notify the renderer
    m_Renderer->notifyOverlayUpdated(type);

    // Free the old surface
    if (oldSurface != nullptr) {
        SDL_FreeSurface(oldSurface);
    }
}

SDL_Color OverlayManager::getToastColor(ToastType type)
{
    switch (type) {
    case ToastWarning:
        return {0xFF, 0xD7, 0x00, 0xFF};
    case ToastError:
        return {0xFF, 0x45, 0x45, 0xFF};
    case ToastInfo:
    default:
        return {0xFF, 0xFF, 0xFF, 0xFF};
    }
}

void OverlayManager::showToast(ToastType type, ToastCategory category, const char* text)
{
    if (m_MouseModeOverlayActive) {
        strncpy(m_MouseModeOverlayText, text, sizeof(m_MouseModeOverlayText) - 1);
        m_MouseModeOverlayText[sizeof(m_MouseModeOverlayText) - 1] = '\0';
        return;
    }

    if (m_ToastActive && m_ToastCategory == category) {
        strncpy(m_Overlays[OverlayStatusUpdate].text, text, sizeof(m_Overlays[0].text) - 1);
        m_Overlays[OverlayStatusUpdate].text[sizeof(m_Overlays[0].text) - 1] = '\0';
        m_Overlays[OverlayStatusUpdate].color = getToastColor(type);
        m_ToastType = type;
        m_ToastStartTime = SDL_GetTicks();
        m_Overlays[OverlayStatusUpdate].enabled = true;
        notifyOverlayUpdated(OverlayStatusUpdate);
        return;
    }

    strncpy(m_Overlays[OverlayStatusUpdate].text, text, sizeof(m_Overlays[0].text) - 1);
    m_Overlays[OverlayStatusUpdate].text[sizeof(m_Overlays[0].text) - 1] = '\0';
    m_Overlays[OverlayStatusUpdate].color = getToastColor(type);
    m_Overlays[OverlayStatusUpdate].enabled = true;
    m_ToastType = type;
    m_ToastCategory = category;
    m_ToastStartTime = SDL_GetTicks();
    m_ToastDuration = TOAST_DISPLAY_MS;
    m_ToastActive = true;

    notifyOverlayUpdated(OverlayStatusUpdate);
}

float OverlayManager::getToastOpacity()
{
    if (!m_ToastActive) {
        return 1.0f;
    }

    Uint32 elapsed = SDL_GetTicks() - m_ToastStartTime;

    if (elapsed >= m_ToastDuration) {
        m_ToastActive = false;
        m_ToastCategory = ToastCategoryNone;
        setOverlayState(OverlayStatusUpdate, false);

        if (m_MouseModeOverlayActive && m_MouseModeOverlayText[0] != '\0') {
            strncpy(m_Overlays[OverlayStatusUpdate].text, m_MouseModeOverlayText, sizeof(m_Overlays[0].text) - 1);
            m_Overlays[OverlayStatusUpdate].text[sizeof(m_Overlays[0].text) - 1] = '\0';
            m_Overlays[OverlayStatusUpdate].color = {0xCC, 0x00, 0x00, 0xFF};
            m_Overlays[OverlayStatusUpdate].enabled = true;
            notifyOverlayUpdated(OverlayStatusUpdate);
        }
        return 0.0f;
    }

    if (elapsed < TOAST_FADE_IN_MS) {
        return (float)elapsed / TOAST_FADE_IN_MS;
    }

    if (elapsed > m_ToastDuration - TOAST_FADE_OUT_MS) {
        Uint32 fadeElapsed = elapsed - (m_ToastDuration - TOAST_FADE_OUT_MS);
        float opacity = 1.0f - ((float)fadeElapsed / TOAST_FADE_OUT_MS);
        return opacity > 0.0f ? opacity : 0.0f;
    }

    return 1.0f;
}

SDL_Color OverlayManager::getToastColor()
{
    return getToastColor(m_ToastType);
}

Overlay::ToastType OverlayManager::getToastType()
{
    return m_ToastType;
}

void OverlayManager::setMouseModeOverlayActive(bool active)
{
    m_MouseModeOverlayActive = active;
    if (!active) {
        m_MouseModeOverlayText[0] = '\0';
    }
}

bool OverlayManager::isMouseModeOverlayActive()
{
    return m_MouseModeOverlayActive;
}
