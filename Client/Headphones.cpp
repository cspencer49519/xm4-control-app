#include "Headphones.h"
#include "CommandSerializer.h"

#include <stdexcept>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

Headphones::Headphones(BluetoothWrapper& conn) : 
_conn(conn)
{
	std::vector<BluetoothDevice> devices({BluetoothDevice({"",""})});
	this->_savedDevices = devices;
}

void Headphones::setAmbientSoundControl(bool val)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_ambientSoundControl.desired = val;
}

bool Headphones::getAmbientSoundControl()
{
	return this->_ambientSoundControl.current;
}

bool Headphones::isFocusOnVoiceAvailable()
{
	return this->_ambientSoundControl.current && this->_asmLevel.current > MINIMUM_VOICE_FOCUS_STEP;
}

void Headphones::setFocusOnVoice(bool val)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_focusOnVoice.desired = val;
}

bool Headphones::getFocusOnVoice()
{
	return this->_focusOnVoice.current;
}

bool Headphones::isSetAsmLevelAvailable()
{
	return this->_ambientSoundControl.current;
}

void Headphones::setAsmLevel(int val)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_asmLevel.desired = val;
}

int Headphones::getAsmLevel()
{
	return this->_asmLevel.current;
}

void Headphones::setOptimizerState(OPTIMIZER_STATE val)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_optimizerState.desired = val;
}

OPTIMIZER_STATE Headphones::getOptimizerState()
{
	return this->_optimizerState.current;
}

void Headphones::setS2CToggle(S2C_TOGGLE val)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_speakToChat.desired = val;
}

void Headphones::setS2COptions(int sensitivity, bool voice, int offTime)
{
	unsigned int sens = (unsigned int) sensitivity;
	unsigned int time = (unsigned int) offTime;
	std::lock_guard guard(this->_propertyMtx);
	this->_s2cOptions.desired = { (sens << 16) | (voice << 8) | (time) };
}

S2C_TOGGLE Headphones::getS2CToggle()
{
	return this->_speakToChat.current;
}

unsigned int Headphones::getS2COptions()
{
	return this->_s2cOptions.current;
}

void Headphones::setSurroundPosition(SOUND_POSITION_PRESET val)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_surroundPosition.desired = val;
}

SOUND_POSITION_PRESET Headphones::getSurroundPosition()
{
	return this->_surroundPosition.current;
}

void Headphones::setVptType(int val)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_vptType.desired = val;
}

int Headphones::getVptType()
{
	return this->_vptType.current;
}

bool Headphones::getMultiPointSetting()
{
	return this->_multiPointSetting;
}

std::vector<BluetoothDevice> Headphones::getDevices()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_savedDevices;
}

std::pair<int, int> Headphones::getConnectedDevices()
{
	return {this->_dev1.current, this->_dev2.current};
}

void Headphones::setMultiPointConnection(int connectionId, int newDevice)
{
	Property<int>* dev = (connectionId == 1) ? &this->_dev1 : &this->_dev2;

	std::lock_guard guard(this->_propertyMtx);
	dev->desired = newDevice;
}

inline void Headphones::disconnect(int deviceIdx)
{
	std::string deviceMac;
	{
		std::lock_guard guard(this->_propertyMtx);
		if (deviceIdx < 0 || deviceIdx >= static_cast<int>(this->_savedDevices.size()))
			return;
		deviceMac = this->_savedDevices[deviceIdx].mac;
	}
	this->_conn.sendCommand(CommandSerializer::serializeMultiPointCommand(
			MULTI_POINT_COMMANDS::DISCONNECT,
			deviceMac
		),
		DATA_TYPE::DATA_MDR_NO2
	);
}

inline void Headphones::connect(int deviceIdx)
{
	std::string deviceMac;
	{
		std::lock_guard guard(this->_propertyMtx);
		if (deviceIdx < 0 || deviceIdx >= static_cast<int>(this->_savedDevices.size()))
			return;
		deviceMac = this->_savedDevices[deviceIdx].mac;
	}
	this->_conn.sendCommand(CommandSerializer::serializeMultiPointCommand(
			MULTI_POINT_COMMANDS::CONNECT,
			deviceMac
		),
		DATA_TYPE::DATA_MDR_NO2
	);
}

bool Headphones::isChanged()
{
	std::lock_guard guard(this->_propertyMtx);
	return !(this->_ambientSoundControl.isFulfilled() &&
		this->_asmLevel.isFulfilled() &&
		this->_focusOnVoice.isFulfilled() &&
		this->_surroundPosition.isFulfilled() &&
		this->_vptType.isFulfilled() &&
		this->_optimizerState.isFulfilled() &&
		this->_speakToChat.isFulfilled() &&
		this->_s2cOptions.isFulfilled() &&
		this->_dev1.isFulfilled() &&
		this->_dev2.isFulfilled() &&
		this->_eqPreset.isFulfilled() &&
		this->_eqBands.isFulfilled()
		);
}

// At most one instance of this function is invoked at any time.
// Each block snapshots the desired values under the lock, sends them without
// holding the lock, then records exactly what was sent (markSent). A change the
// user makes while a command is in flight therefore stays pending and is sent
// on the next call instead of being lost.
void Headphones::setChanges()
{
	bool optimizerPending;
	OPTIMIZER_STATE optimizerState;
	{
		std::lock_guard guard(this->_propertyMtx);
		optimizerPending = !this->_optimizerState.isFulfilled();
		optimizerState = this->_optimizerState.desired;
	}
	if (optimizerPending)
	{
		this->_conn.sendCommand(CommandSerializer::serializeXM4OptimizeCommand(optimizerState));
		std::lock_guard guard(this->_propertyMtx);
		this->_optimizerState.markSent(optimizerState);
	}

	bool s2cPending;
	S2C_TOGGLE s2cState;
	{
		std::lock_guard guard(this->_propertyMtx);
		s2cPending = !this->_speakToChat.isFulfilled();
		s2cState = this->_speakToChat.desired;
	}
	if (s2cPending)
	{
		this->_conn.sendCommand(CommandSerializer::serializeXM4SpeakToChat(s2cState));
		std::lock_guard guard(this->_propertyMtx);
		this->_speakToChat.markSent(s2cState);
	}

	bool s2cOptionsPending;
	unsigned int s2cOptions;
	{
		std::lock_guard guard(this->_propertyMtx);
		s2cOptionsPending = !this->_s2cOptions.isFulfilled();
		s2cOptions = this->_s2cOptions.desired;
	}
	if (s2cOptionsPending)
	{
		unsigned char sensitivity = (unsigned char) (s2cOptions >> 16) & 0xff;
		unsigned char voice = (unsigned char) (s2cOptions >> 8) & 0xff;
		unsigned char offTime = (unsigned char) (s2cOptions) & 0xff;

		this->_conn.sendCommand(CommandSerializer::serializeXM4_S2C_Options(
			sensitivity,
			voice,
			offTime
		));

		std::lock_guard guard(this->_propertyMtx);
		this->_s2cOptions.markSent(s2cOptions);
	}

	bool ncAsmPending;
	bool ambient, focusOnVoice;
	int level;
	unsigned char ncAsmSettingType, asmSettingType;
	int ambientSteps;
	{
		std::lock_guard guard(this->_propertyMtx);
		ncAsmPending = !(this->_ambientSoundControl.isFulfilled() && this->_focusOnVoice.isFulfilled() && this->_asmLevel.isFulfilled());
		ambient = this->_ambientSoundControl.desired;
		focusOnVoice = this->_focusOnVoice.desired;
		level = this->_asmLevel.desired;
		ncAsmSettingType = this->_ncAsmSettingType;
		asmSettingType = this->_asmSettingType;
		ambientSteps = this->_ambientSteps;
	}
	if (ncAsmPending)
	{
		// Legacy level encoding -> wire values (see getAmbientSteps()):
		// 0 = noise cancelling (DUAL), 1 = wind noise reduction (SINGLE), L+1 = ambient level L.
		bool dualSingleDevice = ncAsmSettingType == static_cast<unsigned char>(NC_ASM_SETTING_TYPE::DUAL_SINGLE_OFF);
		unsigned char ncValue;
		unsigned char asmValue;
		if (level <= 0) {
			ncValue = dualSingleDevice ? static_cast<unsigned char>(NC_DUAL_SINGLE_VALUE::DUAL) : 0x01;
			asmValue = 0;
		}
		else if (level == 1 && dualSingleDevice) {
			ncValue = static_cast<unsigned char>(NC_DUAL_SINGLE_VALUE::SINGLE);
			asmValue = 0;
		}
		else {
			ncValue = 0x00; // NC off / NC_DUAL_SINGLE_VALUE::OFF
			asmValue = static_cast<unsigned char>(std::clamp(level - 1, 1, ambientSteps));
		}

		// Turning NC/ASM off re-sends the current mode with effect OFF.
		auto packet = CommandSerializer::serializeNcAndAsmSetting(
			ambient ? NC_ASM_EFFECT::ADJUSTMENT_COMPLETION : NC_ASM_EFFECT::OFF,
			ncAsmSettingType,
			ncValue,
			asmSettingType,
			focusOnVoice ? ASM_ID::VOICE : ASM_ID::NORMAL,
			asmValue
		);

		this->_conn.sendCommand(packet);
		std::lock_guard guard(this->_propertyMtx);
		this->_ambientSoundControl.markSent(ambient);
		this->_asmLevel.markSent(level);
		this->_focusOnVoice.markSent(focusOnVoice);
	}

	// Equalizer: a preset selection first (the device replies with its bands), then band edits.
	bool eqPresetPending, eqBandsPending;
	unsigned char eqPreset, eqType;
	std::vector<unsigned char> eqBands;
	{
		std::lock_guard guard(this->_propertyMtx);
		eqPresetPending = !this->_eqPreset.isFulfilled();
		eqBandsPending = !this->_eqBands.isFulfilled();
		eqPreset = this->_eqPreset.desired;
		eqBands = this->_eqBands.desired;
		eqType = this->_eqInquiredType;
	}
	if (eqPresetPending)
	{
		this->_conn.sendCommand({ static_cast<char>(COMMAND_TYPE::EQ_SET_PARAM), static_cast<char>(eqType), static_cast<char>(eqPreset), 0x00 });
		std::lock_guard guard(this->_propertyMtx);
		this->_eqPreset.markSent(eqPreset);
	}
	if (eqBandsPending && !eqBands.empty())
	{
		Buffer packet = { static_cast<char>(COMMAND_TYPE::EQ_SET_PARAM), static_cast<char>(eqType),
			static_cast<char>(EQ_PRESET_UNSPECIFIED), static_cast<char>(eqBands.size()) };
		packet.insert(packet.end(), eqBands.begin(), eqBands.end());
		this->_conn.sendCommand(packet);
		std::lock_guard guard(this->_propertyMtx);
		this->_eqBands.markSent(eqBands);
	}

	bool vptPending;
	int vptDesired, vptCurrent;
	SOUND_POSITION_PRESET positionDesired, positionCurrent;
	{
		std::lock_guard guard(this->_propertyMtx);
		vptPending = !(this->_vptType.isFulfilled() && this->_surroundPosition.isFulfilled());
		vptDesired = this->_vptType.desired;
		vptCurrent = this->_vptType.current;
		positionDesired = this->_surroundPosition.desired;
		positionCurrent = this->_surroundPosition.current;
	}
	if (vptPending)
	{
		VPT_INQUIRED_TYPE command;
		unsigned char preset;
		if (vptDesired != 0) {
			command = VPT_INQUIRED_TYPE::VPT;
			preset = static_cast<unsigned char>(vptDesired);
		}
		else if (positionDesired != SOUND_POSITION_PRESET::OFF) {
			command = VPT_INQUIRED_TYPE::SOUND_POSITION;
			preset = static_cast<unsigned char>(positionDesired);
		}
		else if (positionCurrent != SOUND_POSITION_PRESET::OFF) {
			command = VPT_INQUIRED_TYPE::SOUND_POSITION;
			preset = static_cast<unsigned char>(SOUND_POSITION_PRESET::OFF);
		}
		else if (vptCurrent != 0) {
			command = VPT_INQUIRED_TYPE::VPT;
			preset = 0;
		}
		else {
			throw std::logic_error("it's impossible that both values were changed to zero and were also previously zero");
		}

		this->_conn.sendCommand(CommandSerializer::serializeVPTSetting(command, preset));

		std::lock_guard guard(this->_propertyMtx);
		this->_vptType.markSent(vptDesired);
		this->_surroundPosition.markSent(positionDesired);
	}

	for (Property<int>* dev : { &this->_dev1, &this->_dev2 })
	{
		bool pending;
		int devCurrent, devDesired;
		{
			std::lock_guard guard(this->_propertyMtx);
			pending = !dev->isFulfilled();
			devCurrent = dev->current;
			devDesired = dev->desired;
		}
		if (!pending)
			continue;

		if (devCurrent != 0)
			this->disconnect(devCurrent);
		if (devDesired != 0)
			this->connect(devDesired);

		// The real connection state arrives in a DEVICES_STATE_RESPONSE notification.
		std::lock_guard guard(this->_propertyMtx);
		dev->desired = dev->current;
	}
}

unsigned int Headphones::getCapabilities()
{
	return this->_capabilities;
}

void Headphones::queryState()
{
	this->_conn.sendCommand({0x00,0x00}); // Init
	this->_conn.sendCommand({0x02,0x00}); // get mac address of device
	this->queryDeviceCapabilities();
	this->queryDevices();
}

void Headphones::resetState()
{
	{
		std::lock_guard guard(this->_propertyMtx);
		this->_capabilities = 0;
		this->_loadedState = 0;
		this->_supportedFunctions.clear();
		this->_ncAsmSettingType = DEFAULT_NC_ASM_SETTING_TYPE;
		this->_asmSettingType = DEFAULT_ASM_SETTING_TYPE;
		this->_ambientSteps = DEFAULT_AMBIENT_STEPS;
		this->_deviceStatus = DeviceStatus{};
		this->_eqCapability = EqCapability{};
		this->_eqInquiredType = EQ_TYPE_PRESET_EQ;
		this->_eqPreset.setState(0);
		this->_eqBands.setState({});
		this->_ambientSoundControl.setState(false);
		this->_focusOnVoice.setState(false);
		this->_asmLevel.setState(0);
		this->_surroundPosition.setState(SOUND_POSITION_PRESET::OFF);
		this->_vptType.setState(0);
		this->_optimizerState.setState(OPTIMIZER_STATE::IDLE);
		this->_speakToChat.setState(S2C_TOGGLE::INACTIVE);
		this->_s2cOptions.setState(0);
		this->_savedDevices = { BluetoothDevice{"", ""} };
		this->_dev1.setState(0);
		this->_dev2.setState(0);
	}
	this->_notifyStateChanged();
}

bool Headphones::syncState(std::chrono::milliseconds timeout)
{
	auto deadline = std::chrono::steady_clock::now() + timeout;

	this->_conn.sendCommand({0x00, 0x00}); // Init
	this->queryDeviceCapabilities();

	{
		std::unique_lock lock(this->_propertyMtx);
		bool gotCapabilities = this->_stateCv.wait_until(lock, deadline, [this] {
			return (this->_loadedState & STATE_CAPABILITIES) != 0;
		});
		if (!gotCapabilities)
			return false;
	}

	unsigned int caps = this->_capabilities;
	unsigned int expected = 0;

	// Read-only info; replies update getDeviceStatus() whenever they arrive.
	this->queryDeviceInfo(DEVICE_INFO_TYPE::MODEL_NAME);
	this->queryDeviceInfo(DEVICE_INFO_TYPE::FW_VERSION);
	if (this->supportsFunction(FUNCTION::BATTERY_LEVEL))
		this->queryBattery();
	if (this->supportsFunction(FUNCTION::CODEC_INDICATOR))
		this->queryCodec();
	if (this->supportsFunction(FUNCTION::UPSCALING_INDICATOR))
		this->queryUpscalingIndicator();

	if (caps & DEVICE_CAPABILITIES::NC_ASM)
	{
		// The capability tells us the setting types to echo and the ambient step count,
		// so it must arrive before the state is interpreted or anything is sent.
		this->queryNcAsmCapability();
		this->queryNcAsm();
		expected |= STATE_NC_ASM | STATE_NC_ASM_CAPABILITY;
	}
	if (caps & DEVICE_CAPABILITIES::OPTIMIZER)
		this->queryOptimizerStatus();
	if (this->supportsFunction(FUNCTION::PRESET_EQ) || this->supportsFunction(FUNCTION::PRESET_EQ_NONCUSTOMIZABLE))
	{
		{
			std::lock_guard guard(this->_propertyMtx);
			this->_eqInquiredType = std::find(this->_supportedFunctions.begin(), this->_supportedFunctions.end(), FUNCTION::PRESET_EQ) != this->_supportedFunctions.end()
				? EQ_TYPE_PRESET_EQ : EQ_TYPE_PRESET_EQ_NONCUSTOMIZABLE;
		}
		this->queryEq();
		expected |= STATE_EQ | STATE_EQ_CAPABILITY;
	}
	if (caps & DEVICE_CAPABILITIES::VPT)
	{
		this->queryVpt();
		expected |= STATE_VPT;
	}
	if (caps & DEVICE_CAPABILITIES::SPEAK_TO_CHAT)
	{
		this->queryS2C();
		this->queryS2COptions();
		expected |= STATE_S2C | STATE_S2C_OPTIONS;
	}
	if (caps & DEVICE_CAPABILITIES::MULTI_POINT)
	{
		this->queryDevices();
		expected |= STATE_DEVICES;
	}

	std::unique_lock lock(this->_propertyMtx);
	return this->_stateCv.wait_until(lock, deadline, [this, expected] {
		return (this->_loadedState & expected) == expected;
	});
}

bool Headphones::supportsFunction(unsigned char functionType)
{
	std::lock_guard guard(this->_propertyMtx);
	return std::find(this->_supportedFunctions.begin(), this->_supportedFunctions.end(), functionType) != this->_supportedFunctions.end();
}

std::vector<unsigned char> Headphones::getSupportedFunctions()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_supportedFunctions;
}

EqCapability Headphones::getEqCapability()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_eqCapability;
}

void Headphones::setEqPreset(unsigned char presetId)
{
	std::lock_guard guard(this->_propertyMtx);
	this->_eqPreset.desired = presetId;
	// The preset defines the bands; drop any band edit that hasn't been sent yet.
	this->_eqBands.desired = this->_eqBands.current;
}

void Headphones::setEqBands(const std::vector<unsigned char>& bands)
{
	std::lock_guard guard(this->_propertyMtx);
	if (bands.size() != static_cast<size_t>(this->_eqCapability.bandCount) || !this->_eqCapability.customizable)
		return;
	std::vector<unsigned char> clamped = bands;
	for (auto& b : clamped)
		b = static_cast<unsigned char>(std::min<int>(b, std::max(this->_eqCapability.levelSteps - 1, 0)));
	this->_eqBands.desired = clamped;
	// Band edits switch the device to "Manual"; drop any preset choice that hasn't been sent yet.
	this->_eqPreset.desired = this->_eqPreset.current;
}

void Headphones::queryEq()
{
	char type;
	{
		std::lock_guard guard(this->_propertyMtx);
		type = static_cast<char>(this->_eqInquiredType);
	}
	this->_conn.sendCommand({ static_cast<char>(COMMAND_TYPE::EQ_GET_CAPABILITY), type, static_cast<char>(DISPLAY_LANGUAGE_ENGLISH) });
	this->_conn.sendCommand({ static_cast<char>(COMMAND_TYPE::EQ_GET_EXTENDED_INFO), type });
	this->_conn.sendCommand({ static_cast<char>(COMMAND_TYPE::EQ_GET_STATUS), type });
	this->_conn.sendCommand({ static_cast<char>(COMMAND_TYPE::EQ_GET_PARAM), type });
}

DeviceStatus Headphones::getDeviceStatus()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_deviceStatus;
}

int Headphones::getAmbientSteps()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_ambientSteps;
}

unsigned int Headphones::getLoadedState()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_loadedState;
}

HeadphonesSettings Headphones::getSettings()
{
	std::lock_guard guard(this->_propertyMtx);
	HeadphonesSettings s;
	s.ambientSoundControl = this->_ambientSoundControl.desired;
	s.focusOnVoice = this->_focusOnVoice.desired;
	s.asmLevel = this->_asmLevel.desired;
	s.surroundPosition = this->_surroundPosition.desired;
	s.vptType = this->_vptType.desired;
	s.optimizerState = this->_optimizerState.desired;
	s.speakToChat = this->_speakToChat.desired;
	unsigned int opts = this->_s2cOptions.desired;
	s.s2cSensitivity = static_cast<int>((opts >> 16) & 0xff);
	s.s2cVoice = ((opts >> 8) & 0xff) != 0;
	s.s2cOffTime = static_cast<int>(opts & 0xff);
	s.eqPreset = this->_eqPreset.desired;
	s.eqBands = this->_eqBands.desired;
	return s;
}

void Headphones::setStateChangedCallback(std::function<void()> callback)
{
	std::lock_guard guard(this->_callbackMtx);
	this->_stateChangedCallback = std::move(callback);
}

void Headphones::_markLoaded(unsigned int flags)
{
	{
		std::lock_guard guard(this->_propertyMtx);
		this->_loadedState |= flags;
	}
	this->_stateCv.notify_all();
}

void Headphones::_notifyStateChanged()
{
	std::function<void()> callback;
	{
		std::lock_guard guard(this->_callbackMtx);
		callback = this->_stateChangedCallback;
	}
	if (callback)
		callback();
}

void Headphones::setStateFromReply(BtMessage replyMessage)
{
	const Buffer& bytes = replyMessage.messageBytes;
	if (bytes.empty())
		return;

	auto byteAt = [&bytes](size_t i) { return static_cast<unsigned char>(bytes[i]); };
	COMMAND_TYPE cmd = static_cast<COMMAND_TYPE>(bytes[0]);

	switch (cmd)
	{
	case COMMAND_TYPE::DEVICES_QUERY_RESPONSE:
	case COMMAND_TYPE::DEVICES_STATE_RESPONSE:
	{
		if (bytes.size() < 3 || bytes[1] != 0x01)
			// Wrong query type, break
			break;

		std::vector<BluetoothDevice> savedDevices = { BluetoothDevice{"", ""} };
		int dev1 = 0, dev2 = 0;
		size_t idx = 3;
		int numDevices = byteAt(2);
		for (; numDevices > 0; numDevices--)
		{
			// MAC string, slot number, name length
			if (idx + MAC_ADDR_STR_SIZE + 2 > bytes.size())
				break;
			std::string mac_addr(bytes.begin() + idx, bytes.begin() + idx + MAC_ADDR_STR_SIZE);
			idx += MAC_ADDR_STR_SIZE;

			unsigned char number = byteAt(idx++);
			unsigned char name_length = byteAt(idx++);
			if (idx + name_length > bytes.size())
				break;
			std::string name(bytes.begin() + idx, bytes.begin() + idx + name_length);
			idx += name_length;

			savedDevices.push_back(BluetoothDevice{name, mac_addr});
			if (number == 0x01)
				dev1 = static_cast<int>(savedDevices.size()) - 1;
			if (number == 0x02)
				dev2 = static_cast<int>(savedDevices.size()) - 1;
		}

		{
			std::lock_guard guard(this->_propertyMtx);
			this->_savedDevices = std::move(savedDevices);
			this->_dev1.setState(dev1);
			this->_dev2.setState(dev2);
		}
		this->_markLoaded(STATE_DEVICES);
		this->_notifyStateChanged();
		break;
	}

	// [0x51, type, bandCount, levelSteps, n, n x (presetId, nameLen, name)]
	case COMMAND_TYPE::EQ_RET_CAPABILITY:
	{
		if (bytes.size() < 5 || (byteAt(1) != EQ_TYPE_PRESET_EQ && byteAt(1) != EQ_TYPE_PRESET_EQ_NONCUSTOMIZABLE))
			break;
		EqCapability capability;
		capability.available = true;
		capability.customizable = byteAt(1) == EQ_TYPE_PRESET_EQ;
		capability.bandCount = byteAt(2);
		capability.levelSteps = byteAt(3);
		size_t presetCount = byteAt(4);
		size_t idx = 5;
		for (size_t i = 0; i < presetCount && idx + 2 <= bytes.size(); i++)
		{
			unsigned char id = byteAt(idx);
			size_t len = byteAt(idx + 1);
			if (len > 128)
				len = 0;
			idx += 2;
			if (idx + len > bytes.size())
				break;
			capability.presets.push_back(EqPreset{ id, std::string(bytes.begin() + idx, bytes.begin() + idx + len) });
			idx += len;
		}
		{
			std::lock_guard guard(this->_propertyMtx);
			// keep labels/status that may already have arrived
			capability.enabled = this->_eqCapability.enabled;
			capability.bandLabels = this->_eqCapability.bandLabels;
			capability.clearBassIndex = this->_eqCapability.clearBassIndex;
			if (capability.bandLabels.size() != static_cast<size_t>(capability.bandCount))
			{
				capability.bandLabels.clear();
				capability.clearBassIndex = -1;
				if (capability.bandCount == 6)
				{
					// The standard 6-band layout (used by the official app when no extended info is given).
					capability.bandLabels = { "Clear Bass", "400", "1k", "2.5k", "6.3k", "16k" };
					capability.clearBassIndex = 0;
				}
				else
				{
					for (int i = 0; i < capability.bandCount; i++)
						capability.bandLabels.push_back("Band " + std::to_string(i + 1));
				}
			}
			this->_eqCapability = std::move(capability);
		}
		this->_markLoaded(STATE_EQ_CAPABILITY);
		this->_notifyStateChanged();
		break;
	}

	// [0x5b, type, n, n x (infoType, value u16 big-endian)]; infoType 1 = Hz, 2 = kHz, 0x10 = special (1 = Clear Bass)
	case COMMAND_TYPE::EQ_RET_EXTENDED_INFO:
	{
		if (bytes.size() < 3)
			break;
		size_t n = byteAt(2);
		if (3 + 3 * n > bytes.size())
			break;
		std::vector<std::string> labels;
		int clearBass = -1;
		for (size_t i = 0; i < n; i++)
		{
			unsigned char type = byteAt(3 + 3 * i);
			unsigned int value = (byteAt(4 + 3 * i) << 8) | byteAt(5 + 3 * i);
			std::string label;
			if (type == 0x01 && value < 1000)
				label = std::to_string(value);
			else if (type == 0x01)
			{
				// 2500 -> "2.5k", 6300 -> "6.3k", 15662 -> "15.6k", 1000 -> "1k"
				unsigned int tenths = value / 100;
				label = std::to_string(tenths / 10);
				if (tenths % 10)
					label += "." + std::to_string(tenths % 10);
				label += "k";
			}
			else if (type == 0x02)
				label = std::to_string(value) + "k";
			else if (type == 0x10 && value == 0x0001)
			{
				label = "Clear Bass";
				clearBass = static_cast<int>(i);
			}
			labels.push_back(label);
		}
		{
			std::lock_guard guard(this->_propertyMtx);
			// Only valid when it matches the band count (if that is already known).
			if (this->_eqCapability.bandCount == 0 || static_cast<size_t>(this->_eqCapability.bandCount) == n)
			{
				this->_eqCapability.bandLabels = std::move(labels);
				this->_eqCapability.clearBassIndex = clearBass;
			}
		}
		this->_notifyStateChanged();
		break;
	}

	// [cmd, type, CommonStatus (0 = enabled)]
	case COMMAND_TYPE::EQ_RET_STATUS:
	case COMMAND_TYPE::EQ_NTFY_STATUS:
	{
		if (bytes.size() < 3)
			break;
		{
			std::lock_guard guard(this->_propertyMtx);
			this->_eqCapability.enabled = byteAt(2) != 0x01;
		}
		this->_notifyStateChanged();
		break;
	}

	// [cmd, type, presetId, n, n x step]
	case COMMAND_TYPE::EQ_RET_PARAM:
	case COMMAND_TYPE::EQ_NTFY_PARAM:
	{
		if (bytes.size() < 4 || (byteAt(1) != EQ_TYPE_PRESET_EQ && byteAt(1) != EQ_TYPE_PRESET_EQ_NONCUSTOMIZABLE))
			break;
		size_t n = byteAt(3);
		if (4 + n > bytes.size())
			break;
		std::vector<unsigned char> bands(bytes.begin() + 4, bytes.begin() + 4 + n);
		{
			std::lock_guard guard(this->_propertyMtx);
			this->_eqPreset.updateFromDevice(byteAt(2));
			if (!bands.empty())
				this->_eqBands.updateFromDevice(bands);
		}
		this->_markLoaded(STATE_EQ);
		this->_notifyStateChanged();
		break;
	}

	// [0x05, type, len, UTF-8 string]  (MODEL_NAME / FW_VERSION)
	case COMMAND_TYPE::DEVICE_INFO_RET:
	{
		if (bytes.size() < 3)
			break;
		auto type = static_cast<DEVICE_INFO_TYPE>(bytes[1]);
		if (type != DEVICE_INFO_TYPE::MODEL_NAME && type != DEVICE_INFO_TYPE::FW_VERSION)
			break;
		size_t len = std::min<size_t>(byteAt(2), bytes.size() - 3);
		std::string value(bytes.begin() + 3, bytes.begin() + 3 + len);
		{
			std::lock_guard guard(this->_propertyMtx);
			(type == DEVICE_INFO_TYPE::MODEL_NAME ? this->_deviceStatus.modelName : this->_deviceStatus.firmwareVersion) = value;
		}
		this->_notifyStateChanged();
		break;
	}

	// [cmd, BATTERY_TYPE, level, chargingStatus, ...]
	case COMMAND_TYPE::BATTERY_RET:
	case COMMAND_TYPE::BATTERY_NTFY:
	{
		if (bytes.size() < 4 || static_cast<BATTERY_TYPE>(bytes[1]) != BATTERY_TYPE::SINGLE)
			break;
		{
			std::lock_guard guard(this->_propertyMtx);
			this->_deviceStatus.batteryLevel = std::min<int>(byteAt(2), 100);
			this->_deviceStatus.charging = byteAt(3) == 0x01;
		}
		this->_notifyStateChanged();
		break;
	}

	// [cmd, 0x00, AUDIO_CODEC]
	case COMMAND_TYPE::AUDIO_CODEC_RET:
	case COMMAND_TYPE::AUDIO_CODEC_NTFY:
	{
		if (bytes.size() < 3)
			break;
		{
			std::lock_guard guard(this->_propertyMtx);
			this->_deviceStatus.hasCodec = true;
			this->_deviceStatus.codec = static_cast<AUDIO_CODEC>(byteAt(2));
		}
		this->_notifyStateChanged();
		break;
	}

	// [cmd, 0x00, UPSCALING_TYPE, status (0 off, 1 active, 2 enabled but not in effect)]
	case COMMAND_TYPE::UPSCALING_EFFECT_RET:
	case COMMAND_TYPE::UPSCALING_EFFECT_NTFY:
	{
		if (bytes.size() < 4)
			break;
		{
			std::lock_guard guard(this->_propertyMtx);
			this->_deviceStatus.hasUpscaling = true;
			this->_deviceStatus.upscalingType = static_cast<UPSCALING_TYPE>(byteAt(2));
			this->_deviceStatus.upscalingActive = byteAt(3) == 0x01;
		}
		this->_notifyStateChanged();
		break;
	}

	// [cmd, inquiredType, CommonStatus (0 = enabled), OptimizerStatus]
	case COMMAND_TYPE::OPT_RET_STATUS:
	case COMMAND_TYPE::XM4_OPTIMIZER_RESPONSE:
	{
		if (bytes.size() < 4)
			break;
		unsigned char status = byteAt(3);
		bool busy = status == OPTIMIZER_STATUS_MEASURING_PERSONAL ||
			status == OPTIMIZER_STATUS_MEASURING_PRESSURE ||
			status == OPTIMIZER_STATUS_OPTIMIZING;
		{
			std::lock_guard guard(this->_propertyMtx);
			this->_optimizerState.updateFromDevice(busy ? OPTIMIZER_STATE::OPTIMIZING : OPTIMIZER_STATE::IDLE);
		}
		this->_notifyStateChanged();
		break;
	}

	// [0x07, inquiredType, n, FunctionType x n]
	case COMMAND_TYPE::CAPABILITY_QUERY_RESPONSE:
	{
		if (bytes.size() < 3)
			break;
		size_t count = std::min<size_t>(byteAt(2), bytes.size() - 3);
		std::vector<unsigned char> functions(bytes.begin() + 3, bytes.begin() + 3 + count);

		unsigned int caps = 0;
		for (unsigned char f : functions)
		{
			switch (static_cast<FUNCTION_TYPE>(f))
			{
			case FUNCTION_TYPE::DEVICE_MANAGEMENT: caps |= DEVICE_CAPABILITIES::MULTI_POINT; break;
			case FUNCTION_TYPE::VPT: caps |= DEVICE_CAPABILITIES::VPT; break;
			case FUNCTION_TYPE::OPTIMIZER: caps |= DEVICE_CAPABILITIES::OPTIMIZER; break;
			case FUNCTION_TYPE::NC_ASM: caps |= DEVICE_CAPABILITIES::NC_ASM; break;
			case FUNCTION_TYPE::SMART_TALKING_MODE: caps |= DEVICE_CAPABILITIES::SPEAK_TO_CHAT; break;
			default: break;
			}
		}
		{
			std::lock_guard guard(this->_propertyMtx);
			this->_supportedFunctions = std::move(functions);
		}
		this->_capabilities |= caps;
		this->_markLoaded(STATE_CAPABILITIES);
		this->_notifyStateChanged();
		break;
	}

	// [0x61, 0x02, ncAsmSettingType, ncSteps, asmSettingType, n, (asmId, steps) x n]
	case COMMAND_TYPE::NCASM_RET_CAPABILITY:
	{
		if (bytes.size() < 6 || bytes[1] != static_cast<char>(NC_ASM_INQUIRED_TYPE::NOISE_CANCELLING_AND_AMBIENT_SOUND_MODE))
			break;
		int steps = 0;
		size_t n = byteAt(5);
		for (size_t i = 0; i < n && 6 + 2 * i + 1 < bytes.size(); i++)
		{
			unsigned char asmId = byteAt(6 + 2 * i);
			unsigned char asmSteps = byteAt(7 + 2 * i);
			// Prefer the NORMAL entry; VOICE normally has the same count.
			if (asmSteps > 0 && (steps == 0 || asmId == static_cast<unsigned char>(ASM_ID::NORMAL)))
				steps = asmSteps;
		}
		{
			std::lock_guard guard(this->_propertyMtx);
			this->_ncAsmSettingType = byteAt(2);
			this->_asmSettingType = byteAt(4);
			if (steps > 0)
				this->_ambientSteps = steps;
		}
		this->_markLoaded(STATE_NC_ASM_CAPABILITY);
		this->_notifyStateChanged();
		break;
	}

	// [cmd, 0x02, effect, ncAsmSettingType, ncValue, asmSettingType, asmId, asmValue]
	case COMMAND_TYPE::NCASM_RET_PARAM:
	case COMMAND_TYPE::NCASM_NTFY_PARAM:
	{
		if (bytes.size() < 8 || bytes[1] != static_cast<char>(NC_ASM_INQUIRED_TYPE::NOISE_CANCELLING_AND_AMBIENT_SOUND_MODE))
			break;

		bool enabled = static_cast<NC_ASM_EFFECT>(bytes[2]) != NC_ASM_EFFECT::OFF;
		unsigned char settingType = byteAt(3);
		unsigned char ncValue = byteAt(4);
		bool voice = static_cast<ASM_ID>(bytes[6]) == ASM_ID::VOICE;
		int ambientLevel = byteAt(7);

		{
			std::lock_guard guard(this->_propertyMtx);
			this->_ambientSoundControl.updateFromDevice(enabled);
			if (enabled)
			{
				// Wire values -> legacy level encoding (see getAmbientSteps()).
				int level;
				if (settingType == static_cast<unsigned char>(NC_ASM_SETTING_TYPE::DUAL_SINGLE_OFF) &&
					ncValue == static_cast<unsigned char>(NC_DUAL_SINGLE_VALUE::DUAL))
					level = 0;
				else if (settingType == static_cast<unsigned char>(NC_ASM_SETTING_TYPE::DUAL_SINGLE_OFF) &&
					ncValue == static_cast<unsigned char>(NC_DUAL_SINGLE_VALUE::SINGLE))
					level = 1;
				else if (settingType == static_cast<unsigned char>(NC_ASM_SETTING_TYPE::ON_OFF) && ncValue == 0x01)
					level = 0;
				else
					level = std::clamp(ambientLevel, 1, this->_ambientSteps) + 1;
				this->_asmLevel.updateFromDevice(level);
				this->_focusOnVoice.updateFromDevice(voice);
			}
		}
		this->_markLoaded(STATE_NC_ASM);
		this->_notifyStateChanged();
		break;
	}

	// [cmd, inquiredType (1 = VPT, 2 = sound position), preset]
	case COMMAND_TYPE::VPT_RET_PARAM:
	case COMMAND_TYPE::VPT_NTFY_PARAM:
	{
		if (bytes.size() < 3)
			break;

		auto type = static_cast<VPT_INQUIRED_TYPE>(bytes[1]);
		unsigned char preset = byteAt(2);
		{
			std::lock_guard guard(this->_propertyMtx);
			if (type == VPT_INQUIRED_TYPE::VPT)
			{
				this->_vptType.updateFromDevice(preset <= static_cast<unsigned char>(VPT_PRESET_ID::CLUB) ? preset : 0);
			}
			else if (type == VPT_INQUIRED_TYPE::SOUND_POSITION)
			{
				SOUND_POSITION_PRESET position = SOUND_POSITION_PRESET::OFF;
				for (SOUND_POSITION_PRESET p : SOUND_POSITION_PRESET_ARRAY)
				{
					if (p != SOUND_POSITION_PRESET::OUT_OF_RANGE && static_cast<unsigned char>(p) == preset)
						position = p;
				}
				this->_surroundPosition.updateFromDevice(position);
			}
		}
		this->_markLoaded(STATE_VPT);
		this->_notifyStateChanged();
		break;
	}

	// [cmd, 0x05, 0x01, state] - mirrors serializeXM4SpeakToChat
	case COMMAND_TYPE::S2C_TOGGLE_RET:
	case COMMAND_TYPE::S2C_TOGGLE_NTFY:
	{
		if (bytes.size() < 4 || bytes[1] != S2C_INQUIRED_TYPE)
			break;
		// RET has byte 2 = setting type (0x00); NTFY has byte 2 = which switch changed.
		// A preview-mode notification is not the main toggle.
		if (cmd == COMMAND_TYPE::S2C_TOGGLE_NTFY && bytes[2] != S2C_PARAM_MODE_ON_OFF)
			break;
		{
			std::lock_guard guard(this->_propertyMtx);
			this->_speakToChat.updateFromDevice(byteAt(3) == 0x01 ? S2C_TOGGLE::ACTIVE : S2C_TOGGLE::INACTIVE);
		}
		this->_markLoaded(STATE_S2C);
		this->_notifyStateChanged();
		break;
	}

	// [cmd, 0x05, (0x00,) sensitivity, voice, offTime] - the three values are the last bytes
	case COMMAND_TYPE::S2C_OPTIONS_RET:
	case COMMAND_TYPE::S2C_OPTIONS_NTFY:
	{
		if (bytes.size() < 5 || bytes[1] != S2C_INQUIRED_TYPE)
			break;
		size_t n = bytes.size();
		unsigned int sensitivity = byteAt(n - 3);
		unsigned int voice = byteAt(n - 2) ? 1 : 0;
		unsigned int offTime = byteAt(n - 1);
		{
			std::lock_guard guard(this->_propertyMtx);
			this->_s2cOptions.updateFromDevice((sensitivity << 16) | (voice << 8) | offTime);
		}
		this->_markLoaded(STATE_S2C_OPTIONS);
		this->_notifyStateChanged();
		break;
	}

	default:
		break;
	}
}

void Headphones::queryDeviceCapabilities()
{
	this->_conn.sendCommand({
		static_cast<char>(COMMAND_TYPE::CAPABILITY_QUERY),
		0x00 // CommonCapabilityInquiredType::FIXED_VALUE
	});
}

void Headphones::queryDevices()
{
	this->_conn.sendCommand({
			static_cast<char>(COMMAND_TYPE::MULTI_POINT_DEVICES_QUERY),
			0x01
		},
		DATA_TYPE::DATA_MDR_NO2
	);
}

void Headphones::queryDeviceInfo(DEVICE_INFO_TYPE type)
{
	this->_conn.sendCommand({ static_cast<char>(COMMAND_TYPE::DEVICE_INFO_GET), static_cast<char>(type) });
}

void Headphones::queryBattery()
{
	this->_conn.sendCommand({ static_cast<char>(COMMAND_TYPE::BATTERY_GET), static_cast<char>(BATTERY_TYPE::SINGLE) });
}

void Headphones::queryCodec()
{
	this->_conn.sendCommand({ static_cast<char>(COMMAND_TYPE::AUDIO_CODEC_GET), 0x00 });
}

void Headphones::queryUpscalingIndicator()
{
	this->_conn.sendCommand({ static_cast<char>(COMMAND_TYPE::UPSCALING_EFFECT_GET), 0x00 });
}

void Headphones::queryNcAsmCapability()
{
	this->_conn.sendCommand({
		static_cast<char>(COMMAND_TYPE::NCASM_GET_CAPABILITY),
		static_cast<char>(NC_ASM_INQUIRED_TYPE::NOISE_CANCELLING_AND_AMBIENT_SOUND_MODE)
	});
}

void Headphones::queryOptimizerStatus()
{
	this->_conn.sendCommand({
		static_cast<char>(COMMAND_TYPE::OPT_GET_STATUS),
		0x01 // OptimizerInquiredType::NC_OPTIMIZER
	});
}

void Headphones::queryNcAsm()
{
	this->_conn.sendCommand({
		static_cast<char>(COMMAND_TYPE::NCASM_GET_PARAM),
		static_cast<char>(NC_ASM_INQUIRED_TYPE::NOISE_CANCELLING_AND_AMBIENT_SOUND_MODE)
	});
}

void Headphones::queryVpt()
{
	this->_conn.sendCommand({
		static_cast<char>(COMMAND_TYPE::VPT_GET_PARAM),
		static_cast<char>(VPT_INQUIRED_TYPE::VPT)
	});
	this->_conn.sendCommand({
		static_cast<char>(COMMAND_TYPE::VPT_GET_PARAM),
		static_cast<char>(VPT_INQUIRED_TYPE::SOUND_POSITION)
	});
}

void Headphones::queryS2C()
{
	this->_conn.sendCommand({
		static_cast<char>(COMMAND_TYPE::S2C_QUERY),
		S2C_INQUIRED_TYPE
	});
}

void Headphones::queryS2COptions()
{
	this->_conn.sendCommand({
		static_cast<char>(COMMAND_TYPE::S2C_OPTIONS_QUERY),
		S2C_INQUIRED_TYPE
	});
}
