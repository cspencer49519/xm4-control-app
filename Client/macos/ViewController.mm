//
//  ViewController.mm
//  SonyHeadphonesClient
//
//  Created by Sem Visscher on 01/12/2020.
//
//  The window UI is built in code. Threading model:
//   * main thread  - all AppKit work, reads Headphones::getSettings() snapshots
//   * _ioQueue     - serial queue for every blocking Bluetooth call
//                    (connect, initial state sync, sending changes, disconnect)
//   * listener     - std::thread running Listener::listen(); feeds replies and
//                    notifications into Headphones, which calls back here
//

#import "ViewController.h"
#include "Listener.h"

#include <algorithm>
#include <memory>
#include <thread>

typedef NS_ENUM(NSInteger, ConnectionState) {
    ConnectionStateDisconnected,
    ConnectionStateConnecting,
    ConnectionStateSyncing,
    ConnectionStateReady,
};

typedef NS_ENUM(NSInteger, NoiseMode) {
    NoiseModeCancelling = 0,
    NoiseModeAmbient = 1,
    NoiseModeOff = 2,
};

// Levels as encoded by Headphones: 0 = NC (dual), 1 = NC (single / wind), L+1 = ambient level L
// (1..N, where N = Headphones::getAmbientSteps(), reported by the device).
static const int kAmbientMinLevel = 2;
static const auto kSyncTimeout = std::chrono::milliseconds(8000);
static NSString* const kLastDeviceKey = @"LastDeviceAddress";
static NSString* const kLastDeviceNameKey = @"LastDeviceName";
static NSString* const kSelectedTabKey = @"SelectedTab";

// Process-wide Bluetooth objects. Headphones keeps a reference to the wrapper,
// so both live for the whole app lifetime.
static BluetoothWrapper* g_bt = nullptr;
static Headphones* g_headphones = nullptr;
static std::atomic<bool> g_listenerRunning{ false };
static std::unique_ptr<Listener> g_listener;
static std::thread g_listenerThread;

// Stop the listener thread and close the connection. Safe to call repeatedly.
static void StopConnection()
{
    g_listenerRunning = false;
    g_bt->disconnect(); // also wakes the listener blocked in recv()
    if (g_listenerThread.joinable())
        g_listenerThread.join();
    g_listener.reset();
}

static NSString* CodecName(AUDIO_CODEC codec) {
    switch (codec) {
        case AUDIO_CODEC::SBC: return @"SBC";
        case AUDIO_CODEC::AAC: return @"AAC";
        case AUDIO_CODEC::LDAC: return @"LDAC";
        case AUDIO_CODEC::APTX: return @"aptX";
        case AUDIO_CODEC::APTX_HD: return @"aptX HD";
        default: return @"Other codec";
    }
}

// The official app's labels for known preset IDs; other IDs use the name the device sends.
static NSString* EqPresetName(const EqPreset& preset) {
    switch (preset.id) {
        case 0x00: return @"Off";
        case 0x01: return @"Rock";
        case 0x02: return @"Pop";
        case 0x03: return @"Jazz";
        case 0x04: return @"Dance";
        case 0x05: return @"EDM";
        case 0x06: return @"R&B / Hip Hop";
        case 0x07: return @"Acoustic";
        case 0x10: return @"Bright";
        case 0x11: return @"Excited";
        case 0x12: return @"Mellow";
        case 0x13: return @"Relaxed";
        case 0x14: return @"Vocal";
        case 0x15: return @"Treble Boost";
        case 0x16: return @"Bass Boost";
        case 0x17: return @"Speech";
        case EQ_PRESET_MANUAL: return @"Manual";
        case 0xa1: case 0xa2: case 0xa3: case 0xa4: case 0xa5:
            return [NSString stringWithFormat:@"Custom %d", preset.id - 0xa0];
    }
    return preset.name.empty() ? [NSString stringWithFormat:@"Preset %02X", preset.id] : @(preset.name.c_str());
}

// --- Device settings text ---

// Display names for GENERAL_SETTING keys the device sends as symbolic names.
static NSString* GeneralSettingText(const GeneralSettingItem& item) {
    if (!item.enumName)
        return @(item.name.c_str());
    static NSDictionary<NSString*, NSString*>* names = @{
        @"TOUCH_PANEL_SETTING": @"Touch sensor control panel",
        @"MULTIPOINT_SETTING": @"Connect to 2 devices simultaneously",
        @"MULTIPOINT_SETTING_SUMMARY": @"Stay connected to two devices, such as this Mac and your phone, and switch between them.",
        @"SIDETONE_SETTING": @"Hear your own voice during calls",
        @"VOICE_GUIDANCE_SETTING": @"Voice guidance",
        @"TAP_SENSITIVITY_SETTING": @"Tap sensitivity",
        @"FACETAP_SETTING": @"Face tap",
    };
    NSString* key = @(item.name.c_str());
    if (names[key])
        return names[key];
    // Unknown key: "SOME_NEW_SETTING" -> "Some new setting"
    NSString* text = [[key stringByReplacingOccurrencesOfString:@"_" withString:@" "] lowercaseString];
    if ([text hasSuffix:@" setting"])
        text = [text substringToIndex:text.length - 8];
    return text.length ? [[[text substringToIndex:1] uppercaseString] stringByAppendingString:[text substringFromIndex:1]] : key;
}

static NSString* AssignablePresetName(unsigned char preset) {
    switch (preset) {
        case ASSIGNABLE_AMBIENT_SOUND_CONTROL: return @"Ambient Sound Control";
        case ASSIGNABLE_VOLUME_CONTROL: return @"Volume control";
        case ASSIGNABLE_PLAYBACK_CONTROL: return @"Playback control";
        case ASSIGNABLE_VOICE_RECOGNITION: return @"Voice assistant (Siri)";
        case ASSIGNABLE_GOOGLE_ASSISTANT: return @"Google Assistant";
        case ASSIGNABLE_AMAZON_ALEXA: return @"Amazon Alexa";
        case ASSIGNABLE_TENCENT_XIAOWEI: return @"Tencent Xiaowei";
        case ASSIGNABLE_NO_FUNCTION: return @"No function";
    }
    return [NSString stringWithFormat:@"Function %02X", preset];
}

static NSString* AssignableKeyName(const AssignableKey& key) {
    switch (key.key) {
        case 0x00: return @"Left touch sensor";
        case 0x01: return @"Right touch sensor";
        case 0x02: return @"Custom button";
        case 0x03: return @"C button";
    }
    return @"Button";
}

static NSString* AutoPowerOffName(int element) {
    switch (element) {
        case AUTO_POWER_OFF_5_MIN: return @"After 5 minutes";
        case AUTO_POWER_OFF_30_MIN: return @"After 30 minutes";
        case AUTO_POWER_OFF_60_MIN: return @"After 1 hour";
        case AUTO_POWER_OFF_180_MIN: return @"After 3 hours";
        case AUTO_POWER_OFF_WHEN_REMOVED: return @"When taken off";
        case AUTO_POWER_OFF_DISABLED: return @"Never";
    }
    return [NSString stringWithFormat:@"Option %02X", element];
}

// Text for the device's confirmation alerts (AlertMessageType).
static NSString* AlertText(unsigned char message) {
    switch (message) {
        case 0x01: return @"Changing this setting reconnects the headphones. Playback stops briefly.";
        case 0x02: return @"Changing the button function reconnects the headphones.";
        case 0x03: return @"The headphones need to disconnect to update their firmware.";
        case 0x04: return @"Google Assistant is now available on the headphones.";
        case 0x05: return @"Only one voice assistant can be assigned at a time.";
        case 0x06: return @"Turning on multipoint reconnects the headphones.";
        case 0x07: return @"Turning off multipoint disconnects the headphones from the other device.";
        case 0x08: return @"Using the equalizer together with DSEE increases battery use.";
    }
    return [NSString stringWithFormat:@"The headphones are asking for confirmation (message %02X).", message];
}

static NSString* EqValueText(int value) {
    return value == 0 ? @"0" : [NSString stringWithFormat:@"%+d", value];
}

static NSString* UpscalingName(UPSCALING_TYPE type) {
    switch (type) {
        case UPSCALING_TYPE::DSEE_HX: return @"DSEE HX";
        case UPSCALING_TYPE::DSEE: return @"DSEE";
        case UPSCALING_TYPE::DSEE_EXTREME: return @"DSEE Extreme";
    }
    return @"DSEE";
}

@interface FlippedView : NSView
@end
@implementation FlippedView
- (BOOL)isFlipped { return YES; }
@end

@implementation ViewController {
    dispatch_queue_t _ioQueue;
    ConnectionState _state;
    // Incremented on every connect/disconnect so stale async completions can be ignored.
    NSUInteger _generation;
    BOOL _applying;
    BOOL _didAutoConnect;
    NSString* _deviceName;
    int _lastAmbientLevel;
    NSString* _devicesSignature;
    NSTimer* _connectionTimer;
    NSStatusItem* _statusItem;
    id _terminateObserver;

    // Header
    NSTextField* _deviceNameLabel;
    NSTextField* _statusLabel;
    NSTextField* _detailLabel;
    NSTextField* _batteryLabel;
    NSImageView* _batteryIcon;
    NSProgressIndicator* _spinner;
    NSButton* _connectButton;
    NSButton* _reloadButton;
    NSTextField* _messageLabel;

    // Tabs
    NSSegmentedControl* _tabBar;
    // Holds every tab page. Pages stay in the window and are only hidden, so they keep
    // laying out while another tab is shown (NSTabView detaches them, and pages rebuilt
    // while detached came back empty).
    NSView* _tabView;
    NSArray<NSView*>* _tabPages;

    // Now playing
    NSView* _nowPlayingCard;
    NSStackView* _transportButtons;
    NSButton* _previousButton;
    NSButton* _playPauseButton;
    NSButton* _nextButton;
    NSTextField* _trackLabel;
    NSTextField* _artistLabel;
    NSStackView* _volumeRow;
    NSSlider* _volumeSlider;
    NSTextField* _volumeValue;
    BOOL _metadataRefreshQueued;

    // Cards
    NSView* _emptyCard;
    NSView* _noiseCard;
    NSView* _soundCard;
    NSView* _s2cCard;
    NSView* _optimizerCard;
    NSView* _devicesCard;
    NSView* _eqCard;
    NSView* _connectionCard;
    NSView* _controlsCard;
    NSGridView* _connectionGrid;
    NSGridView* _controlsGrid;
    NSString* _settingsSignature;
    // control -> what it edits: tag = SETTING value, or 100 + key index for custom buttons
    NSMutableArray<NSControl*>* _settingControls;
    NSMutableDictionary<NSNumber*, NSTextField*>* _assignableHints;
    BOOL _alertShowing;
    NSButton* _powerOffButton;

    // Equalizer (rebuilt when the device's EQ description changes)
    NSPopUpButton* _eqPresetPopup;
    NSButton* _eqFlatButton;
    NSStackView* _eqBody;
    NSStackView* _eqClearBassRow;
    NSStackView* _eqBandsRow;
    NSTextField* _eqHint;
    NSMutableArray<NSSlider*>* _eqSliders;      // index = band index
    NSMutableArray<NSTextField*>* _eqValueLabels;
    NSString* _eqSignature;

    // Noise control
    NSSegmentedControl* _noiseMode;
    NSView* _ncOptions;
    NSButton* _windNoise;
    NSView* _ambientOptions;
    NSSlider* _ambientSlider;
    NSTextField* _ambientValue;
    NSButton* _focusOnVoice;

    // Sound
    NSPopUpButton* _vptPopup;
    NSPopUpButton* _positionPopup;

    // Speak-to-Chat
    NSSwitch* _s2cSwitch;
    NSPopUpButton* _s2cSensitivity;
    NSPopUpButton* _s2cAutoClose;
    NSButton* _s2cVoice;

    // Optimizer
    NSButton* _optimizeButton;
    NSTextField* _optimizeStatus;

    // Multipoint
    NSStackView* _devicesList;
    NSTextField* _devicesHint;
}

#pragma mark - Lifecycle

- (void)viewDidLoad {
    [super viewDidLoad];

    if (g_bt == nullptr) {
        g_bt = new BluetoothWrapper(std::make_unique<MacOSBluetoothConnector>());
        g_headphones = new Headphones(*g_bt);
    }

    _ioQueue = dispatch_queue_create("SonyHeadphonesClient.io", DISPATCH_QUEUE_SERIAL);
    _state = ConnectionStateDisconnected;
    _lastAmbientLevel = DEFAULT_AMBIENT_STEPS + 1;

    __weak ViewController* weakSelf = self;
    g_headphones->setAlertCallback([weakSelf](unsigned char message, bool needsChoice) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [weakSelf showDeviceAlert:message needsChoice:needsChoice];
        });
    });
    g_headphones->setStateChangedCallback([weakSelf]() {
        dispatch_async(dispatch_get_main_queue(), ^{
            [weakSelf deviceStateChanged];
        });
    });

    [self buildUI];
    [self setUpStatusItem];

    _connectionTimer = [NSTimer scheduledTimerWithTimeInterval:1.0 target:self selector:@selector(checkConnection:) userInfo:nil repeats:YES];

    _terminateObserver = [[NSNotificationCenter defaultCenter] addObserverForName:NSApplicationWillTerminateNotification object:nil queue:nil usingBlock:^(NSNotification* note) {
        StopConnection();
    }];

    [self refreshUI];
}

- (void)viewDidAppear {
    [super viewDidAppear];
    self.view.window.minSize = NSMakeSize(420, 420);
    if (!_didAutoConnect) {
        _didAutoConnect = YES;
        [self autoConnectToLastDevice];
    }
}

- (void)dealloc {
    [_connectionTimer invalidate];
    if (_terminateObserver)
        [[NSNotificationCenter defaultCenter] removeObserver:_terminateObserver];
}

#pragma mark - UI construction helpers

static NSTextField* MakeLabel(NSString* text, NSFont* font, NSColor* color) {
    NSTextField* label = [NSTextField labelWithString:text ?: @""];
    label.font = font;
    label.textColor = color;
    label.translatesAutoresizingMaskIntoConstraints = NO;
    return label;
}

static NSImageView* MakeSymbol(NSString* name, CGFloat pointSize, NSColor* tint) {
    NSImage* image = [NSImage imageWithSystemSymbolName:name accessibilityDescription:nil];
    NSImageView* view = [NSImageView imageViewWithImage:image ?: [NSImage new]];
    view.symbolConfiguration = [NSImageSymbolConfiguration configurationWithPointSize:pointSize weight:NSFontWeightRegular];
    view.contentTintColor = tint;
    view.translatesAutoresizingMaskIntoConstraints = NO;
    [view setContentHuggingPriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationHorizontal];
    return view;
}

static NSStackView* MakeStack(NSUserInterfaceLayoutOrientation orientation, CGFloat spacing, NSArray<NSView*>* views) {
    NSStackView* stack = [NSStackView stackViewWithViews:views];
    stack.orientation = orientation;
    stack.spacing = spacing;
    stack.alignment = orientation == NSUserInterfaceLayoutOrientationVertical ? NSLayoutAttributeLeading : NSLayoutAttributeCenterY;
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    return stack;
}

static NSView* MakeSpacer(void) {
    NSView* spacer = [NSView new];
    spacer.translatesAutoresizingMaskIntoConstraints = NO;
    [spacer setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];
    return spacer;
}

static NSPopUpButton* MakePopup(NSArray<NSString*>* items, id target, SEL action) {
    NSPopUpButton* popup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [popup addItemsWithTitles:items];
    popup.target = target;
    popup.action = action;
    popup.translatesAutoresizingMaskIntoConstraints = NO;
    [popup.widthAnchor constraintGreaterThanOrEqualToConstant:180].active = YES;
    return popup;
}

// A rounded card with an icon + title header above `body`.
- (NSView*)cardWithTitle:(NSString*)title symbol:(NSString*)symbol body:(NSView*)body {
    NSBox* box = [NSBox new];
    box.boxType = NSBoxCustom;
    box.titlePosition = NSNoTitle;
    box.cornerRadius = 10;
    box.borderWidth = 1;
    box.borderColor = NSColor.separatorColor;
    box.fillColor = NSColor.controlBackgroundColor;
    box.contentViewMargins = NSZeroSize;
    box.translatesAutoresizingMaskIntoConstraints = NO;

    NSView* content;
    if (title) {
        NSStackView* header = MakeStack(NSUserInterfaceLayoutOrientationHorizontal, 8, @[
            MakeSymbol(symbol, 15, NSColor.controlAccentColor),
            MakeLabel(title, [NSFont systemFontOfSize:14 weight:NSFontWeightSemibold], NSColor.labelColor),
        ]);
        content = MakeStack(NSUserInterfaceLayoutOrientationVertical, 12, @[header, body]);
        [body.widthAnchor constraintEqualToAnchor:content.widthAnchor].active = YES;
    } else {
        content = body;
    }

    [box.contentView addSubview:content];
    [NSLayoutConstraint activateConstraints:@[
        [content.leadingAnchor constraintEqualToAnchor:box.contentView.leadingAnchor constant:16],
        [content.trailingAnchor constraintEqualToAnchor:box.contentView.trailingAnchor constant:-16],
        [content.topAnchor constraintEqualToAnchor:box.contentView.topAnchor constant:14],
        [content.bottomAnchor constraintEqualToAnchor:box.contentView.bottomAnchor constant:-14],
    ]];
    return box;
}

// Two-column grid of "Label:  control" rows.
static NSGridView* MakeFormGrid(NSArray<NSArray<NSView*>*>* rows) {
    NSGridView* grid = [NSGridView gridViewWithViews:rows];
    grid.rowSpacing = 10;
    grid.columnSpacing = 10;
    grid.translatesAutoresizingMaskIntoConstraints = NO;
    // (an empty grid has no columns yet; rebuildSettingsCards sets placement after adding rows)
    if (grid.numberOfColumns > 0)
        [grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
    // keep the grid at its natural width so it sits at the leading edge of the card
    [grid setContentHuggingPriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationHorizontal];
    for (NSInteger i = 0; i < grid.numberOfRows; i++)
        [grid rowAtIndex:i].yPlacement = NSGridCellPlacementCenter;
    return grid;
}

#pragma mark - Build UI

// A scrolling column of cards, used for each tab (scrolls only if the window is short).
- (NSScrollView*)tabColumnWithCards:(NSArray<NSView*>*)cards {
    NSScrollView* scroll = [NSScrollView new];
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.hasVerticalScroller = YES;
    scroll.autohidesScrollers = YES;
    scroll.drawsBackground = NO;

    FlippedView* document = [FlippedView new];
    document.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.documentView = document;
    NSClipView* clip = scroll.contentView;
    [NSLayoutConstraint activateConstraints:@[
        [document.leadingAnchor constraintEqualToAnchor:clip.leadingAnchor],
        [document.trailingAnchor constraintEqualToAnchor:clip.trailingAnchor],
        [document.topAnchor constraintEqualToAnchor:clip.topAnchor],
    ]];

    NSStackView* column = MakeStack(NSUserInterfaceLayoutOrientationVertical, 14, cards);
    column.alignment = NSLayoutAttributeCenterX;
    column.edgeInsets = NSEdgeInsetsMake(4, 18, 18, 18);
    [document addSubview:column];
    [NSLayoutConstraint activateConstraints:@[
        [column.leadingAnchor constraintEqualToAnchor:document.leadingAnchor],
        [column.trailingAnchor constraintEqualToAnchor:document.trailingAnchor],
        [column.topAnchor constraintEqualToAnchor:document.topAnchor],
        [column.bottomAnchor constraintEqualToAnchor:document.bottomAnchor],
    ]];
    for (NSView* card in cards)
        [card.widthAnchor constraintEqualToAnchor:column.widthAnchor constant:-36].active = YES;
    return scroll;
}

- (void)buildUI {
    NSView* root = self.view;

    NSView* header = [self buildHeaderCard];
    _nowPlayingCard = [self buildNowPlayingCard];
    _emptyCard = [self buildEmptyCard];
    _noiseCard = [self buildNoiseCard];
    _soundCard = [self buildSoundCard];
    _s2cCard = [self buildSpeakToChatCard];
    _optimizerCard = [self buildOptimizerCard];
    _eqCard = [self buildEqCard];
    _connectionGrid = MakeFormGrid(@[]);
    _controlsGrid = MakeFormGrid(@[]);
    _connectionCard = [self cardWithTitle:@"Connection & Audio Quality" symbol:@"antenna.radiowaves.left.and.right" body:_connectionGrid];
    NSButton* powerOff = [NSButton buttonWithTitle:@"Power Off Headphones" target:self action:@selector(powerOffClicked:)];
    powerOff.image = [NSImage imageWithSystemSymbolName:@"power" accessibilityDescription:nil];
    powerOff.imagePosition = NSImageLeading;
    _powerOffButton = powerOff;
    NSStackView* controlsBody = MakeStack(NSUserInterfaceLayoutOrientationVertical, 14, @[_controlsGrid, powerOff]);
    _controlsCard = [self cardWithTitle:@"Buttons & Power" symbol:@"hand.tap" body:controlsBody];
    _settingControls = [NSMutableArray array];
    _assignableHints = [NSMutableDictionary dictionary];
    _devicesCard = [self buildDevicesCard];

    // Tabs: a segmented control drives a tabless NSTabView.
    NSArray<NSString*>* tabTitles = @[@"Sound", @"Controls", @"Connection"];
    NSArray<NSString*>* tabSymbols = @[@"waveform", @"hand.tap", @"antenna.radiowaves.left.and.right"];
    NSArray<NSArray<NSView*>*>* tabCards = @[
        @[_noiseCard, _eqCard, _soundCard, _optimizerCard],
        @[_s2cCard, _controlsCard],
        @[_connectionCard, _devicesCard],
    ];
    _tabBar = [NSSegmentedControl segmentedControlWithLabels:tabTitles
                                                trackingMode:NSSegmentSwitchTrackingSelectOne
                                                      target:self
                                                      action:@selector(tabChanged:)];
    _tabBar.segmentDistribution = NSSegmentDistributionFillEqually;
    _tabBar.translatesAutoresizingMaskIntoConstraints = NO;
    _tabView = [NSView new];
    _tabView.translatesAutoresizingMaskIntoConstraints = NO;
    NSMutableArray<NSView*>* pages = [NSMutableArray array];
    for (NSUInteger i = 0; i < tabTitles.count; i++) {
        [_tabBar setImage:[NSImage imageWithSystemSymbolName:tabSymbols[i] accessibilityDescription:nil] forSegment:i];
        [_tabBar setImageScaling:NSImageScaleProportionallyDown forSegment:i];
        NSView* page = [self tabColumnWithCards:tabCards[i]];
        [_tabView addSubview:page];
        [NSLayoutConstraint activateConstraints:@[
            [page.leadingAnchor constraintEqualToAnchor:_tabView.leadingAnchor],
            [page.trailingAnchor constraintEqualToAnchor:_tabView.trailingAnchor],
            [page.topAnchor constraintEqualToAnchor:_tabView.topAnchor],
            [page.bottomAnchor constraintEqualToAnchor:_tabView.bottomAnchor],
        ]];
        [pages addObject:page];
    }
    _tabPages = pages;
    NSInteger savedTab = [[NSUserDefaults standardUserDefaults] integerForKey:kSelectedTabKey];
    savedTab = std::clamp<NSInteger>(savedTab, 0, tabTitles.count - 1);
    _tabBar.selectedSegment = savedTab;
    [self showTab:savedTab];

    // Fixed top area: header, now playing, tab bar (or the getting-started card).
    NSStackView* top = MakeStack(NSUserInterfaceLayoutOrientationVertical, 12, @[header, _nowPlayingCard, _emptyCard, _tabBar]);
    top.alignment = NSLayoutAttributeCenterX;
    top.edgeInsets = NSEdgeInsetsMake(18, 18, 6, 18);
    for (NSView* v in @[header, _nowPlayingCard, _emptyCard])
        [v.widthAnchor constraintEqualToAnchor:top.widthAnchor constant:-36].active = YES;
    [_tabBar.widthAnchor constraintLessThanOrEqualToConstant:420].active = YES;
    NSLayoutConstraint* tabWidth = [_tabBar.widthAnchor constraintEqualToAnchor:top.widthAnchor constant:-36];
    tabWidth.priority = NSLayoutPriorityDefaultHigh;
    tabWidth.active = YES;
    [root addSubview:top];
    [root addSubview:_tabView];
    [NSLayoutConstraint activateConstraints:@[
        [top.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],
        [top.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],
        [top.topAnchor constraintEqualToAnchor:root.topAnchor],
        [_tabView.topAnchor constraintEqualToAnchor:top.bottomAnchor constant:6],
        [_tabView.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],
        [_tabView.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],
        [_tabView.bottomAnchor constraintEqualToAnchor:root.bottomAnchor],
    ]];
}

- (void)showTab:(NSInteger)index {
    for (NSUInteger i = 0; i < _tabPages.count; i++)
        _tabPages[i].hidden = (NSInteger)i != index;
}

- (void)tabChanged:(NSSegmentedControl*)sender {
    [self showTab:sender.selectedSegment];
    [[NSUserDefaults standardUserDefaults] setInteger:sender.selectedSegment forKey:kSelectedTabKey];
}

#pragma mark - Now playing

static NSButton* MakeSymbolButton(NSString* symbol, CGFloat size, id target, SEL action, NSString* tip) {
    NSImageSymbolConfiguration* config = [NSImageSymbolConfiguration configurationWithPointSize:size weight:NSFontWeightRegular];
    NSImage* image = [[NSImage imageWithSystemSymbolName:symbol accessibilityDescription:tip] imageWithSymbolConfiguration:config];
    NSButton* button = [NSButton buttonWithImage:image target:target action:action];
    button.bordered = NO;
    button.toolTip = tip;
    button.translatesAutoresizingMaskIntoConstraints = NO;
    [button.widthAnchor constraintEqualToConstant:size + 14].active = YES;
    return button;
}

- (NSView*)buildNowPlayingCard {
    _previousButton = MakeSymbolButton(@"backward.fill", 15, self, @selector(previousClicked:), @"Previous track");
    _playPauseButton = MakeSymbolButton(@"play.fill", 20, self, @selector(playPauseClicked:), @"Play");
    _nextButton = MakeSymbolButton(@"forward.fill", 15, self, @selector(nextClicked:), @"Next track");
    _transportButtons = MakeStack(NSUserInterfaceLayoutOrientationHorizontal, 2, @[_previousButton, _playPauseButton, _nextButton]);

    _trackLabel = MakeLabel(@"", [NSFont systemFontOfSize:13 weight:NSFontWeightSemibold], NSColor.labelColor);
    _artistLabel = MakeLabel(@"", [NSFont systemFontOfSize:11], NSColor.secondaryLabelColor);
    for (NSTextField* label in @[_trackLabel, _artistLabel]) {
        label.lineBreakMode = NSLineBreakByTruncatingTail;
        [label setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    }
    NSStackView* titles = MakeStack(NSUserInterfaceLayoutOrientationVertical, 1, @[_trackLabel, _artistLabel]);
    titles.alignment = NSLayoutAttributeLeading;
    for (NSTextField* label in @[_trackLabel, _artistLabel])
        [label setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow + 10 forOrientation:NSLayoutConstraintOrientationHorizontal];

    _volumeSlider = [NSSlider sliderWithValue:0 minValue:0 maxValue:30 target:self action:@selector(volumeChanged:)];
    _volumeSlider.continuous = YES;
    _volumeSlider.controlSize = NSControlSizeSmall;
    _volumeSlider.translatesAutoresizingMaskIntoConstraints = NO;
    _volumeSlider.toolTip = @"Headphone volume";
    [_volumeSlider.widthAnchor constraintGreaterThanOrEqualToConstant:90].active = YES;
    // Track names get the room first; the slider takes what's left, up to 150.
    [_volumeSlider.widthAnchor constraintLessThanOrEqualToConstant:150].active = YES;
    NSLayoutConstraint* volumeWidth = [_volumeSlider.widthAnchor constraintEqualToConstant:150];
    volumeWidth.priority = NSLayoutPriorityDefaultLow - 10;
    volumeWidth.active = YES;
    _volumeValue = MakeLabel(@"", [NSFont monospacedDigitSystemFontOfSize:11 weight:NSFontWeightRegular], NSColor.secondaryLabelColor);
    _volumeValue.alignment = NSTextAlignmentRight;
    [_volumeValue.widthAnchor constraintEqualToConstant:20].active = YES;
    _volumeRow = MakeStack(NSUserInterfaceLayoutOrientationHorizontal, 6, @[
        MakeSymbol(@"speaker.fill", 11, NSColor.secondaryLabelColor), _volumeSlider, _volumeValue,
    ]);

    NSStackView* row = MakeStack(NSUserInterfaceLayoutOrientationHorizontal, 12, @[_transportButtons, titles, MakeSpacer(), _volumeRow]);
    NSStackView* body = MakeStack(NSUserInterfaceLayoutOrientationVertical, 0, @[row]);
    [row.widthAnchor constraintEqualToAnchor:body.widthAnchor].active = YES;
    return [self cardWithTitle:nil symbol:nil body:body];
}

- (void)sendPlaybackControl:(PLAYBACK_CONTROL)control {
    if (_state != ConnectionStateReady)
        return;
    dispatch_async(_ioQueue, ^{
        try {
            g_headphones->sendPlaybackControl(control);
        } catch (const std::exception&) {
            // A lost connection is picked up by checkConnection:.
        }
    });
}

- (void)playPauseClicked:(id)sender {
    bool playing = g_headphones->getPlaybackState().status == PLAYBACK_STATUS::PLAYING;
    [self sendPlaybackControl:playing ? PLAYBACK_CONTROL::PAUSE : PLAYBACK_CONTROL::PLAY];
}

- (void)previousClicked:(id)sender {
    [self sendPlaybackControl:PLAYBACK_CONTROL::PREVIOUS_TRACK];
}

- (void)nextClicked:(id)sender {
    [self sendPlaybackControl:PLAYBACK_CONTROL::NEXT_TRACK];
}

- (void)volumeChanged:(NSSlider*)sender {
    g_headphones->setVolume(sender.intValue);
    _volumeValue.stringValue = [NSString stringWithFormat:@"%d", sender.intValue];
    [self scheduleApply];
}

- (void)refreshNowPlaying:(BOOL)mouseDown {
    PlaybackCapability cap = g_headphones->getPlaybackCapability();
    PlaybackState ps = g_headphones->getPlaybackState();
    _nowPlayingCard.hidden = !cap.available;
    if (!cap.available)
        return;

    // Track names arrive as "changed" notifications and are fetched off the listener thread.
    if (g_headphones->hasStalePlaybackMetadata() && !_metadataRefreshQueued) {
        _metadataRefreshQueued = YES;
        __weak ViewController* weakSelf = self;
        dispatch_async(_ioQueue, ^{
            try {
                g_headphones->refreshPlaybackMetadata();
            } catch (const std::exception&) {
            }
            dispatch_async(dispatch_get_main_queue(), ^{
                ViewController* strongSelf = weakSelf;
                if (strongSelf)
                    strongSelf->_metadataRefreshQueued = NO;
            });
        });
    }

    bool playing = ps.status == PLAYBACK_STATUS::PLAYING;
    _transportButtons.hidden = !cap.transport;
    NSImageSymbolConfiguration* config = [NSImageSymbolConfiguration configurationWithPointSize:20 weight:NSFontWeightRegular];
    _playPauseButton.image = [[NSImage imageWithSystemSymbolName:playing ? @"pause.fill" : @"play.fill"
                                       accessibilityDescription:playing ? @"Pause" : @"Play"] imageWithSymbolConfiguration:config];
    _playPauseButton.toolTip = playing ? @"Pause" : @"Play";

    NSString* track = ps.track.empty() ? nil : @(ps.track.c_str());
    NSMutableArray<NSString*>* subtitle = [NSMutableArray array];
    for (const std::string* part : { &ps.artist, &ps.album })
        if (!part->empty())
            [subtitle addObject:@(part->c_str())];
    NSString* status = playing ? @"Playing" : ps.status == PLAYBACK_STATUS::PAUSED ? @"Paused"
                     : ps.status == PLAYBACK_STATUS::STOPPED ? @"Stopped" : @"Not playing";
    _trackLabel.stringValue = track ?: status;
    if (subtitle.count == 0 && !ps.player.empty())
        [subtitle addObject:@(ps.player.c_str())];
    if (subtitle.count == 0 && track)
        [subtitle addObject:status];
    _artistLabel.stringValue = [subtitle componentsJoinedByString:@" — "];
    _artistLabel.hidden = subtitle.count == 0;

    bool hasVolume = cap.volumeSteps > 0 && ps.volume >= 0;
    _volumeRow.hidden = !hasVolume;
    if (hasVolume) {
        if (_volumeSlider.maxValue != cap.volumeSteps - 1)
            _volumeSlider.maxValue = cap.volumeSteps - 1;
        if (!mouseDown)
            _volumeSlider.intValue = ps.volume;
        _volumeValue.stringValue = [NSString stringWithFormat:@"%d", ps.volume];
    }
}

- (NSView*)buildHeaderCard {
    _deviceNameLabel = MakeLabel(@"No headphones", [NSFont systemFontOfSize:17 weight:NSFontWeightSemibold], NSColor.labelColor);
    _deviceNameLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    // Shrinks only after the spacer is gone.
    [_deviceNameLabel setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow + 10 forOrientation:NSLayoutConstraintOrientationHorizontal];
    _statusLabel = MakeLabel(@"Not connected", [NSFont systemFontOfSize:12], NSColor.secondaryLabelColor);

    _spinner = [NSProgressIndicator new];
    _spinner.style = NSProgressIndicatorStyleSpinning;
    _spinner.controlSize = NSControlSizeSmall;
    _spinner.displayedWhenStopped = NO;
    _spinner.translatesAutoresizingMaskIntoConstraints = NO;

    _batteryIcon = MakeSymbol(@"battery.100", 14, NSColor.secondaryLabelColor);
    _batteryLabel = MakeLabel(@"", [NSFont monospacedDigitSystemFontOfSize:12 weight:NSFontWeightMedium], NSColor.secondaryLabelColor);

    _detailLabel = MakeLabel(@"", [NSFont systemFontOfSize:11], NSColor.tertiaryLabelColor);
    _detailLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    [_detailLabel setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    NSStackView* titles = MakeStack(NSUserInterfaceLayoutOrientationVertical, 2, @[_deviceNameLabel, _statusLabel, _detailLabel]);
    _connectButton = [NSButton buttonWithTitle:@"Connect…" target:self action:@selector(connectClicked:)];
    _connectButton.bezelStyle = NSBezelStyleRounded;
    _connectButton.keyEquivalent = @"\r";
    _reloadButton = [NSButton buttonWithImage:[NSImage imageWithSystemSymbolName:@"arrow.clockwise" accessibilityDescription:@"Reload settings"]
                                       target:self action:@selector(reloadClicked:)];
    _reloadButton.bezelStyle = NSBezelStyleRounded;
    _connectButton.controlSize = _reloadButton.controlSize = NSControlSizeSmall;
    _reloadButton.toolTip = @"Read every setting from the headphones again";

    NSStackView* top = MakeStack(NSUserInterfaceLayoutOrientationHorizontal, 12, @[
        MakeSymbol(@"headphones", 28, NSColor.controlAccentColor), titles, MakeSpacer(), _spinner, _batteryIcon, _batteryLabel,
        _reloadButton, _connectButton,
    ]);
    [top setCustomSpacing:4 afterView:_batteryIcon];
    [top setCustomSpacing:16 afterView:_batteryLabel];
    [top setCustomSpacing:6 afterView:_reloadButton];

    _messageLabel = MakeLabel(@"", [NSFont systemFontOfSize:12], NSColor.systemRedColor);
    _messageLabel.maximumNumberOfLines = 4;
    _messageLabel.lineBreakMode = NSLineBreakByWordWrapping;
    _messageLabel.selectable = YES;
    _messageLabel.hidden = YES;
    [_messageLabel setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];

    NSStackView* body = MakeStack(NSUserInterfaceLayoutOrientationVertical, 10, @[top, _messageLabel]);
    [top.widthAnchor constraintEqualToAnchor:body.widthAnchor].active = YES;
    [_messageLabel.widthAnchor constraintEqualToAnchor:body.widthAnchor].active = YES;
    return [self cardWithTitle:nil symbol:nil body:body];
}

- (NSView*)buildEmptyCard {
    NSTextField* text = MakeLabel(@"Connect your headphones to see and change their settings. "
                                  @"They need to be paired and connected in macOS Bluetooth settings first.",
                                  [NSFont systemFontOfSize:12], NSColor.secondaryLabelColor);
    text.maximumNumberOfLines = 0;
    text.lineBreakMode = NSLineBreakByWordWrapping;
    [text setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    NSStackView* body = MakeStack(NSUserInterfaceLayoutOrientationVertical, 0, @[text]);
    [text.widthAnchor constraintEqualToAnchor:body.widthAnchor].active = YES;
    return [self cardWithTitle:@"Getting started" symbol:@"info.circle" body:body];
}

- (NSView*)buildNoiseCard {
    _noiseMode = [NSSegmentedControl segmentedControlWithLabels:@[@"Noise Cancelling", @"Ambient Sound", @"Off"]
                                                   trackingMode:NSSegmentSwitchTrackingSelectOne
                                                         target:self
                                                         action:@selector(noiseModeChanged:)];
    _noiseMode.segmentDistribution = NSSegmentDistributionFillEqually;
    _noiseMode.translatesAutoresizingMaskIntoConstraints = NO;

    _windNoise = [NSButton checkboxWithTitle:@"Wind noise reduction (single-microphone mode)" target:self action:@selector(noiseModeChanged:)];
    _ncOptions = MakeStack(NSUserInterfaceLayoutOrientationVertical, 8, @[_windNoise]);

    _ambientSlider = [NSSlider sliderWithValue:_lastAmbientLevel minValue:kAmbientMinLevel maxValue:_lastAmbientLevel target:self action:@selector(ambientLevelChanged:)];
    _ambientSlider.continuous = YES;
    _ambientSlider.numberOfTickMarks = DEFAULT_AMBIENT_STEPS;
    _ambientSlider.allowsTickMarkValuesOnly = YES;
    _ambientSlider.translatesAutoresizingMaskIntoConstraints = NO;
    _ambientValue = MakeLabel(@"", [NSFont monospacedDigitSystemFontOfSize:12 weight:NSFontWeightRegular], NSColor.secondaryLabelColor);
    _ambientValue.alignment = NSTextAlignmentRight;
    [_ambientValue.widthAnchor constraintEqualToConstant:28].active = YES;
    NSStackView* sliderRow = MakeStack(NSUserInterfaceLayoutOrientationHorizontal, 10, @[
        MakeLabel(@"Level", [NSFont systemFontOfSize:13], NSColor.labelColor), _ambientSlider, _ambientValue,
    ]);
    _focusOnVoice = [NSButton checkboxWithTitle:@"Focus on voice" target:self action:@selector(focusOnVoiceChanged:)];
    _ambientOptions = MakeStack(NSUserInterfaceLayoutOrientationVertical, 8, @[sliderRow, _focusOnVoice]);
    [sliderRow.widthAnchor constraintEqualToAnchor:_ambientOptions.widthAnchor].active = YES;

    NSStackView* body = MakeStack(NSUserInterfaceLayoutOrientationVertical, 12, @[_noiseMode, _ncOptions, _ambientOptions]);
    [_noiseMode.widthAnchor constraintEqualToAnchor:body.widthAnchor].active = YES;
    [_ambientOptions.widthAnchor constraintEqualToAnchor:body.widthAnchor].active = YES;
    return [self cardWithTitle:@"Noise Control" symbol:@"waveform" body:body];
}

- (NSView*)buildSoundCard {
    _vptPopup = MakePopup(@[@"Off", @"Outdoor Festival", @"Arena", @"Concert Hall", @"Club"], self, @selector(vptChanged:));
    _positionPopup = MakePopup(@[@"Off", @"Front Left", @"Front Right", @"Front", @"Rear Left", @"Rear Right"], self, @selector(positionChanged:));
    NSGridView* grid = MakeFormGrid(@[
        @[MakeLabel(@"Surround:", [NSFont systemFontOfSize:13], NSColor.labelColor), _vptPopup],
        @[MakeLabel(@"Sound position:", [NSFont systemFontOfSize:13], NSColor.labelColor), _positionPopup],
    ]);
    NSTextField* hint = MakeLabel(@"Only one of these effects can be active at a time.", [NSFont systemFontOfSize:11], NSColor.secondaryLabelColor);
    NSStackView* body = MakeStack(NSUserInterfaceLayoutOrientationVertical, 10, @[grid, hint]);
    return [self cardWithTitle:@"Sound" symbol:@"hifispeaker" body:body];
}

- (NSView*)buildSpeakToChatCard {
    _s2cSwitch = [NSSwitch new];
    _s2cSwitch.target = self;
    _s2cSwitch.action = @selector(s2cChanged:);
    _s2cSwitch.translatesAutoresizingMaskIntoConstraints = NO;
    NSTextField* description = MakeLabel(@"Pause music and let in ambient sound when you start talking.",
                                         [NSFont systemFontOfSize:12], NSColor.secondaryLabelColor);
    description.maximumNumberOfLines = 2;
    description.lineBreakMode = NSLineBreakByWordWrapping;
    [description setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    NSStackView* toggleRow = MakeStack(NSUserInterfaceLayoutOrientationHorizontal, 12, @[description, MakeSpacer(), _s2cSwitch]);

    _s2cSensitivity = MakePopup(@[@"Auto", @"High", @"Low"], self, @selector(s2cChanged:));
    _s2cAutoClose = MakePopup(@[@"Short", @"Standard", @"Long", @"Never"], self, @selector(s2cChanged:));
    _s2cVoice = [NSButton checkboxWithTitle:@"Voice passthrough" target:self action:@selector(s2cChanged:)];
    NSGridView* grid = MakeFormGrid(@[
        @[MakeLabel(@"Sensitivity:", [NSFont systemFontOfSize:13], NSColor.labelColor), _s2cSensitivity],
        @[MakeLabel(@"End after:", [NSFont systemFontOfSize:13], NSColor.labelColor), _s2cAutoClose],
        @[[NSGridCell emptyContentView], _s2cVoice],
    ]);

    NSStackView* body = MakeStack(NSUserInterfaceLayoutOrientationVertical, 12, @[toggleRow, grid]);
    [toggleRow.widthAnchor constraintEqualToAnchor:body.widthAnchor].active = YES;
    return [self cardWithTitle:@"Speak-to-Chat" symbol:@"bubble.left.and.bubble.right" body:body];
}

- (NSView*)buildEqCard {
    _eqPresetPopup = MakePopup(@[], self, @selector(eqPresetChanged:));
    _eqFlatButton = [NSButton buttonWithTitle:@"Flat" target:self action:@selector(eqFlatClicked:)];
    _eqFlatButton.controlSize = NSControlSizeSmall;
    _eqFlatButton.toolTip = @"Set every band to 0";
    NSStackView* presetRow = MakeStack(NSUserInterfaceLayoutOrientationHorizontal, 10, @[
        MakeLabel(@"Preset:", [NSFont systemFontOfSize:13], NSColor.labelColor), _eqPresetPopup, MakeSpacer(), _eqFlatButton,
    ]);

    _eqClearBassRow = MakeStack(NSUserInterfaceLayoutOrientationHorizontal, 10, @[]);
    _eqBandsRow = MakeStack(NSUserInterfaceLayoutOrientationHorizontal, 0, @[]);
    _eqBandsRow.distribution = NSStackViewDistributionFillEqually;
    _eqHint = MakeLabel(@"", [NSFont systemFontOfSize:11], NSColor.secondaryLabelColor);
    _eqHint.hidden = YES;
    _eqSliders = [NSMutableArray array];
    _eqValueLabels = [NSMutableArray array];

    _eqBody = MakeStack(NSUserInterfaceLayoutOrientationVertical, 14, @[presetRow, _eqClearBassRow, _eqBandsRow, _eqHint]);
    for (NSView* v in @[presetRow, _eqClearBassRow, _eqBandsRow])
        [v.widthAnchor constraintEqualToAnchor:_eqBody.widthAnchor].active = YES;
    return [self cardWithTitle:@"Equalizer" symbol:@"slider.vertical.3" body:_eqBody];
}

// Rebuild the sliders for the device's band layout.
- (void)rebuildEqBands:(const EqCapability&)eq {
    for (NSView* v in [_eqClearBassRow.arrangedSubviews copy]) [v removeFromSuperview];
    for (NSView* v in [_eqBandsRow.arrangedSubviews copy]) [v removeFromSuperview];
    [_eqSliders removeAllObjects];
    [_eqValueLabels removeAllObjects];

    int half = (eq.levelSteps - 1) / 2;
    for (int i = 0; i < eq.bandCount; i++) {
        NSString* label = i < (int)eq.bandLabels.size() ? @(eq.bandLabels[i].c_str()) : [NSString stringWithFormat:@"%d", i + 1];
        NSSlider* slider = [NSSlider sliderWithValue:0 minValue:-half maxValue:half target:self action:@selector(eqBandChanged:)];
        slider.numberOfTickMarks = 2 * half + 1;
        slider.allowsTickMarkValuesOnly = YES;
        slider.continuous = YES;
        slider.tag = i;
        slider.translatesAutoresizingMaskIntoConstraints = NO;
        NSTextField* value = MakeLabel(@"0", [NSFont monospacedDigitSystemFontOfSize:11 weight:NSFontWeightMedium], NSColor.secondaryLabelColor);
        value.alignment = NSTextAlignmentCenter;
        [_eqSliders addObject:slider];
        [_eqValueLabels addObject:value];

        if (i == eq.clearBassIndex) {
            // Clear Bass gets its own horizontal row, like the official app.
            slider.numberOfTickMarks = 0;
            [value.widthAnchor constraintEqualToConstant:28].active = YES;
            for (NSView* v in @[MakeLabel(@"Clear Bass", [NSFont systemFontOfSize:13], NSColor.labelColor), slider, value])
                [_eqClearBassRow addArrangedSubview:v];
        } else {
            slider.vertical = YES;
            slider.tickMarkPosition = NSTickMarkPositionLeading;
            [slider.heightAnchor constraintEqualToConstant:130].active = YES;
            NSTextField* freq = MakeLabel(label, [NSFont systemFontOfSize:11], NSColor.secondaryLabelColor);
            NSStackView* column = MakeStack(NSUserInterfaceLayoutOrientationVertical, 4, @[value, slider, freq]);
            column.alignment = NSLayoutAttributeCenterX;
            [_eqBandsRow addArrangedSubview:column];
        }
    }
    _eqClearBassRow.hidden = eq.clearBassIndex < 0;
    _eqBandsRow.hidden = !eq.customizable;
    _eqFlatButton.hidden = !eq.customizable;
}

- (void)eqPresetChanged:(id)sender {
    g_headphones->setEqPreset((unsigned char)_eqPresetPopup.selectedItem.tag);
    [self scheduleApply];
}

- (void)sendEqBandsFromSliders {
    EqCapability eq = g_headphones->getEqCapability();
    int half = (eq.levelSteps - 1) / 2;
    std::vector<unsigned char> bands;
    for (NSSlider* slider in _eqSliders)
        bands.push_back((unsigned char)(slider.intValue + half));
    g_headphones->setEqBands(bands);
    [self scheduleApply];
}

- (void)eqBandChanged:(NSSlider*)sender {
    if (sender.tag < (NSInteger)_eqValueLabels.count)
        _eqValueLabels[sender.tag].stringValue = EqValueText(sender.intValue);
    [self sendEqBandsFromSliders];
}

- (void)eqFlatClicked:(id)sender {
    for (NSSlider* slider in _eqSliders)
        slider.intValue = 0;
    [self sendEqBandsFromSliders];
    [self refreshUI];
}

- (void)refreshEq:(const HeadphonesSettings&)s mouseDown:(BOOL)mouseDown {
    EqCapability eq = g_headphones->getEqCapability();

    NSMutableString* signature = [NSMutableString stringWithFormat:@"%d/%d/%d/%d", eq.bandCount, eq.levelSteps, eq.clearBassIndex, eq.customizable];
    for (const auto& label : eq.bandLabels) [signature appendFormat:@"|%s", label.c_str()];
    for (const auto& p : eq.presets) [signature appendFormat:@"#%d", p.id];
    if (![signature isEqualToString:_eqSignature]) {
        _eqSignature = signature;
        [self rebuildEqBands:eq];
        [_eqPresetPopup removeAllItems];
        for (const auto& preset : eq.presets) {
            [_eqPresetPopup addItemWithTitle:EqPresetName(preset)];
            _eqPresetPopup.lastItem.tag = preset.id;
        }
    }

    // A preset the device didn't list (e.g. "Manual" after editing bands) still needs to show.
    if ([_eqPresetPopup indexOfItemWithTag:s.eqPreset] < 0) {
        [_eqPresetPopup addItemWithTitle:EqPresetName(EqPreset{ s.eqPreset, "" })];
        _eqPresetPopup.lastItem.tag = s.eqPreset;
    }
    [_eqPresetPopup selectItemWithTag:s.eqPreset];

    int half = (eq.levelSteps - 1) / 2;
    for (NSUInteger i = 0; i < _eqSliders.count && i < s.eqBands.size(); i++) {
        int value = (int)s.eqBands[i] - half;
        if (!mouseDown)
            _eqSliders[i].intValue = value;
        _eqValueLabels[i].stringValue = EqValueText(value);
    }

    BOOL enabled = eq.enabled;
    _eqPresetPopup.enabled = _eqFlatButton.enabled = enabled;
    for (NSSlider* slider in _eqSliders)
        slider.enabled = enabled;
    _eqHint.stringValue = enabled ? @"" : @"The headphones have the equalizer turned off right now.";
    _eqHint.hidden = enabled;
}

#pragma mark - Device settings

static NSTextField* GridLabel(NSString* text) {
    NSTextField* label = MakeLabel(text, [NSFont systemFontOfSize:13], NSColor.labelColor);
    // The control column gives way first; labels stay readable.
    [label setContentCompressionResistancePriority:NSLayoutPriorityDefaultHigh forOrientation:NSLayoutConstraintOrientationHorizontal];
    return label;
}

- (NSButton*)settingCheckbox:(NSString*)title setting:(SETTING)setting {
    NSButton* box = [NSButton checkboxWithTitle:title target:self action:@selector(settingControlChanged:)];
    box.tag = (NSInteger)setting;
    [_settingControls addObject:box];
    return box;
}

- (NSPopUpButton*)settingPopup:(NSArray<NSString*>*)titles values:(NSArray<NSNumber*>*)values setting:(NSInteger)tag {
    NSPopUpButton* popup = MakePopup(@[], self, @selector(settingControlChanged:));
    for (NSUInteger i = 0; i < titles.count; i++) {
        [popup addItemWithTitle:titles[i]];
        popup.lastItem.tag = values[i].integerValue;
    }
    popup.tag = tag;
    [_settingControls addObject:popup];
    return popup;
}

static NSView* WithHint(NSView* control, NSString* hint, NSTextField** hintOut) {
    NSTextField* label = MakeLabel(hint ?: @"", [NSFont systemFontOfSize:11], NSColor.secondaryLabelColor);
    label.maximumNumberOfLines = 3;
    label.lineBreakMode = NSLineBreakByWordWrapping;
    label.preferredMaxLayoutWidth = 250;
    label.hidden = hint.length == 0;
    if (hintOut) *hintOut = label;
    return MakeStack(NSUserInterfaceLayoutOrientationVertical, 3, @[control, label]);
}

static void ResetGrid(NSGridView* grid) {
    while (grid.numberOfRows > 0)
        [grid removeRowAtIndex:0];
}

// Rebuild both settings cards for the settings the device reported.
- (void)rebuildSettingsCards:(const DeviceSettingsCapability&)cap {
    ResetGrid(_connectionGrid);
    ResetGrid(_controlsGrid);
    [_settingControls removeAllObjects];
    [_assignableHints removeAllObjects];

    // Sound & Connection
    if (cap.connectionMode && g_headphones->getSetting(SETTING::CONNECTION_MODE)) {
        NSPopUpButton* p = [self settingPopup:@[@"Priority on sound quality", @"Priority on stable connection"] values:@[@0, @1] setting:(NSInteger)SETTING::CONNECTION_MODE];
        [_connectionGrid addRowWithViews:@[GridLabel(@"Bluetooth:"), WithHint(p, @"Changing this reconnects the headphones.", nil)]];
    }
    if (cap.upscaling && g_headphones->getSetting(SETTING::UPSCALING)) {
        NSString* title = [UpscalingName(cap.upscalingType) stringByAppendingString:@" (upscale compressed music)"];
        [_connectionGrid addRowWithViews:@[GridLabel(@"Upscaling:"), [self settingCheckbox:title setting:SETTING::UPSCALING]]];
    }
    // Device-described settings (touch panel, multipoint, ...)
    for (int i = 0; i < 3; i++) {
        const GeneralSettingCapability& gs = cap.general[i];
        SETTING setting = (SETTING)((int)SETTING::GENERAL_SETTING_1 + i);
        if (!gs.available || !g_headphones->getSetting(setting))
            continue;
        NSString* title = GeneralSettingText(gs.title);
        NSString* summary = gs.title.description.empty() ? nil
            : GeneralSettingText(GeneralSettingItem{ gs.title.enumName, gs.title.description, "" });
        BOOL isMultipoint = gs.title.enumName && gs.title.name == "MULTIPOINT_SETTING";
        NSGridView* grid = isMultipoint ? _connectionGrid : _controlsGrid;
        if (gs.isList) {
            NSMutableArray* titles = [NSMutableArray array];
            NSMutableArray* values = [NSMutableArray array];
            for (size_t k = 0; k < gs.items.size(); k++) {
                [titles addObject:GeneralSettingText(gs.items[k])];
                [values addObject:@(k)];
            }
            NSPopUpButton* p = [self settingPopup:titles values:values setting:(NSInteger)setting];
            [grid addRowWithViews:@[GridLabel([title stringByAppendingString:@":"]), WithHint(p, summary, nil)]];
        } else {
            NSButton* box = [self settingCheckbox:isMultipoint ? @"Connect to 2 devices at once" : title setting:setting];
            [grid addRowWithViews:@[GridLabel(isMultipoint ? @"Multipoint:" : @""), WithHint(box, summary, nil)]];
        }
    }

    // Controls & Power
    for (size_t k = 0; k < cap.assignableKeys.size(); k++) {
        const AssignableKey& key = cap.assignableKeys[k];
        NSMutableArray* titles = [NSMutableArray array];
        NSMutableArray* values = [NSMutableArray array];
        for (unsigned char preset : key.presets) {
            [titles addObject:AssignablePresetName(preset)];
            [values addObject:@(preset)];
        }
        NSPopUpButton* p = [self settingPopup:titles values:values setting:100 + (NSInteger)k];
        NSTextField* hint = nil;
        NSView* v = WithHint(p, nil, &hint);
        _assignableHints[@(k)] = hint;
        [_controlsGrid addRowWithViews:@[GridLabel([AssignableKeyName(key) stringByAppendingString:@":"]), v]];
    }
    if (cap.pauseWhenRemoved && g_headphones->getSetting(SETTING::PAUSE_WHEN_REMOVED))
        [_controlsGrid addRowWithViews:@[GridLabel(@"Wearing:"), [self settingCheckbox:@"Pause music when taken off" setting:SETTING::PAUSE_WHEN_REMOVED]]];
    if (!cap.autoPowerOffElements.empty() && g_headphones->getSetting(SETTING::AUTO_POWER_OFF)) {
        NSMutableArray* titles = [NSMutableArray array];
        NSMutableArray* values = [NSMutableArray array];
        for (unsigned char e : cap.autoPowerOffElements) {
            [titles addObject:AutoPowerOffName(e)];
            [values addObject:@(e)];
        }
        [_controlsGrid addRowWithViews:@[GridLabel(@"Auto power off:"), [self settingPopup:titles values:values setting:(NSInteger)SETTING::AUTO_POWER_OFF]]];
    }
    if (cap.voiceGuidance && g_headphones->getSetting(SETTING::VOICE_GUIDANCE))
        [_controlsGrid addRowWithViews:@[GridLabel(@"Voice guidance:"), [self settingCheckbox:@"Announce power, battery and modes" setting:SETTING::VOICE_GUIDANCE]]];

    for (NSGridView* grid in @[_connectionGrid, _controlsGrid]) {
        if (grid.numberOfColumns > 0)
            [grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
        for (NSInteger r = 0; r < grid.numberOfRows; r++)
            [grid rowAtIndex:r].yPlacement = NSGridCellPlacementTop;
    }
}

// What the custom button does with each function. Written from the action lists a
// WH-1000XM4 reports in its ASSIGNABLE_SETTINGS capability (docs/PROTOCOL.md).
static NSString* AssignableActionsText(unsigned char preset) {
    switch (preset) {
        case ASSIGNABLE_AMBIENT_SOUND_CONTROL: return @"Press: cycle noise cancelling / ambient sound / off. Hold: NC Optimizer.";
        case ASSIGNABLE_GOOGLE_ASSISTANT: return @"Press: hear notifications. Hold: talk to Google Assistant. Double-press: stop.";
        case ASSIGNABLE_AMAZON_ALEXA: return @"Press: talk to Alexa.";
        case ASSIGNABLE_VOICE_RECOGNITION: return @"Press: start your device's voice assistant.";
    }
    return nil;
}

- (void)refreshSettingsCards {
    DeviceSettingsCapability cap = g_headphones->getSettingsCapability();

    // Layout changes when features or reported values appear.
    NSMutableString* signature = [NSMutableString stringWithFormat:@"%d%d%d%d%d|%zu|%zu", cap.connectionMode, cap.upscaling,
        cap.pauseWhenRemoved, cap.voiceGuidance, (int)cap.upscalingType, cap.autoPowerOffElements.size(), cap.assignableKeys.size()];
    for (int s = 0; s <= (int)SETTING::GENERAL_SETTING_3; s++)
        [signature appendFormat:@"%d", g_headphones->getSetting((SETTING)s).has_value()];
    for (int i = 0; i < 3; i++)
        [signature appendFormat:@"|%d%s%zu", cap.general[i].available, cap.general[i].title.name.c_str(), cap.general[i].items.size()];
    for (const auto& key : cap.assignableKeys)
        for (unsigned char p : key.presets) [signature appendFormat:@",%d", p];
    if (![signature isEqualToString:_settingsSignature]) {
        _settingsSignature = signature;
        [self rebuildSettingsCards:cap];
    }

    std::vector<unsigned char> presets = g_headphones->getAssignablePresets();
    for (NSControl* control in _settingControls) {
        NSInteger tag = control.tag;
        BOOL enabled = YES;
        if (tag >= 100) {
            size_t k = (size_t)(tag - 100);
            if (k < presets.size()) {
                [(NSPopUpButton*)control selectItemWithTag:presets[k]];
                NSString* actions = AssignableActionsText(presets[k]);
                _assignableHints[@(k)].stringValue = actions ?: @"";
                _assignableHints[@(k)].hidden = actions == nil;
            }
            enabled = cap.assignableEnabled;
        } else {
            SETTING setting = (SETTING)tag;
            std::optional<int> value = g_headphones->getSetting(setting);
            if (!value)
                continue;
            if ([control isKindOfClass:NSPopUpButton.class])
                [(NSPopUpButton*)control selectItemWithTag:*value];
            else
                ((NSButton*)control).state = *value ? NSControlStateValueOn : NSControlStateValueOff;
            auto it = cap.enabled.find(setting);
            enabled = it == cap.enabled.end() || it->second;
        }
        control.enabled = enabled;
    }

    _connectionCard.hidden = _connectionGrid.numberOfRows == 0;
    _controlsCard.hidden = NO;
    _powerOffButton.hidden = !g_headphones->supportsFunction(FUNCTION::POWER_OFF);
    if (_controlsGrid.numberOfRows == 0 && _powerOffButton.hidden)
        _controlsCard.hidden = YES;
}

- (void)settingControlChanged:(NSControl*)sender {
    NSInteger tag = sender.tag;
    if (tag >= 100) {
        std::vector<unsigned char> presets = g_headphones->getAssignablePresets();
        size_t k = (size_t)(tag - 100);
        if (k < presets.size()) {
            presets[k] = (unsigned char)((NSPopUpButton*)sender).selectedItem.tag;
            g_headphones->setAssignablePresets(presets);
        }
    } else if ([sender isKindOfClass:NSPopUpButton.class]) {
        g_headphones->setSetting((SETTING)tag, (int)((NSPopUpButton*)sender).selectedItem.tag);
    } else {
        g_headphones->setSetting((SETTING)tag, ((NSButton*)sender).state == NSControlStateValueOn ? 1 : 0);
    }
    [self scheduleApply];
    [self refreshUI];
}

- (void)powerOffClicked:(id)sender {
    NSAlert* alert = [NSAlert new];
    alert.messageText = @"Turn off the headphones?";
    alert.informativeText = @"You'll need to turn them on again with their power button.";
    [alert addButtonWithTitle:@"Power Off"];
    [alert addButtonWithTitle:@"Cancel"];
    [alert beginSheetModalForWindow:self.view.window completionHandler:^(NSModalResponse response) {
        if (response != NSAlertFirstButtonReturn || self->_state != ConnectionStateReady)
            return;
        dispatch_async(self->_ioQueue, ^{
            try {
                g_headphones->powerOff();
            } catch (const std::exception&) {
                // the link usually drops right away; the connection check handles it
            }
        });
    }];
}

// The headphones ask before changes that disconnect them; they wait for this answer.
- (void)showDeviceAlert:(unsigned char)message needsChoice:(bool)needsChoice {
    if (_state == ConnectionStateDisconnected)
        return;
    NSAlert* alert = [NSAlert new];
    alert.messageText = @"Headphones";
    alert.informativeText = AlertText(message);
    [alert addButtonWithTitle:needsChoice ? @"Continue" : @"OK"];
    if (needsChoice)
        [alert addButtonWithTitle:@"Cancel"];
    void (^answer)(NSModalResponse) = ^(NSModalResponse response) {
        bool accept = response == NSAlertFirstButtonReturn;
        dispatch_async(self->_ioQueue, ^{
            try {
                g_headphones->answerAlert(message, accept);
            } catch (const std::exception&) {
            }
        });
        if (!accept) // the device keeps the old value; re-read so the UI matches
            [self refreshUI];
    };
    NSWindow* window = self.view.window;
    if (window.isVisible)
        [alert beginSheetModalForWindow:window completionHandler:answer];
    else
        answer([alert runModal]);
}

- (NSView*)buildOptimizerCard {
    _optimizeButton = [NSButton buttonWithTitle:@"Optimize" target:self action:@selector(optimizeClicked:)];
    _optimizeStatus = MakeLabel(@"Tunes noise cancelling to your head shape and air pressure. Takes about 15 seconds.",
                                [NSFont systemFontOfSize:12], NSColor.secondaryLabelColor);
    _optimizeStatus.maximumNumberOfLines = 2;
    _optimizeStatus.lineBreakMode = NSLineBreakByWordWrapping;
    [_optimizeStatus setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    NSStackView* row = MakeStack(NSUserInterfaceLayoutOrientationHorizontal, 12, @[_optimizeStatus, MakeSpacer(), _optimizeButton]);
    NSStackView* body = MakeStack(NSUserInterfaceLayoutOrientationVertical, 0, @[row]);
    [row.widthAnchor constraintEqualToAnchor:body.widthAnchor].active = YES;
    return [self cardWithTitle:@"NC Optimizer" symbol:@"dial.medium" body:body];
}

- (NSView*)buildDevicesCard {
    _devicesList = MakeStack(NSUserInterfaceLayoutOrientationVertical, 8, @[]);
    _devicesHint = MakeLabel(@"", [NSFont systemFontOfSize:11], NSColor.secondaryLabelColor);
    NSStackView* body = MakeStack(NSUserInterfaceLayoutOrientationVertical, 10, @[_devicesList, _devicesHint]);
    [_devicesList.widthAnchor constraintEqualToAnchor:body.widthAnchor].active = YES;
    return [self cardWithTitle:@"Paired Devices" symbol:@"laptopcomputer.and.iphone" body:body];
}

#pragma mark - Status item

- (void)setUpStatusItem {
    _statusItem = [NSStatusBar.systemStatusBar statusItemWithLength:NSVariableStatusItemLength];
    NSImage* image = [NSImage imageWithSystemSymbolName:@"headphones" accessibilityDescription:@"Sony Headphones"];
    [image setTemplate:YES];
    _statusItem.button.image = image;
    NSMenu* menu = [NSMenu new];
    menu.delegate = self;
    menu.autoenablesItems = NO;
    _statusItem.menu = menu;
}

- (void)menuNeedsUpdate:(NSMenu*)menu {
    [menu removeAllItems];
    BOOL ready = _state == ConnectionStateReady;

    NSMenuItem* status = [[NSMenuItem alloc] initWithTitle:[self headerStatusText] action:nil keyEquivalent:@""];
    status.enabled = NO;
    [menu addItem:status];

    if (ready && (g_headphones->getCapabilities() & DEVICE_CAPABILITIES::NC_ASM)) {
        [menu addItem:[NSMenuItem separatorItem]];
        NoiseMode current = [self noiseModeForSettings:g_headphones->getSettings()];
        NSArray* titles = @[@"Noise Cancelling", @"Ambient Sound", @"Off"];
        for (NSInteger mode = 0; mode < 3; mode++) {
            NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:titles[mode] action:@selector(menuNoiseModeSelected:) keyEquivalent:@""];
            item.target = self;
            item.tag = mode;
            item.state = mode == current ? NSControlStateValueOn : NSControlStateValueOff;
            [menu addItem:item];
        }
    }

    PlaybackCapability playback = g_headphones->getPlaybackCapability();
    if (ready && playback.available && playback.transport) {
        [menu addItem:[NSMenuItem separatorItem]];
        bool playing = g_headphones->getPlaybackState().status == PLAYBACK_STATUS::PLAYING;
        NSArray* items = @[
            @[playing ? @"Pause" : @"Play", NSStringFromSelector(@selector(playPauseClicked:))],
            @[@"Next Track", NSStringFromSelector(@selector(nextClicked:))],
            @[@"Previous Track", NSStringFromSelector(@selector(previousClicked:))],
        ];
        for (NSArray* entry in items) {
            NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:entry[0] action:NSSelectorFromString(entry[1]) keyEquivalent:@""];
            item.target = self;
            [menu addItem:item];
        }
    }

    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* show = [[NSMenuItem alloc] initWithTitle:@"Show Window" action:@selector(showWindow:) keyEquivalent:@""];
    show.target = self;
    [menu addItem:show];
    NSMenuItem* settings = [[NSMenuItem alloc] initWithTitle:@"Settings…" action:@selector(showSettings:) keyEquivalent:@""];
    settings.target = self;
    [menu addItem:settings];
    NSMenuItem* quit = [[NSMenuItem alloc] initWithTitle:@"Quit" action:@selector(terminate:) keyEquivalent:@"q"];
    quit.target = NSApp;
    [menu addItem:quit];
}

- (void)menuNoiseModeSelected:(NSMenuItem*)sender {
    [self applyNoiseMode:(NoiseMode)sender.tag windNoise:_windNoise.state == NSControlStateValueOn];
    [self refreshUI];
}

- (void)showWindow:(id)sender {
    [(AppDelegate*)NSApp.delegate showMainWindow];
}

- (void)showSettings:(id)sender {
    [(AppDelegate*)NSApp.delegate showSettings:sender];
}

#pragma mark - Connection

- (void)autoConnectToLastDevice {
    if (![[NSUserDefaults standardUserDefaults] boolForKey:kAutoConnectKey])
        return;
    NSString* address = [[NSUserDefaults standardUserDefaults] stringForKey:kLastDeviceKey];
    if (address.length == 0)
        return;
    try {
        for (const auto& device : g_bt->getConnectedDevices()) {
            NSString* mac = @(device.mac.c_str());
            if ([mac caseInsensitiveCompare:address] == NSOrderedSame) {
                [self connectToAddress:mac name:@(device.name.c_str())];
                return;
            }
        }
    } catch (const std::exception&) {
        // Bluetooth unavailable; the user can connect manually.
    }
}

- (void)connectClicked:(NSButton*)sender {
    if (_state != ConnectionStateDisconnected) {
        [self disconnectWithMessage:nil];
        return;
    }

    NSMenu* menu = [NSMenu new];
    menu.autoenablesItems = NO;
    std::vector<BluetoothDevice> devices;
    try {
        devices = g_bt->getConnectedDevices();
    } catch (const std::exception& e) {
        [self showMessage:@(e.what()) isError:YES];
    }

    NSMenuItem* header = [[NSMenuItem alloc] initWithTitle:devices.empty() ? @"No connected Bluetooth devices" : @"Connected Bluetooth devices"
                                                    action:nil keyEquivalent:@""];
    header.enabled = NO;
    [menu addItem:header];
    for (const auto& device : devices) {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:@(device.name.c_str()) action:@selector(deviceMenuSelected:) keyEquivalent:@""];
        item.target = self;
        item.representedObject = @(device.mac.c_str());
        item.image = [NSImage imageWithSystemSymbolName:@"headphones" accessibilityDescription:nil];
        [menu addItem:item];
    }
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* other = [[NSMenuItem alloc] initWithTitle:@"Choose Another Device…" action:@selector(chooseOtherDevice:) keyEquivalent:@""];
    other.target = self;
    [menu addItem:other];

    [menu popUpMenuPositioningItem:nil atLocation:NSMakePoint(0, NSHeight(sender.bounds) + 4) inView:sender];
}

- (void)deviceMenuSelected:(NSMenuItem*)item {
    [self connectToAddress:item.representedObject name:item.title];
}

- (void)chooseOtherDevice:(id)sender {
    IOBluetoothDeviceSelectorController* selector = [IOBluetoothDeviceSelectorController deviceSelector];
    if ([selector runModal] != kIOBluetoothUISuccess)
        return;
    IOBluetoothDevice* device = [[selector getResults] lastObject];
    if (device)
        [self connectToAddress:device.addressString name:device.nameOrAddress];
}

- (void)connectToAddress:(NSString*)address name:(NSString*)name {
    if (_state != ConnectionStateDisconnected || address.length == 0)
        return;

    NSUInteger generation = ++_generation;
    _state = ConnectionStateConnecting;
    _deviceName = name;
    _devicesSignature = nil;
    [self showMessage:nil isError:NO];
    [self refreshUI];

    std::string mac = address.UTF8String;
    __weak ViewController* weakSelf = self;
    dispatch_async(_ioQueue, ^{
        NSString* error = nil;
        try {
            g_headphones->resetState();
            g_bt->connect(mac);
            // The RFCOMM channel opens asynchronously.
            for (int i = 0; i < 50 && !g_bt->isConnected(); i++)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!g_bt->isConnected())
                throw RecoverableException("The headphones didn't accept the connection in time.", true);

            g_listenerRunning = true;
            g_listener = std::make_unique<Listener>(*g_headphones, *g_bt, g_listenerRunning);
            g_listenerThread = std::thread(&Listener::listen, g_listener.get());
        } catch (const std::exception& e) {
            error = @(e.what());
            StopConnection();
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            ViewController* strongSelf = weakSelf;
            if (!strongSelf || strongSelf->_generation != generation)
                return;
            if (error) {
                strongSelf->_state = ConnectionStateDisconnected;
                [strongSelf showMessage:[@"Couldn't connect: " stringByAppendingString:error] isError:YES];
                [strongSelf refreshUI];
                return;
            }
            [[NSUserDefaults standardUserDefaults] setObject:address forKey:kLastDeviceKey];
            [[NSUserDefaults standardUserDefaults] setObject:name forKey:kLastDeviceNameKey];
            [strongSelf syncStateForGeneration:generation];
        });
    });
}

// Read every supported setting from the headphones. Controls stay disabled until this finishes,
// so nothing is sent to the device based on placeholder values.
- (void)syncStateForGeneration:(NSUInteger)generation {
    _state = ConnectionStateSyncing;
    [self refreshUI];

    __weak ViewController* weakSelf = self;
    dispatch_async(_ioQueue, ^{
        bool complete = false;
        NSString* error = nil;
        try {
            complete = g_headphones->syncState(kSyncTimeout);
        } catch (const std::exception& e) {
            error = @(e.what());
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            ViewController* strongSelf = weakSelf;
            if (!strongSelf || strongSelf->_generation != generation || strongSelf->_state == ConnectionStateDisconnected)
                return;
            if (error) {
                [strongSelf disconnectWithMessage:[@"Lost connection while reading settings: " stringByAppendingString:error]];
                return;
            }
            strongSelf->_state = ConnectionStateReady;
            if (g_headphones->getCapabilities() == 0)
                [strongSelf showMessage:@"The headphones didn't report which features they support. Showing the standard controls; some may not work." isError:YES];
            else if (!complete)
                [strongSelf showMessage:@"Some settings couldn't be read from the headphones, so they may not match. Use the reload button to try again." isError:YES];
            [strongSelf refreshUI];
        });
    });
}

- (void)reloadClicked:(id)sender {
    if (_state != ConnectionStateReady)
        return;
    [self showMessage:nil isError:NO];
    [self syncStateForGeneration:_generation];
}

- (void)disconnectWithMessage:(NSString*)message {
    if (_state == ConnectionStateDisconnected)
        return;
    _generation++;
    _state = ConnectionStateDisconnected;
    _applying = NO;
    _devicesSignature = nil;
    [self showMessage:message isError:YES];
    [self refreshUI];
    dispatch_async(_ioQueue, ^{
        StopConnection();
        g_headphones->resetState();
    });
}

- (void)checkConnection:(NSTimer*)timer {
    if ((_state == ConnectionStateSyncing || _state == ConnectionStateReady) && !g_bt->isConnected())
        [self disconnectWithMessage:@"The connection to the headphones was lost."];
}

#pragma mark - Sending changes

// Send pending changes on the I/O queue. Changes made while a send is in progress are
// picked up by the loop (or by the follow-up call), so rapid slider moves coalesce.
- (void)scheduleApply {
    if (_state != ConnectionStateReady || _applying || !g_headphones->isChanged())
        return;
    _applying = YES;
    NSUInteger generation = _generation;
    [self refreshHeader];

    __weak ViewController* weakSelf = self;
    dispatch_async(_ioQueue, ^{
        NSString* error = nil;
        bool lostConnection = false;
        try {
            for (int i = 0; i < 20 && g_headphones->isChanged(); i++)
                g_headphones->setChanges();
        } catch (const RecoverableException& e) {
            error = @(e.what());
            lostConnection = e.shouldDisconnect;
        } catch (const std::exception& e) {
            error = @(e.what());
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            ViewController* strongSelf = weakSelf;
            if (!strongSelf || strongSelf->_generation != generation)
                return;
            strongSelf->_applying = NO;
            if (lostConnection) {
                [strongSelf disconnectWithMessage:[@"Disconnected: " stringByAppendingString:error]];
                return;
            }
            if (error)
                [strongSelf showMessage:[@"Couldn't apply the change: " stringByAppendingString:error] isError:YES];
            [strongSelf refreshUI];
            [strongSelf scheduleApply];
        });
    });
}

#pragma mark - Control actions

- (void)applyNoiseMode:(NoiseMode)mode windNoise:(BOOL)windNoise {
    switch (mode) {
        case NoiseModeCancelling:
            g_headphones->setAmbientSoundControl(true);
            g_headphones->setAsmLevel(windNoise ? 1 : 0);
            break;
        case NoiseModeAmbient:
            g_headphones->setAmbientSoundControl(true);
            g_headphones->setAsmLevel(_lastAmbientLevel);
            if (_lastAmbientLevel <= MINIMUM_VOICE_FOCUS_STEP)
                g_headphones->setFocusOnVoice(false);
            break;
        case NoiseModeOff:
            g_headphones->setAmbientSoundControl(false);
            break;
    }
    [self scheduleApply];
}

- (void)noiseModeChanged:(id)sender {
    [self applyNoiseMode:(NoiseMode)_noiseMode.selectedSegment windNoise:_windNoise.state == NSControlStateValueOn];
    [self refreshUI];
}

- (void)ambientLevelChanged:(id)sender {
    _lastAmbientLevel = _ambientSlider.intValue;
    g_headphones->setAmbientSoundControl(true);
    g_headphones->setAsmLevel(_lastAmbientLevel);
    if (_lastAmbientLevel <= MINIMUM_VOICE_FOCUS_STEP)
        g_headphones->setFocusOnVoice(false);
    [self scheduleApply];
    [self refreshUI];
}

- (void)focusOnVoiceChanged:(id)sender {
    g_headphones->setFocusOnVoice(_focusOnVoice.state == NSControlStateValueOn);
    [self scheduleApply];
}

- (void)vptChanged:(id)sender {
    g_headphones->setVptType((int)_vptPopup.indexOfSelectedItem);
    if (_vptPopup.indexOfSelectedItem != 0)
        g_headphones->setSurroundPosition(SOUND_POSITION_PRESET::OFF);
    [self scheduleApply];
    [self refreshUI];
}

- (void)positionChanged:(id)sender {
    NSInteger index = _positionPopup.indexOfSelectedItem;
    g_headphones->setSurroundPosition(SOUND_POSITION_PRESET_ARRAY[index]);
    if (index != 0)
        g_headphones->setVptType(0);
    [self scheduleApply];
    [self refreshUI];
}

- (void)s2cChanged:(id)sender {
    g_headphones->setS2CToggle(_s2cSwitch.state == NSControlStateValueOn ? S2C_TOGGLE::ACTIVE : S2C_TOGGLE::INACTIVE);
    g_headphones->setS2COptions((int)_s2cSensitivity.indexOfSelectedItem,
                                _s2cVoice.state == NSControlStateValueOn,
                                (int)_s2cAutoClose.indexOfSelectedItem);
    [self scheduleApply];
    [self refreshUI];
}

- (void)optimizeClicked:(id)sender {
    bool optimizing = g_headphones->getSettings().optimizerState == OPTIMIZER_STATE::OPTIMIZING;
    g_headphones->setOptimizerState(optimizing ? OPTIMIZER_STATE::IDLE : OPTIMIZER_STATE::OPTIMIZING);
    [self scheduleApply];
    [self refreshUI];
}

- (void)deviceButtonClicked:(NSButton*)sender {
    int index = (int)sender.tag;
    auto slots = g_headphones->getConnectedDevices();
    if (index == slots.first)
        g_headphones->setMultiPointConnection(1, 0);
    else if (index == slots.second)
        g_headphones->setMultiPointConnection(2, 0);
    else if (slots.first == 0)
        g_headphones->setMultiPointConnection(1, index);
    else if (slots.second == 0)
        g_headphones->setMultiPointConnection(2, index);
    [self scheduleApply];
}

#pragma mark - Refreshing the UI from state

- (void)deviceStateChanged {
    if (_state == ConnectionStateSyncing || _state == ConnectionStateReady)
        [self refreshUI];
}

- (void)showMessage:(NSString*)message isError:(BOOL)isError {
    _messageLabel.stringValue = message ?: @"";
    _messageLabel.textColor = isError ? NSColor.systemRedColor : NSColor.secondaryLabelColor;
    _messageLabel.hidden = message.length == 0;
}

- (NoiseMode)noiseModeForSettings:(const HeadphonesSettings&)s {
    if (!s.ambientSoundControl)
        return NoiseModeOff;
    return s.asmLevel < kAmbientMinLevel ? NoiseModeCancelling : NoiseModeAmbient;
}

- (NSString*)headerStatusText {
    switch (_state) {
        case ConnectionStateDisconnected: return @"Not connected";
        case ConnectionStateConnecting: return @"Connecting…";
        case ConnectionStateSyncing: return @"Reading settings from the headphones…";
        case ConnectionStateReady: return _applying ? @"Connected · applying changes…" : @"Connected · in sync";
    }
    return @"";
}

- (void)refreshHeader {
    BOOL connected = _state != ConnectionStateDisconnected;
    BOOL busy = _state == ConnectionStateConnecting || _state == ConnectionStateSyncing || _applying;

    DeviceStatus device = g_headphones->getDeviceStatus();
    NSString* model = device.modelName.empty() ? nil : @(device.modelName.c_str());
    _deviceNameLabel.stringValue = !connected ? @"No headphones" : (model ?: (_deviceName.length ? _deviceName : @"Headphones"));

    NSMutableArray<NSString*>* details = [NSMutableArray array];
    if (_state == ConnectionStateReady) {
        if (!device.firmwareVersion.empty())
            [details addObject:[@"Firmware " stringByAppendingString:@(device.firmwareVersion.c_str())]];
        if (device.hasCodec && device.codec != AUDIO_CODEC::UNSETTLED)
            [details addObject:CodecName(device.codec)];
        if (device.hasUpscaling)
            [details addObject:[NSString stringWithFormat:@"%@ %@", UpscalingName(device.upscalingType),
                                device.upscalingActive ? @"active" : @"off"]];
    }
    _detailLabel.stringValue = [details componentsJoinedByString:@" · "];
    _detailLabel.hidden = details.count == 0;
    _statusLabel.stringValue = [self headerStatusText];
    _statusLabel.textColor = _state == ConnectionStateReady ? NSColor.systemGreenColor : NSColor.secondaryLabelColor;
    // Hidden (not just stopped) so it doesn't take room from the device name.
    _spinner.hidden = !busy;
    if (busy)
        [_spinner startAnimation:nil];
    else
        [_spinner stopAnimation:nil];

    _connectButton.title = connected ? @"Disconnect" : @"Connect…";
    _connectButton.keyEquivalent = connected ? @"" : @"\r";
    _reloadButton.hidden = _state != ConnectionStateReady;

    // Prefer the level reported over the protocol; fall back to the connector (e.g. BlueZ on Linux).
    int battery = !connected ? -1 : device.batteryLevel >= 0 ? device.batteryLevel : g_bt->getBatteryLevel();
    _batteryIcon.hidden = _batteryLabel.hidden = battery < 0;
    if (battery >= 0) {
        NSString* symbol = device.charging ? @"battery.100.bolt"
            : battery > 87 ? @"battery.100" : battery > 62 ? @"battery.75" : battery > 37 ? @"battery.50" : battery > 12 ? @"battery.25" : @"battery.0";
        _batteryIcon.image = [NSImage imageWithSystemSymbolName:symbol accessibilityDescription:nil];
        _batteryIcon.contentTintColor = device.charging ? NSColor.systemGreenColor
            : battery <= 15 ? NSColor.systemRedColor : NSColor.secondaryLabelColor;
        _batteryLabel.stringValue = [NSString stringWithFormat:@"%d%%", battery];
        _batteryLabel.toolTip = device.charging ? @"Charging" : nil;
    }

    _statusItem.button.appearsDisabled = _state != ConnectionStateReady;
}

- (void)refreshUI {
    [self refreshHeader];

    BOOL ready = _state == ConnectionStateReady;
    unsigned int caps = g_headphones->getCapabilities();
    if (ready && caps == 0)
        caps = DEVICE_CAPABILITIES::NC_ASM | DEVICE_CAPABILITIES::VPT; // device didn't say; offer the basics

    _emptyCard.hidden = _state != ConnectionStateDisconnected;
    _tabBar.hidden = _tabView.hidden = !ready;
    if (!ready)
        _nowPlayingCard.hidden = YES;
    _noiseCard.hidden = !ready || !(caps & DEVICE_CAPABILITIES::NC_ASM);
    _soundCard.hidden = !ready || !(caps & DEVICE_CAPABILITIES::VPT);
    _s2cCard.hidden = !ready || !(caps & DEVICE_CAPABILITIES::SPEAK_TO_CHAT);
    _optimizerCard.hidden = !ready || !(caps & DEVICE_CAPABILITIES::OPTIMIZER);
    _eqCard.hidden = !ready || !g_headphones->getEqCapability().available;
    if (!ready) {
        _connectionCard.hidden = _controlsCard.hidden = YES;
    }
    _devicesCard.hidden = !ready || !(caps & DEVICE_CAPABILITIES::MULTI_POINT);
    if (!ready)
        return;

    HeadphonesSettings s = g_headphones->getSettings();
    BOOL mouseDown = NSEvent.pressedMouseButtons != 0;

    // Noise control
    NoiseMode mode = [self noiseModeForSettings:s];
    if (mode == NoiseModeAmbient)
        _lastAmbientLevel = s.asmLevel;
    _noiseMode.selectedSegment = mode;
    _ncOptions.hidden = mode != NoiseModeCancelling;
    _windNoise.state = (mode == NoiseModeCancelling && s.asmLevel == 1) ? NSControlStateValueOn : NSControlStateValueOff;
    _ambientOptions.hidden = mode != NoiseModeAmbient;
    int steps = g_headphones->getAmbientSteps();
    if (_ambientSlider.maxValue != steps + 1) {
        _ambientSlider.maxValue = steps + 1;
        _ambientSlider.numberOfTickMarks = steps;
    }
    if (!mouseDown) // don't fight the user while they drag
        _ambientSlider.intValue = _lastAmbientLevel;
    _ambientValue.stringValue = [NSString stringWithFormat:@"%d", _lastAmbientLevel - kAmbientMinLevel + 1];
    _ambientValue.toolTip = [NSString stringWithFormat:@"Level %d of %d", _lastAmbientLevel - kAmbientMinLevel + 1, steps];
    BOOL voiceAvailable = _lastAmbientLevel > MINIMUM_VOICE_FOCUS_STEP;
    _focusOnVoice.enabled = voiceAvailable;
    _focusOnVoice.state = (voiceAvailable && s.focusOnVoice) ? NSControlStateValueOn : NSControlStateValueOff;
    _focusOnVoice.toolTip = voiceAvailable ? nil : @"Raise the ambient level to use Focus on voice";

    // Sound
    [_vptPopup selectItemAtIndex:std::clamp(s.vptType, 0, (int)_vptPopup.numberOfItems - 1)];
    NSInteger positionIndex = 0;
    for (NSInteger i = 0; i < _positionPopup.numberOfItems; i++)
        if (SOUND_POSITION_PRESET_ARRAY[i] == s.surroundPosition)
            positionIndex = i;
    [_positionPopup selectItemAtIndex:positionIndex];

    // Speak-to-Chat
    BOOL s2cOn = s.speakToChat == S2C_TOGGLE::ACTIVE;
    _s2cSwitch.state = s2cOn ? NSControlStateValueOn : NSControlStateValueOff;
    [_s2cSensitivity selectItemAtIndex:std::clamp(s.s2cSensitivity, 0, (int)_s2cSensitivity.numberOfItems - 1)];
    [_s2cAutoClose selectItemAtIndex:std::clamp(s.s2cOffTime, 0, (int)_s2cAutoClose.numberOfItems - 1)];
    _s2cVoice.state = s.s2cVoice ? NSControlStateValueOn : NSControlStateValueOff;
    _s2cSensitivity.enabled = _s2cAutoClose.enabled = _s2cVoice.enabled = s2cOn;

    // Optimizer
    BOOL optimizing = s.optimizerState == OPTIMIZER_STATE::OPTIMIZING;
    _optimizeButton.title = optimizing ? @"Stop" : @"Optimize";
    _optimizeStatus.stringValue = optimizing ? @"Optimizing… keep the headphones on and stay still."
                                             : @"Tunes noise cancelling to your head shape and air pressure. Takes about 15 seconds.";

    [self refreshNowPlaying:mouseDown];
    [self refreshDevicesList];
    if (!_eqCard.hidden)
        [self refreshEq:s mouseDown:mouseDown];
    [self refreshSettingsCards];
}

- (void)refreshDevicesList {
    std::vector<BluetoothDevice> devices = g_headphones->getDevices();
    auto slots = g_headphones->getConnectedDevices();

    NSMutableString* signature = [NSMutableString stringWithFormat:@"%d,%d", slots.first, slots.second];
    for (const auto& d : devices)
        [signature appendFormat:@"|%s|%s", d.mac.c_str(), d.name.c_str()];
    if ([signature isEqualToString:_devicesSignature])
        return;
    _devicesSignature = signature;

    for (NSView* row in [_devicesList.arrangedSubviews copy])
        [row removeFromSuperview];

    BOOL slotFree = slots.first == 0 || slots.second == 0;
    // Index 0 is a placeholder meaning "no device".
    for (int i = 1; i < (int)devices.size(); i++) {
        BOOL connected = i == slots.first || i == slots.second;
        NSString* name = devices[i].name.empty() ? @(devices[i].mac.c_str()) : @(devices[i].name.c_str());
        NSTextField* nameLabel = MakeLabel(name, [NSFont systemFontOfSize:13], NSColor.labelColor);
        nameLabel.lineBreakMode = NSLineBreakByTruncatingTail;
        [nameLabel setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
        NSImageView* dot = MakeSymbol(@"circle.fill", 7, connected ? NSColor.systemGreenColor : NSColor.tertiaryLabelColor);
        NSButton* button = [NSButton buttonWithTitle:connected ? @"Disconnect" : @"Connect" target:self action:@selector(deviceButtonClicked:)];
        button.controlSize = NSControlSizeSmall;
        button.tag = i;
        button.enabled = connected || slotFree;
        NSStackView* row = MakeStack(NSUserInterfaceLayoutOrientationHorizontal, 8, @[dot, nameLabel, MakeSpacer(), button]);
        [_devicesList addArrangedSubview:row];
        [row.widthAnchor constraintEqualToAnchor:_devicesList.widthAnchor].active = YES;
    }

    if (devices.size() <= 1)
        _devicesHint.stringValue = @"The headphones haven't reported any paired devices.";
    else if (!slotFree)
        _devicesHint.stringValue = @"Two devices are connected. Disconnect one to connect another.";
    else
        _devicesHint.stringValue = @"Up to two devices can be connected at the same time.";
}

@end
