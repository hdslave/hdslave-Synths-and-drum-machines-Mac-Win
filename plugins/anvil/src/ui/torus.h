// Orthographic wire torus for the right-hand 80 x 60 pixels of the masthead.
#pragma once

#include "print.h"
#include <array>

namespace hdui::torus
{
constexpr int frames = 160, frameMs = 40;
constexpr float tau = 6.28318530718f;
struct Angle { float c, s; };
struct Vertex { float x, y, z, facing; };

// In this part of the aperture the outgoing cloth descends behind the wire.
// Retain the coverage of nearer strands for the final fabric composite.
struct Outlet
{
    static constexpr int first = 30, end = 80;
    uint8_t wire[60][end - first] {};
};
inline Ink wireInk (int x, int y, float offset)
{
    return print::sampleInk (art::foil, x + .5f*y + offset);
}

using Coverage = uint8_t[60][80];
inline void wire (Coverage& mesh, Vertex a, Vertex b, float light, Outlet& outlet)
{
    if (light <= 0.f) return;
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float length2 = dx*dx + dy*dy;
    const float inverse = length2 > .0001f ? 1.f / length2 : 0.f;
    const int left = std::max (0, int (std::floor (std::min (a.x,b.x))));
    const int top = std::max (0, int (std::floor (std::min (a.y,b.y))));
    const int right = std::min (79, int (std::ceil (std::max (a.x,b.x))));
    const int bottom = std::min (59, int (std::ceil (std::max (a.y,b.y))));
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x)
        {
            const float t = std::clamp (((x-a.x)*dx + (y-a.y)*dy) * inverse, 0.f, 1.f);
            const float ex = x - (a.x+t*dx), ey = y - (a.y+t*dy);
            const float d2 = ex*ex + ey*ey;
            if (d2 >= 1.f) continue;
            const float coverage = (1.f - std::sqrt (d2)) * light;
            auto& pixel = mesh[y][x];
            pixel = std::max (pixel, uint8_t (255.f * coverage + .5f));
            if (x >= Outlet::first && x < Outlet::end)
            {
                const float clothZ = (42.f - x) * 3.f;
                const float wireZ = a.z + t*(b.z - a.z);
                const float inFront = std::clamp (wireZ - clothZ + .5f, 0.f, 1.f);
                auto& mask = outlet.wire[y][x - Outlet::first];
                mask = std::max (mask, uint8_t (255.f * coverage * inFront + .5f));
            }
        }
}

inline void draw (const Raster& r, float phase, Outlet& outlet, float inkOffset)
{
    Coverage mesh {};
    // Both wire families share these angles. The spin advances by two rib
    // spacings per cycle, so the last pose flows through the loop boundary.
    constexpr int samples = 80;
    static const auto circle = [] {
        std::array<Angle, samples> a {};
        for (int i = 0; i < samples; ++i)
            a[i] = { std::cos (i * tau / samples), std::sin (i * tau / samples) };
        return a;
    }();
    const float tilt = .95f + .22f * std::sin (phase);
    const float roll = -.46f + .12f * std::cos (phase);
    const Angle pitch { std::cos (tilt), std::sin (tilt) };
    const Angle bank { std::cos (roll), std::sin (roll) };
    const Angle spin { std::cos (phase / 8), std::sin (phase / 8) };
    std::array<Angle, samples> major {};
    for (int i = 0; i < samples; ++i)
        major[i] = { circle[i].c*spin.c - circle[i].s*spin.s, circle[i].s*spin.c + circle[i].c*spin.s };

    auto project = [&](Angle u, Angle v) -> Vertex {
        const float radius = 27.5f + 7.f*v.c;
        const float x = radius*u.c, y = radius*u.s*pitch.c - 7.f*v.s*pitch.s;
        return { 41.f + bank.c*x - bank.s*y, 29.5f + bank.s*x + bank.c*y,
                 radius*u.s*pitch.s + 7.f*v.s*pitch.c, u.s*v.c*pitch.s + v.s*pitch.c };
    };
    auto curve = [&](int count, auto vertex, bool contour = false) {
        Vertex a = vertex (0);
        for (int i = 1; i <= count; ++i)
        {
            const Vertex b = vertex (i % count);
            const float facing = (a.facing + b.facing) * .5f;
            const float light = contour ? .67f : std::clamp ((facing + .14f)*2.5f, 0.f, .82f);
            wire (mesh, a, b, light, outlet);
            a = b;
        }
    };
    for (int v = 0; v < samples; v += 10)
        curve (samples, [&](int i) { return project (major[i], circle[v]); });
    for (int u = 0; u < samples; u += 5)
        curve (20, [&](int i) { return project (major[u], circle[i*4]); });
    // At the two silhouettes the tube normal is perpendicular to the view.
    for (float side : {-1.f, 1.f})
        curve (samples, [&](int i) {
            const float s = -circle[i].s * pitch.s;
            const float scale = side / std::sqrt (pitch.c*pitch.c + s*s);
            return project (circle[i], {pitch.c*scale, s*scale});
        }, true);

    for (int y = 0; y < r.h; ++y)
        for (int x = 0; x < r.w; ++x)
            if (mesh[y][x])
            {
                auto& pixel = r.px[y*r.w+x];
                pixel = print::mix (pixel, wireInk (x, y, inkOffset), mesh[y][x]);
            }
}
} // namespace hdui::torus
