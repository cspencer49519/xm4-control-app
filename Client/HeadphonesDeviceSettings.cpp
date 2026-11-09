// Device settings: wearing detection, auto power off, custom button, connection
// mode, DSEE, voice guidance, device-described general settings, power off and
// confirmation alerts. Layouts are documented in docs/PROTOCOL.md.

#include "Headphones.h"

#include <algorithm>

namespace
{
	// SYSTEM_* inquired types
	constexpr char SYSTEM_CONTROL_BY_WEARING = 0x03;
	constexpr char SYSTEM_AUTO_POWER_OFF = 0x04;
	constexpr char SYSTEM_ASSIGNABLE_SETTINGS = 0x06;
	// AUDIO_* inquired types
	constexpr char AUDIO_CONNECTION_MODE = 0x01;
	constexpr char AUDIO_UPSCALING = 0x02;
	// Table 2 voice guidance
	constexpr char VOICE_GUIDANCE_SETTING = 0x01;
	constexpr char VG_ON_OFF = 0x01;
	// ALERT
	constexpr char ALERT_FIXED_MESSAGE = 0x01;
	constexpr char ALERT_ACTION_POSITIVE_NEGATIVE = 0x01;

	// FunctionType bytes that gate each setting
	constexpr unsigned char F_CONNECTION_MODE = 0xe1;
	constexpr unsigned char F_UPSCALING = 0xe2;
	constexpr unsigned char F_CONTROL_BY_WEARING = 0xf3;
	constexpr unsigned char F_AUTO_POWER_OFF = 0xf4;
	constexpr unsigned char F_ASSIGNABLE_SETTINGS = 0xf6;
	constexpr unsigned char F_VOICE_GUIDANCE = 0x39;
	constexpr unsigned char F_GENERAL_SETTING_1 = 0xd1;

	enum : unsigned char
	{
		SYSTEM_GET_CAPABILITY = 0xf0, SYSTEM_RET_CAPABILITY = 0xf1, SYSTEM_GET_STATUS = 0xf2,
		SYSTEM_RET_STATUS = 0xf3, SYSTEM_NTFY_STATUS = 0xf5, SYSTEM_GET_PARAM = 0xf6,
		SYSTEM_RET_PARAM = 0xf7, SYSTEM_SET_PARAM = 0xf8, SYSTEM_NTFY_PARAM = 0xf9,
		AUDIO_GET_CAPABILITY = 0xe0, AUDIO_RET_CAPABILITY = 0xe1, AUDIO_GET_STATUS = 0xe2,
		AUDIO_RET_STATUS = 0xe3, AUDIO_NTFY_STATUS = 0xe5, AUDIO_GET_PARAM = 0xe6,
		AUDIO_RET_PARAM = 0xe7, AUDIO_SET_PARAM = 0xe8, AUDIO_NTFY_PARAM = 0xe9,
		GS_GET_CAPABILITY = 0xd0, GS_RET_CAPABILITY = 0xd1, GS_GET_STATUS = 0xd2,
		GS_RET_STATUS = 0xd3, GS_NTFY_STATUS = 0xd5, GS_GET_PARAM = 0xd6,
		GS_RET_PARAM = 0xd7, GS_SET_PARAM = 0xd8, GS_NTFY_PARAM = 0xd9,
		ALERT_SET_STATUS = 0x94, ALERT_SET_PARAM = 0x98, ALERT_NTFY_PARAM = 0x99,
		POWER_OFF = 0x22,
		// table 2
		VG_GET_CAPABILITY = 0x40, VG_RET_CAPABILITY = 0x41, VG_GET_STATUS = 0x42,
		VG_RET_STATUS = 0x43, VG_NTFY_STATUS = 0x45, VG_GET_PARAM = 0x46,
		VG_RET_PARAM = 0x47, VG_SET_PARAM = 0x48, VG_NTFY_PARAM = 0x49,
	};

	constexpr unsigned char GS_TYPE_BOOLEAN = 0x01;
	constexpr unsigned char GS_TYPE_LIST = 0x02;
	constexpr unsigned char GS_FORMAT_ENUM_NAME = 0x02;

	SETTING generalSetting(int index)
	{
		return static_cast<SETTING>(static_cast<int>(SETTING::GENERAL_SETTING_1) + index);
	}

	// Reads one GENERAL_SETTING info block: format, len, name, len, description.
	bool readGsInfo(const Buffer& b, size_t& idx, GeneralSettingItem& out)
	{
		if (idx + 2 > b.size())
			return false;
		out.enumName = static_cast<unsigned char>(b[idx]) == GS_FORMAT_ENUM_NAME;
		size_t len = static_cast<unsigned char>(b[idx + 1]);
		idx += 2;
		if (idx + len + 1 > b.size())
			return false;
		out.name.assign(b.begin() + idx, b.begin() + idx + len);
		idx += len;
		size_t descLen = static_cast<unsigned char>(b[idx]);
		idx += 1;
		if (idx + descLen > b.size())
			return false;
		out.description.assign(b.begin() + idx, b.begin() + idx + descLen);
		idx += descLen;
		return true;
	}
}

// ---------------------------------------------------------------------------
// Public API

DeviceSettingsCapability Headphones::getSettingsCapability()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_settingsCapability;
}

std::optional<int> Headphones::getSetting(SETTING setting)
{
	std::lock_guard guard(this->_propertyMtx);
	auto it = this->_settings.find(setting);
	if (it == this->_settings.end())
		return std::nullopt;
	return it->second.desired;
}

void Headphones::setSetting(SETTING setting, int value)
{
	std::lock_guard guard(this->_propertyMtx);
	auto it = this->_settings.find(setting);
	// Only settings the device has reported can be changed.
	if (it != this->_settings.end())
		it->second.desired = value;
}

std::vector<unsigned char> Headphones::getAssignablePresets()
{
	std::lock_guard guard(this->_propertyMtx);
	return this->_assignablePresets.desired;
}

void Headphones::setAssignablePresets(const std::vector<unsigned char>& presets)
{
	std::lock_guard guard(this->_propertyMtx);
	if (presets.size() == this->_assignablePresets.current.size())
		this->_assignablePresets.desired = presets;
}

void Headphones::powerOff()
{
	this->_conn.sendCommand({ static_cast<char>(POWER_OFF), 0x00, 0x01 });
}

void Headphones::setAlertCallback(std::function<void(unsigned char, bool)> callback)
{
	std::lock_guard guard(this->_alertMtx);
	this->_alertCallback = std::move(callback);
}

void Headphones::answerAlert(unsigned char messageType, bool accept)
{
	this->_conn.sendCommand({ static_cast<char>(ALERT_SET_PARAM), ALERT_FIXED_MESSAGE,
		static_cast<char>(messageType), static_cast<char>(accept ? 0x01 : 0x00) });
}

// ---------------------------------------------------------------------------
// Sync / send

void Headphones::_resetDeviceSettings()
{
	this->_settingsCapability = DeviceSettingsCapability{};
	this->_settings.clear();
	this->_autoPowerOffSelectTime = AUTO_POWER_OFF_30_MIN;
	this->_assignablePresets.setState({});
}

void Headphones::_queryDeviceSettings()
{
	auto send = [this](std::initializer_list<unsigned char> bytes, DATA_TYPE type = DATA_TYPE::DATA_MDR) {
		Buffer b;
		for (unsigned char c : bytes) b.push_back(static_cast<char>(c));
		this->_conn.sendCommand(b, type);
	};

	// Ask the device to send confirmation alerts (e.g. before changes that disconnect it).
	send({ ALERT_SET_STATUS, ALERT_FIXED_MESSAGE, 0x00 });

	for (auto [function, type] : { std::pair{ F_CONTROL_BY_WEARING, SYSTEM_CONTROL_BY_WEARING },
		std::pair{ F_AUTO_POWER_OFF, SYSTEM_AUTO_POWER_OFF }, std::pair{ F_ASSIGNABLE_SETTINGS, SYSTEM_ASSIGNABLE_SETTINGS } })
	{
		if (!this->supportsFunction(function))
			continue;
		unsigned char t = static_cast<unsigned char>(type);
		send({ SYSTEM_GET_CAPABILITY, t });
		send({ SYSTEM_GET_STATUS, t });
		send({ SYSTEM_GET_PARAM, t });
	}
	for (auto [function, type] : { std::pair{ F_CONNECTION_MODE, AUDIO_CONNECTION_MODE }, std::pair{ F_UPSCALING, AUDIO_UPSCALING } })
	{
		if (!this->supportsFunction(function))
			continue;
		unsigned char t = static_cast<unsigned char>(type);
		send({ AUDIO_GET_CAPABILITY, t });
		send({ AUDIO_GET_STATUS, t });
		send({ AUDIO_GET_PARAM, t });
	}
	for (unsigned char gs = 0; gs < 3; gs++)
	{
		unsigned char type = F_GENERAL_SETTING_1 + gs; // GsInquiredType == FunctionType (0xd1..0xd3)
		if (!this->supportsFunction(type))
			continue;
		send({ GS_GET_CAPABILITY, type, DISPLAY_LANGUAGE_ENGLISH });
		send({ GS_GET_STATUS, type });
		send({ GS_GET_PARAM, type });
	}
	if (this->supportsFunction(F_VOICE_GUIDANCE))
	{
		send({ VG_GET_CAPABILITY, VOICE_GUIDANCE_SETTING }, DATA_TYPE::DATA_MDR_NO2);
		send({ VG_GET_STATUS, VOICE_GUIDANCE_SETTING, VG_ON_OFF }, DATA_TYPE::DATA_MDR_NO2);
		send({ VG_GET_PARAM, VOICE_GUIDANCE_SETTING, VG_ON_OFF }, DATA_TYPE::DATA_MDR_NO2);
	}
}

bool Headphones::_deviceSettingsChanged()
{
	for (auto& [setting, property] : this->_settings)
		if (!property.isFulfilled())
			return true;
	return !this->_assignablePresets.isFulfilled();
}

void Headphones::_sendDeviceSettingChanges()
{
	struct Pending { SETTING setting; int value; unsigned char gsType; unsigned char selectTime; };
	std::vector<Pending> pending;
	bool presetsPending;
	std::vector<unsigned char> presets;
	{
		std::lock_guard guard(this->_propertyMtx);
		for (auto& [setting, property] : this->_settings)
		{
			if (property.isFulfilled())
				continue;
			Pending p{ setting, property.desired, 0, this->_autoPowerOffSelectTime };
			if (setting >= SETTING::GENERAL_SETTING_1)
			{
				int index = static_cast<int>(setting) - static_cast<int>(SETTING::GENERAL_SETTING_1);
				p.gsType = this->_settingsCapability.general[index].isList ? GS_TYPE_LIST : GS_TYPE_BOOLEAN;
			}
			pending.push_back(p);
		}
		presetsPending = !this->_assignablePresets.isFulfilled();
		presets = this->_assignablePresets.desired;
	}

	for (const Pending& p : pending)
	{
		Buffer b;
		DATA_TYPE type = DATA_TYPE::DATA_MDR;
		char v = static_cast<char>(p.value);
		switch (p.setting)
		{
		case SETTING::PAUSE_WHEN_REMOVED:
			b = { static_cast<char>(SYSTEM_SET_PARAM), SYSTEM_CONTROL_BY_WEARING, 0x00, v };
			break;
		case SETTING::AUTO_POWER_OFF:
		{
			// [F8, 04, 01 (active + select-time), active element, select-time element]
			unsigned char selectTime = p.value <= AUTO_POWER_OFF_180_MIN ? static_cast<unsigned char>(p.value) : p.selectTime;
			b = { static_cast<char>(SYSTEM_SET_PARAM), SYSTEM_AUTO_POWER_OFF, 0x01, v, static_cast<char>(selectTime) };
			break;
		}
		case SETTING::CONNECTION_MODE:
			b = { static_cast<char>(AUDIO_SET_PARAM), AUDIO_CONNECTION_MODE, 0x00, v };
			break;
		case SETTING::UPSCALING:
			b = { static_cast<char>(AUDIO_SET_PARAM), AUDIO_UPSCALING, 0x00, v };
			break;
		case SETTING::VOICE_GUIDANCE:
			b = { static_cast<char>(VG_SET_PARAM), VOICE_GUIDANCE_SETTING, VG_ON_OFF, v };
			type = DATA_TYPE::DATA_MDR_NO2;
			break;
		case SETTING::GENERAL_SETTING_1:
		case SETTING::GENERAL_SETTING_2:
		case SETTING::GENERAL_SETTING_3:
		{
			char gsType = static_cast<char>(F_GENERAL_SETTING_1 + (static_cast<int>(p.setting) - static_cast<int>(SETTING::GENERAL_SETTING_1)));
			b = { static_cast<char>(GS_SET_PARAM), gsType, static_cast<char>(p.gsType), v };
			break;
		}
		}
		this->_conn.sendCommand(b, type);
		std::lock_guard guard(this->_propertyMtx);
		auto it = this->_settings.find(p.setting);
		if (it != this->_settings.end())
			it->second.markSent(p.value);
		if (p.setting == SETTING::AUTO_POWER_OFF && p.value <= AUTO_POWER_OFF_180_MIN)
			this->_autoPowerOffSelectTime = static_cast<unsigned char>(p.value);
	}

	if (presetsPending && !presets.empty())
	{
		Buffer b = { static_cast<char>(SYSTEM_SET_PARAM), SYSTEM_ASSIGNABLE_SETTINGS, static_cast<char>(presets.size()) };
		b.insert(b.end(), presets.begin(), presets.end());
		this->_conn.sendCommand(b);
		std::lock_guard guard(this->_propertyMtx);
		this->_assignablePresets.markSent(presets);
	}
}

// ---------------------------------------------------------------------------
// Replies

bool Headphones::_handleDeviceSettingsReply(const Buffer& bytes)
{
	auto at = [&bytes](size_t i) { return static_cast<unsigned char>(bytes[i]); };
	const unsigned char op = at(0);
	const size_t n = bytes.size();

	// Records a value reported by the device; creates the setting on first report.
	auto report = [this](SETTING setting, int value) {
		auto it = this->_settings.find(setting);
		if (it == this->_settings.end())
			this->_settings.emplace(setting, Property<int>{ value, value });
		else
			it->second.updateFromDevice(value);
	};
	// The caller notifies listeners once the lock is released.
	auto done = []() { return true; };

	switch (op)
	{
	case SYSTEM_RET_CAPABILITY:
	{
		if (n < 3)
			return false;
		std::lock_guard guard(this->_propertyMtx);
		if (bytes[1] == SYSTEM_CONTROL_BY_WEARING)
			this->_settingsCapability.pauseWhenRemoved = true;
		else if (bytes[1] == SYSTEM_AUTO_POWER_OFF)
		{
			size_t count = std::min<size_t>(at(2), n - 3);
			this->_settingsCapability.autoPowerOffElements.assign(bytes.begin() + 3, bytes.begin() + 3 + count);
		}
		else if (bytes[1] == SYSTEM_ASSIGNABLE_SETTINGS)
		{
			// [F1, 06, keyCount, keyCount x (key, keyType, defaultPreset, presetCount,
			//   presetCount x (preset, actionCount, actionCount x (action, function)))]
			std::vector<AssignableKey> keys;
			size_t idx = 3;
			for (size_t k = 0; k < at(2) && idx + 4 <= n; k++)
			{
				AssignableKey key{ at(idx), at(idx + 1), at(idx + 2), {} };
				size_t presetCount = at(idx + 3);
				idx += 4;
				for (size_t p = 0; p < presetCount && idx + 2 <= n; p++)
				{
					key.presets.push_back(at(idx));
					size_t actionCount = at(idx + 1);
					idx += 2 + 2 * actionCount;
				}
				if (idx > n)
					break; // ran past the end
				keys.push_back(std::move(key));
			}
			if (keys.size() != at(2))
				return true; // malformed; keep what we had
			this->_settingsCapability.assignableKeys = std::move(keys);
		}
		else
			return false;
		return done();
	}
	case SYSTEM_RET_STATUS:
	case SYSTEM_NTFY_STATUS:
	{
		if (n < 3)
			return false;
		std::lock_guard guard(this->_propertyMtx);
		if (bytes[1] == SYSTEM_CONTROL_BY_WEARING)
			this->_settingsCapability.enabled[SETTING::PAUSE_WHEN_REMOVED] = at(2) == 0x00;
		else if (bytes[1] == SYSTEM_AUTO_POWER_OFF)
			this->_settingsCapability.enabled[SETTING::AUTO_POWER_OFF] = at(2) == 0x00;
		else if (bytes[1] == SYSTEM_ASSIGNABLE_SETTINGS)
		{
			// [op, 06, n, status x n]; usable if any key is enabled
			bool any = false;
			for (size_t i = 3; i < n && i < 3 + static_cast<size_t>(at(2)); i++)
				any = any || at(i) == 0x00;
			this->_settingsCapability.assignableEnabled = any;
		}
		else
			return false; // e.g. Speak-to-Chat status, handled elsewhere
		return done();
	}
	case SYSTEM_RET_PARAM:
	case SYSTEM_NTFY_PARAM:
	{
		if (n < 3)
			return false;
		std::lock_guard guard(this->_propertyMtx);
		if (bytes[1] == SYSTEM_CONTROL_BY_WEARING && n >= 4)
			report(SETTING::PAUSE_WHEN_REMOVED, at(3) == 0x01 ? 1 : 0);
		else if (bytes[1] == SYSTEM_AUTO_POWER_OFF && n >= 5)
		{
			report(SETTING::AUTO_POWER_OFF, at(3));
			if (at(4) <= AUTO_POWER_OFF_180_MIN)
				this->_autoPowerOffSelectTime = at(4);
		}
		else if (bytes[1] == SYSTEM_ASSIGNABLE_SETTINGS)
		{
			size_t count = std::min<size_t>(at(2), n - 3);
			std::vector<unsigned char> presets(bytes.begin() + 3, bytes.begin() + 3 + count);
			if (this->_assignablePresets.current.empty())
				this->_assignablePresets.setState(presets);
			else
				this->_assignablePresets.updateFromDevice(presets);
		}
		else
			return false; // Speak-to-Chat (0x05) is handled by the main dispatcher
		return done();
	}

	case AUDIO_RET_CAPABILITY:
	{
		if (n < 3)
			return false;
		std::lock_guard guard(this->_propertyMtx);
		if (bytes[1] == AUDIO_CONNECTION_MODE)
			this->_settingsCapability.connectionMode = true;
		else if (bytes[1] == AUDIO_UPSCALING)
		{
			this->_settingsCapability.upscaling = true;
			this->_settingsCapability.upscalingType = static_cast<UPSCALING_TYPE>(at(2));
		}
		return done();
	}
	case AUDIO_RET_STATUS:
	case AUDIO_NTFY_STATUS:
	{
		if (n < 3)
			return false;
		std::lock_guard guard(this->_propertyMtx);
		SETTING s = bytes[1] == AUDIO_CONNECTION_MODE ? SETTING::CONNECTION_MODE : SETTING::UPSCALING;
		this->_settingsCapability.enabled[s] = at(2) == 0x00;
		return done();
	}
	case AUDIO_RET_PARAM:
	case AUDIO_NTFY_PARAM:
	{
		if (n < 4)
			return false;
		std::lock_guard guard(this->_propertyMtx);
		if (bytes[1] == AUDIO_CONNECTION_MODE)
			report(SETTING::CONNECTION_MODE, at(3));
		else if (bytes[1] == AUDIO_UPSCALING)
			report(SETTING::UPSCALING, at(3));
		return done();
	}

	case GS_RET_CAPABILITY:
	{
		// [D1, gsType, titleInfo, settingType, (if list) count, count x itemInfo]
		if (n < 3 || at(1) < F_GENERAL_SETTING_1 || at(1) > F_GENERAL_SETTING_1 + 2)
			return false;
		GeneralSettingCapability gs;
		size_t idx = 2;
		if (!readGsInfo(bytes, idx, gs.title) || idx >= n)
			return true;
		unsigned char settingType = at(idx++);
		gs.isList = settingType == GS_TYPE_LIST;
		if (gs.isList)
		{
			if (idx >= n)
				return true;
			size_t count = at(idx++);
			for (size_t i = 0; i < count; i++)
			{
				GeneralSettingItem item;
				if (!readGsInfo(bytes, idx, item))
					break;
				gs.items.push_back(std::move(item));
			}
			if (gs.items.empty())
				return true;
		}
		else if (settingType != GS_TYPE_BOOLEAN)
			return true;
		gs.available = true;
		{
			std::lock_guard guard(this->_propertyMtx);
			this->_settingsCapability.general[at(1) - F_GENERAL_SETTING_1] = std::move(gs);
		}
		return done();
	}
	case GS_RET_STATUS:
	case GS_NTFY_STATUS:
	{
		if (n < 3 || at(1) < F_GENERAL_SETTING_1 || at(1) > F_GENERAL_SETTING_1 + 2)
			return false;
		std::lock_guard guard(this->_propertyMtx);
		this->_settingsCapability.enabled[generalSetting(at(1) - F_GENERAL_SETTING_1)] = at(2) == 0x00;
		return done();
	}
	case GS_RET_PARAM:
	case GS_NTFY_PARAM:
	{
		if (n < 4 || at(1) < F_GENERAL_SETTING_1 || at(1) > F_GENERAL_SETTING_1 + 2)
			return false;
		std::lock_guard guard(this->_propertyMtx);
		report(generalSetting(at(1) - F_GENERAL_SETTING_1), at(3));
		return done();
	}

	case ALERT_NTFY_PARAM:
	{
		// [99, 01, messageType, actionType]
		if (n < 4 || bytes[1] != ALERT_FIXED_MESSAGE)
			return false;
		std::function<void(unsigned char, bool)> callback;
		{
			std::lock_guard guard(this->_alertMtx);
			callback = this->_alertCallback;
		}
		// The answer must not be sent from here: this runs on the listener thread, and
		// sendCommand() waits for an ACK that only this thread can deliver.
		if (callback)
			callback(at(2), at(3) == ALERT_ACTION_POSITIVE_NEGATIVE);
		return true;
	}
	default:
		return false;
	}
}

void Headphones::_handleTable2Reply(const Buffer& bytes)
{
	auto at = [&bytes](size_t i) { return static_cast<unsigned char>(bytes[i]); };
	const size_t n = bytes.size();
	if (n < 4 || bytes[1] != VOICE_GUIDANCE_SETTING)
		return;
	{
		std::lock_guard guard(this->_propertyMtx);
		switch (at(0))
		{
		case VG_RET_CAPABILITY:
			// [41, 01, onOffSupported, languageSwitchSupported, ...]
			if (at(2) != 0x01)
				return;
			this->_settingsCapability.voiceGuidance = true;
			break;
		case VG_RET_STATUS:
		case VG_NTFY_STATUS:
			// [43|45, 01, statusType, CommonStatus]
			if (bytes[2] != VG_ON_OFF)
				return;
			this->_settingsCapability.enabled[SETTING::VOICE_GUIDANCE] = at(3) == 0x00;
			break;
		case VG_RET_PARAM:
		case VG_NTFY_PARAM:
		{
			// [47|49, 01, 01 (on/off), value]
			if (bytes[2] != VG_ON_OFF)
				return;
			int value = at(3) == 0x01 ? 1 : 0;
			auto it = this->_settings.find(SETTING::VOICE_GUIDANCE);
			if (it == this->_settings.end())
				this->_settings.emplace(SETTING::VOICE_GUIDANCE, Property<int>{ value, value });
			else
				it->second.updateFromDevice(value);
			break;
		}
		default:
			return;
		}
	}
	this->_notifyStateChanged();
}
