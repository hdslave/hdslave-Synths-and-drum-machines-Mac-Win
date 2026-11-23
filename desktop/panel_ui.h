// The desktop panel: the plug-in's live MPC page with its hardware controls (encoder, push, -/+, shift, K1-K3) on the
// left, and every parameter on a wall of module cards on the right, grouped into tabs that fit the window.
//
// Drawing: a base layer (grounds, cards, titles) and a frame. A change redraws only the items whose look changed
// (restored from the base first) and reports the dirty rectangle; the platform view blits just that.
#pragma once

#include "desktop_wrapper.h"
#include "panel_renderer.h"

#include <cstdint>
#include <string>
#include <vector>

namespace hdslave_desktop
{

enum Mods : uint32_t
{
    kModShift = 1,  // fine
    kModAlt = 2,    // reset to default on click
    kModCmd = 4,    // reset to default on click (Cmd on macOS, Ctrl on Windows)
    kModTouch = 8,  // the event came from a touch screen or pen
};

enum class Kind : uint8_t
{
    Knob,     // continuous
    Toggle,   // two states
    Button,   // momentary ("Press" / "Pressed")
    Selector, // a list of choices: click opens the list, drag or wheel steps through it
    Stepper,  // an integer: drag, wheel or the arrows
    Steps,    // a bank of step faders (Pitch 1..8): click or draw across them
};

struct ControlSpec
{
    Kind kind = Kind::Knob;
    std::vector<int> params; // indices into DesktopPlugin::params (several for Steps)
    std::string label;
    int units = 1; // width in cells
    int rows = 1;  // height in cells
};

struct Section
{
    std::string title;
    std::string tag; // short name for tabs (the params' shared prefix, e.g. BD)
    std::vector<ControlSpec> specs;
};

struct Control
{
    const ControlSpec* spec = nullptr;
    Rect r;              // content coordinates (wall origin, before scrolling)
    uint64_t drawn = 0;  // look last drawn (hash)
};

struct Card
{
    const Section* section = nullptr;
    Rect r;
};

struct Page
{
    std::string label;
    std::vector<Card> cards;
    std::vector<Control> controls;
    int contentH = 0;
    Rect tab;
};

// A dynamic item outside the wall (header widgets, the live page, the hardware controls).
enum class Item : uint8_t
{
    PresetPrev, PresetBox, PresetNext, Learn, ClearCc, Zoom, Readout,
    Screen, Encoder, Minus, Push, Plus, Shift, K1, K2, K3,
    Count
};

class PanelUI
{
public:
    explicit PanelUI (DesktopPlugin* plugin);

    // Logical size and device scale (2 on Retina). Rebuilds the layout when they change.
    void setSize (int w, int h, int scale);
    // Advances time, polls values and the live page, redraws what changed. Returns the dirty area (logical; empty
    // when nothing changed).
    Rect update (uint64_t now);
    void invalidateAll() { fullDirty_ = true; }
    void layoutChanged() { layoutDirty_ = true; }

    const uint32_t* pixels() const { return frame_.data(); }
    int physWidth() const { return physW_; }
    int physHeight() const { return physH_; }
    int scale() const { return scale_; }
    int width() const { return w_; }
    int height() const { return h_; }

    // Input, logical coordinates. button: 0 left, 1 right. clicks: 1, 2 (double).
    void mouseDown (float x, float y, int button, uint32_t mods, int clicks);
    void mouseMove (float x, float y, uint32_t mods); // drag when a button is down
    void mouseUp (float x, float y, int button, uint32_t mods);
    // dy > 0: up / away. precise: trackpad pixels (else wheel notches).
    void wheel (float x, float y, float dx, float dy, bool precise, uint32_t mods);
    // Trackpad rotation in degrees, counter-clockwise positive.
    void rotate (float x, float y, float degrees);
    void mouseExit();
    // Keys: 0 left, 1 right, 2 up, 3 down, 4 enter/space, 5 escape.
    bool key (int k);

    double lastRenderMs() const { return lastRenderMs_; }

private:
    DesktopPlugin* p_;
    int w_ = int (kDefaultGuiWidth), h_ = int (kDefaultGuiHeight), scale_ = 1;
    std::vector<uint32_t> base_, frame_;
    int physW_ = 0, physH_ = 0;
    Canvas c_;
    bool fullDirty_ = true, layoutDirty_ = true;
    Rect dirty_ {};
    double lastRenderMs_ = 0.0;
    uint64_t now_ = 0;

    // Model (built once).
    std::vector<Section> sections_;
    void buildSections();

    // Layout.
    int zoom_ = 2;         // live page zoom (1 or 2), 0 = automatic
    int zoomChoice_ = 0;   // 0 auto, 1, 2
    Rect top_, wall_, tabsRow_, screenCard_, screenRect_, hwRow_;
    Rect items_[(size_t) Item::Count] {};
    uint64_t itemDrawn_[(size_t) Item::Count] {};
    std::vector<Page> pages_;
    int page_ = 0;
    int scroll_ = 0;
    int cellW_ = 82, cellH_ = 88;
    void layout();
    void layoutWall();

    // Interaction.
    enum class Drag : uint8_t { None, Control, Step, Encoder, Screen, ScreenXY, ScreenKnob, Scrollbar, WallScroll, Button, ScreenButton };
    Drag drag_ = Drag::None;
    int dragCtrl_ = -1;      // control index on the page
    int dragParam_ = -1;     // param index
    int dragParamY_ = -1;    // optional second param for 2D XY dragging on the visual screen
    int dragStep_ = -1;      // step index inside a Steps bank
    Item dragItem_ = Item::Count;
    float downX_ = 0, downY_ = 0, lastX_ = 0, lastY_ = 0;
    double dragStartNorm_ = 0.0;
    double dragStartNormY_ = 0.0;
    double dragAccum_ = 0.0;
    bool moved_ = false;
    uint64_t downAt_ = 0;
    bool longPressFired_ = false;
    uint32_t downMods_ = 0;
    int hoverCtrl_ = -1, hoverStep_ = -1;
    Item hoverItem_ = Item::Count;
    float hoverX_ = -1, hoverY_ = -1;
    int lastTouched_ = -1; // param index for the readout
    // Wheel gestures: one begin/end around a burst of wheel steps.
    int wheelParam_ = -1;
    uint64_t wheelAt_ = 0;
    float wheelAccum_ = 0.0f, screenWheelAccum_ = 0.0f;
    float encoderAngle_ = 0.0f;
    uint64_t screenFlashAt_ = 0;
    uint64_t lastScreenPaint_ = 0;
    bool screenChanged_ = true;

    // Popups: a list of choices for a selector, the presets, or a control's menu.
    enum class PopupKind : uint8_t { None, Choices, Presets, Menu };
    PopupKind popup_ = PopupKind::None;
    int popupParam_ = -1;
    std::vector<std::string> popupItems_;
    std::vector<double> popupValues_;
    std::vector<Rect> popupRects_;
    Rect popupRect_ {};
    int popupHover_ = -1;
    int popupSelected_ = -1;
    void openChoices (int param, const Rect& anchor);
    void openPresets();
    void openMenu (int param, float x, float y);
    void closePopup();
    void layoutPopup (const Rect& anchor);
    void popupPick (int i);

    // Values.
    double norm (int param) const;
    double fromNorm (int param, double n) const;
    void setParam (int param, double v);       // inside a gesture
    void stepParam (int param, int steps);     // wheel / arrows, its own short gesture
    void endWheelGesture();
    void resetParam (int param);
    void beginControlDrag (int ctrl, float x, float y, uint32_t mods);

    // Hit testing.
    int hitControl (float x, float y) const;
    int hitStep (int ctrl, float x) const;
    Item hitItem (float x, float y) const;
    Rect screenRect (int ctrl) const; // a control's rect on screen
    Page* current() { return pages_.empty() ? nullptr : &pages_[(size_t) page_]; }
    const Page* current() const { return pages_.empty() ? nullptr : &pages_[(size_t) page_]; }

    // Drawing.
    void renderAll();
    void renderBase();
    void restore (const Rect& r);
    void markDirty (const Rect& r) { dirty_ = dirty_.unite (r); }
    uint64_t controlLook (int index) const;
    uint64_t itemLook (Item it) const;
    void drawControl (int index);
    void drawItem (Item it);
    void drawKnobCell (const Rect& r, int param, const std::string& label, bool hot, bool big);
    void drawToggle (const Rect& r, int param, const std::string& label, bool hot);
    void drawButton (const Rect& r, int param, const std::string& label, bool hot, bool down);
    void drawSelector (const Rect& r, int param, const std::string& label, bool hot, bool stepper);
    void drawSteps (const Rect& r, const ControlSpec& s, bool hot);
    void drawBadges (const Rect& r, int param);
    void drawPopup();
    void drawScrollbar();
    std::string valueText (int param) const;
    std::string readoutText (bool& learnLine) const;

    // Live page.
    void paintScreen();
    void screenInput (uint32_t kind, int value);
    int screenKnobParam (int k) const;
};

} // namespace hdslave_desktop
