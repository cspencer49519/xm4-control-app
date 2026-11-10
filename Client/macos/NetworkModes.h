//
//  NetworkModes.h
//  SonyHeadphonesClient
//
//  Noise control by Wi-Fi network: a Mac take on the "places" part of Sony's
//  Adaptive Sound Control, which runs in the phone app (the headphones have no
//  such setting). A Mac doesn't move around much, so the Wi-Fi network stands in
//  for the place. Reading the network name needs Location access (macOS 14+).
//

#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

extern NSString* const kNetworkModesEnabledKey; // BOOL: switch noise control by Wi-Fi network
extern NSString* const kNetworkModesKey;        // NSDictionary: network name -> saved mode (below)

// Keys of a saved mode.
extern NSString* const kNetworkModeMode;  // NSNumber: 0 noise cancelling, 1 ambient, 2 off
extern NSString* const kNetworkModeLevel; // NSNumber: ambient level 1..N (ambient only)
extern NSString* const kNetworkModeWind;  // NSNumber BOOL: wind noise reduction (noise cancelling only)
extern NSString* const kNetworkModeVoice; // NSNumber BOOL: focus on voice (ambient only)

// Posted on the main thread when the network, Location access or the saved modes change.
extern NSNotificationName const NetworkModesDidChangeNotification;

typedef NS_ENUM(NSInteger, WiFiNameAccess) {
    WiFiNameAccessNotAsked,
    WiFiNameAccessDenied,
    WiFiNameAccessAllowed,
};

@interface NetworkModes : NSObject
+ (instancetype)shared;

@property (readonly) WiFiNameAccess access;
// The current Wi-Fi network name; nil when not on Wi-Fi or the name can't be read.
@property (readonly, nullable) NSString* currentNetwork;

// Asks for Location access (shows the system prompt the first time).
- (void)requestAccess;
// Opens System Settings at Privacy & Security > Location Services.
- (void)openLocationSettings;
// Re-reads the network name; posts the notification when it changed. Cheap enough
// to call every few seconds.
- (void)poll;

- (nullable NSDictionary<NSString*, NSNumber*>*)modeForNetwork:(NSString*)network;
- (void)setMode:(nullable NSDictionary<NSString*, NSNumber*>*)mode forNetwork:(NSString*)network;
- (NSArray<NSString*>*)savedNetworks;

// "Noise Cancelling", "Ambient Sound 12, focus on voice", "Off"...
+ (NSString*)describeMode:(NSDictionary<NSString*, NSNumber*>*)mode;
@end

NS_ASSUME_NONNULL_END
