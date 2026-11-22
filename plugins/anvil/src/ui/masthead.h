// Print the flowing checker fabric and chrome over the rotating wire mesh.
#pragma once

#include "torus.h"
#include <iterator>

namespace hdui::masthead
{
constexpr int frames = 2 * torus::frames;

// The screen clock gives the tip long, uneven spells of plain chrome.
constexpr int glintFrames = 16;
constexpr int glintFrame (uint64_t tick)
{
    const int at = int (tick % (68000 / torus::frameMs));
    for (int startMs : {6000, 25000, 49000})
    {
        const int age = at - startMs / torus::frameMs;
        if (age >= 0 && age < glintFrames) return age;
    }
    return -1;
}

inline float glintPulse (float age, float peak, float end)
{
    const float t = std::clamp (std::min (age/peak, (end-age)/(end-peak)), 0.f, 1.f);
    return t*t*(3.f-2.f*t);
}

inline void drawGlint (const Raster& r, int frame)
{
    if (frame < 0) return;
    // The long facet catches first. The short facet lingers as it closes.
    const float age = frame + .5f;
    const float core = glintPulse (age, 3.f, glintFrames);
    const float horizontal = glintPulse (age, 4.f, 12.f);
    const float vertical = glintPulse (age-1.f, 4.5f, 15.f);
    constexpr int tipX = 175 - 160, tipY = 17; // Ring tile, upper serif of the L.
    for (int y = -7; y <= 7; ++y)
        for (int x = -8; x <= 8; ++x)
        {
            const float u = std::abs (x - .11f*y), v = std::abs (y + .11f*x);
            const float pin = core * std::max (0.f, 1.f - (u+v)/(1.f+1.8f*core));
            const float rayX = horizontal * std::max (0.f, 1.f-u/(1.f+7.5f*horizontal)) * std::max (0.f, 1.f-v);
            const float rayY = vertical * std::max (0.f, 1.f-v/(1.f+5.8f*vertical)) * std::max (0.f, 1.f-u);
            const float light = std::max ({pin, rayX, rayY});
            const unsigned alpha = unsigned (256.f * light);
            if (!alpha) continue;
            auto& pixel = r.px[(tipY+y)*r.w + tipX+x];
            pixel = print::mix (pixel, 0xfffffcfa, alpha);
        }
}

inline Ink printedPixel (int x, int y)
{
    const auto pair = art::masthead[y][x / 2];
    return art::inks[x % 2 ? pair & 15 : pair >> 4];
}

// Filter a repeating light/dark pair over one display pixel.
inline float stripe (float coordinate, float footprint)
{
    const float local = coordinate - 2.f * std::floor (coordinate * .5f);
    const float distance = .5f - std::abs (local - 1.f);
    return std::clamp (.5f + distance / footprint, 0.f, 1.f);
}

inline void ribbonColumn (const Raster& r, int dx, int x, art::RibbonSpan span, float phase, float travel, float inkOffset, const torus::Outlet* outlet)
{
    // One travelling wave along the cloth, including the lengths hidden by the
    // lettering. The crest at the ring follows its tilt. A small change in
    // thickness lets each fold roll as it rises.
    const float along = phase + (200 - x) * .044f;
    const float gain = span.gain / 255.f;
    const float wave = std::sin (along), tilt = std::cos (along);
    const float lift = 3.4f * gain * wave;
    const float stretch = 1.f + .085f * gain * tilt;
    const float mid = (span.top + span.end - 1) * .5f;
    const float inverse = 1.f / stretch;
    const float upper = span.edgeTop16 / 16.f, lower = span.edgeBottom16 / 16.f;
    const float invHeight = 1.f / (lower - upper);
    // The cross-grain follows the normal of the waving centreline. Moving
    // the print advances only its length coordinate, keeping the rows on the fold.
    const float slope = span.slope128 / 128.f + 3.4f*(span.gainSlope1024 / 1024.f * wave - .044f*gain*tilt);
    const float du = span.du256 / 256.f;
    // Bank the outgoing fold about its centreline. The near hem projects
    // slightly along the ribbon as it turns toward the viewer.
    const float offset = (x - 249.f - 5.f*tilt) / 43.f;
    const float envelope = std::max (0.f, 1.f - offset*offset);
    const float tanRoll = envelope*envelope*(.88f + .10f*wave);
    const float inverseWidth = std::sqrt (1.f + tanRoll*tanRoll);
    const float widthScale = 1.f / inverseWidth, depth = tanRoll * widthScale;
    const float across = du * slope / (1.f + slope*slope) - .30f*du*tanRoll;
    const float centre = (upper + lower) * .5f;
    const float lengthwise = span.u256 / 256.f - travel;
    const float uFilter = du + std::abs (across);
    const float vFilter = art::checkerRows * invHeight * (1.f + std::abs (slope)) * inverseWidth;
    const float faceWidth = (lower-upper)*stretch*du / (art::checkerRows*(1.f+slope*slope)) * widthScale;
    const float facing = .64f + .28f*std::min (1.f, faceWidth) - .07f*slope;
    const Ink face = print::sampleInk (art::fabric, x + inkOffset);
    const Ink reverse = print::mix (ink::bg, art::inks[2], 55);
    const Ink edgeInk = print::mix (art::inks[3], art::inks[5], 96);
    const int top = std::max (0, int (span.top) - 6), end = std::min (56, int (span.end) + 6);
    for (int y = top; y < end; ++y)
    {
        // Recover a point on the unwarped cloth, then slide the print along it.
        const float free = std::min (1.f, y / 12.f);
        const float flat = y + free * ((y - mid) * (inverse - 1.f) - lift * inverse);
        const float source = flat + (flat - centre)*(inverseWidth - 1.f);
        const float edge = std::min (source - upper, lower - source) * widthScale;
        const float cut = std::min (source - span.top + 1.f, span.end - source);
        const float alpha = std::clamp (std::min (cut, edge + .5f), 0.f, 1.f);
        if (alpha == 0.f) continue;

        const float v = (source - upper) * invHeight;
        const float u = lengthwise + (flat - centre)*stretch*across;
        const float column = stripe (u, uFilter), row = stripe (v * art::checkerRows + .5f, vFilter);
        const float checks = row * column + (1.f - row) * (1.f - column);
        const float rim = 1.f - std::clamp (edge, 0.f, 1.f);
        const Ink dark = print::mix (reverse, edgeInk, unsigned (14 + 160*rim));
        const float shade = std::clamp (facing + (.16f - .65f*depth)*(.5f - v), 0.f, 1.f);
        const Ink light = print::mix (edgeInk, face, unsigned (256 * shade));
        Ink cloth = print::mix (dark, light, unsigned (checks * 256));
        const float hem = rim * std::clamp (2.f*v - 1.f, 0.f, 1.f);
        cloth = print::mix (cloth, art::inks[5], unsigned (76.f * depth * hem));
        if (outlet && dx >= torus::Outlet::first && dx < torus::Outlet::end)
            cloth = print::mix (cloth, torus::wireInk (dx, y + 3, inkOffset), outlet->wire[y + 3][dx - torus::Outlet::first]);
        const auto under = r.px[(y + 3)*r.w + dx];
        r.dot (dx, y + 3, print::mix (under, cloth, unsigned (alpha * 256)));
    }
}

inline void draw (const Raster& r, int origin, float phase, float travel, float inkOffset, const torus::Outlet* outlet = nullptr)
{
    const int first = std::max (0, origin - 1);
    const int end = std::min (int (std::size (art::ribbon)), origin + r.w - 1);
    for (int x = first; x < end; ++x)
    {
        const int dx = x + 1 - origin;
        const auto span = art::ribbon[x];
        if (span.end) ribbonColumn (r, dx, x, span, phase, travel, inkOffset, outlet);
        if (x >= int (2 * std::size (art::masthead[0]))) continue;
        for (int y = 0; y < int (std::size (art::masthead)); ++y)
        {
            if (y >= span.top && y < span.end) continue;
            const int bit = x - 159;
            if (bit >= 0 && !(art::torusCover[y][bit / 8] & (1 << (bit % 8)))) continue;
            const auto colour = printedPixel (x, y);
            if (bit >= 0 || colour != ink::bg) r.dot (dx, y + 3, colour);
        }
    }
    r.box (4 - origin, 59, 236, 1, ink::rule);
}
} // namespace hdui::masthead
