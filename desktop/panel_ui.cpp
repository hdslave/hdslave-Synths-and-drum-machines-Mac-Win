#include "panel_ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace hdslave_desktop
{

namespace
{
constexpr int kHeaderH = 54;
constexpr int kBankBarH = 30;
constexpr int kMargin = 10;
constexpr int kCardGap = 10;
constexpr int kCardHeaderH = 22;
constexpr int kCellW = 94;
constexpr int kCellH = 92;
constexpr int kCardPadX = 8;
constexpr int kCardPadY = 8;
constexpr int kScrollbarW = 10;

std::string toUpper (std::string s)
{
    for (char& ch : s)
    {
        if (ch >= 'a' && ch <= 'z')
            ch = char (ch - 'a' + 'A');
    }
    return s;
}

std::string shortModuleName (const std::string& raw)
{
    if (raw.empty()) return "MAIN";
    std::string out = raw;
    // Replace '/' with ' / '
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] == '_') out[i] = ' ';
    }
    return toUpper (out);
}
} // namespace

PanelUI::PanelUI (DesktopPlugin* plugin) : plugin_ (plugin)
{
    rebuildLayout (int (kDefaultGuiWidth), int (kDefaultGuiHeight));
}

void PanelUI::clampScroll()
{
    maxScrollY_ = std::max (0, contentHeight_ - workspaceRect_.h);
    scrollY_ = std::clamp (scrollY_, 0, maxScrollY_);
    if (maxScrollY_ > 0 && scrollbarTrackRect_.h > 0)
    {
        int thumbH = std::max (28, (workspaceRect_.h * scrollbarTrackRect_.h) / std::max (1, contentHeight_));
        int travel = std::max (1, scrollbarTrackRect_.h - thumbH);
        int thumbY = scrollbarTrackRect_.y + (scrollY_ * travel) / maxScrollY_;
        scrollbarThumbRect_ = { scrollbarTrackRect_.x, thumbY, scrollbarTrackRect_.w, thumbH };
    }
    else
    {
        scrollbarThumbRect_ = {};
    }
}

void PanelUI::rebuildLayout (int width, int height)
{
    width_ = std::max (640, width);
    height_ = std::max (400, height);

    headerRect_ = { 0, 0, width_, kHeaderH };

    // Header controls
    int px = 270;
    presetPrevRect_ = { px, 12, 28, 30 };
    presetBoxRect_ = { px + 32, 12, 236, 30 };
    presetNextRect_ = { px + 272, 12, 28, 30 };

    int mx = px + 318;
    midiLearnRect_ = { mx, 12, 118, 30 };
    clearCcRect_ = { mx + 126, 12, 112, 30 };

    // Group parameters by module while preserving declaration order
    struct ModuleGroup
    {
        std::string title;
        std::vector<int> paramIndices;
    };
    std::vector<ModuleGroup> groups;
    std::unordered_map<std::string, size_t> groupMap;

    for (size_t i = 0; i < plugin_->params.size(); ++i)
    {
        std::string mod = shortModuleName (plugin_->params[i].module);
        auto it = groupMap.find (mod);
        if (it == groupMap.end())
        {
            groupMap[mod] = groups.size();
            groups.push_back ({ mod, { int (i) } });
        }
        else
        {
            groups[it->second].paramIndices.push_back (int (i));
        }
    }

    // Create banks only if there are many modules (> 10), so giant plugins like Foundry (16 voices + FX)
    // can show ALL or jump straight to a clean bank.
    bankTabs_.clear();
    const bool useBanks = groups.size() > 10;
    int topOffset = kHeaderH;
    if (useBanks)
    {
        bankBarRect_ = { 0, kHeaderH, width_, kBankBarH };
        topOffset += kBankBarH;

        const size_t modsPerBank = 6;
        numBanks_ = 1 + int ((groups.size() + modsPerBank - 1) / modsPerBank);
        if (activeBank_ >= numBanks_) activeBank_ = 0;

        int tx = kMargin;
        for (int b = 0; b < numBanks_; ++b)
        {
            std::string label;
            if (b == 0)
            {
                label = "ALL MODULES";
            }
            else
            {
                size_t first = size_t (b - 1) * modsPerBank;
                size_t last = std::min (groups.size(), first + modsPerBank) - 1;
                label = groups[first].title.substr (0, 6) + " - " + groups[last].title.substr (0, 6);
            }
            int tw = std::max (86, Canvas::textWidth (label, 1) + 22);
            bankTabs_.push_back ({ label, { tx, kHeaderH + 4, tw, kBankBarH - 8 } });
            tx += tw + 6;
        }
    }
    else
    {
        bankBarRect_ = {};
        numBanks_ = 1;
        activeBank_ = 0;
    }

    workspaceRect_ = { 0, topOffset, width_ - kScrollbarW - 4, height_ - topOffset };
    scrollbarTrackRect_ = { width_ - kScrollbarW - 2, topOffset + 6, kScrollbarW, workspaceRect_.h - 12 };

    // Shelf-pack the live screen card + module cards into the expanded workspace
    cards_.clear();
    const int availW = workspaceRect_.w - 2 * kMargin;
    int cursorX = kMargin;
    int cursorY = kMargin;
    int rowH = 0;

    // 1. Live MPC Display Card (always top-left if the plugin provides CLAP_EXT_MELODEE_SCREEN)
    if (plugin_->innerScreen)
    {
        const int cardW = MELODEE_SCREEN_WIDTH + 16;  // 336
        const int cardH = MELODEE_SCREEN_HEIGHT + kCardHeaderH + 14; // 254
        screenCardRect_ = { cursorX, cursorY, cardW, cardH };
        screenImageRect_ = { cursorX + 8, cursorY + kCardHeaderH + 6, MELODEE_SCREEN_WIDTH, MELODEE_SCREEN_HEIGHT };
        cursorX += cardW + kCardGap;
        rowH = std::max (rowH, cardH);
    }
    else
    {
        screenCardRect_ = {};
        screenImageRect_ = {};
    }

    // 2. Pack all active Module Cards
    const int maxColsFit = std::max (1, (availW - 2 * kCardPadX) / kCellW);

    for (size_t gIdx = 0; gIdx < groups.size(); ++gIdx)
    {
        int bankForGroup = useBanks ? (1 + int (gIdx / 6)) : 0;
        if (useBanks && activeBank_ != 0 && bankForGroup != activeBank_)
            continue;

        const auto& grp = groups[gIdx];
        const int n = int (grp.paramIndices.size());

        // Choose a pleasing hardware module aspect ratio
        int cols = n;
        if (n <= 4) cols = n;
        else if (n <= 8) cols = std::min (n, 4);
        else if (n <= 12) cols = 6;
        else if (n <= 16) cols = 8;
        else cols = std::min (10, maxColsFit);

        cols = std::clamp (cols, 1, maxColsFit);
        int rows = (n + cols - 1) / cols;

        int cardW = cols * kCellW + 2 * kCardPadX;
        int cardH = kCardHeaderH + rows * kCellH + 2 * kCardPadY;

        // Wrap to next shelf if it doesn't fit on current row
        if (cursorX > kMargin && cursorX + cardW > kMargin + availW)
        {
            cursorX = kMargin;
            cursorY += rowH + kCardGap;
            rowH = 0;
        }

        ModuleCard card;
        card.title = grp.title;
        card.bankIndex = bankForGroup;
        card.bounds = { cursorX, cursorY, cardW, cardH };

        for (int i = 0; i < n; ++i)
        {
            int col = i % cols;
            int row = i / cols;
            int cx = card.bounds.x + kCardPadX + col * kCellW;
            int cy = card.bounds.y + kCardHeaderH + kCardPadY + row * kCellH;

            ControlWidget w;
            w.paramIndex = grp.paramIndices[size_t (i)];
            w.paramId = plugin_->params[size_t (w.paramIndex)].id;
            w.bounds = { cx, cy, kCellW, kCellH };
            w.knobRect = { cx + (kCellW - 44) / 2, cy + 20, 44, 44 };
            card.controls.push_back (w);
        }

        cards_.push_back (std::move (card));
        cursorX += cardW + kCardGap;
        rowH = std::max (rowH, cardH);
    }

    contentHeight_ = cursorY + rowH + kMargin;
    clampScroll();
}

int PanelUI::hitTestControl (int x, int y) const
{
    if (!workspaceRect_.contains (x, y)) return -1;
    int wy = y - workspaceRect_.y + scrollY_;
    int wx = x - workspaceRect_.x;

    for (const auto& card : cards_)
    {
        if (!card.bounds.contains (wx, wy)) continue;
        for (const auto& ctrl : card.controls)
        {
            if (ctrl.bounds.contains (wx, wy))
                return ctrl.paramIndex;
        }
    }
    return -1;
}

void PanelUI::renderHeader (Canvas& c)
{
    const auto& th = plugin_->theme;
    c.fillRect (headerRect_.x, headerRect_.y, headerRect_.w, headerRect_.h, th.headerBg);
    c.fillRect (0, kHeaderH - 2, width_, 2, th.accentDim);
    // Left vertical accent stripe
    c.fillRect (0, 0, 5, kHeaderH - 2, th.accent);

    // Plugin title & version
    std::string title = toUpper (plugin_->pluginName);
    c.drawText (16, 12, title, th.textPrimary, 2, 240);
    std::string sub = "DESKTOP CLAP  v" + plugin_->pluginVersion;
    c.drawText (16, 33, sub, th.textMuted, 1, 240);

    // Preset Prev / Box / Next
    auto drawBtn = [&] (const Rect& r, const std::string& label, bool active, uint32_t activeCol) {
        c.fillRoundedRect (r.x, r.y, r.w, r.h, 5, active ? activeCol : 0xFF1F2430u);
        c.drawRoundedRect (r.x, r.y, r.w, r.h, 5, active ? Canvas::brighten (activeCol, 30) : th.panelBorder);
        c.drawTextCentered (r.x + r.w / 2, r.y + (r.h - 7) / 2, label, active ? 0xFF0A0C10u : th.textPrimary, 1, r.w - 8);
    };

    drawBtn (presetPrevRect_, "<", false, th.accent);
    drawBtn (presetNextRect_, ">", false, th.accent);

    std::string presetLabel = "INIT / CUSTOM";
    if (!plugin_->presets.empty())
    {
        if (plugin_->currentPresetIndex >= 0 && plugin_->currentPresetIndex < int (plugin_->presets.size()))
        {
            char buf[96];
            std::snprintf (buf, sizeof (buf), "%02d/%02d %s", plugin_->currentPresetIndex + 1,
                           int (plugin_->presets.size()), plugin_->presets[size_t (plugin_->currentPresetIndex)].name.c_str());
            presetLabel = buf;
        }
        else
        {
            char buf[64];
            std::snprintf (buf, sizeof (buf), "PRESETS (%d) v", int (plugin_->presets.size()));
            presetLabel = buf;
        }
    }
    c.fillRoundedRect (presetBoxRect_.x, presetBoxRect_.y, presetBoxRect_.w, presetBoxRect_.h, 5,
                       presetMenuOpen_ ? 0xFF283044u : 0xFF1A1F2Bu);
    c.drawRoundedRect (presetBoxRect_.x, presetBoxRect_.y, presetBoxRect_.w, presetBoxRect_.h, 5,
                       presetMenuOpen_ ? th.accent : th.panelBorder);
    c.drawText (presetBoxRect_.x + 10, presetBoxRect_.y + 11, toUpper (presetLabel), th.textPrimary, 1, presetBoxRect_.w - 20);

    // MIDI Learn button
    const uint32_t learnTarget = plugin_->midiLearnTarget.load (std::memory_order_relaxed);
    const bool learning = (learnTarget != CLAP_INVALID_ID);
    drawBtn (midiLearnRect_, learning ? "LEARNING..." : "MIDI LEARN", learning, 0xFFFF3B5Cu);

    // Clear CC button
    int mappedCount = plugin_->countMappedCc();
    char ccBuf[32];
    std::snprintf (ccBuf, sizeof (ccBuf), "CLR CC (%d)", mappedCount);
    drawBtn (clearCcRect_, ccBuf, false, th.midiBadge);

    // Right-side live readout / MIDI Learn prompt
    int rx = clearCcRect_.x + clearCcRect_.w + 18;
    int rw = width_ - rx - 14;
    if (rw > 80)
    {
        c.fillRoundedRect (rx, 10, rw, 34, 5, 0xFF151922u);
        c.drawRoundedRect (rx, 10, rw, 34, 5, learning ? 0xFFFF3B5Cu : th.panelBorder);

        if (learning)
        {
            if (learnTarget == DesktopPlugin::kLearnArmedNoParam)
            {
                c.drawText (rx + 10, 16, "MIDI LEARN ARMED: CLICK ANY CONTROL", 0xFFFF5E78u, 1, rw - 20);
                c.drawText (rx + 10, 29, "OR RIGHT-CLICK ANY KNOB TO MAP CC", th.textMuted, 1, rw - 20);
            }
            else
            {
                int idx = plugin_->findParamIndex (learnTarget);
                std::string pname = (idx >= 0) ? toUpper (plugin_->params[size_t (idx)].name) : "PARAM";
                c.drawText (rx + 10, 16, "MOVE HARDWARE MIDI CC FOR:", 0xFFFF5E78u, 1, rw - 20);
                c.drawText (rx + 10, 29, pname, th.textPrimary, 1, rw - 20);
            }
        }
        else
        {
            int showIdx = (draggingParamIdx_ >= 0) ? draggingParamIdx_
                        : (hoveredParamIdx_ >= 0) ? hoveredParamIdx_
                        : lastTweakedParamIdx_;
            if (showIdx >= 0 && showIdx < int (plugin_->params.size()))
            {
                const auto& p = plugin_->params[size_t (showIdx)];
                double val = plugin_->getParamValue (p.id);
                std::string valStr = plugin_->formatParamValue (p.id, val);
                int cc = plugin_->findCcForParam (p.id);
                std::string topLine = shortModuleName (p.module) + " / " + toUpper (p.name);
                if (cc >= 0) topLine += "  [CC " + std::to_string (cc) + "]";
                c.drawText (rx + 10, 15, topLine, th.textMuted, 1, rw - 20);
                c.drawText (rx + 10, 28, toUpper (valStr), th.accent, 1, rw - 20);
            }
            else
            {
                c.drawText (rx + 10, 15, "DRAG: ADJUST | SHIFT: FINE | DBL-CLICK: RESET", th.textMuted, 1, rw - 20);
                c.drawText (rx + 10, 28, "RIGHT-CLICK ANY CONTROL FOR MIDI LEARN", th.midiBadge, 1, rw - 20);
            }
        }
    }
}

void PanelUI::renderBankBar (Canvas& c)
{
    if (bankTabs_.empty()) return;
    const auto& th = plugin_->theme;
    c.fillRect (bankBarRect_.x, bankBarRect_.y, bankBarRect_.w, bankBarRect_.h, 0xFF13161Eu);
    c.fillRect (0, bankBarRect_.y + bankBarRect_.h - 1, width_, 1, th.panelBorder);

    for (size_t i = 0; i < bankTabs_.size(); ++i)
    {
        const bool active = (int (i) == activeBank_);
        const Rect& r = bankTabs_[i].rect;
        c.fillRoundedRect (r.x, r.y, r.w, r.h, 4, active ? th.accent : 0xFF1D2230u);
        c.drawRoundedRect (r.x, r.y, r.w, r.h, 4, active ? th.accent : th.panelBorder);
        c.drawTextCentered (r.x + r.w / 2, r.y + (r.h - 7) / 2, bankTabs_[i].label,
                            active ? 0xFF0A0C10u : th.textPrimary, 1, r.w - 8);
    }
}

void PanelUI::renderWorkspace (Canvas& c)
{
    const auto& th = plugin_->theme;
    const int wy0 = workspaceRect_.y;
    const int wy1 = workspaceRect_.y + workspaceRect_.h;

    const uint32_t learnTarget = plugin_->midiLearnTarget.load (std::memory_order_relaxed);

    // 1. Live embedded MPC display card
    if (plugin_->innerScreen && screenCardRect_.w > 0)
    {
        int sx = workspaceRect_.x + screenCardRect_.x;
        int sy = workspaceRect_.y + screenCardRect_.y - scrollY_;
        if (sy + screenCardRect_.h >= wy0 && sy < wy1)
        {
            c.fillRoundedRect (sx, sy, screenCardRect_.w, screenCardRect_.h, 7, th.panelBg);
            c.drawRoundedRect (sx, sy, screenCardRect_.w, screenCardRect_.h, 7, th.accentDim);
            c.fillRect (sx + 1, sy + 1, screenCardRect_.w - 2, kCardHeaderH, 0xFF141821u);
            c.fillRect (sx + 1, sy + kCardHeaderH, screenCardRect_.w - 2, 1, th.panelBorder);
            c.drawText (sx + 10, sy + 8, "LIVE MONITOR  (CLICK / SCROLL)", th.accent, 1, screenCardRect_.w - 20);

            int ix = workspaceRect_.x + screenImageRect_.x;
            int iy = workspaceRect_.y + screenImageRect_.y - scrollY_;
            c.drawRect (ix - 1, iy - 1, MELODEE_SCREEN_WIDTH + 2, MELODEE_SCREEN_HEIGHT + 2, 0xFF050608u);
            if (plugin_->screenPixels.size() == size_t (MELODEE_SCREEN_WIDTH * MELODEE_SCREEN_HEIGHT))
            {
                // Clip blit to workspace vertical bounds
                for (int row = 0; row < MELODEE_SCREEN_HEIGHT; ++row)
                {
                    int dy = iy + row;
                    if (dy < wy0 || dy >= wy1) continue;
                    const uint32_t* srcRow = plugin_->screenPixels.data() + row * MELODEE_SCREEN_WIDTH;
                    uint32_t* dstRow = c.pixels + dy * c.width + ix;
                    for (int col = 0; col < MELODEE_SCREEN_WIDTH; ++col)
                    {
                        if (ix + col >= 0 && ix + col < c.width)
                            dstRow[col] = srcRow[col] | 0xFF000000u;
                    }
                }
            }
        }
    }

    // 2. Module Cards
    for (const auto& card : cards_)
    {
        int cx = workspaceRect_.x + card.bounds.x;
        int cy = workspaceRect_.y + card.bounds.y - scrollY_;
        if (cy + card.bounds.h < wy0 || cy >= wy1) continue;

        // Card container
        c.fillRoundedRect (cx, cy, card.bounds.w, card.bounds.h, 7, th.panelBg);
        c.drawRoundedRect (cx, cy, card.bounds.w, card.bounds.h, 7, th.panelBorder);

        // Header bar
        c.fillRect (cx + 1, cy + 1, card.bounds.w - 2, kCardHeaderH, 0xFF141821u);
        c.fillRect (cx + 1, cy + 1, 3, kCardHeaderH, th.accent);
        c.fillRect (cx + 1, cy + kCardHeaderH, card.bounds.w - 2, 1, th.panelBorder);
        c.drawText (cx + 10, cy + 8, card.title, th.textPrimary, 1, card.bounds.w - 16);

        // Controls
        for (const auto& ctrl : card.controls)
        {
            int bx = workspaceRect_.x + ctrl.bounds.x;
            int by = workspaceRect_.y + ctrl.bounds.y - scrollY_;
            if (by + ctrl.bounds.h < wy0 || by >= wy1) continue;

            const auto& p = plugin_->params[size_t (ctrl.paramIndex)];
            const double val = plugin_->getParamValue (p.id);
            const double range = std::max (1e-9, p.maxVal - p.minVal);
            const float norm = float (std::clamp ((val - p.minVal) / range, 0.0, 1.0));
            const bool hovered = (hoveredParamIdx_ == ctrl.paramIndex);
            const bool active = (draggingParamIdx_ == ctrl.paramIndex);
            const bool isLearnTarget = (learnTarget == p.id);
            const int mappedCc = plugin_->findCcForParam (p.id);

            // Label at top of cell
            c.drawTextCentered (bx + ctrl.bounds.w / 2, by + 5, toUpper (p.name),
                                hovered || active ? 0xFFFFFFFFu : th.textMuted, 1, ctrl.bounds.w - 6);

            // MIDI CC badge (top-right)
            if (mappedCc >= 0)
            {
                char ccTag[12];
                std::snprintf (ccTag, sizeof (ccTag), "C%d", mappedCc);
                int bw = Canvas::textWidth (ccTag, 1) + 6;
                int rx = bx + ctrl.bounds.w - bw - 2;
                int ry = by + 15;
                c.fillRoundedRect (rx, ry, bw, 11, 3, th.midiBadge);
                c.drawText (rx + 3, ry + 2, ccTag, 0xFF071018u, 1);
            }

            std::string valText = toUpper (plugin_->formatParamValue (p.id, val));

            if (p.isBool())
            {
                // Illuminated toggle switch
                bool on = (val >= 0.5);
                Rect sw { bx + 14, by + 28, ctrl.bounds.w - 28, 30 };
                c.fillRoundedRect (sw.x, sw.y, sw.w, sw.h, 6, on ? th.accentDim : 0xFF141822u);
                c.drawRoundedRect (sw.x, sw.y, sw.w, sw.h, 6,
                                   isLearnTarget ? 0xFFFF3B5Cu : (on ? th.accent : (hovered ? 0xFF566078u : th.panelBorder)));
                // LED strip
                c.fillRect (sw.x + 8, sw.y + 5, sw.w - 16, 4, on ? th.accent : 0xFF2B3140u);
                c.drawTextCentered (sw.x + sw.w / 2, sw.y + 15, valText, on ? 0xFFFFFFFFu : th.textMuted, 1, sw.w - 8);
            }
            else if (p.isEnum() || (p.isStepped() && (p.maxVal - p.minVal) <= 24.0))
            {
                // Clickable / draggable stepped selector box
                Rect sb { bx + 6, by + 26, ctrl.bounds.w - 12, 34 };
                c.fillRoundedRect (sb.x, sb.y, sb.w, sb.h, 5, active ? 0xFF2B3244u : 0xFF141822u);
                c.drawRoundedRect (sb.x, sb.y, sb.w, sb.h, 5,
                                   isLearnTarget ? 0xFFFF3B5Cu : (active ? th.accent : (hovered ? 0xFF5A6580u : th.panelBorder)));
                // Progress bar at bottom of selector
                int barW = int (float (sb.w - 8) * norm);
                c.fillRect (sb.x + 4, sb.y + sb.h - 5, sb.w - 8, 2, 0xFF262C3Au);
                if (barW > 0) c.fillRect (sb.x + 4, sb.y + sb.h - 5, barW, 2, th.accent);

                c.drawTextCentered (sb.x + sb.w / 2, sb.y + 11, valText, th.textPrimary, 1, sb.w - 10);
                // Subtle step arrows on hover
                if (hovered || active)
                {
                    c.drawText (sb.x + 3, sb.y + 11, "<", th.accent, 1);
                    c.drawText (sb.x + sb.w - 8, sb.y + 11, ">", th.accent, 1);
                }
            }
            else
            {
                // Anti-aliased rotary knob
                int kcx = bx + ctrl.bounds.w / 2;
                int kcy = by + 43;
                c.drawKnob (kcx, kcy, 21, norm, th.accent, 0xFF2A3040u, hovered, active, isLearnTarget);
                // Formatted value readout below knob
                c.drawTextCentered (kcx, by + 72, valText, active ? th.accent : th.textPrimary, 1, ctrl.bounds.w - 6);
            }
        }
    }

    // Re-paint header & bank bar cleanly over any scrolled content that bled above workspaceRect_.y
    // (Wait: we render header & bank bar AFTER workspace in render() so clipping is 100% pixel-perfect!)

    // Scrollbar
    if (maxScrollY_ > 0)
    {
        c.fillRoundedRect (scrollbarTrackRect_.x, scrollbarTrackRect_.y, scrollbarTrackRect_.w, scrollbarTrackRect_.h,
                           4, 0xFF151822u);
        c.fillRoundedRect (scrollbarThumbRect_.x, scrollbarThumbRect_.y, scrollbarThumbRect_.w, scrollbarThumbRect_.h,
                           4, draggingScrollbar_ ? th.accent : 0xFF3C455Au);
    }
}

void PanelUI::renderPresetOverlay (Canvas& c)
{
    if (!presetMenuOpen_ || plugin_->presets.empty()) return;
    const auto& th = plugin_->theme;

    const int n = int (plugin_->presets.size());
    const int cols = (n > 16) ? 3 : ((n > 8) ? 2 : 1);
    const int rows = (n + cols - 1) / cols;
    const int itemW = 220;
    const int itemH = 26;
    const int menuW = cols * itemW + 16;
    const int menuH = std::min (height_ - kHeaderH - 24, rows * itemH + 16);
    const int menuX = presetBoxRect_.x;
    const int menuY = presetBoxRect_.y + presetBoxRect_.h + 4;

    c.fillRoundedRect (menuX, menuY, menuW, menuH, 6, 0xFF161A24u);
    c.drawRoundedRect (menuX, menuY, menuW, menuH, 6, th.accent);

    presetItemRects_.resize (size_t (n));
    for (int i = 0; i < n; ++i)
    {
        int col = i / rows;
        int row = i % rows;
        Rect r { menuX + 8 + col * itemW, menuY + 8 + row * itemH, itemW - 4, itemH - 2 };
        presetItemRects_[size_t (i)] = r;
        if (r.y + r.h > menuY + menuH - 4) continue;

        bool selected = (i == plugin_->currentPresetIndex);
        c.fillRoundedRect (r.x, r.y, r.w, r.h, 4, selected ? th.accentDim : 0xFF1D2230u);
        char buf[80];
        std::snprintf (buf, sizeof (buf), "%02d %s", i + 1, plugin_->presets[size_t (i)].name.c_str());
        c.drawText (r.x + 8, r.y + 8, toUpper (buf), selected ? 0xFFFFFFFFu : th.textPrimary, 1, r.w - 14);
    }
}

void PanelUI::render (uint32_t* pixels, int width, int height, uint64_t nowMs)
{
    if (!pixels || width <= 0 || height <= 0) return;
    if (width != width_ || height != height_)
        rebuildLayout (width, height);

    plugin_->updateEmbeddedScreen (nowMs);

    Canvas c { pixels, width, height };
    c.clear (plugin_->theme.bg);

    renderWorkspace (c);
    renderHeader (c);
    renderBankBar (c);
    renderPresetOverlay (c);
}

void PanelUI::onMouseDown (int x, int y, int button, bool shift, bool doubleClick)
{
    // Preset overlay intercepts clicks first
    if (presetMenuOpen_)
    {
        for (size_t i = 0; i < presetItemRects_.size(); ++i)
        {
            if (presetItemRects_[i].contains (x, y))
            {
                plugin_->loadPresetByIndex (int (i));
                presetMenuOpen_ = false;
                return;
            }
        }
        presetMenuOpen_ = false;
        if (presetBoxRect_.contains (x, y)) return;
    }

    // Header buttons (left click)
    if (button == 0)
    {
        if (presetPrevRect_.contains (x, y) && !plugin_->presets.empty())
        {
            int next = (plugin_->currentPresetIndex <= 0) ? int (plugin_->presets.size()) - 1 : plugin_->currentPresetIndex - 1;
            plugin_->loadPresetByIndex (next);
            return;
        }
        if (presetNextRect_.contains (x, y) && !plugin_->presets.empty())
        {
            int next = (plugin_->currentPresetIndex + 1) % int (plugin_->presets.size());
            plugin_->loadPresetByIndex (next);
            return;
        }
        if (presetBoxRect_.contains (x, y) && !plugin_->presets.empty())
        {
            presetMenuOpen_ = !presetMenuOpen_;
            return;
        }
        if (midiLearnRect_.contains (x, y))
        {
            uint32_t cur = plugin_->midiLearnTarget.load (std::memory_order_relaxed);
            plugin_->midiLearnTarget.store (cur == CLAP_INVALID_ID ? DesktopPlugin::kLearnArmedNoParam : CLAP_INVALID_ID,
                                            std::memory_order_relaxed);
            return;
        }
        if (clearCcRect_.contains (x, y))
        {
            plugin_->clearAllCc();
            plugin_->midiLearnTarget.store (CLAP_INVALID_ID, std::memory_order_relaxed);
            return;
        }

        // Bank tabs
        for (size_t i = 0; i < bankTabs_.size(); ++i)
        {
            if (bankTabs_[i].rect.contains (x, y))
            {
                activeBank_ = int (i);
                scrollY_ = 0;
                rebuildLayout (width_, height_);
                return;
            }
        }

        // Scrollbar
        if (maxScrollY_ > 0 && scrollbarTrackRect_.contains (x, y))
        {
            draggingScrollbar_ = true;
            scrollbarDragStartY_ = y;
            scrollbarDragStartScroll_ = scrollY_;
            return;
        }

        // Embedded live MPC screen click -> send MELODEE_SCREEN_PUSH (audition/select)
        if (plugin_->innerScreen && screenImageRect_.w > 0)
        {
            Rect sImg = { workspaceRect_.x + screenImageRect_.x,
                          workspaceRect_.y + screenImageRect_.y - scrollY_,
                          screenImageRect_.w, screenImageRect_.h };
            if (workspaceRect_.contains (x, y) && sImg.contains (x, y))
            {
                plugin_->sendScreenInput (MELODEE_SCREEN_PUSH, 1, shift);
                return;
            }
        }
    }

    // Control hit test
    int idx = hitTestControl (x, y);
    if (idx < 0) return;

    const auto& p = plugin_->params[size_t (idx)];
    lastTweakedParamIdx_ = idx;

    // Right-click: arm MIDI Learn for this control (or if already learning it, clear its CC & disarm)
    if (button == 1)
    {
        uint32_t curLearn = plugin_->midiLearnTarget.load (std::memory_order_relaxed);
        if (curLearn == p.id)
        {
            plugin_->clearCcForParam (p.id);
            plugin_->midiLearnTarget.store (CLAP_INVALID_ID, std::memory_order_relaxed);
        }
        else
        {
            plugin_->midiLearnTarget.store (p.id, std::memory_order_relaxed);
        }
        return;
    }

    if (button != 0) return;

    // If MIDI Learn is armed, clicking a control selects it as the MIDI Learn target
    if (plugin_->midiLearnTarget.load (std::memory_order_relaxed) != CLAP_INVALID_ID)
    {
        plugin_->midiLearnTarget.store (p.id, std::memory_order_relaxed);
        return;
    }

    // Double-click resets to default value
    if (doubleClick)
    {
        plugin_->enqueueUiParam (UiEventKind::GestureBegin, p.id, p.defVal);
        plugin_->enqueueUiParam (UiEventKind::Value, p.id, p.defVal);
        plugin_->enqueueUiParam (UiEventKind::GestureEnd, p.id, p.defVal);
        return;
    }

    // Boolean toggle: flip immediately on click
    if (p.isBool())
    {
        double cur = plugin_->getParamValue (p.id);
        double next = (cur >= 0.5) ? 0.0 : 1.0;
        plugin_->enqueueUiParam (UiEventKind::GestureBegin, p.id, next);
        plugin_->enqueueUiParam (UiEventKind::Value, p.id, next);
        plugin_->enqueueUiParam (UiEventKind::GestureEnd, p.id, next);
        return;
    }

    // Start click-and-drag gesture
    draggingParamIdx_ = idx;
    dragStartVal_ = plugin_->getParamValue (p.id);
    dragStartY_ = y;
    plugin_->enqueueUiParam (UiEventKind::GestureBegin, p.id, dragStartVal_);

    // For enum / small stepped selector, clicking left/right edge steps immediately
    if (p.isEnum() || (p.isStepped() && (p.maxVal - p.minVal) <= 24.0))
    {
        int wy = y - workspaceRect_.y + scrollY_;
        int wx = x - workspaceRect_.x;
        for (const auto& card : cards_)
        {
            for (const auto& ctrl : card.controls)
            {
                if (ctrl.paramIndex == idx && ctrl.bounds.contains (wx, wy))
                {
                    int relX = wx - ctrl.bounds.x;
                    if (relX < 22)
                    {
                        double nv = std::clamp (std::round (dragStartVal_ - 1.0), p.minVal, p.maxVal);
                        dragStartVal_ = nv;
                        plugin_->enqueueUiParam (UiEventKind::Value, p.id, nv);
                    }
                    else if (relX > ctrl.bounds.w - 22)
                    {
                        double nv = std::clamp (std::round (dragStartVal_ + 1.0), p.minVal, p.maxVal);
                        dragStartVal_ = nv;
                        plugin_->enqueueUiParam (UiEventKind::Value, p.id, nv);
                    }
                    break;
                }
            }
        }
    }
}

void PanelUI::onMouseMove (int x, int y, bool shift)
{
    if (draggingScrollbar_ && maxScrollY_ > 0)
    {
        int travel = std::max (1, scrollbarTrackRect_.h - scrollbarThumbRect_.h);
        int dy = y - scrollbarDragStartY_;
        scrollY_ = scrollbarDragStartScroll_ + (dy * maxScrollY_) / travel;
        clampScroll();
        return;
    }

    if (draggingParamIdx_ >= 0 && draggingParamIdx_ < int (plugin_->params.size()))
    {
        const auto& p = plugin_->params[size_t (draggingParamIdx_)];
        const double range = p.maxVal - p.minVal;
        const double pixelsForFullSweep = shift ? 750.0 : 160.0;
        double delta = (double (dragStartY_ - y) / pixelsForFullSweep) * range;
        double nextVal = std::clamp (dragStartVal_ + delta, p.minVal, p.maxVal);
        if (p.isStepped())
            nextVal = std::clamp (std::round (nextVal), p.minVal, p.maxVal);
        plugin_->enqueueUiParam (UiEventKind::Value, p.id, nextVal);
        return;
    }

    hoveredParamIdx_ = hitTestControl (x, y);
}

void PanelUI::onMouseUp (int, int, int button)
{
    if (button != 0) return;
    draggingScrollbar_ = false;
    if (draggingParamIdx_ >= 0 && draggingParamIdx_ < int (plugin_->params.size()))
    {
        const auto& p = plugin_->params[size_t (draggingParamIdx_)];
        double cur = plugin_->getParamValue (p.id);
        plugin_->enqueueUiParam (UiEventKind::GestureEnd, p.id, cur);
    }
    draggingParamIdx_ = -1;
}

void PanelUI::onMouseWheel (int x, int y, float deltaY, bool shift)
{
    if (std::fabs (deltaY) < 1e-4f) return;

    // Wheel over live MPC screen -> send MELODEE_SCREEN_WHEEL
    if (plugin_->innerScreen && screenImageRect_.w > 0)
    {
        Rect sImg = { workspaceRect_.x + screenImageRect_.x,
                      workspaceRect_.y + screenImageRect_.y - scrollY_,
                      screenImageRect_.w, screenImageRect_.h };
        if (workspaceRect_.contains (x, y) && sImg.contains (x, y))
        {
            int dir = (deltaY > 0.0f) ? 1 : -1;
            plugin_->sendScreenInput (MELODEE_SCREEN_WHEEL, dir, shift);
            return;
        }
    }

    // Wheel over a control -> nudge parameter
    int idx = hitTestControl (x, y);
    if (idx >= 0)
    {
        const auto& p = plugin_->params[size_t (idx)];
        lastTweakedParamIdx_ = idx;
        double cur = plugin_->getParamValue (p.id);
        double step = p.isStepped() ? 1.0 : ((p.maxVal - p.minVal) * (shift ? 0.005 : 0.025));
        double dir = (deltaY > 0.0f) ? 1.0 : -1.0;
        double next = std::clamp (cur + dir * step, p.minVal, p.maxVal);
        if (p.isStepped()) next = std::round (next);

        plugin_->enqueueUiParam (UiEventKind::GestureBegin, p.id, next);
        plugin_->enqueueUiParam (UiEventKind::Value, p.id, next);
        plugin_->enqueueUiParam (UiEventKind::GestureEnd, p.id, next);
        return;
    }

    // Otherwise scroll the expanded control panel vertically
    if (maxScrollY_ > 0)
    {
        scrollY_ -= int (deltaY * 42.0f);
        clampScroll();
    }
}

} // namespace hdslave_desktop
