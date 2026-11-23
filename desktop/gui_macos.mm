#import <Cocoa/Cocoa.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "desktop_wrapper.h"
#include "panel_ui.h"

#ifndef HDSLAVE_OBJC_VIEW_CLASS
#define HDSLAVE_OBJC_VIEW_CLASS HdslavePanelView
#endif

static uint32_t modsFromEvent (NSEvent* event)
{
    uint32_t m = 0;
    const NSEventModifierFlags f = [event modifierFlags];
    if (f & NSEventModifierFlagShift) m |= hdslave_desktop::kModShift;
    if (f & NSEventModifierFlagOption) m |= hdslave_desktop::kModAlt;
    if (f & NSEventModifierFlagCommand) m |= hdslave_desktop::kModCmd;
    return m;
}

@interface HDSLAVE_OBJC_VIEW_CLASS : NSView
{
@public
    hdslave_desktop::DesktopPlugin* _plugin;
    NSTimer* _timer;
    NSTrackingArea* _trackingArea;
    int _scale;
}
- (instancetype)initWithPlugin:(hdslave_desktop::DesktopPlugin*)plugin frame:(NSRect)frame;
- (void)startTimer;
- (void)stopTimer;
- (void)syncSize;
- (void)pumpFrame;
@end

@implementation HDSLAVE_OBJC_VIEW_CLASS

- (instancetype)initWithPlugin:(hdslave_desktop::DesktopPlugin*)plugin frame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self)
    {
        _plugin = plugin;
        _timer = nil;
        _trackingArea = nil;
        _scale = 2;
        self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        [self setAllowedTouchTypes:NSTouchTypeMaskIndirect];
    }
    return self;
}

- (void)dealloc
{
    [self stopTimer];
    if (_trackingArea)
    {
        [self removeTrackingArea:_trackingArea];
        [_trackingArea release];
        _trackingArea = nil;
    }
    [super dealloc];
}

- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { (void)event; return YES; }

- (int)backingScaleInt
{
    CGFloat sf = 2.0;
    if (self.window)
        sf = self.window.backingScaleFactor;
    else if ([NSScreen mainScreen])
        sf = [[NSScreen mainScreen] backingScaleFactor];
    return std::clamp (int (std::round (sf)), 1, 3);
}

- (void)syncSize
{
    if (!_plugin || !_plugin->ui) return;
    _scale = [self backingScaleInt];
    const int w = std::max (320, int (std::round (self.bounds.size.width)));
    const int h = std::max (240, int (std::round (self.bounds.size.height)));
    _plugin->ui->setSize (w, h, _scale);
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [self syncSize];
    if (self.window)
    {
        if (_plugin && _plugin->ui) _plugin->ui->invalidateAll();
        [self pumpFrame];
    }
}

- (void)viewDidChangeBackingProperties
{
    [super viewDidChangeBackingProperties];
    [self syncSize];
    if (_plugin && _plugin->ui) _plugin->ui->invalidateAll();
    [self pumpFrame];
}

- (void)setFrameSize:(NSSize)newSize
{
    [super setFrameSize:newSize];
    [self syncSize];
    [self pumpFrame];
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    if (_trackingArea)
    {
        [self removeTrackingArea:_trackingArea];
        [_trackingArea release];
        _trackingArea = nil;
    }
    NSTrackingAreaOptions opts = NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited
                               | NSTrackingActiveAlways | NSTrackingInVisibleRect;
    _trackingArea = [[NSTrackingArea alloc] initWithRect:self.bounds options:opts owner:self userInfo:nil];
    [self addTrackingArea:_trackingArea];
}

- (void)startTimer
{
    if (_timer) return;
    _timer = [NSTimer scheduledTimerWithTimeInterval:(1.0 / 60.0)
                                              target:self
                                            selector:@selector(onTick:)
                                            userInfo:nil
                                             repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)stopTimer
{
    if (_timer)
    {
        [_timer invalidate];
        _timer = nil;
    }
}

- (void)pumpFrame
{
    if (!_plugin || !_plugin->ui) return;
    [self syncSize];
    const hdslave_desktop::Rect dirty = _plugin->ui->update (hdslave_desktop::nowMs());
    if (!dirty.empty())
        [self setNeedsDisplayInRect:NSMakeRect (dirty.x, dirty.y, dirty.w, dirty.h)];
}

- (void)onTick:(NSTimer*)timer
{
    (void)timer;
    [self pumpFrame];
}

- (void)drawRect:(NSRect)dirtyRect
{
    if (!_plugin || !_plugin->ui) return;
    [self syncSize];
    if (_plugin->ui->physWidth() <= 0 || _plugin->ui->physHeight() <= 0)
        _plugin->ui->update (hdslave_desktop::nowMs());

    const uint32_t* px = _plugin->ui->pixels();
    const int pw = _plugin->ui->physWidth();
    const int ph = _plugin->ui->physHeight();
    const int sc = _plugin->ui->scale();
    if (!px || pw <= 0 || ph <= 0) return;

    CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
    if (!ctx) return;

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef provider = CGDataProviderCreateWithData (nullptr, px, size_t (pw) * size_t (ph) * 4, nullptr);
    CGImageRef fullImg = CGImageCreate (
        size_t (pw), size_t (ph), 8, 32, size_t (pw * 4), cs,
        kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst,
        provider, nullptr, false, kCGRenderingIntentDefault);

    if (fullImg)
    {
        // Crop to dirtyRect in physical pixels so dragging a knob only blits that small rect.
        const int rx0 = std::clamp (int (std::floor (dirtyRect.origin.x)) * sc, 0, pw);
        const int ry0 = std::clamp (int (std::floor (dirtyRect.origin.y)) * sc, 0, ph);
        const int rx1 = std::clamp (int (std::ceil (dirtyRect.origin.x + dirtyRect.size.width)) * sc, 0, pw);
        const int ry1 = std::clamp (int (std::ceil (dirtyRect.origin.y + dirtyRect.size.height)) * sc, 0, ph);

        if (rx1 > rx0 && ry1 > ry0)
        {
            CGRect cropRect = CGRectMake (rx0, ry0, rx1 - rx0, ry1 - ry0);
            CGImageRef subImg = CGImageCreateWithImageInRect (fullImg, cropRect);
            if (subImg)
            {
                const CGFloat lx = CGFloat (rx0) / CGFloat (sc);
                const CGFloat ly = CGFloat (ry0) / CGFloat (sc);
                const CGFloat lw = CGFloat (rx1 - rx0) / CGFloat (sc);
                const CGFloat lh = CGFloat (ry1 - ry0) / CGFloat (sc);

                CGContextSaveGState (ctx);
                CGContextTranslateCTM (ctx, lx, ly + lh);
                CGContextScaleCTM (ctx, 1.0, -1.0);
                CGContextSetInterpolationQuality (ctx, kCGInterpolationNone);
                CGContextDrawImage (ctx, CGRectMake (0, 0, lw, lh), subImg);
                CGContextRestoreGState (ctx);
                CGImageRelease (subImg);
            }
        }
        CGImageRelease (fullImg);
    }

    CGDataProviderRelease (provider);
    CGColorSpaceRelease (cs);
}

- (NSPoint)localPoint:(NSEvent*)event
{
    return [self convertPoint:[event locationInWindow] fromView:nil];
}

- (void)mouseDown:(NSEvent*)event
{
    if (!_plugin || !_plugin->ui) return;
    if (self.window) [self.window makeFirstResponder:self];
    NSPoint pt = [self localPoint:event];
    _plugin->ui->mouseDown (float (pt.x), float (pt.y), 0, modsFromEvent (event), int ([event clickCount]));
    [self pumpFrame];
}

- (void)rightMouseDown:(NSEvent*)event
{
    if (!_plugin || !_plugin->ui) return;
    if (self.window) [self.window makeFirstResponder:self];
    NSPoint pt = [self localPoint:event];
    _plugin->ui->mouseDown (float (pt.x), float (pt.y), 1, modsFromEvent (event), int ([event clickCount]));
    [self pumpFrame];
}

- (void)mouseDragged:(NSEvent*)event
{
    if (!_plugin || !_plugin->ui) return;
    NSPoint pt = [self localPoint:event];
    _plugin->ui->mouseMove (float (pt.x), float (pt.y), modsFromEvent (event));
    [self pumpFrame];
}

- (void)mouseMoved:(NSEvent*)event
{
    if (!_plugin || !_plugin->ui) return;
    NSPoint pt = [self localPoint:event];
    _plugin->ui->mouseMove (float (pt.x), float (pt.y), modsFromEvent (event));
    [self pumpFrame];
}

- (void)mouseUp:(NSEvent*)event
{
    if (!_plugin || !_plugin->ui) return;
    NSPoint pt = [self localPoint:event];
    _plugin->ui->mouseUp (float (pt.x), float (pt.y), 0, modsFromEvent (event));
    [self pumpFrame];
}

- (void)mouseExited:(NSEvent*)event
{
    (void)event;
    if (!_plugin || !_plugin->ui) return;
    _plugin->ui->mouseExit();
    [self pumpFrame];
}

- (void)scrollWheel:(NSEvent*)event
{
    if (!_plugin || !_plugin->ui) return;
    NSPoint pt = [self localPoint:event];
    const bool precise = [event hasPreciseScrollingDeltas];
    float dx = float ([event scrollingDeltaX]);
    float dy = float ([event scrollingDeltaY]);
    _plugin->ui->wheel (float (pt.x), float (pt.y), dx, dy, precise, modsFromEvent (event));
    [self pumpFrame];
}

- (void)rotateWithEvent:(NSEvent*)event
{
    if (!_plugin || !_plugin->ui) return;
    NSPoint pt = [self localPoint:event];
    _plugin->ui->rotate (float (pt.x), float (pt.y), float ([event rotation]));
    [self pumpFrame];
}

- (void)keyDown:(NSEvent*)event
{
    if (!_plugin || !_plugin->ui) { [super keyDown:event]; return; }
    int k = -1;
    switch ([event keyCode])
    {
        case 123: k = 0; break; // Left
        case 124: k = 1; break; // Right
        case 126: k = 2; break; // Up
        case 125: k = 3; break; // Down
        case 36:  case 49: case 76: k = 4; break; // Return / Space / Enter
        case 53:  k = 5; break; // Escape
        default: break;
    }
    if (k >= 0 && _plugin->ui->key (k))
    {
        [self pumpFrame];
        return;
    }
    [super keyDown:event];
}

@end

namespace hdslave_desktop
{

struct PlatformGui
{
    DesktopPlugin* plugin = nullptr;
    HDSLAVE_OBJC_VIEW_CLASS* view = nil;
};

const char* platformGuiApiName()
{
    return CLAP_WINDOW_API_COCOA;
}

PlatformGui* createPlatformGui (DesktopPlugin* plugin)
{
    auto* g = new (std::nothrow) PlatformGui();
    if (!g) return nullptr;
    g->plugin = plugin;
    NSRect frame = NSMakeRect (0, 0, CGFloat (plugin->guiWidth), CGFloat (plugin->guiHeight));
    g->view = [[HDSLAVE_OBJC_VIEW_CLASS alloc] initWithPlugin:plugin frame:frame];
    return g;
}

void destroyPlatformGui (PlatformGui* gui)
{
    if (!gui) return;
    if (gui->view)
    {
        [gui->view stopTimer];
        [gui->view removeFromSuperview];
        [gui->view release];
        gui->view = nil;
    }
    delete gui;
}

bool platformGuiSetParent (PlatformGui* gui, const clap_window_t* window)
{
    if (!gui || !gui->view || !window || !window->cocoa)
        return false;
    NSView* parent = (__bridge NSView*) window->cocoa;
    [gui->view removeFromSuperview];
    [gui->view setFrame:NSMakeRect (0, 0, CGFloat (gui->plugin->guiWidth), CGFloat (gui->plugin->guiHeight))];
    [parent addSubview:gui->view];
    [gui->view syncSize];
    if (gui->plugin && gui->plugin->ui) gui->plugin->ui->invalidateAll();
    [gui->view startTimer];
    [gui->view pumpFrame];
    return true;
}

bool platformGuiShow (PlatformGui* gui)
{
    if (!gui || !gui->view) return false;
    [gui->view setHidden:NO];
    [gui->view syncSize];
    if (gui->plugin && gui->plugin->ui) gui->plugin->ui->invalidateAll();
    [gui->view startTimer];
    [gui->view pumpFrame];
    return true;
}

bool platformGuiHide (PlatformGui* gui)
{
    if (!gui || !gui->view) return false;
    [gui->view stopTimer];
    [gui->view setHidden:YES];
    return true;
}

bool platformGuiSetSize (PlatformGui* gui, uint32_t width, uint32_t height)
{
    if (!gui || !gui->view) return false;
    [gui->view setFrameSize:NSMakeSize (CGFloat (width), CGFloat (height))];
    return true;
}

} // namespace hdslave_desktop
