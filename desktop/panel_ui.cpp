#include "panel_ui.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

namespace hdslave_desktop
{

namespace
{
constexpr int kTopH = 46;
constexpr int kMargin = 10;
constexpr int kTabsH = 24;
constexpr int kCardPad = 8;
constexpr int kCardTitleH = 22;
constexpr int kGap = 8;
constexpr int kHwH = 124;
constexpr int kScrollW = 10;

struct Hash
{
    uint64_t h = 1469598103934665603ull;
    void u (uint64_t v) { h = (h ^ v) * 1099511628211ull; h ^= h >> 29; }
    void d (double v) { uint64_t b; std::memcpy (&b, &v, 8); u (b); }
    void s (const std::string& t) { for (char ch : t) u ((uint64_t) (unsigned char) ch); u (0xFFu); }
};

std::string upper (std::string s)
{
    for (char& ch : s)
        if (ch >= 'a' && ch <= 'z') ch = char (ch - 'a' + 'A');
    return s;
}

// Up to two lines of at most maxChars, split at spaces.
std::vector<std::string> wrap2 (const std::string& text, int maxChars)
{
    if ((int) text.size() <= maxChars) return { text };
    size_t cut = std::string::npos;
    for (size_t i = 0; i < text.size() && (int) i <= maxChars; ++i)
        if (text[i] == ' ') cut = i;
    if (cut == std::string::npos) return { Canvas::fit (text, maxChars * Canvas::glyphW) };
    std::string a = text.substr (0, cut), b = text.substr (cut + 1);
    return { a, Canvas::fit (b, maxChars * Canvas::glyphW) };
}

bool parseStep (const std::string& name, std::string& stem, int& num)
{
    const size_t sp = name.find_last_of (' ');
    if (sp == std::string::npos || sp + 1 >= name.size()) return false;
    for (size_t i = sp + 1; i < name.size(); ++i)
        if (name[i] < '0' || name[i] > '9') return false;
    stem = name.substr (0, sp);
    num = std::atoi (name.c_str() + sp + 1);
    return true;
}

bool hasLetters (const std::string& s)
{
    for (char ch : s)
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) return true;
    return false;
}
} // namespace

PanelUI::PanelUI (DesktopPlugin* plugin) : p_ (plugin)
{
    buildSections();
}

// Model --------------------------------------------------------------------------------------------------------

void PanelUI::buildSections()
{
    sections_.clear();
    const auto& P = p_->params;

    std::vector<std::string> order;
    std::map<std::string, std::vector<int>> byModule;
    for (size_t i = 0; i < P.size(); ++i)
    {
        if (P[i].flags & CLAP_PARAM_IS_HIDDEN) continue;
        const std::string m = P[i].module.empty() ? std::string ("Main") : P[i].module;
        if (!byModule.count (m)) order.push_back (m);
        byModule[m].push_back ((int) i);
    }

    // Parents first, each followed by its children ("Sequencer", then "Sequencer/Pitch").
    std::vector<std::string> sorted;
    std::vector<bool> used (order.size(), false);
    for (size_t a = 0; a < order.size(); ++a)
    {
        if (used[a]) continue;
        const std::string& m = order[a];
        const size_t slash = m.find ('/');
        if (slash != std::string::npos && byModule.count (m.substr (0, slash))) continue;
        used[a] = true;
        sorted.push_back (m);
        for (size_t b = 0; b < order.size(); ++b)
            if (!used[b] && order[b].rfind (m + "/", 0) == 0) { used[b] = true; sorted.push_back (order[b]); }
    }
    for (size_t a = 0; a < order.size(); ++a)
        if (!used[a]) sorted.push_back (order[a]);

    auto text = [&] (int i, double v) { return p_->formatParamValue (P[(size_t) i].id, v); };
    auto unitsFor = [&] (size_t chars, int perUnit) { return chars <= (size_t) perUnit ? 1 : chars <= (size_t) (perUnit * 2 + 2) ? 2 : 3; };

    for (const auto& m : sorted)
    {
        const auto& idx = byModule[m];
        Section s;
        std::string title = m;
        std::replace (title.begin(), title.end(), '/', ' ');
        s.title = upper (title);

        // A prefix every name shares ("BD Level", "BD Tune"): the tab's name, left out of the labels.
        std::string prefix;
        if (idx.size() >= 2)
        {
            const std::string& first = P[(size_t) idx[0]].name;
            const size_t sp = first.find (' ');
            if (sp != std::string::npos)
            {
                prefix = first.substr (0, sp + 1);
                for (int i : idx)
                    if (P[(size_t) i].name.rfind (prefix, 0) != 0 || P[(size_t) i].name.size() <= prefix.size()) { prefix.clear(); break; }
            }
        }
        const bool numberedTitle = !s.title.empty() && s.title.back() >= '0' && s.title.back() <= '9';
        s.tag = (prefix.empty() || numberedTitle) ? s.title : upper (prefix.substr (0, prefix.size() - 1));
        if (s.tag.size() > 12) s.tag = s.tag.substr (0, 12);
        auto label = [&] (const std::string& name) {
            return upper (!prefix.empty() && name.rfind (prefix, 0) == 0 ? name.substr (prefix.size()) : name);
        };

        std::vector<bool> taken (idx.size(), false);
        for (size_t k = 0; k < idx.size(); ++k)
        {
            if (taken[k]) continue;
            const int i = idx[k];
            const auto& pi = P[(size_t) i];

            // Step banks: "Pitch 1" .. "Pitch 8", continuous or wide integers.
            std::string stem;
            int num = 0;
            if (parseStep (pi.name, stem, num) && num == 1 && !pi.isEnum() && !pi.isBool())
            {
                std::vector<std::pair<int, size_t>> run;
                for (size_t q = k; q < idx.size(); ++q)
                {
                    std::string st2;
                    int n2 = 0;
                    const auto& pq = P[(size_t) idx[q]];
                    if (!taken[q] && parseStep (pq.name, st2, n2) && st2 == stem && !pq.isEnum() && !pq.isBool())
                        run.push_back ({ n2, q });
                }
                std::sort (run.begin(), run.end());
                bool consecutive = run.size() >= 4;
                for (size_t r = 0; consecutive && r < run.size(); ++r)
                    consecutive = run[r].first == (int) r + 1;
                if (consecutive)
                {
                    ControlSpec sp;
                    sp.kind = Kind::Steps;
                    sp.label = label (stem);
                    for (auto& [n2, q] : run) { sp.params.push_back (idx[q]); taken[q] = true; }
                    sp.units = std::max (4, (int) std::ceil (run.size() * 60.0 / 82.0));
                    sp.rows = 2;
                    s.specs.push_back (std::move (sp));
                    continue;
                }
            }

            taken[k] = true;
            ControlSpec sp;
            sp.params = { i };
            sp.label = label (pi.name);
            if (pi.isBool())
            {
                const std::string t0 = text (i, pi.minVal), t1 = text (i, pi.maxVal);
                sp.kind = (t0 == "Press") ? Kind::Button : Kind::Toggle;
                sp.units = unitsFor (std::max (t0.size(), t1.size()), 8);
            }
            else if (pi.isStepped())
            {
                sp.kind = pi.isEnum() ? Kind::Selector : Kind::Stepper;
                size_t longest = 0;
                const double range = pi.range();
                if (range <= 160)
                    for (double v = pi.minVal; v <= pi.maxVal + 1e-9; v += 1.0) longest = std::max (longest, text (i, v).size());
                else
                    for (double v : { pi.minVal, pi.maxVal, pi.defVal }) longest = std::max (longest, text (i, v).size());
                if (!pi.isEnum() && range <= 160 && hasLetters (text (i, pi.minVal + 1)) && hasLetters (text (i, pi.maxVal)) && longest > 6)
                    sp.kind = Kind::Selector; // named steps (a filter list): a list reads better than arrows
                sp.units = unitsFor (longest + (sp.kind == Kind::Stepper ? 5 : 4), 9);
            }
            else
            {
                sp.kind = Kind::Knob;
                size_t longest = 0;
                for (double v : { pi.minVal, pi.maxVal, pi.defVal }) longest = std::max (longest, text (i, v).size());
                sp.units = unitsFor (longest, 11);
            }
            sp.units = std::min (sp.units, 3);
            s.specs.push_back (std::move (sp));
        }
        if (!s.specs.empty()) sections_.push_back (std::move (s));
    }
}

// Layout -------------------------------------------------------------------------------------------------------

void PanelUI::setSize (int w, int h, int scale)
{
    w = std::max (w, 320);
    h = std::max (h, 240);
    scale = std::clamp (scale, 1, 4);
    if (w == w_ && h == h_ && scale == scale_ && !frame_.empty()) return;
    w_ = w;
    h_ = h;
    scale_ = scale;
    layoutDirty_ = true;
}

void PanelUI::layout()
{
    layoutDirty_ = false;
    physW_ = w_ * scale_;
    physH_ = h_ * scale_;
    base_.assign ((size_t) physW_ * (size_t) physH_, p_->theme.bgBottom);
    frame_.assign ((size_t) physW_ * (size_t) physH_, p_->theme.bgBottom);

    for (auto& r : items_) r = {};
    top_ = { 0, 0, w_, kTopH };

    // Header.
    const std::string name = upper (p_->pluginName.rfind ("hdslave ", 0) == 0 ? p_->pluginName.substr (8) : p_->pluginName);
    const int wordW = Canvas::textWidth (name, 2);
    int x = 16 + wordW + 20;
    items_[(size_t) Item::PresetPrev] = { x, 9, 28, 28 };
    x += 31;
    const int boxW = std::clamp (w_ - x - 31 - 12 - 90 - 82 - 84 - 240, 160, 300);
    items_[(size_t) Item::PresetBox] = { x, 9, boxW, 28 };
    x += boxW + 3;
    items_[(size_t) Item::PresetNext] = { x, 9, 28, 28 };
    x += 40;
    items_[(size_t) Item::Learn] = { x, 9, 86, 28 };
    x += 90;
    items_[(size_t) Item::ClearCc] = { x, 9, 78, 28 };
    x += 82;
    if (p_->innerScreen)
    {
        items_[(size_t) Item::Zoom] = { x, 9, 78, 28 };
        x += 90;
    }
    items_[(size_t) Item::Readout] = { x, 7, std::max (0, w_ - x - 12), 32 };

    // The live page and its hardware controls.
    int colW = 0;
    screenCard_ = screenRect_ = hwRow_ = {};
    if (p_->innerScreen)
    {
        const int roomH = h_ - kTopH - 2 * kMargin - kCardTitleH - 26;
        int z = zoomChoice_;
        if (z == 0) z = (roomH - kHwH >= 218 * 2 && w_ >= 1180) ? 2 : 1;
        zoom_ = z;
        const int sw = 320 * z, sh = 218 * z;
        const int hwH = (z == 2) ? kHwH : 2 * kHwH;
        screenCard_ = { kMargin, kTopH + kMargin, sw + 24, kCardTitleH + 12 + sh + 10 + hwH + 6 };
        screenCard_.h = std::min (screenCard_.h, h_ - screenCard_.y - kMargin);
        screenRect_ = { screenCard_.x + 12, screenCard_.y + kCardTitleH + 12, sw, sh };
        hwRow_ = { screenRect_.x, screenRect_.y + sh + 10, sw, hwH };
        items_[(size_t) Item::Screen] = screenRect_;
        int hx = hwRow_.x, hy = hwRow_.y;
        items_[(size_t) Item::Encoder] = { hx, hy, 104, kHwH };
        hx += 108;
        const int by = hy + 52;
        items_[(size_t) Item::Minus] = { hx, by, 36, 38 };
        hx += 40;
        items_[(size_t) Item::Push] = { hx, by, 56, 38 };
        hx += 60;
        items_[(size_t) Item::Plus] = { hx, by, 36, 38 };
        hx += 40;
        items_[(size_t) Item::Shift] = { hx, by, 56, 38 };
        hx += 64;
        if (z == 1) { hx = hwRow_.x; hy += kHwH; }
        const int kw = (hwRow_.x + hwRow_.w - hx) / 3;
        for (int k = 0; k < 3; ++k)
            items_[(size_t) Item::K1 + (size_t) k] = { hx + k * kw, hy, kw, kHwH };
        colW = screenCard_.w + kMargin;
    }

    tabsRow_ = { kMargin + colW, kTopH + kMargin, w_ - colW - 2 * kMargin, kTabsH };
    wall_ = { tabsRow_.x, tabsRow_.y + kTabsH + 6, tabsRow_.w, h_ - (tabsRow_.y + kTabsH + 6) - kMargin };
    layoutWall();
    fullDirty_ = true;
}

void PanelUI::layoutWall()
{
    const int keepPage = page_;
    pages_.clear();
    const int innerW = wall_.w - kScrollW - 2;
    const int availH = wall_.h;
    const int maxCols = std::max (1, (innerW - 2 * kCardPad) / cellW_);

    struct Natural
    {
        const Section* s;
        int cols;
        std::vector<int> lineRows;                 // rows per line
        std::vector<std::tuple<int, int, int>> at; // spec index, col, line
        int w, h;
    };
    std::vector<Natural> nat;
    for (const auto& s : sections_)
    {
        Natural n { &s, 1, {}, {}, 0, 0 };
        int total = 0, widest = 1;
        for (const auto& sp : s.specs)
        {
            const int u = std::min (sp.units, maxCols);
            widest = std::max (widest, u);
            if (sp.kind != Kind::Steps) total += u;
        }
        int cols = total;
        if (total > maxCols)
        {
            const int rows = (total + maxCols - 1) / maxCols;
            cols = (total + rows - 1) / rows;
        }
        cols = std::clamp (std::max (cols, widest), 1, maxCols);
        n.cols = cols;

        int col = 0, line = 0;
        n.lineRows.push_back (1);
        for (size_t k = 0; k < s.specs.size(); ++k)
        {
            const auto& sp = s.specs[k];
            const int u = std::min (sp.units, cols);
            const bool tall = sp.kind == Kind::Steps;
            if ((tall && col > 0) || col + u > cols)
            {
                ++line;
                col = 0;
                n.lineRows.push_back (1);
            }
            n.at.push_back ({ (int) k, col, line });
            n.lineRows[(size_t) line] = std::max (n.lineRows[(size_t) line], sp.rows);
            col += u;
            if (tall && k + 1 < s.specs.size())
            {
                ++line;
                col = 0;
                n.lineRows.push_back (1);
            }
        }
        int rows = 0;
        for (int r : n.lineRows) rows += r;
        n.w = cols * cellW_ + 2 * kCardPad;
        n.h = kCardTitleH + 4 + rows * cellH_ + kCardPad / 2;
        nat.push_back (std::move (n));
    }

    // Shelves of cards, pages of shelves.
    struct Placement
    {
        size_t nat;
        Rect r;
        int shelf;
    };
    std::vector<std::vector<Placement>> pagesPl (1);
    int x = 0, y = 0, shelfH = 0, shelf = 0;
    for (size_t i = 0; i < nat.size(); ++i)
    {
        const auto& n = nat[i];
        const int w = std::min (n.w, innerW);
        if (x > 0 && x + w > innerW)
        {
            y += shelfH + kGap;
            x = 0;
            shelfH = 0;
            ++shelf;
        }
        if (y > 0 && y + n.h > availH && !pagesPl.back().empty())
        {
            pagesPl.emplace_back();
            x = y = shelfH = 0;
            ++shelf;
        }
        pagesPl.back().push_back ({ i, { x, y, w, n.h }, shelf });
        x += w + kGap;
        shelfH = std::max (shelfH, n.h);
    }

    for (auto& pl : pagesPl)
    {
        if (pl.empty()) continue;
        // Even out each shelf: same height, cards widened to fill the row.
        for (size_t a = 0; a < pl.size();)
        {
            size_t b = a;
            int used = 0, hMax = 0;
            while (b < pl.size() && pl[b].shelf == pl[a].shelf)
            {
                used += pl[b].r.w;
                hMax = std::max (hMax, pl[b].r.h);
                ++b;
            }
            const int count = (int) (b - a);
            const int extra = innerW - used - kGap * (count - 1);
            int cx = 0;
            for (size_t k = a; k < b; ++k)
            {
                const int add = extra / count + ((int) (k - a) < extra % count ? 1 : 0);
                pl[k].r.x = cx;
                pl[k].r.w += std::max (0, add);
                pl[k].r.h = hMax;
                cx += pl[k].r.w + kGap;
            }
            a = b;
        }

        Page page;
        std::string label;
        for (const auto& p : pl)
        {
            const auto& n = nat[p.nat];
            page.cards.push_back ({ n.s, p.r });
            if (!label.empty()) label += " ";
            label += n.s->tag;
            // Controls, each line centred in the card.
            std::vector<int> lineUnits (n.lineRows.size(), 0), lineY (n.lineRows.size(), 0);
            for (auto& [k, col, line] : n.at)
                lineUnits[(size_t) line] = std::max (lineUnits[(size_t) line], col + std::min (n.s->specs[(size_t) k].units, n.cols));
            for (size_t l = 1; l < lineY.size(); ++l) lineY[l] = lineY[l - 1] + n.lineRows[l - 1];
            const int innerCardW = p.r.w - 2 * kCardPad;
            for (auto& [k, col, line] : n.at)
            {
                const auto& sp = n.s->specs[(size_t) k];
                const int u = std::min (sp.units, n.cols);
                const int off = std::max (0, (innerCardW - lineUnits[(size_t) line] * cellW_) / 2);
                Control c;
                c.spec = &sp;
                c.r = { p.r.x + kCardPad + off + col * cellW_, p.r.y + kCardTitleH + 4 + lineY[(size_t) line] * cellH_, u * cellW_,
                        sp.rows * cellH_ };
                page.controls.push_back (c);
            }
            page.contentH = std::max (page.contentH, p.r.y + p.r.h + 4);
        }
        page.label = label;
        pages_.push_back (std::move (page));
    }

    // Tabs: use each tab's natural width when the row fits, and only trim the longest tabs if it overflows.
    const int n = (int) pages_.size();
    std::vector<int> widths ((size_t) n, 0);
    int totalW = 0;
    for (int i = 0; i < n; ++i)
    {
        pages_[(size_t) i].label = Canvas::fit (pages_[(size_t) i].label, 214);
        widths[(size_t) i] = Canvas::textWidth (pages_[(size_t) i].label) + 14;
        totalW += widths[(size_t) i] + (i > 0 ? 4 : 0);
    }
    int cap = 214;
    while (totalW > tabsRow_.w && cap > 30)
    {
        cap -= 10;
        totalW = 0;
        for (int i = 0; i < n; ++i)
        {
            pages_[(size_t) i].label = Canvas::fit (pages_[(size_t) i].label, cap);
            widths[(size_t) i] = Canvas::textWidth (pages_[(size_t) i].label) + 14;
            totalW += widths[(size_t) i] + (i > 0 ? 4 : 0);
        }
    }
    int tx = tabsRow_.x;
    for (int i = 0; i < n; ++i)
    {
        pages_[(size_t) i].tab = { tx, tabsRow_.y, widths[(size_t) i], kTabsH };
        tx += widths[(size_t) i] + 4;
    }
    page_ = std::clamp (keepPage, 0, std::max (0, n - 1));
    scroll_ = 0;
    hoverCtrl_ = -1;
}

// Values -------------------------------------------------------------------------------------------------------

double PanelUI::norm (int param) const
{
    const auto& p = p_->params[(size_t) param];
    const double r = p.range();
    return r > 0 ? std::clamp ((p_->displayValue (param) - p.minVal) / r, 0.0, 1.0) : 0.0;
}

double PanelUI::fromNorm (int param, double n) const
{
    const auto& p = p_->params[(size_t) param];
    double v = p.minVal + std::clamp (n, 0.0, 1.0) * p.range();
    if (p.isStepped()) v = std::round (v);
    return std::clamp (v, p.minVal, p.maxVal);
}

void PanelUI::setParam (int param, double v)
{
    p_->setEdit (param, v);
    lastTouched_ = param;
}

void PanelUI::endWheelGesture()
{
    if (wheelParam_ >= 0) p_->endEdit (wheelParam_);
    wheelParam_ = -1;
    wheelAccum_ = 0.0f;
}

void PanelUI::stepParam (int param, int steps)
{
    if (steps == 0) return;
    const auto& p = p_->params[(size_t) param];
    if (wheelParam_ != param)
    {
        endWheelGesture();
        p_->beginEdit (param);
        wheelParam_ = param;
    }
    wheelAt_ = now_;
    double v = p_->displayValue (param);
    if (p.isStepped()) v += steps;
    else v += steps * p.range() * 0.01;
    setParam (param, std::clamp (v, p.minVal, p.maxVal));
}

void PanelUI::resetParam (int param)
{
    p_->setValueOnce (param, p_->params[(size_t) param].defVal);
    lastTouched_ = param;
}

int PanelUI::screenKnobParam (int k) const
{
    const clap_id id = p_->screenKnobIds[k];
    return id == CLAP_INVALID_ID ? -1 : p_->findParamIndex (id);
}

std::string PanelUI::valueText (int param) const
{
    return p_->formatParamValue (p_->params[(size_t) param].id, p_->displayValue (param));
}

// Hit testing --------------------------------------------------------------------------------------------------

Rect PanelUI::screenRect (int ctrl) const
{
    const Page* pg = current();
    if (!pg || ctrl < 0 || ctrl >= (int) pg->controls.size()) return {};
    const Rect& r = pg->controls[(size_t) ctrl].r;
    return { wall_.x + r.x, wall_.y + r.y - scroll_, r.w, r.h };
}

int PanelUI::hitControl (float x, float y) const
{
    const Page* pg = current();
    if (!pg || !wall_.contains ((int) x, (int) y)) return -1;
    for (size_t i = 0; i < pg->controls.size(); ++i)
        if (screenRect ((int) i).contains ((int) x, (int) y)) return (int) i;
    return -1;
}

int PanelUI::hitStep (int ctrl, float x) const
{
    const Page* pg = current();
    if (!pg || ctrl < 0) return -1;
    const auto& c = pg->controls[(size_t) ctrl];
    if (c.spec->kind != Kind::Steps) return -1;
    const Rect r = screenRect (ctrl);
    const int n = (int) c.spec->params.size();
    return std::clamp ((int) ((x - r.x) * n / std::max (1, r.w)), 0, n - 1);
}

Item PanelUI::hitItem (float x, float y) const
{
    for (size_t i = 0; i < (size_t) Item::Count; ++i)
        if (items_[i].contains ((int) x, (int) y)) return (Item) i;
    return Item::Count;
}

// Popups -------------------------------------------------------------------------------------------------------

void PanelUI::layoutPopup (const Rect& anchor)
{
    const int n = (int) popupItems_.size();
    int itemW = 120;
    for (const auto& s : popupItems_) itemW = std::max (itemW, Canvas::textWidth (s) + 22);
    itemW = std::min (itemW, 300);
    const int itemH = 22;
    const int maxRows = std::max (4, (h_ - kTopH - 40) / itemH);
    int cols = (n + maxRows - 1) / maxRows;
    while (cols > 1 && cols * itemW + 12 > w_ - 20) itemW = std::max (80, itemW - 10), cols = std::min (cols, (w_ - 32) / itemW);
    cols = std::max (1, cols);
    const int rows = (n + cols - 1) / cols;
    const int pw = cols * itemW + 12, ph = rows * itemH + 12;
    int px = std::clamp (anchor.x, 10, std::max (10, w_ - pw - 10));
    int py = anchor.y + anchor.h + 4;
    if (py + ph > h_ - 10) py = anchor.y - ph - 4;
    if (py < 10) py = std::clamp (h_ / 2 - ph / 2, 10, std::max (10, h_ - ph - 10));
    popupRect_ = { px, py, pw, ph };
    popupRects_.clear();
    for (int i = 0; i < n; ++i)
        popupRects_.push_back ({ px + 6 + (i / rows) * itemW, py + 6 + (i % rows) * itemH, itemW - 4, itemH - 2 });
    popupHover_ = -1;
    fullDirty_ = true;
}

void PanelUI::openChoices (int param, const Rect& anchor)
{
    const auto& p = p_->params[(size_t) param];
    if (p.range() > 400) return;
    popup_ = PopupKind::Choices;
    popupParam_ = param;
    popupItems_.clear();
    popupValues_.clear();
    const double cur = std::round (p_->displayValue (param));
    popupSelected_ = -1;
    for (double v = p.minVal; v <= p.maxVal + 1e-9; v += 1.0)
    {
        if (std::fabs (v - cur) < 0.5) popupSelected_ = (int) popupItems_.size();
        popupItems_.push_back (upper (p_->formatParamValue (p.id, v)));
        popupValues_.push_back (v);
    }
    layoutPopup (anchor);
}

void PanelUI::openPresets()
{
    if (p_->presets.empty()) return;
    popup_ = PopupKind::Presets;
    popupItems_.clear();
    popupValues_.clear();
    for (size_t i = 0; i < p_->presets.size(); ++i)
    {
        char buf[16];
        std::snprintf (buf, sizeof (buf), "%02d ", (int) i + 1);
        popupItems_.push_back (buf + upper (p_->presets[i].name));
        popupValues_.push_back ((double) i);
    }
    popupSelected_ = p_->currentPresetIndex;
    layoutPopup (items_[(size_t) Item::PresetBox]);
}

void PanelUI::openMenu (int param, float x, float y)
{
    if (param < 0) return;
    popup_ = PopupKind::Menu;
    popupParam_ = param;
    popupItems_ = { "MIDI LEARN", "RESET TO DEFAULT" };
    popupValues_ = { 0, 2 };
    const int cc = p_->findCcForParam (p_->params[(size_t) param].id);
    if (cc >= 0)
    {
        popupItems_.insert (popupItems_.begin() + 1, "CLEAR CC " + std::to_string (cc));
        popupValues_.insert (popupValues_.begin() + 1, 1);
    }
    popupSelected_ = -1;
    layoutPopup ({ (int) x, (int) y, 1, 1 });
}

void PanelUI::closePopup()
{
    if (popup_ == PopupKind::None) return;
    popup_ = PopupKind::None;
    popupItems_.clear();
    popupRects_.clear();
    fullDirty_ = true;
}

void PanelUI::popupPick (int i)
{
    if (i < 0 || i >= (int) popupValues_.size()) { closePopup(); return; }
    const PopupKind kind = popup_;
    const int param = popupParam_;
    const double v = popupValues_[(size_t) i];
    closePopup();
    if (kind == PopupKind::Choices)
    {
        p_->setValueOnce (param, v);
        lastTouched_ = param;
    }
    else if (kind == PopupKind::Presets)
        p_->loadPresetByIndex ((int) v);
    else if (kind == PopupKind::Menu)
    {
        const clap_id id = p_->params[(size_t) param].id;
        if (v == 0) p_->midiLearnTarget.store (id, std::memory_order_release);
        else if (v == 1) p_->clearCcForParam (id);
        else resetParam (param);
        fullDirty_ = true;
    }
}

// Live page ----------------------------------------------------------------------------------------------------

void PanelUI::screenInput (uint32_t kind, int value)
{
    p_->sendScreenInput (kind, value);
    if (kind == MELODEE_SCREEN_WHEEL) encoderAngle_ += value * 0.2618f;
    if (kind == MELODEE_SCREEN_PUSH) screenFlashAt_ = now_;
}

void PanelUI::paintScreen()
{
    if (!p_->innerScreen) return;
    const bool asked = p_->screenRequested.load (std::memory_order_acquire);
    if (!asked && now_ - lastScreenPaint_ < 33) return;
    lastScreenPaint_ = now_;
    if (p_->paintScreen (now_)) screenChanged_ = true;
}

// Input --------------------------------------------------------------------------------------------------------

void PanelUI::beginControlDrag (int ctrl, float x, float y, uint32_t mods)
{
    const Page* pg = current();
    const auto& c = pg->controls[(size_t) ctrl];
    drag_ = Drag::Control;
    dragCtrl_ = ctrl;
    dragParam_ = c.spec->params[0];
    dragStartNorm_ = norm (dragParam_);
    dragAccum_ = 0.0;
    (void) x;
    (void) y;
    (void) mods;
    p_->beginEdit (dragParam_);
    lastTouched_ = dragParam_;
}

void PanelUI::mouseDown (float x, float y, int button, uint32_t mods, int clicks)
{
    endWheelGesture();
    downX_ = lastX_ = x;
    downY_ = lastY_ = y;
    moved_ = false;
    downAt_ = now_;
    downMods_ = mods;
    longPressFired_ = false;
    hoverX_ = x;
    hoverY_ = y;

    if (popup_ != PopupKind::None)
    {
        if (popupRect_.contains ((int) x, (int) y))
        {
            for (size_t i = 0; i < popupRects_.size(); ++i)
                if (popupRects_[i].contains ((int) x, (int) y)) { popupPick ((int) i); return; }
            return;
        }
        closePopup();
        return; // the click only closes the list
    }

    const bool learnOn = p_->midiLearnTarget.load (std::memory_order_acquire) != CLAP_INVALID_ID;
    const int ctrl = hitControl (x, y);
    const Page* pg = current();

    if (button == 1)
    {
        if (ctrl >= 0)
        {
            const auto& sp = *pg->controls[(size_t) ctrl].spec;
            const int param = sp.kind == Kind::Steps ? sp.params[(size_t) hitStep (ctrl, x)] : sp.params[0];
            openMenu (param, x, y);
        }
        else
        {
            const Item it = hitItem (x, y);
            if (it >= Item::K1 && it <= Item::K3) openMenu (screenKnobParam ((int) it - (int) Item::K1), x, y);
            else if (it == Item::Screen)
            {
                const int sx = (int) ((x - screenRect_.x) / std::max (1, zoom_));
                const int sy = (int) ((y - screenRect_.y) / std::max (1, zoom_));
                if (sy >= 174)
                    openMenu (screenKnobParam (std::clamp (sx * 3 / MELODEE_SCREEN_WIDTH, 0, 2)), x, y);
                else
                    screenInput (MELODEE_SCREEN_NUDGE, sx < MELODEE_SCREEN_WIDTH / 2 ? -1 : 1);
            }
        }
        return;
    }

    const Item it = hitItem (x, y);
    if (it != Item::Count)
    {
        switch (it)
        {
            case Item::PresetPrev:
                if (!p_->presets.empty())
                    p_->loadPresetByIndex (p_->currentPresetIndex <= 0 ? (int) p_->presets.size() - 1 : p_->currentPresetIndex - 1);
                return;
            case Item::PresetNext:
                if (!p_->presets.empty()) p_->loadPresetByIndex ((p_->currentPresetIndex + 1) % (int) p_->presets.size());
                return;
            case Item::PresetBox: openPresets(); return;
            case Item::Learn:
                p_->midiLearnTarget.store (learnOn ? CLAP_INVALID_ID : DesktopPlugin::kLearnArmedNoParam, std::memory_order_release);
                fullDirty_ = true;
                return;
            case Item::ClearCc:
                p_->clearAllCc();
                p_->midiLearnTarget.store (CLAP_INVALID_ID, std::memory_order_release);
                fullDirty_ = true;
                return;
            case Item::Zoom:
                zoomChoice_ = (zoom_ == 2) ? 1 : 2;
                layoutDirty_ = true;
                return;
            case Item::Readout: return;
            case Item::Screen:
            {
                const int sx = (int) ((x - screenRect_.x) / std::max (1, zoom_));
                const int sy = (int) ((y - screenRect_.y) / std::max (1, zoom_));
                // Bottom K1-K3 strip on the live MPC page: click/drag directly controls K1, K2, or K3.
                if (sy >= 174)
                {
                    const int k = std::clamp (sx * 3 / MELODEE_SCREEN_WIDTH, 0, 2);
                    const int param = screenKnobParam (k);
                    if (param >= 0)
                    {
                        if (learnOn) { p_->midiLearnTarget.store (p_->params[(size_t) param].id, std::memory_order_release); fullDirty_ = true; return; }
                        if ((mods & (kModAlt | kModCmd)) || clicks >= 2) { resetParam (param); return; }
                        drag_ = Drag::ScreenKnob;
                        dragItem_ = (Item) ((int) Item::K1 + k);
                        dragParam_ = param;
                        dragStartNorm_ = norm (param);
                        dragAccum_ = 0;
                        p_->beginEdit (param);
                        lastTouched_ = param;
                        return;
                    }
                }
                // Cortex Filter 3D cube area: direct 2D XY drag controls Morph (X) and Freq (Y).
                if (p_->pluginId == "com.hdslave.cortex-filter" && sy >= 34 && sy < 174 && sx < 175 && p_->params.size() >= 2)
                {
                    drag_ = Drag::ScreenXY;
                    dragParam_ = 0;  // Morph
                    dragParamY_ = 1; // Freq
                    dragStartNorm_ = norm (dragParam_);
                    dragStartNormY_ = norm (dragParamY_);
                    p_->beginEdit (dragParam_);
                    p_->beginEdit (dragParamY_);
                    lastTouched_ = dragParam_;
                    return;
                }
                drag_ = Drag::Screen;
                dragAccum_ = 0;
                return;
            }
            case Item::Encoder: drag_ = Drag::Encoder; dragAccum_ = 0; return;
            case Item::Minus: screenInput (MELODEE_SCREEN_NUDGE, -1); drag_ = Drag::ScreenButton; dragItem_ = it; return;
            case Item::Plus: screenInput (MELODEE_SCREEN_NUDGE, 1); drag_ = Drag::ScreenButton; dragItem_ = it; return;
            case Item::Push: screenInput (MELODEE_SCREEN_PUSH, 1); drag_ = Drag::ScreenButton; dragItem_ = it; return;
            case Item::Shift: p_->screenShift = !p_->screenShift; p_->screenRequested = true; return;
            case Item::K1:
            case Item::K2:
            case Item::K3:
            {
                const int param = screenKnobParam ((int) it - (int) Item::K1);
                if (param < 0) return;
                if (learnOn) { p_->midiLearnTarget.store (p_->params[(size_t) param].id, std::memory_order_release); fullDirty_ = true; return; }
                if ((mods & (kModAlt | kModCmd)) || clicks >= 2) { resetParam (param); return; }
                drag_ = Drag::ScreenKnob;
                dragItem_ = it;
                dragParam_ = param;
                dragStartNorm_ = norm (param);
                dragAccum_ = 0;
                p_->beginEdit (param);
                lastTouched_ = param;
                return;
            }
            default: return;
        }
    }

    for (size_t i = 0; i < pages_.size(); ++i)
        if (pages_[i].tab.contains ((int) x, (int) y))
        {
            if ((int) i != page_) { page_ = (int) i; scroll_ = 0; hoverCtrl_ = -1; fullDirty_ = true; }
            return;
        }

    if (pg && pg->contentH > wall_.h && x >= wall_.x + wall_.w - kScrollW - 2 && wall_.contains ((int) x, (int) y))
    {
        drag_ = Drag::Scrollbar;
        dragAccum_ = scroll_;
        return;
    }

    if (ctrl < 0)
    {
        if (pg && pg->contentH > wall_.h && wall_.contains ((int) x, (int) y)) { drag_ = Drag::WallScroll; dragAccum_ = scroll_; }
        return;
    }

    const auto& sp = *pg->controls[(size_t) ctrl].spec;
    const int step = sp.kind == Kind::Steps ? hitStep (ctrl, x) : -1;
    const int param = sp.kind == Kind::Steps ? sp.params[(size_t) step] : sp.params[0];
    lastTouched_ = param;

    if (learnOn)
    {
        p_->midiLearnTarget.store (p_->params[(size_t) param].id, std::memory_order_release);
        fullDirty_ = true;
        return;
    }
    if ((mods & (kModAlt | kModCmd)) || (clicks >= 2 && sp.kind != Kind::Toggle && sp.kind != Kind::Button))
    {
        resetParam (param);
        return;
    }

    switch (sp.kind)
    {
        case Kind::Knob: beginControlDrag (ctrl, x, y, mods); break;
        case Kind::Toggle:
        {
            const auto& p = p_->params[(size_t) param];
            p_->setValueOnce (param, p_->displayValue (param) >= (p.minVal + p.maxVal) * 0.5 ? p.minVal : p.maxVal);
            drag_ = Drag::Button;
            dragCtrl_ = ctrl;
            break;
        }
        case Kind::Button:
            p_->beginEdit (param);
            setParam (param, p_->params[(size_t) param].maxVal);
            drag_ = Drag::Button;
            dragCtrl_ = ctrl;
            dragParam_ = param;
            break;
        case Kind::Selector:
        case Kind::Stepper:
        {
            const Rect r = screenRect (ctrl);
            if (sp.kind == Kind::Stepper && (x < r.x + 22 || x > r.x + r.w - 22))
            {
                p_->setValueOnce (param, std::clamp (std::round (p_->displayValue (param)) + (x < r.x + 22 ? -1 : 1),
                                                     p_->params[(size_t) param].minVal, p_->params[(size_t) param].maxVal));
                drag_ = Drag::Button;
                dragCtrl_ = ctrl;
                break;
            }
            beginControlDrag (ctrl, x, y, mods);
            break;
        }
        case Kind::Steps:
        {
            drag_ = Drag::Step;
            dragCtrl_ = ctrl;
            dragStep_ = step;
            dragParam_ = param;
            p_->beginEdit (param);
            if (mods & kModShift)
                dragStartNorm_ = norm (param);
            else
            {
                const Rect r = screenRect (ctrl);
                const int top = r.y + 22, bottom = r.y + r.h - 32;
                setParam (param, fromNorm (param, 1.0 - (y - top) / std::max (1, bottom - top)));
            }
            break;
        }
    }
}

void PanelUI::mouseMove (float x, float y, uint32_t mods)
{
    const float dx = x - lastX_, dy = y - lastY_;
    lastX_ = x;
    lastY_ = y;
    hoverX_ = x;
    hoverY_ = y;
    if (std::fabs (x - downX_) > 3 || std::fabs (y - downY_) > 3) moved_ = true;
    const bool fine = (mods & kModShift) != 0;

    switch (drag_)
    {
        case Drag::None:
        {
            if (popup_ != PopupKind::None)
            {
                int hv = -1;
                for (size_t i = 0; i < popupRects_.size(); ++i)
                    if (popupRects_[i].contains ((int) x, (int) y)) hv = (int) i;
                if (hv != popupHover_) { popupHover_ = hv; fullDirty_ = true; }
                return;
            }
            hoverCtrl_ = hitControl (x, y);
            hoverStep_ = hitStep (hoverCtrl_, x);
            hoverItem_ = hitItem (x, y);
            return;
        }
        case Drag::Control:
        case Drag::ScreenKnob:
        {
            const auto& p = p_->params[(size_t) dragParam_];
            const double travel = (double) dx - (double) dy; // up or right turns it up
            if (p.isStepped())
            {
                const double ppStep = std::clamp (240.0 / std::max (1.0, p.range()), 4.0, 26.0) * (fine ? 4.0 : 1.0);
                dragAccum_ += travel / ppStep;
                const double start = p.minVal + dragStartNorm_ * p.range();
                setParam (dragParam_, std::clamp (std::round (start + dragAccum_), p.minVal, p.maxVal));
            }
            else
            {
                dragAccum_ += travel / (fine ? 1800.0 : 240.0);
                setParam (dragParam_, fromNorm (dragParam_, dragStartNorm_ + dragAccum_));
            }
            return;
        }
        case Drag::Step:
        {
            const Page* pg = current();
            const auto& sp = *pg->controls[(size_t) dragCtrl_].spec;
            const int step = hitStep (dragCtrl_, x);
            if (fine)
            {
                dragAccum_ -= dy / 600.0;
                setParam (dragParam_, fromNorm (dragParam_, dragStartNorm_ + dragAccum_));
                return;
            }
            if (step != dragStep_)
            {
                p_->endEdit (dragParam_);
                dragStep_ = step;
                dragParam_ = sp.params[(size_t) step];
                p_->beginEdit (dragParam_);
            }
            const Rect r = screenRect (dragCtrl_);
            const int top = r.y + 22, bottom = r.y + r.h - 32;
            setParam (dragParam_, fromNorm (dragParam_, 1.0 - (y - top) / std::max (1, bottom - top)));
            hoverStep_ = step;
            return;
        }
        case Drag::Encoder:
        case Drag::Screen:
        {
            dragAccum_ += (double) dx - (double) dy;
            const double per = fine ? 30.0 : 12.0;
            while (dragAccum_ >= per) { screenInput (MELODEE_SCREEN_WHEEL, 1); dragAccum_ -= per; }
            while (dragAccum_ <= -per) { screenInput (MELODEE_SCREEN_WHEEL, -1); dragAccum_ += per; }
            return;
        }
        case Drag::ScreenXY:
        {
            const double scale = fine ? 900.0 : 180.0;
            dragStartNorm_ = std::clamp (dragStartNorm_ + (double) dx / scale, 0.0, 1.0);
            dragStartNormY_ = std::clamp (dragStartNormY_ - (double) dy / scale, 0.0, 1.0);
            setParam (dragParam_, fromNorm (dragParam_, dragStartNorm_));
            setParam (dragParamY_, fromNorm (dragParamY_, dragStartNormY_));
            return;
        }
        case Drag::Scrollbar:
        {
            const Page* pg = current();
            const double ratio = (double) pg->contentH / std::max (1, wall_.h);
            scroll_ = std::clamp ((int) (dragAccum_ + (y - downY_) * ratio), 0, std::max (0, pg->contentH - wall_.h));
            fullDirty_ = true;
            return;
        }
        case Drag::WallScroll:
        {
            const Page* pg = current();
            scroll_ = std::clamp ((int) (dragAccum_ - (y - downY_)), 0, std::max (0, pg->contentH - wall_.h));
            fullDirty_ = true;
            return;
        }
        default: return;
    }
}

void PanelUI::mouseUp (float x, float y, int button, uint32_t mods)
{
    (void) button;
    switch (drag_)
    {
        case Drag::Control:
        {
            p_->endEdit (dragParam_);
            const Page* pg = current();
            const auto& sp = *pg->controls[(size_t) dragCtrl_].spec;
            if (!moved_ && (sp.kind == Kind::Selector || sp.kind == Kind::Stepper)) openChoices (dragParam_, screenRect (dragCtrl_));
            break;
        }
        case Drag::ScreenKnob: p_->endEdit (dragParam_); break;
        case Drag::ScreenXY:
            p_->endEdit (dragParam_);
            p_->endEdit (dragParamY_);
            dragParamY_ = -1;
            break;
        case Drag::Step: p_->endEdit (dragParam_); break;
        case Drag::Button:
        {
            const Page* pg = current();
            if (pg && dragCtrl_ >= 0 && pg->controls[(size_t) dragCtrl_].spec->kind == Kind::Button)
            {
                setParam (dragParam_, p_->params[(size_t) dragParam_].minVal);
                p_->endEdit (dragParam_);
            }
            break;
        }
        case Drag::Screen:
            if (!moved_) screenInput (MELODEE_SCREEN_PUSH, 1);
            break;
        default: break;
    }
    drag_ = Drag::None;
    dragCtrl_ = -1;
    dragItem_ = Item::Count;
    if (mods & kModTouch) { hoverCtrl_ = -1; hoverItem_ = Item::Count; }
    else mouseMove (x, y, mods);
}

void PanelUI::mouseExit()
{
    if (drag_ != Drag::None) return;
    hoverCtrl_ = hoverStep_ = -1;
    hoverItem_ = Item::Count;
}

void PanelUI::wheel (float x, float y, float dx, float dy, bool precise, uint32_t mods)
{
    (void) dx;
    if (popup_ != PopupKind::None || dy == 0.0f) return;
    const bool fine = (mods & kModShift) != 0;
    const Item it = hitItem (x, y);

    int param = -1;
    if (it == Item::Screen)
    {
        const int sx = (int) ((x - screenRect_.x) / std::max (1, zoom_));
        const int sy = (int) ((y - screenRect_.y) / std::max (1, zoom_));
        if (sy >= 174)
            param = screenKnobParam (std::clamp (sx * 3 / MELODEE_SCREEN_WIDTH, 0, 2));
        if (param < 0)
        {
            screenWheelAccum_ += precise ? dy / 14.0f : dy;
            while (screenWheelAccum_ >= 1.0f) { screenInput (MELODEE_SCREEN_WHEEL, 1); screenWheelAccum_ -= 1.0f; }
            while (screenWheelAccum_ <= -1.0f) { screenInput (MELODEE_SCREEN_WHEEL, -1); screenWheelAccum_ += 1.0f; }
            return;
        }
    }
    else if (it == Item::Encoder)
    {
        screenWheelAccum_ += precise ? dy / 14.0f : dy;
        while (screenWheelAccum_ >= 1.0f) { screenInput (MELODEE_SCREEN_WHEEL, 1); screenWheelAccum_ -= 1.0f; }
        while (screenWheelAccum_ <= -1.0f) { screenInput (MELODEE_SCREEN_WHEEL, -1); screenWheelAccum_ += 1.0f; }
        return;
    }

    if (param < 0 && it >= Item::K1 && it <= Item::K3) param = screenKnobParam ((int) it - (int) Item::K1);
    const int ctrl = hitControl (x, y);
    if (param < 0 && ctrl >= 0)
    {
        const auto& sp = *current()->controls[(size_t) ctrl].spec;
        if (sp.kind == Kind::Button) return;
        param = sp.kind == Kind::Steps ? sp.params[(size_t) hitStep (ctrl, x)] : sp.params[0];
    }

    if (param >= 0)
    {
        const auto& p = p_->params[(size_t) param];
        if (p.isStepped())
        {
            if (wheelParam_ != param) wheelAccum_ = 0.0f;
            wheelAccum_ += precise ? dy / 16.0f : dy;
            const int steps = (int) wheelAccum_;
            wheelAccum_ -= (float) steps;
            if (steps != 0) stepParam (param, steps);
            else if (wheelParam_ != param) { endWheelGesture(); p_->beginEdit (param); wheelParam_ = param; wheelAt_ = now_; }
        }
        else
        {
            const double amount = (precise ? dy / 300.0 : dy * 0.02) * (fine ? 0.1 : 1.0);
            if (wheelParam_ != param) { endWheelGesture(); p_->beginEdit (param); wheelParam_ = param; }
            wheelAt_ = now_;
            setParam (param, fromNorm (param, norm (param) + amount));
        }
        return;
    }

    if (tabsRow_.contains ((int) x, (int) y) && !pages_.empty())
    {
        page_ = std::clamp (page_ + (dy < 0 ? 1 : -1), 0, (int) pages_.size() - 1);
        scroll_ = 0;
        fullDirty_ = true;
        return;
    }

    const Page* pg = current();
    if (pg && pg->contentH > wall_.h && wall_.contains ((int) x, (int) y))
    {
        scroll_ = std::clamp (scroll_ - (int) (precise ? dy : dy * 40.0f), 0, pg->contentH - wall_.h);
        fullDirty_ = true;
    }
}

void PanelUI::rotate (float x, float y, float degrees)
{
    int param = -1;
    const Item it = hitItem (x, y);
    if (it == Item::Screen || it == Item::Encoder)
    {
        screenWheelAccum_ -= degrees / 15.0f;
        while (screenWheelAccum_ >= 1.0f) { screenInput (MELODEE_SCREEN_WHEEL, 1); screenWheelAccum_ -= 1.0f; }
        while (screenWheelAccum_ <= -1.0f) { screenInput (MELODEE_SCREEN_WHEEL, -1); screenWheelAccum_ += 1.0f; }
        return;
    }
    if (it >= Item::K1 && it <= Item::K3) param = screenKnobParam ((int) it - (int) Item::K1);
    const int ctrl = hitControl (x, y);
    if (param < 0 && ctrl >= 0)
    {
        const auto& sp = *current()->controls[(size_t) ctrl].spec;
        if (sp.kind == Kind::Knob || sp.kind == Kind::Steps) param = sp.kind == Kind::Steps ? sp.params[(size_t) hitStep (ctrl, x)] : sp.params[0];
    }
    if (param < 0) return;
    if (wheelParam_ != param) { endWheelGesture(); p_->beginEdit (param); wheelParam_ = param; }
    wheelAt_ = now_;
    setParam (param, fromNorm (param, norm (param) - degrees / 270.0));
}

bool PanelUI::key (int k)
{
    if (k == 5)
    {
        if (popup_ != PopupKind::None) { closePopup(); return true; }
        if (p_->midiLearnTarget.load() != CLAP_INVALID_ID) { p_->midiLearnTarget.store (CLAP_INVALID_ID); fullDirty_ = true; return true; }
        return false;
    }
    if (!p_->innerScreen) return false;
    switch (k)
    {
        case 0: screenInput (MELODEE_SCREEN_NUDGE, -1); return true;
        case 1: screenInput (MELODEE_SCREEN_NUDGE, 1); return true;
        case 2: screenInput (MELODEE_SCREEN_WHEEL, 1); return true;
        case 3: screenInput (MELODEE_SCREEN_WHEEL, -1); return true;
        case 4: screenInput (MELODEE_SCREEN_PUSH, 1); return true;
        default: return false;
    }
}

// Update -------------------------------------------------------------------------------------------------------

Rect PanelUI::update (uint64_t now)
{
    now_ = now;
    if (layoutDirty_ || frame_.size() != (size_t) w_ * scale_ * (size_t) h_ * scale_) layout();

    if (wheelParam_ >= 0 && now_ - wheelAt_ > 400) endWheelGesture();

    // Touch: a long press without moving opens the control's menu (MIDI learn, reset).
    if ((downMods_ & kModTouch) && !moved_ && !longPressFired_ && now_ - downAt_ > 550
        && (drag_ == Drag::Control || drag_ == Drag::Step || drag_ == Drag::ScreenKnob))
    {
        longPressFired_ = true;
        p_->endEdit (dragParam_);
        const int param = dragParam_;
        drag_ = Drag::None;
        openMenu (param, downX_, downY_);
    }

    paintScreen();

    const auto t0 = std::chrono::steady_clock::now();
    dirty_ = {};
    if (fullDirty_)
        renderAll();
    else
    {
        Page* pg = current();
        bool popupOpen = popup_ != PopupKind::None;
        bool changed = false;
        if (pg)
            for (size_t i = 0; i < pg->controls.size(); ++i)
            {
                const Rect sr = screenRect ((int) i).intersect (wall_);
                if (sr.empty()) continue;
                const uint64_t look = controlLook ((int) i);
                if (look == pg->controls[i].drawn) continue;
                changed = true;
                if (popupOpen) break;
                pg->controls[i].drawn = look;
                restore (sr);
                c_.setClip (sr);
                drawControl ((int) i);
                c_.resetClip();
                markDirty (sr);
            }
        for (size_t i = 0; i < (size_t) Item::Count && !(popupOpen && changed); ++i)
        {
            if (items_[i].empty()) continue;
            const uint64_t look = itemLook ((Item) i);
            if (look == itemDrawn_[i]) continue;
            changed = true;
            if (popupOpen) break;
            itemDrawn_[i] = look;
            restore (items_[i]);
            c_.setClip (items_[i]);
            drawItem ((Item) i);
            c_.resetClip();
            markDirty (items_[i]);
        }
        if (popupOpen && changed) renderAll();
    }
    screenChanged_ = false;
    lastRenderMs_ = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    const Rect d = dirty_;
    dirty_ = {};
    return d;
}

void PanelUI::restore (const Rect& r)
{
    const Rect pr = Rect { r.x * scale_, r.y * scale_, r.w * scale_, r.h * scale_ }.intersect ({ 0, 0, physW_, physH_ });
    for (int y = pr.y; y < pr.y + pr.h; ++y)
        std::memcpy (&frame_[(size_t) y * (size_t) physW_ + (size_t) pr.x], &base_[(size_t) y * (size_t) physW_ + (size_t) pr.x], (size_t) pr.w * 4);
}

uint64_t PanelUI::controlLook (int index) const
{
    const Page* pg = current();
    const auto& c = pg->controls[(size_t) index];
    Hash h;
    const uint32_t learn = p_->midiLearnTarget.load (std::memory_order_relaxed);
    for (int param : c.spec->params)
    {
        h.d (p_->displayValue (param));
        const clap_id id = p_->params[(size_t) param].id;
        h.u ((uint64_t) (p_->findCcForParam (id) + 1));
        if (learn == id) h.u (0x1000 + (now_ / 300) % 2);
    }
    h.u (learn == DesktopPlugin::kLearnArmedNoParam ? 7 : 3);
    h.u (hoverCtrl_ == index ? 11u + (uint64_t) (hoverStep_ + 1) : 5u);
    h.u (dragCtrl_ == index ? 13u + (uint64_t) (dragStep_ + 1) : 2u);
    return h.h;
}

uint64_t PanelUI::itemLook (Item it) const
{
    Hash h;
    h.u ((uint64_t) it);
    h.u (hoverItem_ == it ? 1 : 0);
    h.u ((drag_ != Drag::None && dragItem_ == it) ? 1 : 0);
    const uint32_t learn = p_->midiLearnTarget.load (std::memory_order_relaxed);
    switch (it)
    {
        case Item::PresetBox:
            h.u ((uint64_t) (p_->currentPresetIndex + 1));
            h.u (popup_ == PopupKind::Presets ? 1 : 0);
            break;
        case Item::Learn: h.u (learn); h.u (learn != CLAP_INVALID_ID ? (now_ / 400) % 2 : 0); break;
        case Item::ClearCc: h.u ((uint64_t) p_->countMappedCc()); break;
        case Item::Zoom: h.u ((uint64_t) zoom_); break;
        case Item::Readout:
        {
            bool learnLine = false;
            h.s (readoutText (learnLine));
            break;
        }
        case Item::Screen:
            h.u (screenChanged_ ? now_ : 0);
            h.u (now_ - screenFlashAt_ < 140 ? 1 : 0);
            break;
        case Item::Encoder: h.d ((double) encoderAngle_); h.u (drag_ == Drag::Encoder || drag_ == Drag::Screen); break;
        case Item::Shift: h.u (p_->screenShift ? 1 : 0); break;
        case Item::K1:
        case Item::K2:
        case Item::K3:
        {
            const int param = screenKnobParam ((int) it - (int) Item::K1);
            h.u ((uint64_t) (param + 1));
            if (param >= 0)
            {
                h.d (p_->displayValue (param));
                h.u ((uint64_t) (p_->findCcForParam (p_->params[(size_t) param].id) + 1));
                if (learn == p_->params[(size_t) param].id) h.u (0x1000 + (now_ / 300) % 2);
            }
            break;
        }
        default: break;
    }
    return h.h;
}

// Drawing ------------------------------------------------------------------------------------------------------

void PanelUI::renderBase()
{
    const auto& t = p_->theme;
    c_.begin (base_.data(), physW_, physH_, scale_);
    c_.gradient ({ 0, 0, w_, h_ }, t.bgTop, t.bgBottom);

    // Top bar: the pages' purple strip.
    c_.gradient (top_, t.headerTop, t.headerBottom);
    c_.fill (0, kTopH - 2, w_, 1, mix (t.headerTop, t.accent2, 0.6f));
    c_.fill (0, kTopH - 1, w_, 1, 0xFF05030A);
    const std::string name = upper (p_->pluginName.rfind ("hdslave ", 0) == 0 ? p_->pluginName.substr (8) : p_->pluginName);
    c_.text (18, 13, name, mix (t.accent2, 0xFF000000, 0.35f), 2);
    c_.text (16, 11, name, t.accent, 2);

    // Live page card.
    if (!screenCard_.empty())
    {
        c_.fillChamfer (screenCard_, 8, t.panel);
        c_.frameChamfer (screenCard_, 8, t.border);
        c_.tag (screenCard_.x + 10, screenCard_.y + 5, "LIVE PAGE", t.accent, t.onAccent);
        c_.text (screenCard_.x + 86, screenCard_.y + 6,
                 Canvas::fit (zoom_ == 2 ? "DRAG / SCROLL = ENCODER   CLICK = PUSH   ARROWS = -/+" : "DRAG / SCROLL = ENC  CLICK = PUSH", screenCard_.w - 96),
                 t.muted);
        const Rect bez { screenRect_.x - 6, screenRect_.y - 6, screenRect_.w + 12, screenRect_.h + 12 };
        c_.fill (bez, t.screenBezel);
        c_.frame (bez, mix (t.border, t.accent2, 0.3f));
        c_.hline (hwRow_.x, hwRow_.y - 4, hwRow_.w, t.border);
        c_.text (items_[(size_t) Item::Minus].x, items_[(size_t) Item::Minus].y - 18, "NAVIGATE", t.muted);
    }

    // Tabs.
    for (size_t i = 0; i < pages_.size(); ++i)
    {
        const auto& tab = pages_[i].tab;
        const bool on = (int) i == page_;
        c_.fillChamfer (tab, 4, on ? t.accent : t.panel);
        if (!on) c_.frameChamfer (tab, 4, t.border);
        c_.textAligned (tab, pages_[i].label, on ? t.onAccent : t.text, 1, 1);
    }

    // Cards.
    if (const Page* pg = current())
    {
        c_.setClip (wall_);
        for (const auto& card : pg->cards)
        {
            const Rect r { wall_.x + card.r.x, wall_.y + card.r.y - scroll_, card.r.w, card.r.h };
            if (r.intersect (wall_).empty()) continue;
            c_.fillChamfer (r, 7, t.panel);
            c_.frameChamfer (r, 7, t.border);
            c_.fill (r.x + 1, r.y + kCardTitleH, r.w - 2, 1, mix (t.panel, t.border, 0.6f));
            c_.tag (r.x + 9, r.y + 5, card.section->title, mix (t.panel, t.accent2, 0.45f), t.text);
        }
        c_.resetClip();
        drawScrollbar();
    }
}

void PanelUI::drawScrollbar()
{
    const Page* pg = current();
    if (!pg || pg->contentH <= wall_.h) return;
    const auto& t = p_->theme;
    const Rect track { wall_.x + wall_.w - kScrollW + 2, wall_.y, kScrollW - 4, wall_.h };
    c_.fill (track, t.panel);
    const int th = std::max (30, wall_.h * wall_.h / pg->contentH);
    const int ty = wall_.y + (int) ((int64_t) (wall_.h - th) * scroll_ / std::max (1, pg->contentH - wall_.h));
    c_.fill (track.x, ty, track.w, th, t.accent2);
}

void PanelUI::renderAll()
{
    renderBase();
    std::memcpy (frame_.data(), base_.data(), frame_.size() * 4);
    c_.begin (frame_.data(), physW_, physH_, scale_);
    if (Page* pg = current())
        for (size_t i = 0; i < pg->controls.size(); ++i)
        {
            const Rect sr = screenRect ((int) i).intersect (wall_);
            pg->controls[i].drawn = controlLook ((int) i);
            if (sr.empty()) continue;
            c_.setClip (sr);
            drawControl ((int) i);
        }
    c_.resetClip();
    for (size_t i = 0; i < (size_t) Item::Count; ++i)
    {
        itemDrawn_[i] = itemLook ((Item) i);
        if (items_[i].empty()) continue;
        c_.setClip (items_[i]);
        drawItem ((Item) i);
    }
    c_.resetClip();
    if (popup_ != PopupKind::None) drawPopup();
    fullDirty_ = false;
    dirty_ = { 0, 0, w_, h_ };
}

void PanelUI::drawBadges (const Rect& r, int param)
{
    const auto& t = p_->theme;
    const clap_id id = p_->params[(size_t) param].id;
    const int cc = p_->findCcForParam (id);
    if (cc >= 0)
    {
        const std::string s = std::to_string (cc);
        const int w = Canvas::textWidth (s) + 6;
        c_.fill (r.x + r.w - w - 3, r.y + 3, w, 13, t.accent2);
        c_.text (r.x + r.w - w, r.y + 4, s, t.onAccent);
    }
    const uint32_t learn = p_->midiLearnTarget.load (std::memory_order_relaxed);
    if (learn == id)
        c_.frameChamfer (r.inset (1), 5, t.learn, (now_ / 300) % 2 ? 255 : 120);
    else if (learn == DesktopPlugin::kLearnArmedNoParam)
        c_.frameChamfer (r.inset (1), 5, t.learn, 90);
}

void PanelUI::drawKnobCell (const Rect& r, int param, const std::string& label, bool hot, bool big)
{
    const auto& t = p_->theme;
    const auto& p = p_->params[(size_t) param];
    if (hot) c_.fillChamfer (r.inset (2), 5, t.panelHi);
    const int maxChars = std::max (3, (r.w - 8) / Canvas::glyphW);
    const auto lines = wrap2 (label, maxChars);
    const int ly = r.y + (big ? 18 : 4);
    if (lines.size() == 1)
        c_.textAligned ({ r.x, ly + (big ? 6 : 12), r.w, 12 }, lines[0], hot ? t.text : t.muted, 1, 1);
    else
        for (size_t i = 0; i < 2; ++i) c_.textAligned ({ r.x, ly + (int) i * 11, r.w, 12 }, lines[i], hot ? t.text : t.muted, 1, 1);
    const float radius = big ? 26.0f : 20.0f;
    const float cy = (float) (big ? r.y + 72 : r.y + 51);
    c_.knob ((float) r.x + r.w * 0.5f, cy, radius, (float) norm (param), p.isBipolar(), t.accent, mix (t.panel, t.border, 0.9f),
             0xFF0C0816, mix (t.border, t.text, 0.25f), hot);
    c_.textAligned ({ r.x + 2, (int) (cy + radius + 4), r.w - 4, 12 }, upper (valueText (param)), hot ? t.accent : t.text, 1, 1);
    drawBadges (r, param);
}

void PanelUI::drawToggle (const Rect& r, int param, const std::string& label, bool hot)
{
    const auto& t = p_->theme;
    const auto& p = p_->params[(size_t) param];
    if (hot) c_.fillChamfer (r.inset (2), 5, t.panelHi);
    const auto lines = wrap2 (label, std::max (3, (r.w - 8) / Canvas::glyphW));
    for (size_t i = 0; i < lines.size(); ++i)
        c_.textAligned ({ r.x, r.y + 4 + (int) (lines.size() == 1 ? 12 : i * 12), r.w, 12 }, lines[i], hot ? t.text : t.muted, 1, 1);
    const bool on = p_->displayValue (param) >= (p.minVal + p.maxVal) * 0.5;
    const std::string v = upper (valueText (param));
    const int pw = std::min (r.w - 12, std::max (56, Canvas::textWidth (v) + 26));
    const Rect pill { r.x + (r.w - pw) / 2, r.y + 38, pw, 26 };
    c_.fillChamfer (pill, 5, on ? t.accent : 0xFF0C0816);
    c_.frameChamfer (pill, 5, on ? mix (t.accent, 0xFFFFFFFF, 0.3f) : (hot ? t.accent : t.border));
    c_.fill (pill.x + 7, pill.y + 10, 6, 6, on ? t.onAccent : mix (t.border, t.muted, 0.4f));
    c_.textAligned ({ pill.x + 16, pill.y, pill.w - 20, pill.h }, v, on ? t.onAccent : t.muted, 1, 1);
    drawBadges (r, param);
}

void PanelUI::drawButton (const Rect& r, int param, const std::string& label, bool hot, bool down)
{
    const auto& t = p_->theme;
    if (hot) c_.fillChamfer (r.inset (2), 5, t.panelHi);
    const auto lines = wrap2 (label, std::max (3, (r.w - 8) / Canvas::glyphW));
    for (size_t i = 0; i < lines.size(); ++i)
        c_.textAligned ({ r.x, r.y + 4 + (int) (lines.size() == 1 ? 12 : i * 12), r.w, 12 }, lines[i], hot ? t.text : t.muted, 1, 1);
    const Rect b { r.x + 10, r.y + 36, r.w - 20, 32 };
    c_.fillChamfer (b, 6, down ? t.accent : 0xFF0C0816);
    c_.frameChamfer (b, 6, down ? t.accent : (hot ? t.accent : t.border));
    c_.fillChamfer (b.inset (3), 4, down ? mix (t.accent, 0xFFFFFFFF, 0.25f) : t.panelHi);
    c_.textAligned (b, down ? "PRESSED" : "PRESS", down ? t.onAccent : t.text, 1, 1);
    drawBadges (r, param);
}

void PanelUI::drawSelector (const Rect& r, int param, const std::string& label, bool hot, bool stepper)
{
    const auto& t = p_->theme;
    const auto& p = p_->params[(size_t) param];
    if (hot) c_.fillChamfer (r.inset (2), 5, t.panelHi);
    const auto lines = wrap2 (label, std::max (3, (r.w - 8) / Canvas::glyphW));
    for (size_t i = 0; i < lines.size(); ++i)
        c_.textAligned ({ r.x, r.y + 4 + (int) (lines.size() == 1 ? 12 : i * 12), r.w, 12 }, lines[i], hot ? t.text : t.muted, 1, 1);
    const Rect b { r.x + 5, r.y + 36, r.w - 10, 28 };
    c_.fillChamfer (b, 4, 0xFF0C0816);
    c_.frameChamfer (b, 4, hot ? t.accent : t.border);
    const int padL = stepper ? 14 : 6;
    const int padR = stepper ? 14 : 18;
    c_.textAligned ({ b.x + padL, b.y, std::max (8, b.w - padL - padR), b.h }, upper (valueText (param)), t.text, 1, 1);
    if (stepper)
    {
        c_.text (b.x + 5, b.y + 8, "<", t.accent2);
        c_.text (b.x + b.w - 11, b.y + 8, ">", t.accent2);
    }
    else
    {
        const float ax = (float) (b.x + b.w - 11), ay = (float) (b.y + b.h / 2 - 2);
        c_.line (ax - 3.5f, ay, ax, ay + 3.5f, 1.4f, t.accent);
        c_.line (ax, ay + 3.5f, ax + 3.5f, ay, 1.4f, t.accent);
    }
    // Position: LED segments for short lists, a bar otherwise; and "03/24".
    const int count = (int) std::round (p.range()) + 1;
    const int idx = (int) std::round (p_->displayValue (param) - p.minVal);
    const int sy = b.y + b.h + 4;
    if (count <= 24 && count > 1)
    {
        const int segW = std::max (2, (b.w - (count - 1)) / count);
        const int total = count * segW + (count - 1);
        const int sx = b.x + (b.w - total) / 2;
        for (int i = 0; i < count; ++i) c_.fill (sx + i * (segW + 1), sy, segW, 3, i == idx ? t.accent : mix (t.panel, t.border, 0.8f));
    }
    else if (count > 1)
    {
        c_.fill (b.x, sy, b.w, 3, mix (t.panel, t.border, 0.8f));
        c_.fill (b.x, sy, std::max (3, (int) (b.w * (double) idx / (count - 1))), 3, t.accent);
    }
    char buf[24];
    std::snprintf (buf, sizeof (buf), count < 100 ? "%02d/%02d" : "%d/%d", idx + 1, count);
    if (count <= 999) c_.textAligned ({ r.x, sy + 4, r.w, 12 }, buf, t.muted, 1, 1);
    drawBadges (r, param);
}

void PanelUI::drawSteps (const Rect& r, const ControlSpec& s, bool hot)
{
    const auto& t = p_->theme;
    const int n = (int) s.params.size();
    const Page* pg = current();
    int ctrlIndex = -1;
    for (size_t i = 0; pg && i < pg->controls.size(); ++i)
        if (pg->controls[i].spec == &s) ctrlIndex = (int) i;
    const int focus = (dragCtrl_ == ctrlIndex && drag_ == Drag::Step) ? dragStep_ : (hot ? hoverStep_ : -1);

    // Header: label and the focused step's value.
    c_.text (r.x + 6, r.y + 5, s.label, hot ? t.text : t.muted);
    if (focus >= 0)
    {
        const std::string v = std::to_string (focus + 1) + ": " + upper (valueText (s.params[(size_t) focus]));
        c_.text (r.x + r.w - 6 - Canvas::textWidth (v), r.y + 5, v, t.accent);
    }
    const int top = r.y + 22, bottom = r.y + r.h - 32;
    const double sw = (double) r.w / n;
    for (int i = 0; i < n; ++i)
    {
        const int param = s.params[(size_t) i];
        const auto& p = p_->params[(size_t) param];
        const int cx = r.x + (int) (sw * i + sw / 2);
        const int tw = std::max (8, std::min (18, (int) sw - 14));
        const Rect track { cx - tw / 2, top, tw, bottom - top };
        const bool f = i == focus;
        if (f) c_.fill ((int) (r.x + sw * i + 2), top - 2, (int) sw - 4, bottom - top + 4, t.panelHi);
        c_.fill (track, 0xFF0C0816);
        c_.frame (track, f ? t.accent : t.border);
        const double nv = norm (param);
        const int yv = bottom - (int) std::round (nv * (bottom - top));
        if (p.isBipolar())
        {
            const int y0 = (top + bottom) / 2;
            c_.fill (track.x + 2, std::min (y0, yv), track.w - 4, std::max (1, std::abs (yv - y0)), t.accent);
            c_.fill (track.x - 2, y0, track.w + 4, 1, t.muted);
        }
        else
            c_.fill (track.x + 2, yv, track.w - 4, std::max (1, bottom - yv - 1), t.accent);
        c_.fill (track.x - 3, yv - 1, track.w + 6, 3, f ? 0xFFFFFFFF : mix (t.accent, 0xFFFFFFFF, 0.4f));
        c_.textAligned ({ (int) (r.x + sw * i), bottom + 3, (int) sw, 12 }, std::to_string (i + 1), f ? t.accent : t.muted, 1, 1);
        if (sw >= 52)
            c_.textAligned ({ (int) (r.x + sw * i) + 1, bottom + 16, (int) sw - 2, 12 }, upper (valueText (param)), f ? t.text : t.muted, 1, 1);
        const int cc = p_->findCcForParam (p.id);
        const uint32_t learn = p_->midiLearnTarget.load (std::memory_order_relaxed);
        if (cc >= 0) c_.fill (track.x, top - 4, track.w, 2, t.accent2);
        if (learn == p.id) c_.frame ({ track.x - 3, top - 3, track.w + 6, bottom - top + 6 }, t.learn, (now_ / 300) % 2 ? 255 : 120);
    }
    if (p_->midiLearnTarget.load (std::memory_order_relaxed) == DesktopPlugin::kLearnArmedNoParam) c_.frameChamfer (r.inset (1), 5, t.learn, 90);
}

void PanelUI::drawControl (int index)
{
    const Page* pg = current();
    const auto& c = pg->controls[(size_t) index];
    const Rect r = screenRect (index);
    const bool hot = hoverCtrl_ == index || dragCtrl_ == index;
    const auto& s = *c.spec;
    switch (s.kind)
    {
        case Kind::Knob: drawKnobCell (r, s.params[0], s.label, hot, false); break;
        case Kind::Toggle: drawToggle (r, s.params[0], s.label, hot); break;
        case Kind::Button:
            drawButton (r, s.params[0], s.label, hot,
                        p_->displayValue (s.params[0]) > 0.5 || (drag_ == Drag::Button && dragCtrl_ == index));
            break;
        case Kind::Selector: drawSelector (r, s.params[0], s.label, hot, false); break;
        case Kind::Stepper: drawSelector (r, s.params[0], s.label, hot, true); break;
        case Kind::Steps: drawSteps (r, s, hot); break;
    }
}

std::string PanelUI::readoutText (bool& learnLine) const
{
    learnLine = false;
    const uint32_t learn = p_->midiLearnTarget.load (std::memory_order_relaxed);
    if (learn == DesktopPlugin::kLearnArmedNoParam) { learnLine = true; return "MIDI LEARN: CLICK A CONTROL, THEN MOVE A KNOB ON YOUR CONTROLLER"; }
    if (learn != CLAP_INVALID_ID)
    {
        learnLine = true;
        const int i = p_->findParamIndex (learn);
        return "MIDI LEARN: MOVE A CONTROLLER KNOB FOR " + (i >= 0 ? upper (p_->params[(size_t) i].name) : std::string ("?"));
    }
    int param = (drag_ == Drag::Control || drag_ == Drag::Step || drag_ == Drag::ScreenKnob) ? dragParam_ : -1;
    if (param < 0 && wheelParam_ >= 0) param = wheelParam_;
    if (param < 0 && hoverCtrl_ >= 0 && current())
    {
        const auto& sp = *current()->controls[(size_t) hoverCtrl_].spec;
        param = sp.kind == Kind::Steps ? (hoverStep_ >= 0 ? sp.params[(size_t) hoverStep_] : -1) : sp.params[0];
    }
    if (param < 0 && hoverItem_ >= Item::K1 && hoverItem_ <= Item::K3) param = screenKnobParam ((int) hoverItem_ - (int) Item::K1);
    if (param < 0) param = lastTouched_;
    if (param < 0) return "DRAG TO TURN   SHIFT = FINE   DOUBLE-CLICK = DEFAULT   RIGHT-CLICK = MIDI LEARN";
    const auto& p = p_->params[(size_t) param];
    std::string s = upper (p.module.empty() ? p.name : p.module + " / " + p.name) + "   " + upper (valueText (param));
    const int cc = p_->findCcForParam (p.id);
    if (cc >= 0) s += "   CC " + std::to_string (cc);
    return s;
}

void PanelUI::drawItem (Item it)
{
    const auto& t = p_->theme;
    const Rect r = items_[(size_t) it];
    const bool hot = hoverItem_ == it;
    const uint32_t learn = p_->midiLearnTarget.load (std::memory_order_relaxed);
    auto button = [&] (const std::string& text, bool on, uint32_t onColor) {
        c_.fillChamfer (r, 5, on ? onColor : (hot ? t.panelHi : t.panel));
        c_.frameChamfer (r, 5, on ? onColor : (hot ? t.accent : t.border));
        c_.textAligned (r, text, on ? t.onAccent : t.text, 1, 1);
    };
    switch (it)
    {
        case Item::PresetPrev: button ("<", false, t.accent); break;
        case Item::PresetNext: button (">", false, t.accent); break;
        case Item::PresetBox:
        {
            c_.fillChamfer (r, 5, 0xFF0C0816);
            c_.frameChamfer (r, 5, (hot || popup_ == PopupKind::Presets) ? t.accent : t.border);
            std::string s;
            if (p_->presets.empty()) s = "NO PRESETS";
            else if (p_->currentPresetIndex >= 0)
            {
                char buf[16];
                std::snprintf (buf, sizeof (buf), "%02d/%02d ", p_->currentPresetIndex + 1, (int) p_->presets.size());
                s = buf + upper (p_->presets[(size_t) p_->currentPresetIndex].name);
            }
            else s = "PRESETS: " + std::to_string (p_->presets.size());
            c_.tag (r.x + 6, r.y + 7, "PRESET", t.accent2, t.onAccent);
            c_.textAligned ({ r.x + 62, r.y, r.w - 80, r.h }, s, t.text, 1, 0);
            const float ax = (float) (r.x + r.w - 12), ay = (float) (r.y + r.h / 2 - 2);
            c_.line (ax - 4, ay, ax, ay + 4, 1.4f, t.accent);
            c_.line (ax, ay + 4, ax + 4, ay, 1.4f, t.accent);
            break;
        }
        case Item::Learn:
        {
            const bool on = learn != CLAP_INVALID_ID;
            button (on ? ((now_ / 400) % 2 ? "LEARNING" : "") : "MIDI LEARN", on, t.learn);
            if (on && (now_ / 400) % 2 == 0) c_.textAligned (r, "LEARNING", 0xFFFFFFFF, 1, 1);
            break;
        }
        case Item::ClearCc:
        {
            const int n = p_->countMappedCc();
            button (n ? "CLEAR " + std::to_string (n) : "NO CC", false, t.accent);
            break;
        }
        case Item::Zoom: button (zoom_ == 2 ? "PAGE 2X" : "PAGE 1X", false, t.accent); break;
        case Item::Readout:
        {
            c_.fillChamfer (r, 5, 0xFF0A0614);
            bool learnLine = false;
            const std::string s = readoutText (learnLine);
            c_.frameChamfer (r, 5, learnLine ? t.learn : t.border);
            c_.textAligned ({ r.x + 10, r.y, r.w - 20, r.h }, s, learnLine ? t.learn : t.text, 1, 0);
            break;
        }
        case Item::Screen:
        {
            if (p_->screenPixels.size() == (size_t) MELODEE_SCREEN_WIDTH * MELODEE_SCREEN_HEIGHT)
                c_.blit (p_->screenPixels.data(), MELODEE_SCREEN_WIDTH, MELODEE_SCREEN_HEIGHT, r.x, r.y, zoom_);
            if (now_ - screenFlashAt_ < 140) c_.frame (r, t.accent);
            break;
        }
        case Item::Encoder:
        {
            const bool active = drag_ == Drag::Encoder || drag_ == Drag::Screen;
            c_.textAligned ({ r.x, r.y + 4, r.w, 12 }, "ENCODER", hot || active ? t.text : t.muted, 1, 1);
            const float cx = r.x + r.w * 0.5f, cy = (float) r.y + 66.0f, rad = 36.0f;
            for (int i = 0; i < 30; ++i)
            {
                const float a = i * 6.2831853f / 30.0f;
                c_.disc (cx + std::cos (a) * (rad - 2), cy + std::sin (a) * (rad - 2), 1.4f, mix (t.panel, t.border, 0.9f));
            }
            c_.disc (cx, cy, rad - 7, hot || active ? mix (t.border, t.text, 0.3f) : t.border);
            c_.disc (cx, cy, rad - 9, 0xFF0C0816);
            for (int i = 0; i < 16; ++i)
            {
                const float a = encoderAngle_ + i * 6.2831853f / 16.0f;
                c_.line (cx + std::cos (a) * (rad - 14), cy + std::sin (a) * (rad - 14), cx + std::cos (a) * (rad - 11), cy + std::sin (a) * (rad - 11), 1.2f,
                         mix (t.border, t.muted, 0.5f));
            }
            const float a = encoderAngle_ - 1.5707963f;
            c_.disc (cx + std::cos (a) * (rad - 18), cy + std::sin (a) * (rad - 18), 3.2f, active ? 0xFFFFFFFF : t.accent);
            break;
        }
        case Item::Minus:
        case Item::Plus:
        case Item::Push:
        {
            const bool down = drag_ == Drag::ScreenButton && dragItem_ == it;
            button (it == Item::Minus ? "-" : it == Item::Plus ? "+" : "PUSH", down, t.accent);
            break;
        }
        case Item::Shift: button ("SHIFT", p_->screenShift, t.accent2); break;
        case Item::K1:
        case Item::K2:
        case Item::K3:
        {
            const int k = (int) it - (int) Item::K1;
            const int param = screenKnobParam (k);
            const std::string kt = "K" + std::to_string (k + 1);
            if (param < 0)
            {
                c_.tag (r.x + 6, r.y + 4, kt, t.border, t.muted);
                c_.knob (r.x + r.w * 0.5f, (float) r.y + 70, 28, 0, false, t.border, mix (t.panel, t.border, 0.6f), 0xFF0C0816, t.border, false);
                break;
            }
            drawKnobCell (r, param, upper (p_->params[(size_t) param].name), hot || (drag_ == Drag::ScreenKnob && dragItem_ == it), true);
            c_.tag (r.x + 6, r.y + 2, kt, t.accent, t.onAccent);
            break;
        }
        default: break;
    }
}

void PanelUI::drawPopup()
{
    const auto& t = p_->theme;
    const Rect& r = popupRect_;
    c_.fill (r.x + 4, r.y + 5, r.w, r.h, 0xFF000000, 120);
    c_.fillChamfer (r, 6, mix (t.panel, t.bgTop, 0.3f));
    c_.frameChamfer (r, 6, t.accent);
    for (size_t i = 0; i < popupRects_.size(); ++i)
    {
        const Rect& ir = popupRects_[i];
        const bool sel = (int) i == popupSelected_, hv = (int) i == popupHover_;
        if (sel) c_.fill (ir, t.accent);
        else if (hv) c_.fill (ir, t.panelHi);
        c_.textAligned ({ ir.x + 8, ir.y, ir.w - 12, ir.h }, popupItems_[i], sel ? t.onAccent : (hv ? t.accent : t.text), 1, 0);
    }
}

} // namespace hdslave_desktop
