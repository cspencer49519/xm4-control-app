//
//  AudioDevices.h
//  SonyHeadphonesClient
//
//  Core Audio helpers for the two call options:
//   * keep another microphone as the Mac's input, so the headphones stay in their
//     high-quality music profile (using their mic switches them to the call profile)
//   * notice when the microphone is in use (a call), so the window can switch the
//     headphones to Ambient Sound and you hear your own voice
//

#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

extern NSString* const kPreferredMicKey;      // NSString: UID of the mic to keep as input ("" = leave it to macOS)
extern NSString* const kPreferredMicNameKey;  // NSString: its name, shown while it is unplugged
extern NSString* const kAmbientDuringCallsKey; // BOOL: Ambient Sound while the microphone is in use

// Posted on the main thread when the input devices or the default input change.
extern NSNotificationName const AudioInputsDidChangeNotification;
// Posted on the main thread when the default input starts or stops being used.
extern NSNotificationName const MicrophoneInUseDidChangeNotification;

@interface AudioInput : NSObject
@property (readonly) NSString* uid;
@property (readonly) NSString* name;
@property (readonly) BOOL bluetooth;
@end

@interface AudioDevices : NSObject
+ (instancetype)shared;

// Input devices, in the order Core Audio lists them.
@property (readonly) NSArray<AudioInput*>* inputs;
// Whether some app is recording from the default input.
@property (readonly) BOOL microphoneInUse;

// Starts listening for device changes; enforces kPreferredMicKey from then on.
- (void)start;
// Applies kPreferredMicKey now (after the user changes it).
- (void)applyPreferredMicrophone;
@end

NS_ASSUME_NONNULL_END
