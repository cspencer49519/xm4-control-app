#pragma once

#include "SingleInstanceFuture.h"
#include "BluetoothWrapper.h"
#include "Constants.h"

#include <mutex>
#include <atomic>
#include <vector>
#include <string>
#include <tuple>
#include <chrono>
#include <functional>
#include <condition_variable>
#include <map>
#include <optional>

template <class T>
struct Property {
	T current;
	T desired;

	// Record that `sent` is now the value on the device. If `desired` changed
	// while the command was in flight, the property stays unfulfilled so the
	// newer value gets sent too.
	void markSent(T sent);
	bool isFulfilled();
	void setState(T val);
	// Apply a value reported by the device. A pending user change (desired !=
	// current) is kept so it still gets sent; otherwise desired follows the device.
	void updateFromDevice(T val);
};

// Which pieces of device state have been read from the headphones.
enum STATE_FLAGS : unsigned int
{
	STATE_CAPABILITIES = 1u << 0,
	STATE_NC_ASM = 1u << 1,
	STATE_VPT = 1u << 2,
	STATE_S2C = 1u << 3,
	STATE_S2C_OPTIONS = 1u << 4,
	STATE_DEVICES = 1u << 5,
	STATE_NC_ASM_CAPABILITY = 1u << 6,
	STATE_EQ = 1u << 7,
	STATE_EQ_CAPABILITY = 1u << 8,
};

struct EqPreset
{
	unsigned char id;
	std::string name; // as reported by the device (may be empty)
};

// Equalizer description reported by the device.
struct EqCapability
{
	bool available = false;
	bool customizable = false;   // bands can be edited (PRESET_EQ vs PRESET_EQ_NONCUSTOMIZABLE)
	bool enabled = true;         // EQ status: false while the device has it greyed out
	int bandCount = 0;
	int levelSteps = 0;          // band values are 0..levelSteps-1, flat = (levelSteps-1)/2
	std::vector<EqPreset> presets;
	std::vector<std::string> bandLabels; // e.g. "400", "1k", "Clear Bass"
	int clearBassIndex = -1;     // index into the band array, -1 if none
};

// Snapshot of the settings the UI should display (the desired values, which
// equal the device values unless a user change is still being sent).
struct HeadphonesSettings
{
	bool ambientSoundControl = false;
	bool focusOnVoice = false;
	int asmLevel = 0;
	SOUND_POSITION_PRESET surroundPosition = SOUND_POSITION_PRESET::OFF;
	int vptType = 0;
	OPTIMIZER_STATE optimizerState = OPTIMIZER_STATE::IDLE;
	S2C_TOGGLE speakToChat = S2C_TOGGLE::INACTIVE;
	int s2cSensitivity = 0;
	bool s2cVoice = false;
	int s2cOffTime = 0;
	unsigned char eqPreset = 0;
	std::vector<unsigned char> eqBands; // raw steps, see EqCapability::levelSteps
};

// Read-only information reported by the device.
struct DeviceStatus
{
	std::string modelName;
	std::string firmwareVersion;
	int batteryLevel = -1; // percent, -1 = unknown
	bool charging = false;
	bool hasCodec = false;
	AUDIO_CODEC codec = AUDIO_CODEC::UNSETTLED;
	bool hasUpscaling = false;
	UPSCALING_TYPE upscalingType = UPSCALING_TYPE::DSEE_HX;
	bool upscalingActive = false;
};

// Simple device settings with one integer value each (see HeadphonesDeviceSettings.cpp).
enum class SETTING
{
	PAUSE_WHEN_REMOVED,   // bool (SYSTEM 0x03 control by wearing)
	AUTO_POWER_OFF,       // AutoPowerOffElementId (SYSTEM 0x04)
	CONNECTION_MODE,      // 0 = sound quality, 1 = stable connection (AUDIO 0x01)
	UPSCALING,            // 0 = off, 1 = auto (AUDIO 0x02, DSEE)
	VOICE_GUIDANCE,       // bool (table 2)
	GENERAL_SETTING_1,    // bool or list index, described by GeneralSettingCapability
	GENERAL_SETTING_2,
	GENERAL_SETTING_3,
};

// AutoPowerOffElementId
inline constexpr unsigned char AUTO_POWER_OFF_5_MIN = 0x00;
inline constexpr unsigned char AUTO_POWER_OFF_30_MIN = 0x01;
inline constexpr unsigned char AUTO_POWER_OFF_60_MIN = 0x02;
inline constexpr unsigned char AUTO_POWER_OFF_180_MIN = 0x03;
inline constexpr unsigned char AUTO_POWER_OFF_WHEN_REMOVED = 0x10;
inline constexpr unsigned char AUTO_POWER_OFF_DISABLED = 0x11;

// AssignableSettingsPreset (custom button function)
inline constexpr unsigned char ASSIGNABLE_AMBIENT_SOUND_CONTROL = 0x00;
inline constexpr unsigned char ASSIGNABLE_VOLUME_CONTROL = 0x10;
inline constexpr unsigned char ASSIGNABLE_PLAYBACK_CONTROL = 0x20;
inline constexpr unsigned char ASSIGNABLE_VOICE_RECOGNITION = 0x30;
inline constexpr unsigned char ASSIGNABLE_GOOGLE_ASSISTANT = 0x31;
inline constexpr unsigned char ASSIGNABLE_AMAZON_ALEXA = 0x32;
inline constexpr unsigned char ASSIGNABLE_TENCENT_XIAOWEI = 0x33;
inline constexpr unsigned char ASSIGNABLE_NO_FUNCTION = 0xff;

struct AssignableKey
{
	unsigned char key;        // 0 left, 1 right, 2 custom, 3 C
	unsigned char keyType;    // 0 touch sensor, 1 button
	unsigned char defaultPreset;
	std::vector<unsigned char> presets;
};

struct GeneralSettingItem
{
	bool enumName = false;    // name is a symbolic key (e.g. "TOUCH_PANEL_SETTING") rather than display text
	std::string name;
	std::string description;
};

// A setting the device describes itself (GENERAL_SETTING 0xD1-0xD3).
struct GeneralSettingCapability
{
	bool available = false;
	GeneralSettingItem title;
	bool isList = false;      // false: on/off toggle; true: pick one of `items`
	std::vector<GeneralSettingItem> items;
};

struct DeviceSettingsCapability
{
	bool pauseWhenRemoved = false;
	std::vector<unsigned char> autoPowerOffElements;
	std::vector<AssignableKey> assignableKeys;
	bool connectionMode = false;
	bool upscaling = false;
	UPSCALING_TYPE upscalingType = UPSCALING_TYPE::DSEE_HX;
	bool voiceGuidance = false;
	GeneralSettingCapability general[3];
	std::map<SETTING, bool> enabled;   // false while the device has the setting greyed out
	bool assignableEnabled = true;
};

class Headphones {
public:
	Headphones(BluetoothWrapper& conn);

	void setAmbientSoundControl(bool val);
	bool getAmbientSoundControl();

	bool isFocusOnVoiceAvailable();
	void setFocusOnVoice(bool val);
	bool getFocusOnVoice();

	bool isSetAsmLevelAvailable();
	void setAsmLevel(int val);
	int getAsmLevel();

	void setOptimizerState(OPTIMIZER_STATE val);
	OPTIMIZER_STATE getOptimizerState();

	void setS2CToggle(S2C_TOGGLE val);
	void setS2COptions(int sensitivity, bool voice, int offTime);
	S2C_TOGGLE getS2CToggle();
	unsigned int getS2COptions();

	void setSurroundPosition(SOUND_POSITION_PRESET val);
	SOUND_POSITION_PRESET getSurroundPosition();

	void setVptType(int val);
	int getVptType();

	bool getMultiPointSetting();
	std::vector<BluetoothDevice> getDevices();
	std::pair<int, int> getConnectedDevices();
	void setMultiPointConnection(int connectionId, int newDevice);
	inline void disconnect(int deviceIdx);
	inline void connect(int deviceIdx);

	bool isChanged();
	void setChanges();

	void queryState();
	void setStateFromReply(BtMessage replyMessage);

	void queryDeviceCapabilities();
	void queryDevices();
	void queryNcAsmCapability();
	void queryNcAsm();
	void queryOptimizerStatus();
	void queryEq();
	EqCapability getEqCapability();
	// Select a preset; the device answers with that preset's band levels.
	void setEqPreset(unsigned char presetId);
	// Set all band levels (raw steps); the device switches to the "Manual" preset.
	void setEqBands(const std::vector<unsigned char>& bands);
	void queryDeviceInfo(DEVICE_INFO_TYPE type);

	// --- Device settings (HeadphonesDeviceSettings.cpp) ---
	DeviceSettingsCapability getSettingsCapability();
	// Value as last reported by the device (or the pending change); empty until known.
	std::optional<int> getSetting(SETTING setting);
	void setSetting(SETTING setting, int value);
	std::vector<unsigned char> getAssignablePresets();
	void setAssignablePresets(const std::vector<unsigned char>& presets);
	// Turn the headphones off (sends immediately; call from a background thread).
	void powerOff();
	// Called on the listener thread when the device asks for confirmation, e.g. before a
	// change that will disconnect it. Answer with answerAlert() (from a background thread).
	void setAlertCallback(std::function<void(unsigned char messageType, bool needsChoice)> callback);
	void answerAlert(unsigned char messageType, bool accept);
	void queryBattery();
	void queryCodec();
	void queryUpscalingIndicator();
	DeviceStatus getDeviceStatus();
	void queryVpt();
	void queryS2C();
	void queryS2COptions();

	// Forget everything known about the device (call before connecting).
	void resetState();
	// Blocking: query capabilities, then the state of every supported feature,
	// and wait for the replies. A Listener must be running. Returns true if every
	// supported feature reported its state before `timeout`.
	bool syncState(std::chrono::milliseconds timeout);
	// Bitmask of STATE_FLAGS received so far.
	unsigned int getLoadedState();
	HeadphonesSettings getSettings();
	// Called (on the listener thread) whenever the device reports new state.
	void setStateChangedCallback(std::function<void()> callback);

	unsigned int getCapabilities();
	// Raw FunctionType bytes from the supported-function reply (0x07).
	bool supportsFunction(unsigned char functionType);
	std::vector<unsigned char> getSupportedFunctions();
	// Number of ambient sound levels the device supports (levels 1..N).
	// In the legacy level encoding used by get/setAsmLevel, ambient level L is L + 1,
	// so the valid range there is 2..N+1 (0 = noise cancelling, 1 = wind noise reduction).
	int getAmbientSteps();
private:
	void _markLoaded(unsigned int flags);
	void _queryDeviceSettings();
	void _sendDeviceSettingChanges();
	bool _deviceSettingsChanged();   // caller holds _propertyMtx
	void _resetDeviceSettings();     // caller holds _propertyMtx
	// Returns true if the message was a device-settings message (caller then notifies).
	bool _handleDeviceSettingsReply(const Buffer& bytes);
	void _handleTable2Reply(const Buffer& bytes);
	void _notifyStateChanged();

	std::atomic<unsigned int> _capabilities = 0;
	unsigned int _loadedState = 0;
	std::vector<unsigned char> _supportedFunctions;
	// From NCASM_RET_CAPABILITY; must be echoed in NCASM_SET_PARAM.
	unsigned char _ncAsmSettingType = DEFAULT_NC_ASM_SETTING_TYPE;
	unsigned char _asmSettingType = DEFAULT_ASM_SETTING_TYPE;
	int _ambientSteps = DEFAULT_AMBIENT_STEPS;
	DeviceStatus _deviceStatus;
	EqCapability _eqCapability;
	unsigned char _eqInquiredType = EQ_TYPE_PRESET_EQ;
	Property<unsigned char> _eqPreset = { 0, 0 };
	Property<std::vector<unsigned char>> _eqBands = { {}, {} };

	DeviceSettingsCapability _settingsCapability;
	std::map<SETTING, Property<int>> _settings;      // present once the device reported a value
	unsigned char _autoPowerOffSelectTime = AUTO_POWER_OFF_30_MIN;
	Property<std::vector<unsigned char>> _assignablePresets = { {}, {} };
	std::mutex _alertMtx;
	std::function<void(unsigned char, bool)> _alertCallback;

	Property<bool> _ambientSoundControl = { 0, 0 };
	Property<bool> _focusOnVoice = { 0, 0 };
	Property<int> _asmLevel = { 0, 0 };

	Property<SOUND_POSITION_PRESET> _surroundPosition = { SOUND_POSITION_PRESET::OFF, SOUND_POSITION_PRESET::OFF };
	Property<int> _vptType = { 0, 0 };

	Property<OPTIMIZER_STATE> _optimizerState = { OPTIMIZER_STATE::IDLE, OPTIMIZER_STATE::IDLE };

	Property<S2C_TOGGLE> _speakToChat = { S2C_TOGGLE::INACTIVE, S2C_TOGGLE::INACTIVE };
	Property<unsigned int> _s2cOptions = { 0, 0 };

	std::vector<BluetoothDevice> _savedDevices;
	Property<int> _dev1 = { 0, 0 }, _dev2 = { 0, 0 };
	bool _multiPointSetting = true;

	std::mutex _propertyMtx;
	std::condition_variable _stateCv;

	std::mutex _callbackMtx;
	std::function<void()> _stateChangedCallback;

	BluetoothWrapper& _conn;
};

template<class T>
inline void Property<T>::markSent(T sent)
{
	this->current = sent;
}

template<class T>
inline bool Property<T>::isFulfilled()
{
	return this->desired == this->current;
}

template<class T>
inline void Property<T>::setState(T val)
{
	this->current = val;
	this->desired = val;
}

template<class T>
inline void Property<T>::updateFromDevice(T val)
{
	bool pending = !this->isFulfilled();
	this->current = val;
	if (!pending)
		this->desired = val;
}
