//
//  AppDelegate.m
//  SonyHeadphonesClient
//
//  Created by Sem Visscher on 01/12/2020.
//

#import "AppDelegate.h"

NSString* const kHideDockIconKey = @"HideDockIconWhenHidden";
NSString* const kAutoConnectKey = @"AutoConnectOnLaunch";

static BOOL HideDockIconEnabled(void) {
    return [[NSUserDefaults standardUserDefaults] boolForKey:kHideDockIconKey];
}

@implementation MainWindow

- (void)performMiniaturize:(id)sender {
    if (HideDockIconEnabled())
        [(AppDelegate*)NSApp.delegate hideMainWindow];
    else
        [super performMiniaturize:sender];
}

- (void)miniaturize:(id)sender {
    if (HideDockIconEnabled())
        [(AppDelegate*)NSApp.delegate hideMainWindow];
    else
        [super miniaturize:sender];
}

@end

@interface AppDelegate ()
@end

@implementation AppDelegate {
    NSWindow* _settingsWindow;
}

+ (void)initialize {
    [[NSUserDefaults standardUserDefaults] registerDefaults:@{
        kHideDockIconKey: @NO,
        kAutoConnectKey: @YES,
    }];
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    _window = [[[NSApplication sharedApplication] windows] firstObject];
    _window.delegate = self;
}

- (BOOL)applicationShouldHandleReopen:(NSApplication *)theApplication hasVisibleWindows:(BOOL)flag
{
    if (!flag)
        [self showMainWindow];
    return !flag;
}

- (void)applicationWillTerminate:(NSNotification *)aNotification {
}

#pragma mark - Main window visibility

- (void)showMainWindow {
    if (NSApp.activationPolicy != NSApplicationActivationPolicyRegular)
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    if (_window.isMiniaturized)
        [_window deminiaturize:nil];
    [NSApp activateIgnoringOtherApps:YES];
    [_window makeKeyAndOrderFront:nil];
}

- (void)hideMainWindow {
    [_window orderOut:nil];
    [self updateDockIconForHiddenWindow];
}

// Called when the main window has gone away (closed or hidden).
- (void)updateDockIconForHiddenWindow {
    // Keep the Dock icon while the settings window is still open, so it stays reachable.
    if (HideDockIconEnabled() && !_settingsWindow.isVisible)
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
}

- (void)windowWillClose:(NSNotification *)notification {
    NSWindow* closing = notification.object;
    if (closing == _window || closing == _settingsWindow) {
        // Defer until the window is actually gone.
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!self->_window.isVisible)
                [self updateDockIconForHiddenWindow];
        });
    }
}

#pragma mark - Settings window

static NSButton* MakeCheckbox(NSString* title, NSString* key, id target, SEL action) {
    NSButton* box = [NSButton checkboxWithTitle:title target:target action:action];
    box.state = [[NSUserDefaults standardUserDefaults] boolForKey:key] ? NSControlStateValueOn : NSControlStateValueOff;
    box.identifier = key;
    return box;
}

// A secondary-text note, indented to line up with a checkbox title.
static NSView* MakeNote(NSString* text) {
    NSTextField* note = [NSTextField wrappingLabelWithString:text];
    note.font = [NSFont systemFontOfSize:11];
    note.textColor = NSColor.secondaryLabelColor;
    note.translatesAutoresizingMaskIntoConstraints = NO;
    [note.widthAnchor constraintEqualToConstant:340].active = YES;
    NSView* indent = [NSView new];
    indent.translatesAutoresizingMaskIntoConstraints = NO;
    [indent.widthAnchor constraintEqualToConstant:12].active = YES;
    NSStackView* row = [NSStackView stackViewWithViews:@[indent, note]];
    row.spacing = 8;
    return row;
}

- (IBAction)showSettings:(id)sender {
    if (!_settingsWindow) {
        NSButton* hideDock = MakeCheckbox(@"Hide Dock icon when the window is minimized or closed", kHideDockIconKey, self, @selector(toggleSetting:));
        NSView* hideDockNote = MakeNote(@"The app stays in the menu bar. Choose Show Window from the menu bar icon to bring the window and Dock icon back.");
        NSButton* autoConnect = MakeCheckbox(@"Connect to the last used headphones on launch", kAutoConnectKey, self, @selector(toggleSetting:));
        NSView* autoConnectNote = MakeNote(@"Only when they're already connected to this Mac in Bluetooth settings.");

        NSStackView* stack = [NSStackView stackViewWithViews:@[hideDock, hideDockNote, autoConnect, autoConnectNote]];
        stack.orientation = NSUserInterfaceLayoutOrientationVertical;
        stack.alignment = NSLayoutAttributeLeading;
        stack.spacing = 6;
        [stack setCustomSpacing:18 afterView:hideDockNote];
        stack.edgeInsets = NSEdgeInsetsMake(20, 20, 20, 20);

        _settingsWindow = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 420, 160)
                                                      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                        backing:NSBackingStoreBuffered
                                                          defer:NO];
        _settingsWindow.title = @"Settings";
        _settingsWindow.releasedWhenClosed = NO;
        _settingsWindow.contentView = stack;
        _settingsWindow.delegate = self;
        [_settingsWindow center];
    }
    if (NSApp.activationPolicy != NSApplicationActivationPolicyRegular)
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activateIgnoringOtherApps:YES];
    [_settingsWindow makeKeyAndOrderFront:nil];
}

- (void)toggleSetting:(NSButton*)sender {
    [[NSUserDefaults standardUserDefaults] setBool:sender.state == NSControlStateValueOn forKey:sender.identifier];
}

@end
