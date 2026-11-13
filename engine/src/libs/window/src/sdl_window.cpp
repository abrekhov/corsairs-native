#include "sdl_window.hpp"

#include <SDL2/SDL_syswm.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace storm
{
SDLWindow::SDLWindow(int width, int height, int preferred_display, bool fullscreen, bool bordered)
    : fullscreen_(fullscreen)
{
#ifdef __APPLE__
    // Space fullscreen (separate macOS desktop, no display mode switch) for SDL_WINDOW_FULLSCREEN
    SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "1");
    spaceRequested_ = getenv("STORM_WINDOW_SIZE") && strcmp(getenv("STORM_WINDOW_SIZE"), "space") == 0;
    fullscreen_ = fullscreen = false;
    bordered = true;
    width = std::clamp(width, 320, 960);
    height = std::clamp(height, 240, 600);
#endif
    uint32_t flags = (fullscreen ? SDL_WINDOW_FULLSCREEN : 0) | SDL_WINDOW_HIDDEN;
#if !defined(_WIN32) && !defined(STORM_MESA_NINE) // DXVK-Native
    flags |= SDL_WINDOW_VULKAN;
#endif
    window_ = std::unique_ptr<SDL_Window, std::function<void(SDL_Window *)>>(
        SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED_DISPLAY(preferred_display),
                         SDL_WINDOWPOS_CENTERED_DISPLAY(preferred_display), width, height, flags),
        [](SDL_Window *w) { SDL_DestroyWindow(w); });

    if (!window_)
        throw std::runtime_error(SDL_GetError());
    sdlID_ = SDL_GetWindowID(window_.get());
    SDL_SetWindowBordered(window_.get(), bordered ? SDL_TRUE : SDL_FALSE);
    SDL_AddEventWatch(&SDLEventHandler, this);
}

SDLWindow::~SDLWindow()
{
    CaptureMouse(false);
    SDL_DelEventWatch(&SDLEventHandler, this);
}

void SDLWindow::Show()
{
    SDL_ShowWindow(window_.get());
#ifdef __APPLE__
    if (spaceRequested_)
    {
        spaceFullscreen_ = SDL_SetWindowFullscreen(window_.get(), SDL_WINDOW_FULLSCREEN) == 0;
        fprintf(stderr, "[WINDOW] space fullscreen %s\n", spaceFullscreen_ ? "on" : "failed");
    }
#endif
}

void SDLWindow::Hide()
{
    SDL_HideWindow(window_.get());
}

void SDLWindow::Focus()
{
    SDL_RaiseWindow(window_.get());
    if (SDL_GetHintBoolean("STORM_RELATIVE_MOUSE", SDL_FALSE))
        CaptureMouse(true);
}

void SDLWindow::CaptureMouse(bool capture) const
{
    if (capture && SDL_GetKeyboardFocus() != window_.get())
        return;
    const auto requested = capture ? SDL_TRUE : SDL_FALSE;
    if (SDL_GetRelativeMouseMode() != requested)
    {
        if (SDL_SetRelativeMouseMode(requested) != 0)
            fprintf(stderr, "[INPUT] mouse capture failed: %s\n", SDL_GetError());
        else
            fprintf(stderr, "[INPUT] mouse %s\n", capture ? "captured" : "released");
    }
    if (!capture)
        SDL_SetWindowGrab(window_.get(), SDL_FALSE);
}

int SDLWindow::Width() const
{
    int w, h;
    SDL_GetWindowSize(window_.get(), &w, &h);
    return w;
}

int SDLWindow::Height() const
{
    int w, h;
    SDL_GetWindowSize(window_.get(), &w, &h);
    return h;
}

WindowSize SDLWindow::GetWindowSize() const
{
    int w, h;
    SDL_GetWindowSize(window_.get(), &w, &h);
    return {w, h};
}

bool SDLWindow::Fullscreen() const
{
    return fullscreen_;
}

std::string SDLWindow::Title() const
{
    return std::string(SDL_GetWindowTitle(window_.get()));
}

void SDLWindow::SetFullscreen(bool fullscreen)
{
#ifdef __APPLE__
    if (!SDL_GetHintBoolean("STORM_ALLOW_DESKTOP_WINDOW", SDL_FALSE) || fullscreen == fullscreen_)
        return;
    if (fullscreen)
    {
        SDL_Rect bounds;
        const auto display = SDL_GetWindowDisplayIndex(window_.get());
        if (display < 0 || SDL_GetDisplayUsableBounds(display, &bounds) != 0)
            return;
        SDL_GetWindowPosition(window_.get(), &windowedBounds_.x, &windowedBounds_.y);
        SDL_GetWindowSize(window_.get(), &windowedBounds_.w, &windowedBounds_.h);
        SDL_SetWindowBordered(window_.get(), SDL_FALSE);
        SDL_SetWindowPosition(window_.get(), bounds.x, bounds.y);
        SDL_SetWindowSize(window_.get(), bounds.w, bounds.h);
    }
    else
    {
        SDL_SetWindowBordered(window_.get(), SDL_TRUE);
        SDL_SetWindowPosition(window_.get(), windowedBounds_.x, windowedBounds_.y);
        SDL_SetWindowSize(window_.get(), windowedBounds_.w, windowedBounds_.h);
    }
    fullscreen_ = fullscreen;
#else
    fullscreen_ = fullscreen;
    SDL_SetWindowFullscreen(window_.get(), fullscreen_ ? SDL_WINDOW_FULLSCREEN : 0);
#endif
}

void SDLWindow::ToggleSpaceFullscreen()
{
    if (!SDL_GetHintBoolean("STORM_ALLOW_DESKTOP_WINDOW", SDL_FALSE))
        return;
    spaceFullscreen_ = !spaceFullscreen_;
    if (spaceFullscreen_)
    {
        // leave borderless-fit first so windowed restore state stays sane
        if (fullscreen_)
            SetFullscreen(false);
        spaceFullscreen_ = SDL_SetWindowFullscreen(window_.get(), SDL_WINDOW_FULLSCREEN) == 0;
    }
    else
    {
        SDL_SetWindowFullscreen(window_.get(), 0);
        SDL_SetWindowBordered(window_.get(), SDL_TRUE);
    }
    fprintf(stderr, "[WINDOW] space fullscreen %s\n", spaceFullscreen_ ? "on" : "off");
}

void SDLWindow::Resize(int width, int height)
{
#ifdef __APPLE__
    width = std::clamp(width, 320, 960);
    height = std::clamp(height, 240, 600);
#endif
    SDL_SetWindowSize(window_.get(), width, height);
}

void SDLWindow::WarpMouseInWindow(int x, int y)
{
    if (SDL_GetHintBoolean("STORM_SAFE_WINDOW", SDL_FALSE))
        return;
    SDL_WarpMouseInWindow(window_.get(), x, y);
}

void SDLWindow::SetTitle(const std::string &title)
{
    SDL_SetWindowTitle(window_.get(), title.c_str());
}

void SDLWindow::SetGamma(const uint16_t (&red)[256], const uint16_t (&green)[256], const uint16_t (&blue)[256])
{
#ifdef __APPLE__
    return;
#endif
    SDL_SetWindowGammaRamp(window_.get(), red, green, blue);
}

int SDLWindow::Subscribe(const EventHandler &handler)
{
    int id = 1;
    if (!handlers_.empty())
        id = (--handlers_.end())->first + 1;
    handlers_[id] = handler;
    return id;
}

void SDLWindow::Unsubscribe(int id)
{
    auto it = handlers_.find(id);
    if (it != handlers_.end())
        handlers_.erase(it);
}

// TODO: X/Wayland/MacOS
void *SDLWindow::OSHandle()
{
    if (!window_)
        return nullptr;

#ifdef _WIN32
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    SDL_GetWindowWMInfo(window_.get(), &info);
    return info.info.win.window;
#else
    // dxvk-native uses HWND as SDL2 window handle, so this is allowed
    return window_.get();
#endif
}

SDL_Window *SDLWindow::SDLHandle() const
{
    return window_.get();
}

void SDLWindow::ProcessEvent(const SDL_WindowEvent &evt) const
{
    Event winEvent;
    switch (evt.event)
    {
    case SDL_WINDOWEVENT_FOCUS_GAINED:
        if (SDL_GetHintBoolean("STORM_RELATIVE_MOUSE", SDL_FALSE) && !cursorReleased_)
            CaptureMouse(true);
        winEvent = FocusGained;
        break;

    case SDL_WINDOWEVENT_FOCUS_LOST:
        CaptureMouse(false);
        winEvent = FocusLost;
        break;

    case SDL_WINDOWEVENT_RESIZED:
    case SDL_WINDOWEVENT_SIZE_CHANGED:
        winEvent = Resized;
        break;

    case SDL_WINDOWEVENT_CLOSE:
        winEvent = Closed;
        break;

    default:
        return;
    }

    for (auto handler : handlers_)
        handler.second(winEvent);
}

std::shared_ptr<OSWindow> OSWindow::Create(int width, int height, int preferred_display, bool fullscreen, bool bordered)
{
    return std::make_shared<SDLWindow>(width, height, preferred_display, fullscreen, bordered);
}

int SDLWindow::SDLEventHandler(void *userdata, SDL_Event *evt)
{
    auto w = static_cast<SDLWindow *>(userdata);
    if (evt->type == SDL_QUIT)
    {
        w->CaptureMouse(false);
        for (const auto &handler : w->handlers_)
            handler.second(Closed);
    }
    if (evt->type == SDL_KEYDOWN && evt->key.windowID == w->sdlID_ && !evt->key.repeat)
    {
        if (evt->key.keysym.sym == SDLK_ESCAPE)
        {
            w->cursorReleased_ = true;
            w->CaptureMouse(false);
        }
        if (evt->key.keysym.sym == SDLK_F12 ||
            (evt->key.keysym.sym == SDLK_f && (evt->key.keysym.mod & KMOD_GUI) && (evt->key.keysym.mod & KMOD_CTRL)))
            w->ToggleSpaceFullscreen();
        else if (evt->key.keysym.sym == SDLK_F11)
            w->SetFullscreen(!w->fullscreen_);
    }
    if (evt->type == SDL_MOUSEBUTTONDOWN && evt->button.windowID == w->sdlID_ &&
        evt->button.button == SDL_BUTTON_LEFT && SDL_GetHintBoolean("STORM_RELATIVE_MOUSE", SDL_FALSE))
    {
        w->cursorReleased_ = false;
        w->CaptureMouse(true);
    }

    if ((evt->type != SDL_WINDOWEVENT) || (evt->window.windowID != w->sdlID_))
        return 0;

    w->ProcessEvent(evt->window);

    return 0;
}
} // namespace storm
