//
//  AppDelegate.h
//  SonyHeadphonesClient
//
//  Created by Sem Visscher on 01/12/2020.
//

#import <Cocoa/Cocoa.h>
#import <stdio.h>
#import <IOBluetoothUI/IOBluetoothUI.h>
#import "BluetoothWrapper.h"
#import "Headphones.h"
#import "MacOSBluetoothConnector.h"

// User defaults keys
extern NSString* const kHideDockIconKey;  // BOOL: hide the Dock icon while the window is hidden
extern NSString* const kAutoConnectKey;   // BOOL: connect to the last headphones on launch and when they reconnect
extern NSString* const kLowBatteryNotifyKey; // BOOL: notify at 20 % and 10 % battery

@interface AppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property (weak) NSWindow* window;

// Show the main window (restoring the Dock icon if it was hidden).
- (void)showMainWindow;
// Hide the main window; removes the Dock icon when kHideDockIconKey is set.
- (void)hideMainWindow;
- (IBAction)showSettings:(id)sender;
@end

// Main window class (set in Main.storyboard). With kHideDockIconKey set, minimizing
// hides the window to the menu bar instead of the Dock, since there is no Dock icon
// to restore it from.
@interface MainWindow : NSWindow
@end
