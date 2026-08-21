#pragma once

struct ImFont;

namespace menu {
    inline ImFont* font_body = nullptr;
    inline ImFont* font_title = nullptr;
    inline ImFont* font_hero = nullptr;
    inline ImFont* font_small = nullptr;

    void setup_fonts();
    void render();
    bool capturing_bind();
    bool poll_bind_capture();
}
