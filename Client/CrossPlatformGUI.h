#pragma once

#include "imgui/imgui.h"
#include "Constants.h"
#include "IBluetoothConnector.h"
#include "BluetoothWrapper.h"
#include "CommandSerializer.h"
#include "Exceptions.h"
#include "TimedMessageQueue.h"
#include "SingleInstanceFuture.h"
#include "CascadiaCodeFont.h"
#include "Headphones.h"
#include "Listener.h"

#include <iostream>
#include <future>
#include <string>
#include <cmath>
#include <atomic>

//--- UI Constants ---
constexpr auto GUI_MAX_MESSAGES = 5;
constexpr auto FPS = 60;
constexpr auto MS_PER_FRAME = 1000 / FPS;
constexpr auto FONT_SIZE = 15.0f;
constexpr auto BUTTON_WIDTH = 150.0f;
constexpr auto BUTTON_HEIGHT = 35.0f;
constexpr auto SECTION_SPACING = 8.0f;

// Sony-inspired color palette
const auto COLOR_TEXT      = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
const auto COLOR_SUCCESS   = ImVec4(0.20f, 0.90f, 0.30f, 1.00f);
const auto COLOR_ERROR     = ImVec4(1.00f, 0.30f, 0.30f, 1.00f);
const auto COLOR_WARNING   = ImVec4(1.00f, 0.80f, 0.00f, 1.00f);
const auto COLOR_CONNECTED = ImVec4(0.20f, 0.80f, 0.30f, 1.00f);
const auto COLOR_ACCENT    = ImVec4(0.00f, 0.52f, 0.84f, 1.00f); // Sony blue
const auto COLOR_BG_DARK   = ImVec4(0.06f, 0.06f, 0.07f, 1.00f);

//This class should be constructed after AFTER the Dear ImGUI context is initialized.
class CrossPlatformGUI
{
public:
	CrossPlatformGUI(BluetoothWrapper bt);

	//Run the GUI code once. This function should be called from a loop from one of the GUI impls (Windows, OSX, Linux...)
	//O: true if the user wants to close the window
	bool performGUIPass();
	void cleanup();
	void resetUIState();
private:
	void _drawErrors();
	void _drawDeviceDiscovery();
	void _drawASMControls();
	void _drawSurroundControls();
	void _setHeadphoneSettings();
	void _drawOptimizerButton();
	void _drawSpeakToChat();
	void _drawMultiPointConn();
	void _drawBattery();
	void _drawSpinner(const char* label);

	static int _spinnerFrame();

	std::atomic<bool> _listenerActive{ true };

	BluetoothDevice _connectedDevice;
	BluetoothWrapper _bt;
	SingleInstanceFuture<std::vector<BluetoothDevice>> _connectedDevicesFuture;
	SingleInstanceFuture<void> _sendCommandFuture;
	SingleInstanceFuture<void> _connectFuture;
	TimedMessageQueue _mq;
	Headphones _headphones;

	std::unique_ptr<Listener> _listener;
	std::future<void> _listenerFuture;

	bool _ambientSoundControl = true;
	bool _focusOnVoice = false;
	int _asmLevel = 0;

	// Device discovery state
	std::vector<BluetoothDevice> _discoveredDevices;
	int _selectedDevice = -1;
	std::chrono::steady_clock::time_point _connectStart;

	// Surround controls state
	int _soundPosition = 0;
	int _vptType = 0;

	// Speak-to-Chat state
	bool _s2cToggle = false;
	int _s2cSensitivity = 0;
	bool _s2cVoice = false;
	int _s2cAutoOff = 0;

	// Settings command state
	int _commandLinger = 0;
	std::chrono::steady_clock::time_point _sendStart;
};
