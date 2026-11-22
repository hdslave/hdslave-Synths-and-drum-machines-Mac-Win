#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "desktop_wrapper.h"
#include "panel_renderer.h"

namespace hdslave_desktop
{

struct Rect
{
    int x = 0, y = 0, w = 0, h = 0;
    bool contains (int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

struct ControlWidget
{
    int paramIndex = -1;
    clap_id paramId = CLAP_INVALID_ID;
    Rect bounds {};
    Rect knobRect {};
};

struct ModuleCard
{
    std::string title;
    int bankIndex = 0;
    Rect bounds {};
    std::vector<ControlWidget> controls;
};

class PanelUI
{
public:
    explicit PanelUI (DesktopPlugin* plugin);

    void rebuildLayout (int width, int height);
    void render (uint32_t* pixels, int width, int height, uint64_t nowMs);

    // Input events in logical coordinates
    void onMouseDown (int x, int y, int button, bool shift, bool doubleClick);
    void onMouseMove (int x, int y, bool shift);
    void onMouseUp (int x, int y, int button);
    void onMouseWheel (int x, int y, float deltaY, bool shift);

    int width() const { return width_; }
    int height() const { return height_; }

private:
    DesktopPlugin* plugin_ = nullptr;
    int width_ = int (kDefaultGuiWidth);
    int height_ = int (kDefaultGuiHeight);

    // Layout geometry
    Rect headerRect_ {};
    Rect presetPrevRect_ {};
    Rect presetBoxRect_ {};
    Rect presetNextRect_ {};
    Rect midiLearnRect_ {};
    Rect clearCcRect_ {};
    Rect bankBarRect_ {};
    Rect workspaceRect_ {};
    Rect screenCardRect_ {};
    Rect screenImageRect_ {};
    Rect scrollbarTrackRect_ {};
    Rect scrollbarThumbRect_ {};

    struct BankTab
    {
        std::string label;
        Rect rect;
    };
    std::vector<BankTab> bankTabs_;
    int activeBank_ = 0; // 0 = ALL
    int numBanks_ = 1;

    std::vector<ModuleCard> cards_;
    int contentHeight_ = 0;
    int scrollY_ = 0;
    int maxScrollY_ = 0;

    // Interaction state
    int hoveredParamIdx_ = -1;
    int draggingParamIdx_ = -1;
    double dragStartVal_ = 0.0;
    int dragStartY_ = 0;
    bool draggingScrollbar_ = false;
    int scrollbarDragStartY_ = 0;
    int scrollbarDragStartScroll_ = 0;

    // Preset picker overlay
    bool presetMenuOpen_ = false;
    std::vector<Rect> presetItemRects_;

    // Readout
    int lastTweakedParamIdx_ = -1;

    int hitTestControl (int x, int y) const;
    void clampScroll();
    void renderHeader (Canvas& c);
    void renderBankBar (Canvas& c);
    void renderWorkspace (Canvas& c);
    void renderPresetOverlay (Canvas& c);
};

} // namespace hdslave_desktop
