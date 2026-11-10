//
//  NetworkModes.mm
//  SonyHeadphonesClient
//

#import "NetworkModes.h"
#import <CoreLocation/CoreLocation.h>
#import <CoreWLAN/CoreWLAN.h>

NSString* const kNetworkModesEnabledKey = @"NetworkModesEnabled";
NSString* const kNetworkModesKey = @"NetworkModes";
NSString* const kNetworkModeMode = @"mode";
NSString* const kNetworkModeLevel = @"level";
NSString* const kNetworkModeWind = @"wind";
NSString* const kNetworkModeVoice = @"voice";
NSNotificationName const NetworkModesDidChangeNotification = @"NetworkModesDidChange";

@interface NetworkModes () <CLLocationManagerDelegate>
@end

@implementation NetworkModes {
    CLLocationManager* _location;
    NSString* _currentNetwork;
    WiFiNameAccess _access;
}

+ (instancetype)shared {
    static NetworkModes* shared;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ shared = [NetworkModes new]; });
    return shared;
}

- (instancetype)init {
    if ((self = [super init])) {
        _location = [CLLocationManager new];
        _location.delegate = self;
        _access = [self accessFromStatus];
        _currentNetwork = [self readNetwork];
    }
    return self;
}

- (WiFiNameAccess)access { return _access; }
- (NSString*)currentNetwork { return _currentNetwork; }

- (WiFiNameAccess)accessFromStatus {
    if (!CLLocationManager.locationServicesEnabled)
        return WiFiNameAccessDenied;
    switch (_location.authorizationStatus) {
        case kCLAuthorizationStatusNotDetermined:
            return WiFiNameAccessNotAsked;
        case kCLAuthorizationStatusRestricted:
        case kCLAuthorizationStatusDenied:
            return WiFiNameAccessDenied;
        default:
            return WiFiNameAccessAllowed;
    }
}

- (NSString*)readNetwork {
    if (_access != WiFiNameAccessAllowed)
        return nil;
    NSString* name = CWWiFiClient.sharedWiFiClient.interface.ssid;
    return name.length > 0 ? name : nil;
}

- (void)requestAccess {
    [_location requestWhenInUseAuthorization];
    // macOS sometimes shows the prompt only once a location is actually requested.
    [_location requestLocation];
}

- (void)openLocationSettings {
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_LocationServices"]];
}

- (void)poll {
    WiFiNameAccess access = [self accessFromStatus];
    BOOL changed = access != _access;
    _access = access;
    NSString* network = [self readNetwork];
    if (!(network == _currentNetwork || [network isEqualToString:_currentNetwork])) {
        _currentNetwork = network;
        changed = YES;
    }
    if (changed)
        [self postChange];
}

- (void)postChange {
    [[NSNotificationCenter defaultCenter] postNotificationName:NetworkModesDidChangeNotification object:self];
}

#pragma mark - Saved modes

- (NSDictionary*)allModes {
    NSDictionary* modes = [[NSUserDefaults standardUserDefaults] dictionaryForKey:kNetworkModesKey];
    return modes ?: @{};
}

- (NSDictionary<NSString*, NSNumber*>*)modeForNetwork:(NSString*)network {
    id mode = [self allModes][network];
    return [mode isKindOfClass:[NSDictionary class]] ? mode : nil;
}

- (void)setMode:(NSDictionary<NSString*, NSNumber*>*)mode forNetwork:(NSString*)network {
    NSMutableDictionary* modes = [[self allModes] mutableCopy];
    modes[network] = mode;
    [[NSUserDefaults standardUserDefaults] setObject:modes forKey:kNetworkModesKey];
    [self postChange];
}

- (NSArray<NSString*>*)savedNetworks {
    return [[self allModes].allKeys sortedArrayUsingSelector:@selector(localizedCaseInsensitiveCompare:)];
}

+ (NSString*)describeMode:(NSDictionary<NSString*, NSNumber*>*)mode {
    switch (mode[kNetworkModeMode].integerValue) {
        case 0:
            return mode[kNetworkModeWind].boolValue ? @"Noise Cancelling, wind reduction" : @"Noise Cancelling";
        case 1: {
            NSString* text = [NSString stringWithFormat:@"Ambient Sound %ld", (long)mode[kNetworkModeLevel].integerValue];
            return mode[kNetworkModeVoice].boolValue ? [text stringByAppendingString:@", focus on voice"] : text;
        }
        default:
            return @"Off";
    }
}

#pragma mark - CLLocationManagerDelegate

- (void)locationManagerDidChangeAuthorization:(CLLocationManager*)manager {
    dispatch_async(dispatch_get_main_queue(), ^{ [self poll]; });
}

// requestLocation needs these; the location itself isn't used.
- (void)locationManager:(CLLocationManager*)manager didUpdateLocations:(NSArray<CLLocation*>*)locations {
}

- (void)locationManager:(CLLocationManager*)manager didFailWithError:(NSError*)error {
}

@end
