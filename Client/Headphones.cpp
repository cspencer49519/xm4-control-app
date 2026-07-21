#include "Headphones.h"
#include "CommandSerializer.h"

#include <stdexcept>

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
	std::string deviceMac = this->_savedDevices[deviceIdx].mac;
	this->_conn.sendCommand(CommandSerializer::serializeMultiPointCommand(
			MULTI_POINT_COMMANDS::DISCONNECT,
			deviceMac
		),
		DATA_TYPE::DATA_MDR_NO2
	);
}

inline void Headphones::connect(int deviceIdx)
{
	std::string deviceMac = this->_savedDevices[deviceIdx].mac;
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
		this->_dev2.isFulfilled()
		);
}

// At most one instance of this function is invoked at any time
void Headphones::setChanges()
{
	if (!(this->_optimizerState.isFulfilled()))
	{
		OPTIMIZER_STATE state;
		{
			std::lock_guard guard(this->_propertyMtx);
			state = this->_optimizerState.desired;
		}

		this->_conn.sendCommand(CommandSerializer::serializeXM4OptimizeCommand(
			state
		));

		std::lock_guard guard(this->_propertyMtx);
		this->_optimizerState.fulfill();
	}

	if (!(this->_speakToChat.isFulfilled()))
	{
		S2C_TOGGLE s2cState;
		{
			std::lock_guard guard(this->_propertyMtx);
			s2cState = this->_speakToChat.desired;
		}

		this->_conn.sendCommand(CommandSerializer::serializeXM4SpeakToChat(
			s2cState
		));

		std::lock_guard guard(this->_propertyMtx);
		this->_speakToChat.fulfill();
	}

	if (!(this->_s2cOptions.isFulfilled()))
	{
		unsigned int s2cOptions;
		{
			std::lock_guard guard(this->_propertyMtx);
			s2cOptions = this->_s2cOptions.desired;
		}
		unsigned char sensitivity = (unsigned char) (s2cOptions >> 16) & 0xff;
		unsigned char voice = (unsigned char) (s2cOptions >> 8) & 0xff;
		unsigned char offTime = (unsigned char) (s2cOptions) & 0xff;

		this->_conn.sendCommand(CommandSerializer::serializeXM4_S2C_Options(
			sensitivity,
			voice,
			offTime
		));

		std::lock_guard guard(this->_propertyMtx);
		this->_s2cOptions.fulfill();
	}

	if (!(this->_ambientSoundControl.isFulfilled() && this->_focusOnVoice.isFulfilled() && this->_asmLevel.isFulfilled()))
	{
		NC_ASM_EFFECT ncAsmEffect;
		ASM_ID asmId;
		int asmLevel;
		{
			std::lock_guard guard(this->_propertyMtx);
			ncAsmEffect = this->_ambientSoundControl.desired ? NC_ASM_EFFECT::ADJUSTMENT_COMPLETION : NC_ASM_EFFECT::OFF;
			asmId = this->_focusOnVoice.desired ? ASM_ID::VOICE : ASM_ID::NORMAL;
			asmLevel = this->_ambientSoundControl.desired ? this->_asmLevel.desired : ASM_LEVEL_DISABLED;
		}
		auto packet =
    		CommandSerializer::serializeNcAndAsmSetting(
	        ncAsmEffect,
	        NC_ASM_SETTING_TYPE::LEVEL_ADJUSTMENT,
	        ASM_SETTING_TYPE::LEVEL_ADJUSTMENT,
	        asmId,
	        asmLevel
		);

		this->_conn.sendCommand(packet);
		std::lock_guard guard(this->_propertyMtx);
		this->_ambientSoundControl.fulfill();
		this->_asmLevel.fulfill();
		this->_focusOnVoice.fulfill();
	}

	if (!(this->_vptType.isFulfilled() && this->_surroundPosition.isFulfilled())) {
		VPT_INQUIRED_TYPE command;
		unsigned char preset;
		{
			std::lock_guard guard(this->_propertyMtx);
			if (this->_vptType.desired != 0) {
				command = VPT_INQUIRED_TYPE::VPT;
				preset = static_cast<unsigned char>(this->_vptType.desired);
			}
			else if (this->_surroundPosition.desired != SOUND_POSITION_PRESET::OFF) {
				command = VPT_INQUIRED_TYPE::SOUND_POSITION;
				preset = static_cast<unsigned char>(this->_surroundPosition.desired);
			}
			else {
				if (this->_surroundPosition.current != SOUND_POSITION_PRESET::OFF) {
					command = VPT_INQUIRED_TYPE::SOUND_POSITION;
					preset = static_cast<unsigned char>(SOUND_POSITION_PRESET::OFF);
				}
				else if (this->_vptType.current != 0) {
					command = VPT_INQUIRED_TYPE::VPT;
					preset = 0;
				}
				else {
					throw std::logic_error("it's impossible that both values were changed to zero and were also previously zero");
				}
			}
		}
		auto packet =
			CommandSerializer::serializeVPTSetting(
				command,
				preset
		);

		this->_conn.sendCommand(packet);

		std::lock_guard guard(this->_propertyMtx);
		this->_vptType.fulfill();
		this->_surroundPosition.fulfill();
	}
	
	if (!(this->_dev1.isFulfilled()))
	{
		int dev1Current, dev1Desired;
		{
			std::lock_guard guard(this->_propertyMtx);
			dev1Current = this->_dev1.current;
			dev1Desired = this->_dev1.desired;
		}
		if (dev1Current != 0)
			this->disconnect(dev1Current);
		if (dev1Desired != 0)
			this->connect(dev1Desired);

		std::lock_guard guard(this->_propertyMtx);
		this->_dev1.desired = this->_dev1.current;
	}

	if (!(this->_dev2.isFulfilled()))
	{
		int dev2Current, dev2Desired;
		{
			std::lock_guard guard(this->_propertyMtx);
			dev2Current = this->_dev2.current;
			dev2Desired = this->_dev2.desired;
		}
		if (dev2Current != 0)
			this->disconnect(dev2Current);
		if (dev2Desired != 0)
			this->connect(dev2Desired);

		std::lock_guard guard(this->_propertyMtx);
		this->_dev2.desired = this->_dev2.current;
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

void Headphones::setStateFromReply(BtMessage replyMessage)
{
	Buffer bytes = replyMessage.messageBytes;
	COMMAND_TYPE cmd = static_cast<COMMAND_TYPE>(bytes[0]);

	switch (cmd)
	{
	case COMMAND_TYPE::DEVICES_QUERY_RESPONSE:
	case COMMAND_TYPE::DEVICES_STATE_RESPONSE:
	{
		if (bytes[1] != 0x01)
			// Wrong query type, break
			break;

		std::vector<BluetoothDevice> savedDevices = std::vector<BluetoothDevice>({BluetoothDevice("","")});
		int dev1 = 0, dev2 = 0;
		int idx = 3;
		int numDevices = static_cast<unsigned char>(bytes[2]);
		for (; numDevices > 0; numDevices--)
		{
			std::string mac_addr = "";
			for (int i = idx; i<idx+MAC_ADDR_STR_SIZE; i++)
			{
				mac_addr += bytes[i];
			}

			idx += MAC_ADDR_STR_SIZE;

			unsigned char number = static_cast<unsigned char>(bytes[idx]);

			idx++;
			unsigned char name_length = static_cast<unsigned char>(bytes[idx]);
			idx++;
			std::string name = "";
			for (int i = idx; i<idx+name_length; i++)
			{
				name += bytes[i];
			}

			idx += name_length;

			BluetoothDevice dev = {name, mac_addr};
			savedDevices.push_back(dev);
			if (number == 0x01)
			{
				dev1 = savedDevices.size()-1;
			}
			if (number == 0x02)
			{
				dev2 = savedDevices.size()-1;
			}
		}

		{
			std::lock_guard guard(this->_propertyMtx);
			this->_savedDevices = std::move(savedDevices);
			this->_dev1.setState(dev1);
			this->_dev2.setState(dev2);
		}
		break;
	}
	
	case COMMAND_TYPE::XM4_OPTIMIZER_RESPONSE:
	{
		if (bytes[2] != 0x00)
			this->_optimizerState.setState(OPTIMIZER_STATE::OPTIMIZING);
		else
			this->_optimizerState.setState(OPTIMIZER_STATE::IDLE);
		break;
	}

	case COMMAND_TYPE::CAPABILITY_QUERY_RESPONSE:
	{
		for (char c: bytes){
			switch (c)
			{
			case static_cast<char>(FUNCTION_TYPE::DEVICE_MANAGEMENT):
				this->_capabilities |= DEVICE_CAPABILITIES::MULTI_POINT;
				break;
			
			case static_cast<char>(FUNCTION_TYPE::VPT):
				this->_capabilities |= DEVICE_CAPABILITIES::VPT;
				break;
			
			case static_cast<char>(FUNCTION_TYPE::OPTIMIZER):
				this->_capabilities |= DEVICE_CAPABILITIES::OPTIMIZER;
				break;
			
			case static_cast<char>(FUNCTION_TYPE::NC_ASM):
				this->_capabilities |= DEVICE_CAPABILITIES::NC_ASM;
				break;
			
			case static_cast<char>(FUNCTION_TYPE::SMART_TALKING_MODE):
				this->_capabilities |= DEVICE_CAPABILITIES::SPEAK_TO_CHAT;
				break;
			}
		}
		
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
		0x01
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

void Headphones::queryS2C()
{
	this->_conn.sendCommand({
		static_cast<char>(COMMAND_TYPE::S2C_QUERY),
		0x01
	});
}

void Headphones::queryS2COptions()
{
	this->_conn.sendCommand({
		static_cast<char>(COMMAND_TYPE::S2C_OPTIONS_QUERY),
		0x01
	});
}