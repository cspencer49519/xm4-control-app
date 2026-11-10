//
//  AudioDevices.mm
//  SonyHeadphonesClient
//

#import "AudioDevices.h"
#import <CoreAudio/CoreAudio.h>

#include <vector>

NSString* const kPreferredMicKey = @"PreferredMicrophone";
NSString* const kPreferredMicNameKey = @"PreferredMicrophoneName";
NSString* const kAmbientDuringCallsKey = @"AmbientDuringCalls";
NSNotificationName const AudioInputsDidChangeNotification = @"AudioInputsDidChange";
NSNotificationName const MicrophoneInUseDidChangeNotification = @"MicrophoneInUseDidChange";

static AudioObjectPropertyAddress Address(AudioObjectPropertySelector selector,
                                          AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal) {
    return { selector, scope, kAudioObjectPropertyElementMain };
}

static NSString* StringProperty(AudioObjectID device, AudioObjectPropertySelector selector) {
    AudioObjectPropertyAddress address = Address(selector);
    CFStringRef value = nullptr;
    UInt32 size = sizeof(value);
    if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &value) != noErr || !value)
        return @"";
    return (__bridge_transfer NSString*)value;
}

static UInt32 UInt32Property(AudioObjectID device, AudioObjectPropertySelector selector) {
    AudioObjectPropertyAddress address = Address(selector);
    UInt32 value = 0;
    UInt32 size = sizeof(value);
    AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &value);
    return value;
}

static BOOL HasInput(AudioObjectID device) {
    AudioObjectPropertyAddress address = Address(kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput);
    UInt32 size = 0;
    return AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &size) == noErr && size > 0;
}

static std::vector<AudioObjectID> AllDevices() {
    AudioObjectPropertyAddress address = Address(kAudioHardwarePropertyDevices);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr)
        return {};
    std::vector<AudioObjectID> devices(size / sizeof(AudioObjectID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, devices.data()) != noErr)
        return {};
    devices.resize(size / sizeof(AudioObjectID));
    return devices;
}

static AudioObjectID DefaultInput() {
    return UInt32Property(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultInputDevice);
}

static BOOL IsBluetooth(AudioObjectID device) {
    UInt32 transport = UInt32Property(device, kAudioDevicePropertyTransportType);
    return transport == kAudioDeviceTransportTypeBluetooth || transport == kAudioDeviceTransportTypeBluetoothLE;
}

@implementation AudioInput
- (instancetype)initWithUID:(NSString*)uid name:(NSString*)name bluetooth:(BOOL)bluetooth {
    if ((self = [super init])) {
        _uid = uid;
        _name = name;
        _bluetooth = bluetooth;
    }
    return self;
}
@end

@implementation AudioDevices {
    BOOL _started;
    BOOL _microphoneInUse;
    AudioObjectID _watchedInput;
    AudioObjectPropertyListenerBlock _changeBlock;
    AudioObjectPropertyListenerBlock _runningBlock;
}

+ (instancetype)shared {
    static AudioDevices* shared;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ shared = [AudioDevices new]; });
    return shared;
}

- (BOOL)microphoneInUse { return _microphoneInUse; }

- (NSArray<AudioInput*>*)inputs {
    NSMutableArray* inputs = [NSMutableArray array];
    for (AudioObjectID device : AllDevices())
        if (HasInput(device))
            [inputs addObject:[[AudioInput alloc] initWithUID:StringProperty(device, kAudioDevicePropertyDeviceUID)
                                                         name:StringProperty(device, kAudioObjectPropertyName)
                                                    bluetooth:IsBluetooth(device)]];
    return inputs;
}

- (void)start {
    if (_started)
        return;
    _started = YES;
    __weak AudioDevices* weakSelf = self;
    _changeBlock = ^(UInt32, const AudioObjectPropertyAddress*) { [weakSelf inputsChanged]; };
    _runningBlock = ^(UInt32, const AudioObjectPropertyAddress*) { [weakSelf updateMicrophoneInUse]; };
    AudioObjectPropertyAddress devices = Address(kAudioHardwarePropertyDevices);
    AudioObjectPropertyAddress defaultInput = Address(kAudioHardwarePropertyDefaultInputDevice);
    AudioObjectAddPropertyListenerBlock(kAudioObjectSystemObject, &devices, dispatch_get_main_queue(), _changeBlock);
    AudioObjectAddPropertyListenerBlock(kAudioObjectSystemObject, &defaultInput, dispatch_get_main_queue(), _changeBlock);
    [self inputsChanged];
}

- (void)inputsChanged {
    [self applyPreferredMicrophone];
    [self watchDefaultInput];
    [[NSNotificationCenter defaultCenter] postNotificationName:AudioInputsDidChangeNotification object:self];
}

// macOS makes Bluetooth headphones the input when they connect. Switch back to the
// chosen mic whenever a Bluetooth input takes over and the chosen mic is present.
- (void)applyPreferredMicrophone {
    NSString* preferred = [[NSUserDefaults standardUserDefaults] stringForKey:kPreferredMicKey];
    if (preferred.length == 0)
        return;
    AudioObjectID current = DefaultInput();
    if (!IsBluetooth(current) || [StringProperty(current, kAudioDevicePropertyDeviceUID) isEqualToString:preferred])
        return;
    for (AudioObjectID device : AllDevices()) {
        if (HasInput(device) && [StringProperty(device, kAudioDevicePropertyDeviceUID) isEqualToString:preferred]) {
            AudioObjectPropertyAddress address = Address(kAudioHardwarePropertyDefaultInputDevice);
            OSStatus status = AudioObjectSetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, sizeof(device), &device);
            if (status != noErr)
                NSLog(@"Couldn't switch the input device: %d", (int)status);
            return;
        }
    }
}

// Calls record from the default input; watch whether anyone is using it.
- (void)watchDefaultInput {
    AudioObjectID input = DefaultInput();
    if (input == _watchedInput)
        return;
    AudioObjectPropertyAddress running = Address(kAudioDevicePropertyDeviceIsRunningSomewhere);
    if (_watchedInput != kAudioObjectUnknown)
        AudioObjectRemovePropertyListenerBlock(_watchedInput, &running, dispatch_get_main_queue(), _runningBlock);
    _watchedInput = input;
    if (input != kAudioObjectUnknown)
        AudioObjectAddPropertyListenerBlock(input, &running, dispatch_get_main_queue(), _runningBlock);
    [self updateMicrophoneInUse];
}

- (void)updateMicrophoneInUse {
    BOOL inUse = _watchedInput != kAudioObjectUnknown && UInt32Property(_watchedInput, kAudioDevicePropertyDeviceIsRunningSomewhere) != 0;
    if (inUse == _microphoneInUse)
        return;
    _microphoneInUse = inUse;
    [[NSNotificationCenter defaultCenter] postNotificationName:MicrophoneInUseDidChangeNotification object:self];
}

@end
