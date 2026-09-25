// TianyiPlayer — a Luo Tianyi themed desktop player.
// UI: SDL3 + SDL3_image + SDL3_ttf. Playback engine: SemiPlayer (C ABI).
#define SDL_MAIN_HANDLED

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "player/player_controller.hpp"
#include "theme.hpp"
#include "ui/draw.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace {

namespace theme = tianyi::theme;
namespace ui = tianyi::ui;
using tianyi::player::Controller;
using tianyi::player::Frame;
using tianyi::player::Status;

std::string format_time(std::int64_t us) {
    if (us < 0) {
        us = 0;
    }
    const std::int64_t total = us / 1'000'000;
    const std::int64_t h = total / 3600;
    const std::int64_t m = (total % 3600) / 60;
    const std::int64_t s = total % 60;
    char buf[32];
    if (h > 0) {
        std::snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld", static_cast<long long>(h),
                      static_cast<long long>(m), static_cast<long long>(s));
    } else {
        std::snprintf(buf, sizeof(buf), "%02lld:%02lld", static_cast<long long>(m),
                      static_cast<long long>(s));
    }
    return buf;
}

std::string status_text(const Controller &controller) {
    switch (controller.status()) {
    case Status::Idle:
        return controller.current_path().empty() ? "未打开媒体" : "就绪";
    case Status::Opening:
        return "正在打开…";
    case Status::Ready:
        return "就绪";
    case Status::Playing:
        return "正在播放";
    case Status::Paused:
        return "已暂停";
    case Status::Ended:
        return "播放结束";
    case Status::Error:
        break;
    }
    const std::string err = controller.error_text();
    return err.empty() ? "出错了" : ("出错了：" + err);
}

std::string basename_of(const std::string &path) {
    std::filesystem::path p(path);
    return p.filename().string();
}

void draw_text_right(SDL_Renderer *renderer, ui::TextRenderer &text, const std::string &s,
                     SDL_Color color, float point_size, float x_right, float y_center) {
    SDL_Texture *t = text.render(renderer, s, color, point_size);
    if (!t) {
        return;
    }
    float w = 0.0f, h = 0.0f;
    SDL_GetTextureSize(t, &w, &h);
    SDL_FRect dst{x_right - w, y_center - h * 0.5f, w, h};
    SDL_RenderTexture(renderer, t, nullptr, &dst);
}

void draw_play_icon(SDL_Renderer *renderer, SDL_FRect button, SDL_Color color) {
    SDL_FColor c{color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f};
    SDL_FPoint uv{0.0f, 0.0f};
    const float cx = button.x + button.w * 0.5f + 2.0f;
    const float cy = button.y + button.h * 0.5f;
    const float r = button.h * 0.22f;
    SDL_Vertex verts[3] = {
        {{cx - r * 0.7f, cy - r}, c, uv},
        {{cx - r * 0.7f, cy + r}, c, uv},
        {{cx + r, cy}, c, uv},
    };
    SDL_RenderGeometry(renderer, nullptr, verts, 3, nullptr, 0);
}

void draw_pause_icon(SDL_Renderer *renderer, SDL_FRect button, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    const float bar_w = button.w * 0.14f;
    const float bar_h = button.h * 0.42f;
    const float cy = button.y + button.h * 0.5f;
    SDL_FRect left{button.x + button.w * 0.5f - bar_w * 1.7f, cy - bar_h * 0.5f, bar_w, bar_h};
    SDL_FRect right{button.x + button.w * 0.5f + bar_w * 0.7f, cy - bar_h * 0.5f, bar_w, bar_h};
    SDL_RenderFillRect(renderer, &left);
    SDL_RenderFillRect(renderer, &right);
}

} // namespace

int main(int argc, char *argv[]) {
    // Assets resolve relative to the executable first, then the CWD.
    std::filesystem::path exe_dir =
        argc > 0 ? std::filesystem::path(argv[0]).parent_path() : std::filesystem::path{};
    auto resolve_asset = [&exe_dir](const std::string &rel) {
        std::filesystem::path direct(rel);
        if (std::filesystem::exists(direct)) {
            return direct.string();
        }
        return (exe_dir / rel).string();
    };

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    TTF_Init();

    SDL_Window *window = SDL_CreateWindow("TianyiPlayer · 洛天依主题播放器", 1280, 720,
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!renderer) {
        SDL_Log("renderer creation failed: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // ---- Theme resources ----
    SDL_Texture *background = nullptr;
    SDL_Texture *poster = nullptr;
    if (SDL_Surface *surf = IMG_Load(resolve_asset(theme::background_path()).c_str())) {
        background = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_DestroySurface(surf);
    }
    if (SDL_Surface *surf = IMG_Load(resolve_asset(theme::poster_path()).c_str())) {
        poster = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_DestroySurface(surf);
    }
    if (!background) {
        SDL_Log("background failed to load");
    }

    ui::TextRenderer text;
    text.init(resolve_asset(theme::font_path()));

    Controller controller;
    if (!controller.init()) {
        SDL_Log("player init failed: %s", controller.error_text().c_str());
    }
    if (argc > 1) {
        controller.open(argv[1]);
    }

    // ---- Video texture state ----
    SDL_Texture *video_texture = nullptr;
    int video_w = 0, video_h = 0;
    std::uint64_t frame_seq = 0;
    Frame frame;

    // ---- Interaction state ----
    bool seek_dragging = false;
    float drag_ratio = 0.0f;
    bool running = true;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_DROP_FILE:
                if (event.drop.data) {
                    controller.open(event.drop.data);
                    SDL_free(const_cast<void *>(static_cast<const void *>(event.drop.data)));
                }
                break;
            case SDL_EVENT_KEY_DOWN: {
                switch (event.key.key) {
                case SDLK_ESCAPE:
                    running = false;
                    break;
                case SDLK_SPACE:
                    controller.toggle_play();
                    break;
                case SDLK_LEFT:
                    controller.seek_relative_us(-5'000'000);
                    break;
                case SDLK_RIGHT:
                    controller.seek_relative_us(5'000'000);
                    break;
                case SDLK_F11: {
                    const bool fullscreen =
                        (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
                    SDL_SetWindowFullscreen(window, !fullscreen);
                    break;
                }
                default:
                    break;
                }
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_MOTION:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                const float mx = event.type == SDL_EVENT_MOUSE_MOTION ? event.motion.x
                                                                      : event.button.x;
                const float my = event.type == SDL_EVENT_MOUSE_MOTION ? event.motion.y
                                                                      : event.button.y;
                int ww = 0, wh = 0;
                SDL_GetWindowSize(window, &ww, &wh);
                const float width = static_cast<float>(ww);
                const float height = static_cast<float>(wh);

                const SDL_FRect play_button{48.0f, height - theme::kBottomBarHeight + 24.0f,
                                            theme::kButtonSize, theme::kButtonSize};
                const float seek_x0 = 280.0f;
                const float seek_x1 = width - 56.0f;
                const float seek_y = height - theme::kBottomBarHeight + 44.0f;

                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                    event.button.button == SDL_BUTTON_LEFT) {
                    if (mx >= play_button.x && mx <= play_button.x + play_button.w &&
                        my >= play_button.y && my <= play_button.y + play_button.h) {
                        controller.toggle_play();
                    } else if (my >= seek_y - 12.0f && my <= seek_y + 12.0f && mx >= seek_x0 &&
                               mx <= seek_x1 && controller.media().duration_us > 0) {
                        seek_dragging = true;
                        drag_ratio = std::clamp((mx - seek_x0) / (seek_x1 - seek_x0), 0.0f, 1.0f);
                    }
                } else if (event.type == SDL_EVENT_MOUSE_MOTION && seek_dragging) {
                    drag_ratio = std::clamp((mx - seek_x0) / (seek_x1 - seek_x0), 0.0f, 1.0f);
                } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && seek_dragging) {
                    seek_dragging = false;
                    const auto duration = controller.media().duration_us;
                    controller.seek_us(static_cast<std::int64_t>(drag_ratio * duration));
                }
                break;
            }
            default:
                break;
            }
        }

        controller.poll();

        // Upload the newest engine frame when one arrived.
        if (controller.consume_frame(frame, frame_seq)) {
            if (!video_texture || video_w != frame.width || video_h != frame.height) {
                if (video_texture) {
                    SDL_DestroyTexture(video_texture);
                }
                video_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                                  SDL_TEXTUREACCESS_STREAMING, frame.width,
                                                  frame.height);
                video_w = frame.width;
                video_h = frame.height;
            }
            if (video_texture) {
                SDL_UpdateTexture(video_texture, nullptr, frame.pixels.data(), frame.pitch);
            }
        }

        // ---- Render ----
        int ww = 0, wh = 0;
        SDL_GetWindowSize(window, &ww, &wh);
        const float W = static_cast<float>(ww);
        const float H = static_cast<float>(wh);

        SDL_SetRenderDrawColor(renderer, 0x0B, 0x10, 0x1E, 0xFF);
        SDL_RenderClear(renderer);

        // Background art (cover) + dim overlay.
        if (background) {
            float bw = 0.0f, bh = 0.0f;
            SDL_GetTextureSize(background, &bw, &bh);
            SDL_FRect dst = ui::fit_cover({0, 0, W, H}, bw, bh);
            SDL_RenderTexture(renderer, background, nullptr, &dst);
        }
        SDL_SetRenderDrawColor(renderer, theme::kOverlay.r, theme::kOverlay.g,
                               theme::kOverlay.b, theme::kOverlay.a);
        SDL_FRect full{0, 0, W, H};
        SDL_RenderFillRect(renderer, &full);

        const float content_top = theme::kTopBarHeight;
        const float content_bottom = H - theme::kBottomBarHeight;
        SDL_FRect content{24.0f, content_top + 8.0f, W - 48.0f,
                          content_bottom - content_top - 16.0f};

        const bool show_video = video_texture != nullptr &&
                                (controller.status() == Status::Playing ||
                                 controller.status() == Status::Paused ||
                                 controller.status() == Status::Ended);
        if (show_video) {
            SDL_FRect dst = ui::fit_contain(content, static_cast<float>(video_w),
                                            static_cast<float>(video_h));
            SDL_RenderTexture(renderer, video_texture, nullptr, &dst);
            ui::stroke_rounded_rect(renderer, dst, 6.0f, theme::kAccentSoft, 2.0f);
        } else if (poster && controller.current_path().empty()) {
            float pw = 0.0f, ph = 0.0f;
            SDL_GetTextureSize(poster, &pw, &ph);
            SDL_FRect dst = ui::fit_contain(content, pw, ph);
            SDL_RenderTexture(renderer, poster, nullptr, &dst);
        }

        // ---- Top bar ----
        ui::draw_text(renderer, text, "♪  TianyiPlayer", theme::kAccent, 26.0f, 48.0f, 32.0f);
        ui::draw_text(renderer, text, "洛天依主题播放器", theme::kText, 18.0f, 250.0f, 32.0f);
        draw_text_right(renderer, text, status_text(controller), theme::kTextDim, 16.0f,
                        W - 48.0f, 32.0f);

        // ---- Bottom bar ----
        ui::fill_rounded_rect(renderer, {0, H - theme::kBottomBarHeight, W,
                                         theme::kBottomBarHeight},
                              0.0f, theme::kPanel);
        SDL_SetRenderDrawColor(renderer, theme::kAccentSoft.r, theme::kAccentSoft.g,
                               theme::kAccentSoft.b, theme::kAccentSoft.a);
        SDL_FRect accent_line{0, H - theme::kBottomBarHeight, W, 2.0f};
        SDL_RenderFillRect(renderer, &accent_line);

        // Play / pause button.
        const SDL_FRect play_button{48.0f, H - theme::kBottomBarHeight + 24.0f,
                                    theme::kButtonSize, theme::kButtonSize};
        ui::fill_rounded_rect(renderer, play_button, theme::kCornerRadius, theme::kAccent);
        if (controller.status() == Status::Playing) {
            draw_pause_icon(renderer, play_button, theme::kPanelDeep);
        } else {
            draw_play_icon(renderer, play_button, theme::kPanelDeep);
        }

        // Time display.
        const std::int64_t duration_us = controller.media().duration_us;
        const std::int64_t position_us =
            seek_dragging && duration_us > 0
                ? static_cast<std::int64_t>(drag_ratio * duration_us)
                : controller.position_us();
        ui::draw_text(renderer, text, format_time(position_us), theme::kText, 17.0f, 128.0f,
                      H - theme::kBottomBarHeight + 40.0f);
        ui::draw_text(renderer, text, "/ " + format_time(duration_us), theme::kTextDim, 17.0f,
                      200.0f, H - theme::kBottomBarHeight + 40.0f);

        // Seek bar.
        const float seek_x0 = 280.0f;
        const float seek_x1 = W - 56.0f;
        const float seek_y = H - theme::kBottomBarHeight + 44.0f;
        ui::fill_rounded_rect(renderer, {seek_x0, seek_y - theme::kSeekHeight * 0.5f,
                                         seek_x1 - seek_x0, theme::kSeekHeight},
                              theme::kSeekHeight * 0.5f, theme::kTrack);
        const float ratio =
            duration_us > 0 ? std::clamp(static_cast<float>(position_us) /
                                             static_cast<float>(duration_us),
                                         0.0f, 1.0f)
                            : 0.0f;
        const float filled = (seek_x1 - seek_x0) * ratio;
        ui::fill_rounded_rect(renderer, {seek_x0, seek_y - theme::kSeekHeight * 0.5f, filled,
                                         theme::kSeekHeight},
                              theme::kSeekHeight * 0.5f, theme::kAccent);
        SDL_FRect knob{seek_x0 + filled - 7.0f, seek_y - 7.0f, 14.0f, 14.0f};
        ui::fill_rounded_rect(renderer, knob, 7.0f, theme::kPink);

        // File name + key hints.
        const std::string file_name =
            controller.current_path().empty() ? "把媒体文件拖进来，或在命令行传入路径"
                                              : basename_of(controller.current_path());
        ui::draw_text(renderer, text, file_name, theme::kTextDim, 15.0f, 48.0f, H - 22.0f);
        draw_text_right(renderer, text,
                        "空格 播放/暂停   ←/→ 快退快进 5 秒   F11 全屏   Esc 退出",
                        theme::kTextDim, 14.0f, W - 48.0f, H - 22.0f);

        SDL_RenderPresent(renderer);
        SDL_Delay(8);
    }

    // ---- Teardown ----
    controller.shutdown();
    if (video_texture) {
        SDL_DestroyTexture(video_texture);
    }
    if (background) {
        SDL_DestroyTexture(background);
    }
    if (poster) {
        SDL_DestroyTexture(poster);
    }
    text.shutdown();
    TTF_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
