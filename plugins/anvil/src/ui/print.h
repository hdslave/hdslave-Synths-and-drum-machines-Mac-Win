// Silver and violet inks and the moving contour plate behind the Anvil lettering.
#pragma once

#include "page.h"
#include <array>

namespace hdui::print
{
constexpr Ink mix (Ink a, Ink b, unsigned weight)
{
    constexpr Ink rb = 0x00ff00ff, g = 0x0000ff00;
    const unsigned other = 256 - weight;
    return 0xff000000 | ((((a & rb)*other + (b & rb)*weight) >> 8) & rb)
                      | ((((a & g)*other + (b & g)*weight) >> 8) & g);
}

struct Drift
{
    int left, top, wx, wy;
    float inkOffset;

    explicit Drift (float phase)
    {
        // The plate follows a small ellipse. Its generated margin covers the
        // full excursion plus the next sample used for subpixel interpolation.
        const float x = 4.f + 3.1f*std::sin (phase);
        const float y = 3.f + 2.2f*std::cos (phase);
        left = int (x);
        top = int (y);
        wx = int (256*(x-left));
        wy = int (256*(y-top));
        inkOffset = 10.f*std::sin (phase - .55f);
    }
};

inline void underprint (const Raster& r, int origin, const Drift& drift)
{
    static constexpr auto inks = [] {
        std::array<Ink,193> ramp {};
        for (unsigned i = 0; i < ramp.size(); ++i)
            ramp[i] = mix (ink::bg, art::inks[2], i*256/192);
        return ramp;
    }();
    auto level = [](const uint8_t* row, int x) { return (row[x/4] >> (2*(x%4))) & 3; };
    for (int y = 0; y < 59; ++y)
    {
        const auto* a = art::contours[y + drift.top];
        const auto* b = art::contours[y + drift.top + 1];
        for (int x = 0; x < r.w; ++x)
        {
            const int u = origin + x + drift.left;
            const int q = level (a,u)*(256-drift.wx) + level (a,u+1)*drift.wx;
            const int s = level (b,u)*(256-drift.wx) + level (b,u+1)*drift.wx;
            r.px[y*r.w+x] = inks[(q*(256-drift.wy) + s*drift.wy + 512) >> 10];
        }
    }
}

template<size_t N>
inline Ink sampleInk (const Ink (&ramp)[N], float position)
{
    position = std::clamp (position, 0.f, float (N-1));
    const int left = int (position);
    return mix (ramp[left], ramp[std::min (left+1, int (N-1))], unsigned (256*(position-left)));
}
} // namespace hdui::print
