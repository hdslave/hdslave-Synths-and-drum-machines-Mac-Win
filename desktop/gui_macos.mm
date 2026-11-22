#import <Cocoa/Cocoa.h>
#include <chrono>
#include <vector>

#include "desktop_wrapper.h"
#include "panel_ui.h"

#ifndef HDSLAVE_OBJC_VIEW_CLASS
#define HDSLAVE_OBJC_VIEW_CLASS HdslavePanelView
#endif

@interface HDSLAVE_OBJC_VIEW_CLASS : NSView
{
@public
    hdslave_desktop::DesktopPlugin* _plugin;
    std::vector<uint32_t> _framebuffer;
    int _fbWidth;
    int _fbHeight;
    NSTimer* _timer;
    NSTrackingArea* _trackingArea;
}
- (instancetype)initWithPlugin:(hdslave_desktop::DesktopPlugin*)plugin frame:(NSRect)frame;
- (void)startTimer;
- (void)stopTimer;
@end

@implementation HDSLAVE_OBJC_VIEW_CLASS

- (instancetype)initWithPlugin:(hdslave_desktop::DesktopPlugin*)plugin frame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self)
    {
        _plugin = plugin;
        _fbWidth = 0;
        _fbHeight = 0;
        _timer = nil;
        _trackingArea = nil;
        self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
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

- (BOOL)isFlipped
{
    return YES;
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event
{
    (void)event;
    return YES;
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
    NSTrackingAreaOptions opts = NSTrackingMouseMoved | NSTrackingActiveInKeyWindow
                               | NSTrackingActiveAlways | NSTrackingInVisibleRect;
    _trackingArea = [[NSTrackingArea alloc] initWithRect:self.bounds options:opts owner:self userInfo:nil];
    [self addTrackingArea:_trackingArea];
}

- (void)startTimer
{
    if (_timer) return;
    _timer = [NSTimer scheduledTimerWithTimeInterval:(1.0 / 30.0)
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

- (void)onTick:(NSTimer*)timer
{
    (void)timer;
    if (!_plugin || !_plugin->ui) return;
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    if (!_plugin || !_plugin->ui) return;

    const int w = std::max (1, int (self.bounds.size.width));
    const int h = std::max (1, int (self.bounds.size.height));

    if (w != _fbWidth || h != _fbHeight)
    {
        _fbWidth = w;
        _fbHeight = h;
        _framebuffer.assign (size_t (w * h), 0xFF111318u);
    }

    using namespace std::chrono;
    const uint64_t nowMs = uint64_t (duration_cast<milliseconds> (steady_clock::now().time_since_epoch()).count());
    _plugin->ui->render (_framebuffer.data(), _fbWidth, _fbHeight, nowMs);

    CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
    if (!ctx) return;

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef provider = CGDataProviderCreateWithData (
        nullptr, _framebuffer.data(), size_t (_fbWidth * _fbHeight) * 4, nullptr);
    CGImageRef img = CGImageCreate (
        size_t (_fbWidth), size_t (_fbHeight), 8, 32, size_t (_fbWidth * 4), cs,
        kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst,
        provider, nullptr, false, kCGRenderingIntentDefault);

    if (img)
    {
        // Since isFlipped == YES, flip CTM so CGContextDrawImage draws right-side up
        CGContextSaveGState (ctx);
        CGContextTranslateCTM (ctx, 0.0, CGFloat (_fbHeight));
        CGContextScaleCTM (ctx, 1.0, -1.0);
        CGContextSetInterpolationQuality (ctx, kCGInterpolationNone);
        CGContextDrawImage (ctx, CGRectMake (0, 0, CGFloat (_fbWidth), CGFloat (_fbHeight)), img);
        CGContextRestoreGState (ctx);
        CGImageRelease (img);
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
    NSPoint pt = [self localPoint:event];
    bool shift = ([event modifierFlags] & NSEventModifierFlagShift) != 0;
    bool dbl = ([event clickCount] >= 2);
    _plugin->ui->onMouseDown (int (pt.x), int (pt.y), 0, shift, dbl);
    [self setNeedsDisplay:YES];
}

- (void)rightMouseDown:(NSEvent*)event
{
    if (!_plugin || !_plugin->ui) return;
    NSPoint pt = [self localPoint:event];
    bool shift = ([event modifierFlags] & NSEventModifierFlagShift) != 0;
    _plugin->ui->onMouseDown (int (pt.x), int (pt.y), 1, shift, false);
    [self setNeedsDisplay:YES];
}

- (void)mouseDragged:(NSEvent*)event
{
    if (!_plugin || !_plugin->ui) return;
    NSPoint pt = [self localPoint:event];
    bool shift = ([event modifierFlags] & NSEventModifierFlagShift) != 0;
    _plugin->ui->onMouseMove (int (pt.x), int (pt.y), shift);
    [self setNeedsDisplay:YES];
}

- (void)mouseMoved:(NSEvent*)event
{
    if (!_plugin || !_plugin->ui) return;
    NSPoint pt = [self localPoint:event];
    bool shift = ([event modifierFlags] & NSEventModifierFlagShift) != 0;
    _plugin->ui->onMouseMove (int (pt.x), int (pt.y), shift);
    [self setNeedsDisplay:YES];
}

- (void)mouseUp:(NSEvent*)event
{
    if (!_plugin || !_plugin->ui) return;
    NSPoint pt = [self localPoint:event];
    _plugin->ui->onMouseUp (int (pt.x), int (pt.y), 0);
    [self setNeedsDisplay:YES];
}

- (void)scrollWheel:(NSEvent*)event
{
    if (!_plugin || !_plugin->ui) return;
    NSPoint pt = [self localPoint:event];
    bool shift = ([event modifierFlags] & NSEventModifierFlagShift) != 0;
    float dy = float ([event scrollingDeltaY]);
    if ([event hasPreciseScrollingDeltas])
        dy *= 0.15f;
    _plugin->ui->onMouseWheel (int (pt.x), int (pt.y), dy, shift);
    [self setNeedsDisplay:YES];
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
    [gui->view startTimer];
    [gui->view setNeedsDisplay:YES];
    return true;
}

bool platformGuiShow (PlatformGui* gui)
{
    if (!gui || !gui->view) return false;
    [gui->view setHidden:NO];
    [gui->view startTimer];
    [gui->view setNeedsDisplay:YES];
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
    [gui->view setNeedsDisplay:YES];
    return true;
}

} // namespace hdslave_desktop
