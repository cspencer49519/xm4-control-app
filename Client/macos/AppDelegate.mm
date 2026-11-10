//
//  AppDelegate.m
//  SonyHeadphonesClient
//
//  Created by Sem Visscher on 01/12/2020.
//

#import "AppDelegate.h"
#import <ServiceManagement/ServiceManagement.h>
#import "NetworkModes.h"
#import "AudioDevices.h"

NSString* const kHideDockIconKey = @"HideDockIconWhenHidden";
NSString* const kAutoConnectKey = @"AutoConnectOnLaunch";
NSString* const kLowBatteryNotifyKey = @"NotifyLowBattery";

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
    NSButton* _launchAtLogin;
    NSStackView* _networkList;
    NSPopUpButton* _micPopup;
    id _audioObserver;
    id _networkObserver;
}

+ (void)initialize {
    [[NSUserDefaults standardUserDefaults] registerDefaults:@{
        kHideDockIconKey: @NO,
        kAutoConnectKey: @YES,
        kLowBatteryNotifyKey: @YES,
        kNetworkModesEnabledKey: @YES,
        kAmbientDuringCallsKey: @YES,
        kPreferredMicKey: @"",
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
        NSButton* autoConnect = MakeCheckbox(@"Connect to the last used headphones automatically", kAutoConnectKey, self, @selector(toggleSetting:));
        NSView* autoConnectNote = MakeNote(@"At launch, and whenever they connect to this Mac (for example when you turn them on).");
        NSButton* lowBattery = MakeCheckbox(@"Notify when the battery is low", kLowBatteryNotifyKey, self, @selector(toggleSetting:));
        NSView* lowBatteryNote = MakeNote(@"At 20% and 10%, once per charge.");

        NSButton* networkModes = MakeCheckbox(@"Switch noise control by Wi-Fi network", kNetworkModesEnabledKey, self, @selector(toggleSetting:));
        NSView* networkNote = MakeNote(@"Save a mode for the network you're on from the Noise Control card. It's applied when you join that network or the headphones connect.");
        _networkList = [NSStackView new];
        _networkList.orientation = NSUserInterfaceLayoutOrientationVertical;
        _networkList.alignment = NSLayoutAttributeLeading;
        _networkList.spacing = 4;
        _networkList.edgeInsets = NSEdgeInsetsMake(0, 20, 0, 0);

        NSButton* ambientCalls = MakeCheckbox(@"Switch to Ambient Sound while the microphone is in use", kAmbientDuringCallsKey, self, @selector(toggleSetting:));
        NSView* ambientCallsNote = MakeNote(@"Lets you hear your own voice on calls. Switches back when the call ends, unless you changed the mode during it.");
        NSTextField* micLabel = [NSTextField labelWithString:@"Microphone:"];
        _micPopup = [NSPopUpButton new];
        _micPopup.target = self;
        _micPopup.action = @selector(micChanged:);
        NSStackView* micRow = [NSStackView stackViewWithViews:@[micLabel, _micPopup]];
        micRow.spacing = 8;
        NSView* micNote = MakeNote(@"Using the headphones' microphone switches them to call mode, which lowers music to mono call quality. Pick another microphone to keep it as the Mac's input whenever the headphones connect.");

        NSMutableArray<NSView*>* views = [NSMutableArray arrayWithArray:@[hideDock, hideDockNote, autoConnect, autoConnectNote, lowBattery, lowBatteryNote, networkModes, networkNote, _networkList, ambientCalls, ambientCallsNote, micRow, micNote]];
        if (@available(macOS 13.0, *)) {
            _launchAtLogin = [NSButton checkboxWithTitle:@"Open at login" target:self action:@selector(toggleLaunchAtLogin:)];
            [views addObject:_launchAtLogin];
            [views addObject:MakeNote(@"Keep the app in /Applications so macOS can find it at login.")];
        }

        NSStackView* stack = [NSStackView stackViewWithViews:views];
        stack.orientation = NSUserInterfaceLayoutOrientationVertical;
        stack.alignment = NSLayoutAttributeLeading;
        stack.spacing = 6;
        [stack setCustomSpacing:18 afterView:hideDockNote];
        [stack setCustomSpacing:18 afterView:autoConnectNote];
        [stack setCustomSpacing:18 afterView:lowBatteryNote];
        [stack setCustomSpacing:18 afterView:_networkList];
        [stack setCustomSpacing:18 afterView:networkNote];
        [stack setCustomSpacing:18 afterView:ambientCallsNote];
        [stack setCustomSpacing:18 afterView:micNote];
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
        __weak AppDelegate* weakSelf = self;
        _networkObserver = [[NSNotificationCenter defaultCenter] addObserverForName:NetworkModesDidChangeNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification* note) {
            [weakSelf rebuildNetworkList];
        }];
        _audioObserver = [[NSNotificationCenter defaultCenter] addObserverForName:AudioInputsDidChangeNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification* note) {
            [weakSelf rebuildMicPopup];
        }];
    }
    [self rebuildNetworkList];
    [self rebuildMicPopup];
    if (@available(macOS 13.0, *))
        [self updateLaunchAtLogin];
    if (NSApp.activationPolicy != NSApplicationActivationPolicyRegular)
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activateIgnoringOtherApps:YES];
    [_settingsWindow makeKeyAndOrderFront:nil];
}

// One row per saved network, each with a Remove button.
- (void)rebuildNetworkList {
    for (NSView* view in [_networkList.arrangedSubviews copy])
        [view removeFromSuperview];
    NetworkModes* places = [NetworkModes shared];
    for (NSString* network in [places savedNetworks]) {
        NSTextField* label = [NSTextField labelWithString:[NSString stringWithFormat:@"%@: %@", network, [NetworkModes describeMode:[places modeForNetwork:network]]]];
        label.font = [NSFont systemFontOfSize:12];
        label.lineBreakMode = NSLineBreakByTruncatingTail;
        [label.widthAnchor constraintLessThanOrEqualToConstant:270].active = YES;
        NSButton* remove = [NSButton buttonWithTitle:@"Remove" target:self action:@selector(removeNetwork:)];
        remove.controlSize = NSControlSizeSmall;
        remove.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
        remove.identifier = network;
        NSStackView* row = [NSStackView stackViewWithViews:@[label, remove]];
        row.spacing = 8;
        [_networkList addArrangedSubview:row];
    }
    _networkList.hidden = _networkList.arrangedSubviews.count == 0;
}

// "Headphones' microphone" plus every non-Bluetooth input (Bluetooth ones would put
// headphones into call mode just the same).
- (void)rebuildMicPopup {
    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    NSString* preferred = [defaults stringForKey:kPreferredMicKey] ?: @"";
    [_micPopup removeAllItems];
    [_micPopup addItemWithTitle:@"Headphones (macOS default)"];
    _micPopup.lastItem.representedObject = @"";
    BOOL found = preferred.length == 0;
    for (AudioInput* input in [AudioDevices shared].inputs) {
        if (input.bluetooth)
            continue;
        [_micPopup addItemWithTitle:input.name];
        _micPopup.lastItem.representedObject = input.uid;
        found = found || [input.uid isEqualToString:preferred];
    }
    if (!found) {
        NSString* name = [defaults stringForKey:kPreferredMicNameKey] ?: @"Saved microphone";
        [_micPopup addItemWithTitle:[name stringByAppendingString:@" (not connected)"]];
        _micPopup.lastItem.representedObject = preferred;
    }
    for (NSMenuItem* item in _micPopup.itemArray)
        if ([item.representedObject isEqualToString:preferred])
            [_micPopup selectItem:item];
}

- (void)micChanged:(NSPopUpButton*)sender {
    NSString* uid = sender.selectedItem.representedObject ?: @"";
    [[NSUserDefaults standardUserDefaults] setObject:uid forKey:kPreferredMicKey];
    [[NSUserDefaults standardUserDefaults] setObject:uid.length ? sender.selectedItem.title : @"" forKey:kPreferredMicNameKey];
    [[AudioDevices shared] applyPreferredMicrophone];
}

- (void)removeNetwork:(NSButton*)sender {
    [[NetworkModes shared] setMode:nil forNetwork:sender.identifier];
}

- (void)toggleSetting:(NSButton*)sender {
    [[NSUserDefaults standardUserDefaults] setBool:sender.state == NSControlStateValueOn forKey:sender.identifier];
}

// The checkbox mirrors the system's login item state (the user can also change it in System Settings).
- (void)updateLaunchAtLogin API_AVAILABLE(macos(13.0)) {
    SMAppServiceStatus status = SMAppService.mainAppService.status;
    _launchAtLogin.state = status == SMAppServiceStatusEnabled || status == SMAppServiceStatusRequiresApproval
        ? NSControlStateValueOn : NSControlStateValueOff;
}

- (void)toggleLaunchAtLogin:(NSButton*)sender API_AVAILABLE(macos(13.0)) {
    SMAppService* service = SMAppService.mainAppService;
    NSError* error = nil;
    BOOL ok = sender.state == NSControlStateValueOn ? [service registerAndReturnError:&error] : [service unregisterAndReturnError:&error];
    [self updateLaunchAtLogin];
    if (!ok && error) {
        NSAlert* alert = [NSAlert alertWithError:error];
        alert.messageText = @"Couldn't change the login item";
        [alert beginSheetModalForWindow:_settingsWindow completionHandler:nil];
    } else if (service.status == SMAppServiceStatusRequiresApproval) {
        [SMAppService openSystemSettingsLoginItems];
    }
}

@end
