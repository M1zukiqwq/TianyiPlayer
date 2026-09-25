#include "ui/draw.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace tianyi::ui {
namespace {

constexpr int kCornerSegments = 8;

void fill_triangle_fan(SDL_Renderer *renderer, SDL_FPoint center,
                       const std::vector<SDL_FPoint> &arc, SDL_Color color) {
    // Emit (center, p_i, p_i+1) triangles; SDL_FColor is float 0..1.
    SDL_FColor c{color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f};
    std::vector<SDL_Vertex> verts;
    verts.reserve(arc.size() * 3);
    SDL_FPoint uv{0.0f, 0.0f};
    for (std::size_t i = 0; i + 1 < arc.size(); ++i) {
        verts.push_back({center, c, uv});
        verts.push_back({arc[i], c, uv});
        verts.push_back({arc[i + 1], c, uv});
    }
    if (!verts.empty()) {
        SDL_RenderGeometry(renderer, nullptr, verts.data(), static_cast<int>(verts.size()),
                           nullptr, 0);
    }
}

std::vector<SDL_FPoint> corner_arc(SDL_FPoint center, float radius, double start_rad,
                                   double sweep_rad) {
    std::vector<SDL_FPoint> pts;
    pts.reserve(kCornerSegments + 1);
    for (int i = 0; i <= kCornerSegments; ++i) {
        double a = start_rad + sweep_rad * (static_cast<double>(i) / kCornerSegments);
        pts.push_back({center.x + radius * static_cast<float>(std::cos(a)),
                       center.y + radius * static_cast<float>(std::sin(a))});
    }
    return pts;
}

} // namespace

void fill_rounded_rect(SDL_Renderer *renderer, SDL_FRect rect, float radius, SDL_Color color) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }
    radius = std::min(radius, std::min(rect.w, rect.h) * 0.5f);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    // Body: one big rect plus the corner patches are drawn as fans.
    SDL_FRect body_x{rect.x + radius, rect.y, rect.w - 2.0f * radius, rect.h};
    SDL_FRect body_y{rect.x, rect.y + radius, rect.w, rect.h - 2.0f * radius};
    SDL_RenderFillRect(renderer, &body_x);
    SDL_RenderFillRect(renderer, &body_y);

    const double half_pi = 3.14159265358979323846 / 2.0;
    fill_triangle_fan(renderer,
                      {rect.x + radius, rect.y + radius},
                      corner_arc({rect.x + radius, rect.y + radius}, radius, 1.5 * half_pi,
                                 half_pi),
                      color);
    fill_triangle_fan(renderer,
                      {rect.x + rect.w - radius, rect.y + radius},
                      corner_arc({rect.x + rect.w - radius, rect.y + radius}, radius, 2.0 * half_pi,
                                 half_pi),
                      color);
    fill_triangle_fan(renderer,
                      {rect.x + rect.w - radius, rect.y + rect.h - radius},
                      corner_arc({rect.x + rect.w - radius, rect.y + rect.h - radius}, radius,
                                 0.0, half_pi),
                      color);
    fill_triangle_fan(renderer,
                      {rect.x + radius, rect.y + rect.h - radius},
                      corner_arc({rect.x + radius, rect.y + rect.h - radius}, radius, half_pi,
                                 half_pi),
                      color);
}

void stroke_rounded_rect(SDL_Renderer *renderer, SDL_FRect rect, float radius, SDL_Color color,
                         float thickness) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }
    radius = std::min(radius, std::min(rect.w, rect.h) * 0.5f);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    const double half_pi = 3.14159265358979323846 / 2.0;
    const float x1 = rect.x, y1 = rect.y, x2 = rect.x + rect.w, y2 = rect.y + rect.h;

    for (int t = 0; t < static_cast<int>(thickness); ++t) {
        SDL_RenderLine(renderer, x1 + radius, y1 + t, x2 - radius, y1 + t);
        SDL_RenderLine(renderer, x1 + radius, y2 - t, x2 - radius, y2 - t);
        SDL_RenderLine(renderer, x1 + t, y1 + radius, x1 + t, y2 - radius);
        SDL_RenderLine(renderer, x2 - t, y1 + radius, x2 - t, y2 - radius);
    }

    auto arc = [&](SDL_FPoint center, double start) {
        auto pts = corner_arc(center, radius, start, half_pi);
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
            SDL_RenderLine(renderer, pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y);
        }
    };
    arc({x1 + radius, y1 + radius}, 1.5 * half_pi);
    arc({x2 - radius, y1 + radius}, 2.0 * half_pi);
    arc({x2 - radius, y2 - radius}, 0.0);
    arc({x1 + radius, y2 - radius}, half_pi);
}

SDL_FRect fit_cover(SDL_FRect dst, float src_w, float src_h) {
    if (src_w <= 0.0f || src_h <= 0.0f) {
        return dst;
    }
    const float scale = std::max(dst.w / src_w, dst.h / src_h);
    const float w = src_w * scale, h = src_h * scale;
    return {dst.x + (dst.w - w) * 0.5f, dst.y + (dst.h - h) * 0.5f, w, h};
}

SDL_FRect fit_contain(SDL_FRect dst, float src_w, float src_h) {
    if (src_w <= 0.0f || src_h <= 0.0f) {
        return dst;
    }
    const float scale = std::min(dst.w / src_w, dst.h / src_h);
    const float w = src_w * scale, h = src_h * scale;
    return {dst.x + (dst.w - w) * 0.5f, dst.y + (dst.h - h) * 0.5f, w, h};
}

bool TextRenderer::init(const std::string &font_path) {
    font_path_ = font_path;
    return true;
}

void TextRenderer::shutdown() {
    clear_cache();
    for (auto &[size, font] : fonts_) {
        if (font) {
            TTF_CloseFont(font);
        }
    }
    fonts_.clear();
}

TTF_Font *TextRenderer::open_font(float point_size) {
    auto it = fonts_.find(point_size);
    if (it != fonts_.end()) {
        return it->second;
    }
    TTF_Font *font = TTF_OpenFont(font_path_.c_str(), point_size);
    if (font) {
        fonts_.emplace(point_size, font);
    }
    return font;
}

SDL_Texture *TextRenderer::render(SDL_Renderer *renderer, const std::string &text,
                                  SDL_Color color, float point_size) {
    const std::string key = std::to_string(point_size) + "|" + std::to_string(color.r) + "," +
                            std::to_string(color.g) + "," + std::to_string(color.b) + "," +
                            std::to_string(color.a) + "|" + text;
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        return it->second.texture;
    }

    TTF_Font *font = open_font(point_size);
    if (!font) {
        return nullptr;
    }
    SDL_Surface *surface =
        TTF_RenderText_Blended(font, text.c_str(), text.size(), color);
    if (!surface) {
        return nullptr;
    }
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    Entry entry{texture, static_cast<float>(surface->w), static_cast<float>(surface->h)};
    SDL_DestroySurface(surface);
    if (!texture) {
        return nullptr;
    }
    cache_.emplace(key, entry);
    return texture;
}

void TextRenderer::clear_cache() {
    for (auto &[key, entry] : cache_) {
        if (entry.texture) {
            SDL_DestroyTexture(entry.texture);
        }
    }
    cache_.clear();
}

void draw_text(SDL_Renderer *renderer, TextRenderer &text, const std::string &s, SDL_Color color,
               float point_size, float x, float y_center) {
    SDL_Texture *texture = text.render(renderer, s, color, point_size);
    if (!texture) {
        return;
    }
    float w = 0.0f, h = 0.0f;
    SDL_GetTextureSize(texture, &w, &h);
    SDL_FRect dst{x, y_center - h * 0.5f, w, h};
    SDL_RenderTexture(renderer, texture, nullptr, &dst);
}

} // namespace tianyi::ui
