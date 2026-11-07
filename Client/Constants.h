#pragma once

#include <vector>

inline constexpr auto MAX_BLUETOOTH_MESSAGE_SIZE = 2048;
inline constexpr char START_MARKER{ 62 };
inline constexpr char END_MARKER{ 60 };

inline constexpr auto MAC_ADDR_STR_SIZE = 17;

inline constexpr auto SERVICE_UUID = "96CC203E-5068-46ad-B32D-E316F5E069BA";
inline unsigned char SERVICE_UUID_IN_BYTES[] = { // this is the SERVICE_UUID but in bytes
	0x96, 0xcc, 0x20, 0x3e, 0x50, 0x68, 0x46, 0xad,
	0xb3, 0x2d, 0xe3, 0x16, 0xf5, 0xe0, 0x69, 0xba
};

#define APP_NAME "Sony Headphones App v" __HEADPHONES_APP_VERSION__
#define APP_NAME_W (L"" APP_NAME)
inline constexpr int GUI_WIDTH = 900;
inline constexpr int GUI_HEIGHT = 720;
using Buffer = std::vector<char>;

enum class DATA_TYPE : signed char
{
	DATA = 0,
	ACK = 1,
    DATA_MC_NO1 = 2,
    DATA_ICD = 9,
    DATA_EV = 10,
	DATA_MDR = 12,
    DATA_COMMON = 13,
    DATA_MDR_NO2 = 14,
    SHOT =  16,
    SHOT_MC_NO1 =  18,
    SHOT_ICD =  25,
    SHOT_EV =  26,
    SHOT_MDR =  28,
    SHOT_COMMON =  29,
    SHOT_MDR_NO2 = 30,
    LARGE_DATA_COMMON =  45,
    UNKNOWN = -1
};


enum class NC_ASM_INQUIRED_TYPE : signed char
{
	NO_USE = 0,
	NOISE_CANCELLING = 1,
	NOISE_CANCELLING_AND_AMBIENT_SOUND_MODE = 2,
	AMBIENT_SOUND_MODE = 3
};

enum class NC_ASM_EFFECT : signed char
{
	OFF = 0,
	ON = 1,
	ADJUSTMENT_IN_PROGRESS = 16,
	ADJUSTMENT_COMPLETION = 17
};

enum class NC_ASM_SETTING_TYPE : signed char
{
	ON_OFF = 0,
	LEVEL_ADJUSTMENT = 1,
	DUAL_SINGLE_OFF = 2
};

enum class ASM_SETTING_TYPE : signed char
{
	ON_OFF = 0,
	LEVEL_ADJUSTMENT = 1
};

enum class ASM_ID : signed char
{
	NORMAL = 0,
	VOICE = 1
};

enum class NC_DUAL_SINGLE_VALUE : signed char
{
	OFF = 0,
	SINGLE = 1,
	DUAL = 2
};

enum class COMMAND_TYPE : signed char
{
	VPT_SET_PARAM = 72,
	NCASM_SET_PARAM = 104,
	XM4_OPTIMIZER_PARAM = (signed char) 0x84,
	XM4_S2C_TOGGLE_PARAM = (signed char) 0xf8,
	XM4_S2C_OPTIONS_PARAM = (signed char) 0xfc,
	MULTI_POINT_PARAM = (signed char) 0x3c,

	XM4_OPTIMIZER_RESPONSE = (signed char) 0x85, // OPT_NTFY_STATUS
	OPT_GET_STATUS = (signed char) 0x82,
	OPT_RET_STATUS = (signed char) 0x83,
	DEVICES_QUERY_RESPONSE = (signed char) 0x37,
	DEVICES_STATE_RESPONSE = (signed char) 0x39,
	CAPABILITY_QUERY_RESPONSE = (signed char) 0x07,

	CAPABILITY_QUERY = (signed char) 0x06,
	MULTI_POINT_DEVICES_QUERY = (signed char) 0x36,
	S2C_QUERY = (signed char) 0xf6,
	S2C_OPTIONS_QUERY = (signed char) 0xfa,

	// State reads. Sony's MDR protocol groups opcodes as
	// GET = SET - 2, RET (reply to GET) = SET - 1, NTFY (pushed by the device) = SET + 1.
	DEVICE_INFO_GET = (signed char) 0x04,
	DEVICE_INFO_RET = (signed char) 0x05,
	BATTERY_GET = (signed char) 0x10,
	BATTERY_RET = (signed char) 0x11,
	BATTERY_NTFY = (signed char) 0x13,
	UPSCALING_EFFECT_GET = (signed char) 0x14,
	UPSCALING_EFFECT_RET = (signed char) 0x15,
	UPSCALING_EFFECT_NTFY = (signed char) 0x17,
	AUDIO_CODEC_GET = (signed char) 0x18,
	AUDIO_CODEC_RET = (signed char) 0x19,
	AUDIO_CODEC_NTFY = (signed char) 0x1b,

	EQ_GET_CAPABILITY = (signed char) 0x50,
	EQ_RET_CAPABILITY = (signed char) 0x51,
	EQ_GET_STATUS = (signed char) 0x52,
	EQ_RET_STATUS = (signed char) 0x53,
	EQ_NTFY_STATUS = (signed char) 0x55,
	EQ_GET_PARAM = (signed char) 0x56,
	EQ_RET_PARAM = (signed char) 0x57,
	EQ_SET_PARAM = (signed char) 0x58,
	EQ_NTFY_PARAM = (signed char) 0x59,
	EQ_GET_EXTENDED_INFO = (signed char) 0x5a,
	EQ_RET_EXTENDED_INFO = (signed char) 0x5b,

	NCASM_GET_CAPABILITY = (signed char) 0x60,
	NCASM_RET_CAPABILITY = (signed char) 0x61,
	NCASM_GET_PARAM = (signed char) 0x66,
	NCASM_RET_PARAM = (signed char) 0x67,
	NCASM_NTFY_PARAM = (signed char) 0x69,
	VPT_GET_PARAM = (signed char) 0x46,
	VPT_RET_PARAM = (signed char) 0x47,
	VPT_NTFY_PARAM = (signed char) 0x49,
	S2C_TOGGLE_RET = (signed char) 0xf7,
	S2C_TOGGLE_NTFY = (signed char) 0xf9,
	S2C_OPTIONS_RET = (signed char) 0xfb,
	S2C_OPTIONS_NTFY = (signed char) 0xfd
};

// FunctionType bytes from the supported-function reply (see docs/PROTOCOL.md).
namespace FUNCTION
{
	inline constexpr unsigned char BATTERY_LEVEL = 0x11;
	inline constexpr unsigned char UPSCALING_INDICATOR = 0x12;
	inline constexpr unsigned char CODEC_INDICATOR = 0x13;
	inline constexpr unsigned char LEFT_RIGHT_BATTERY_LEVEL = 0x15;
	inline constexpr unsigned char CRADLE_BATTERY_LEVEL = 0x18;
	inline constexpr unsigned char POWER_OFF = 0x21;
	inline constexpr unsigned char PRESET_EQ = 0x51;
	inline constexpr unsigned char PRESET_EQ_NONCUSTOMIZABLE = 0x53;
}

// EqEbbInquiredType
inline constexpr unsigned char EQ_TYPE_PRESET_EQ = 0x01;
inline constexpr unsigned char EQ_TYPE_PRESET_EQ_NONCUSTOMIZABLE = 0x03;
// EqPresetId values with special meaning
inline constexpr unsigned char EQ_PRESET_OFF = 0x00;
inline constexpr unsigned char EQ_PRESET_MANUAL = 0xa0;
inline constexpr unsigned char EQ_PRESET_UNSPECIFIED = 0xff; // SET: "just set the band levels"
// DisplayLanguage sent with EQ_GET_CAPABILITY (language of the preset names)
inline constexpr unsigned char DISPLAY_LANGUAGE_ENGLISH = 0x01;

enum class DEVICE_INFO_TYPE : unsigned char
{
	MODEL_NAME = 0x01,
	FW_VERSION = 0x02,
	SERIES_AND_COLOR = 0x03,
};

enum class BATTERY_TYPE : unsigned char
{
	SINGLE = 0x00,
	LEFT_RIGHT = 0x01,
	CRADLE = 0x02,
};

enum class AUDIO_CODEC : unsigned char
{
	UNSETTLED = 0x00,
	SBC = 0x01,
	AAC = 0x02,
	LDAC = 0x10,
	APTX = 0x20,
	APTX_HD = 0x21,
	OTHER = 0xff,
};

enum class UPSCALING_TYPE : unsigned char
{
	DSEE_HX = 0x00,
	DSEE = 0x01,
	DSEE_EXTREME = 0x02, // "DSEE_HX_AI" in the protocol
};

// Sub-type byte used by the XM4 Speak-to-Chat get/set commands.
inline constexpr char S2C_INQUIRED_TYPE = 0x05;
// Byte 2 of S2C SET/NTFY param: which switch the value belongs to.
inline constexpr char S2C_PARAM_MODE_ON_OFF = 0x01;
inline constexpr char S2C_PARAM_PREVIEW_ON_OFF = 0x02;

// Optimizer status (byte 3 of OPT_RET/NTFY_STATUS).
inline constexpr unsigned char OPTIMIZER_STATUS_IDLE = 0x00;
inline constexpr unsigned char OPTIMIZER_STATUS_MEASURING_PERSONAL = 0x01;
inline constexpr unsigned char OPTIMIZER_STATUS_MEASURING_PRESSURE = 0x02;
inline constexpr unsigned char OPTIMIZER_STATUS_OPTIMIZING = 0x10;
inline constexpr unsigned char OPTIMIZER_STATUS_END = 0x11;

// Defaults used until the headphones report their NC/ASM capability (WH-1000XM4 values).
inline constexpr unsigned char DEFAULT_NC_ASM_SETTING_TYPE = 0x02; // NC_ASM_SETTING_TYPE::DUAL_SINGLE_OFF
inline constexpr unsigned char DEFAULT_ASM_SETTING_TYPE = 0x01;    // ASM_SETTING_TYPE::LEVEL_ADJUSTMENT
inline constexpr int DEFAULT_AMBIENT_STEPS = 20;

enum class VPT_PRESET_ID : signed char
{
	OFF = 0,
	OUTDOOR_FESTIVAL = 1,
	ARENA = 2,
	CONCERT_HALL = 3,
	CLUB = 4
	//Note: Sony reserved 5~15 "for future"
};

enum class SOUND_POSITION_PRESET : signed char
{
	OFF = 0,
	FRONT_LEFT = 1,
	FRONT_RIGHT = 2,
	FRONT = 3,
	REAR_LEFT = 17,
	REAR_RIGHT = 18,
	OUT_OF_RANGE = -1
};

//Needed for converting the ImGui Combo index into the VPT index.
inline const SOUND_POSITION_PRESET SOUND_POSITION_PRESET_ARRAY[] = {
	SOUND_POSITION_PRESET::OFF,
	SOUND_POSITION_PRESET::FRONT_LEFT,
	SOUND_POSITION_PRESET::FRONT_RIGHT,
	SOUND_POSITION_PRESET::FRONT,
	SOUND_POSITION_PRESET::REAR_LEFT,
	SOUND_POSITION_PRESET::REAR_RIGHT,
	SOUND_POSITION_PRESET::OUT_OF_RANGE
};

enum class VPT_INQUIRED_TYPE : signed char
{
	NO_USE = 0,
	VPT = 1,
	SOUND_POSITION = 2,
	OUT_OF_RANGE = -1
};

enum class OPTIMIZER_STATE : signed char
{
	IDLE = 0,
	OPTIMIZING = 1
};

enum class S2C_TOGGLE : signed char
{
	ACTIVE = 1,
	INACTIVE = 0
};

enum class MULTI_POINT_COMMANDS	: signed char
{
	CONNECT = (signed char) 0x01,
	DISCONNECT = (signed char) 0x00,
	UNPAIR = (signed char) 0x02
};

enum DEVICE_CAPABILITIES
{
	NC_ASM = 0x01 << 0,
	VPT = 0x01 << 1,
	MULTI_POINT = 0x01 << 2,
	OPTIMIZER = 0x01 << 3,
	SPEAK_TO_CHAT = 0x01 << 4,

};

enum class FUNCTION_TYPE : signed char
{
	DEVICE_MANAGEMENT = 56,
	VPT = 65,
	OPTIMIZER = -127,
	NC_ASM = 98,
	SMART_TALKING_MODE = -11
};