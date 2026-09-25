// Small SDL3 drawing helpers: rounded rectangles, image fitting, text.
#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <string>
#include <unordered_map>

namespace tianyi::ui {

// Filled rounded rectangle (approximated with a triangle fan per corner).
void fill_rounded_rect(SDL_Renderer *renderer, SDL_FRect rect, float radius, SDL_Color color);

// Single-line outline for a rounded rectangle.
void stroke_rounded_rect(SDL_Renderer *renderer, SDL_FRect rect, float radius, SDL_Color color,
                         float thickness = 1.0f);

// Destination rect for "cover" fitting (fills dst, crops overflow) and
// "contain" fitting (fits inside dst, letterboxes).
SDL_FRect fit_cover(SDL_FRect dst, float src_w, float src_h);
SDL_FRect fit_contain(SDL_FRect dst, float src_w, float src_h);

// Text rendering with a small texture cache (labels are few and stable).
class TextRenderer {
public:
    bool init(const std::string &font_path);
    void shutdown();

    // Returns a texture valid until clear_cache()/shutdown(), plus its size.
    SDL_Texture *render(SDL_Renderer *renderer, const std::string &text, SDL_Color color,
                        float point_size);
    void clear_cache();

private:
    struct Entry {
        SDL_Texture *texture = nullptr;
        float w = 0.0f;
        float h = 0.0f;
    };

    TTF_Font *open_font(float point_size);

    std::unordered_map<std::string, Entry> cache_;
    std::unordered_map<float, TTF_Font *> fonts_;
    std::string font_path_;
};

// Convenience: draw a cached string at (x, y) with vertical centering.
void draw_text(SDL_Renderer *renderer, TextRenderer &text, const std::string &s, SDL_Color color,
               float point_size, float x, float y_center);

} // namespace tianyi::ui
