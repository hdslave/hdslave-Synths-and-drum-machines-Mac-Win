#pragma once

#include <cstdint>
#include <cstring>

namespace hdslave_desktop
{

struct PluginTheme
{
    uint32_t bg;          // Main chassis background (0xFFRRGGBB)
    uint32_t panelBg;     // Module card background
    uint32_t panelBorder; // Module card border
    uint32_t headerBg;    // Top bar background
    uint32_t accent;      // Primary glowing accent
    uint32_t accentDim;   // Muted accent for tracks/borders
    uint32_t textPrimary; // Main labels
    uint32_t textMuted;   // Secondary labels / units
    uint32_t midiBadge;   // MIDI CC badge highlight
};

inline PluginTheme themeForPluginId (const char* pluginId)
{
    // Base dark studio hardware chassis
    PluginTheme t {
        0xFF111318u, // bg
        0xFF191D26u, // panelBg
        0xFF2B3140u, // panelBorder
        0xFF0D0F14u, // headerBg
        0xFFFF8A2Bu, // accent (default warm amber)
        0xFF5A3316u, // accentDim
        0xFFEAEEF6u, // textPrimary
        0xFF8C96AAu, // textMuted
        0xFF2ED8FFu, // midiBadge
    };

    if (!pluginId)
        return t;

    if (std::strstr (pluginId, "anvil"))
    {
        t.accent = 0xFFFF6B2Bu; // Industrial forge orange
        t.accentDim = 0xFF5E2812u;
    }
    else if (std::strstr (pluginId, "cortex"))
    {
        t.accent = 0xFF26E6D8u; // Spectral cyan
        t.accentDim = 0xFF115452u;
    }
    else if (std::strstr (pluginId, "foundry"))
    {
        t.accent = 0xFFFF9F1Cu; // Foundry molten amber
        t.accentDim = 0xFF5C3A0Cu;
    }
    else if (std::strstr (pluginId, "machine"))
    {
        t.accent = 0xFF4D96FFu; // Cobalt blue
        t.accentDim = 0xFF1B3660u;
    }
    else if (std::strstr (pluginId, "moonbass"))
    {
        t.accent = 0xFF6B8CFFu; // Sub-bass lunar blue
        t.accentDim = 0xFF24315Eu;
    }
    else if (std::strstr (pluginId, "mystik"))
    {
        t.accent = 0xFFF2C14Eu; // Warm bronze gold
        t.accentDim = 0xFF59461Cu;
    }
    else if (std::strstr (pluginId, "necronomicon"))
    {
        t.accent = 0xFFE63956u; // Crimson subharmonic red
        t.accentDim = 0xFF571723u;
    }
    else if (std::strstr (pluginId, "nexus"))
    {
        t.accent = 0xFF2CEAA3u; // Emerald teal
        t.accentDim = 0xFF12543Bu;
    }
    else if (std::strstr (pluginId, "psychic"))
    {
        t.accent = 0xFFE056FDu; // Generative magenta
        t.accentDim = 0xFF521E5Eu;
    }
    else if (std::strstr (pluginId, "reverie"))
    {
        t.accent = 0xFF9B7BFFu; // Dreamy violet
        t.accentDim = 0xFF392B63u;
    }
    else if (std::strstr (pluginId, "tb") || std::strstr (pluginId, "3p0"))
    {
        t.accent = 0xFF7CFF3Au; // Acid neon green
        t.accentDim = 0xFF2C5C16u;
    }

    return t;
}

} // namespace hdslave_desktop
