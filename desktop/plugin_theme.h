// Per plug-in palettes for the desktop panel, taken from each plug-in's own MPC page (Previews/Plugin pages): deep
// purple-black grounds, a neon accent, lavender text, and the purple header strip every page shares.
#pragma once

#include <cstdint>
#include <cstring>

namespace hdslave_desktop
{

struct PluginTheme
{
    uint32_t bgTop;       // window ground, top of the vertical gradient
    uint32_t bgBottom;    // window ground, bottom
    uint32_t panel;       // section cards
    uint32_t panelHi;     // card header strip / hovered cells
    uint32_t border;      // card and control outlines
    uint32_t headerTop;   // the top bar's gradient (the pages' purple strip)
    uint32_t headerBottom;
    uint32_t accent;      // lit LEDs, tags, values
    uint32_t accent2;     // second colour (bipolar fills, highlights)
    uint32_t onAccent;    // text drawn on the accent
    uint32_t text;        // labels
    uint32_t muted;       // secondary labels, unlit segments
    uint32_t screenBezel; // frame around the live page
    uint32_t learn;       // MIDI learn
};

inline PluginTheme themeForPluginId (const char* id)
{
    // The shared base: night purple.
    PluginTheme t {
        0xFF1A1030u, 0xFF0B0716u, 0xFF170F2Au, 0xFF221641u, 0xFF3A2A62u,
        0xFF3B2470u, 0xFF1A1036u,
        0xFFB8F23Au, 0xFF8F7BFFu, 0xFF0D0818u,
        0xFFE6DEFFu, 0xFF8C80B4u, 0xFF05030Au, 0xFFFF3B6Bu,
    };
    if (id == nullptr)
        return t;

    auto set = [&] (uint32_t accent, uint32_t accent2, uint32_t bgTop, uint32_t bgBottom, uint32_t panel, uint32_t panelHi, uint32_t border) {
        t.accent = accent;
        t.accent2 = accent2;
        t.bgTop = bgTop;
        t.bgBottom = bgBottom;
        t.panel = panel;
        t.panelHi = panelHi;
        t.border = border;
    };

    if (std::strstr (id, "anvil"))             set (0xFFB8F23Au, 0xFFB57BFFu, 0xFF1C1033u, 0xFF0B0716u, 0xFF180F2Cu, 0xFF241643u, 0xFF3C2A66u);
    else if (std::strstr (id, "cortex"))       set (0xFFD38CFFu, 0xFF7FE8FFu, 0xFF1A0E33u, 0xFF0A0618u, 0xFF170D2Eu, 0xFF241447u, 0xFF43297Au);
    else if (std::strstr (id, "foundry"))      set (0xFF59D9F5u, 0xFFB8F23Au, 0xFF101A38u, 0xFF070B1Au, 0xFF0F1731u, 0xFF19244Au, 0xFF2A3D70u);
    else if (std::strstr (id, "machine"))      set (0xFFB48CFFu, 0xFF59D9F5u, 0xFF1A1036u, 0xFF0A0718u, 0xFF170F2Eu, 0xFF231646u, 0xFF3D2B70u);
    else if (std::strstr (id, "moonbass"))     set (0xFFFF7A2Eu, 0xFFFFC27Au, 0xFF141533u, 0xFF080917u, 0xFF14142Bu, 0xFF1F1F42u, 0xFF34346Au);
    else if (std::strstr (id, "mystik"))       set (0xFFCDF545u, 0xFFFF4FA8u, 0xFF1B0F33u, 0xFF0A0617u, 0xFF180E2Du, 0xFF251545u, 0xFF3E2A6Au);
    else if (std::strstr (id, "necronomicon")) set (0xFFFF9A3Cu, 0xFFC9B8FFu, 0xFF1D1036u, 0xFF0B0618u, 0xFF1A0F30u, 0xFF28184Au, 0xFF4A3480u);
    else if (std::strstr (id, "nexus"))        set (0xFFE9F53Cu, 0xFFB48CFFu, 0xFF1C1034u, 0xFF0A0617u, 0xFF190F2Eu, 0xFF261747u, 0xFF3E2C6Cu);
    else if (std::strstr (id, "psychic"))      set (0xFF5CCBFFu, 0xFFD8F04Au, 0xFF161236u, 0xFF080718u, 0xFF15112Eu, 0xFF201A48u, 0xFF352E70u);
    else if (std::strstr (id, "reverie"))      set (0xFF7FE6E0u, 0xFFFFA24Au, 0xFF0F2238u, 0xFF06101Cu, 0xFF0E1E33u, 0xFF16304Cu, 0xFF2A4C70u);
    else if (std::strstr (id, "tbmpc") || std::strstr (id, "tb-mpc") || std::strstr (id, "3p0") || std::strstr (id, "tb_mpc"))
                                               set (0xFFD8F03Au, 0xFF3C5CFFu, 0xFF111432u, 0xFF070816u, 0xFF12142Cu, 0xFF1C1F45u, 0xFF2F3570u);
    return t;
}

} // namespace hdslave_desktop
