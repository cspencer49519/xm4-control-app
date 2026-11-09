// End-to-end test of the protocol core: a fake WH-1000XM4 speaking the real wire format
// <-> BluetoothWrapper <-> Listener <-> Headphones. Run with tests/run.sh.
#include "Headphones.h"
#include "Listener.h"
#include "CommandSerializer.h"

#include <cassert>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <thread>
#include <algorithm>

static Buffer operator+(Buffer a, const Buffer& b) { a.insert(a.end(), b.begin(), b.end()); return a; }

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } else { std::printf("ok   %s\n", #cond); } } while (0)

class FakeHeadset : public IBluetoothConnector
{
public:
	// device state
	unsigned char ncEffect = 1, ncSettingType = 2, dualSingle = 0, asmId = 1, asmLevel = 12; // ambient on, wire level 12 of 20, voice
	unsigned char optStatus = 0;
	unsigned char eqPreset = 0x10; // Bright
	std::vector<unsigned char> eqBands = {10, 12, 11, 10, 13, 14};
	std::vector<std::string> eqSets;
	// device settings
	unsigned char pauseWhenRemoved = 1, apoActive = 0x11, apoSelect = 0x01, connMode = 0, dsee = 1, voiceGuidance = 1;
	unsigned char touchPanel = 1, gsList = 1, customKeyPreset = 0x00;
	std::vector<std::string> settingSets; // every settings SET, as hex
	std::vector<std::string> alertAnswers;
	bool poweredOff = false;
	std::vector<std::string> received;
	unsigned char vpt = 0, position = 3; // FRONT
	unsigned char s2c = 1, sens = 2, voice = 1, offTime = 3;
	bool respondToNc = true;
	std::vector<std::string> sentLog;

	int send(char* buf, size_t length) override
	{
		Buffer raw(buf, buf + length);
		// strip markers
		Buffer inner(raw.begin() + 1, raw.end() - 1);
		BtMessage m = CommandSerializer::unpackBtMessage(inner);
		if (m.dataType == DATA_TYPE::ACK)
			return (int)length;
		push(DATA_TYPE::ACK, {}, m.seqNumber ^ 1);
		handle(m);
		return (int)length;
	}

	int recv(char* buf, size_t length) override
	{
		std::unique_lock lk(mtx);
		cv.wait(lk, [&] { return !bytes.empty() || !connected; });
		if (bytes.empty()) return 0;
		size_t n = 0;
		while (n < length && !bytes.empty()) { buf[n++] = bytes.front(); bytes.pop_front(); }
		return (int)n;
	}

	void connect(const std::string&) override { connected = true; }
	void disconnect() noexcept override { { std::lock_guard lk(mtx); connected = false; } cv.notify_all(); }
	bool isConnected() noexcept override { return connected; }
	std::vector<BluetoothDevice> getConnectedDevices() override { return {}; }

	// Simulate the user pressing the NC button on the headphones.
	void pressNcButtonTurnOff()
	{
		ncEffect = 0;
		notifyNc();
	}
	void sendRaw(DATA_TYPE t, Buffer payload) { push(t, payload, seq++); }

private:
	void notifyEq(char op) {
		Buffer p = {op, 0x01, (char)eqPreset, (char)eqBands.size()};
		p.insert(p.end(), eqBands.begin(), eqBands.end());
		push(DATA_TYPE::DATA_MDR, p, seq++);
	}
	void notifyNc() { push(DATA_TYPE::DATA_MDR, {(char)0x69, 0x02, (char)ncEffect, (char)ncSettingType, (char)dualSingle, 0x01, (char)asmId, (char)asmLevel}, seq++); }

	static std::string hex(const Buffer& b)
	{
		std::string out;
		for (char c : b) { char h[4]; std::snprintf(h, sizeof h, "%02x ", (unsigned char)c); out += h; }
		return out;
	}
	static Buffer withString(Buffer head, const std::string& str)
	{
		head.push_back((char)str.size());
		head.insert(head.end(), str.begin(), str.end());
		return head;
	}

	void handleTable2(const BtMessage& m)
	{
		const Buffer& b = m.messageBytes;
		auto mdr2 = [&](Buffer p) { push(DATA_TYPE::DATA_MDR_NO2, p, seq++); };
		switch ((unsigned char)b[0])
		{
		case 0x36: {
			Buffer p = {0x37, 0x01, 0x02};
			std::string macs[2] = {"AA:BB:CC:DD:EE:01", "AA:BB:CC:DD:EE:02"};
			std::string names[2] = {"MacBook", "Phone"};
			for (int i = 0; i < 2; i++) {
				p.insert(p.end(), macs[i].begin(), macs[i].end());
				p.push_back(i == 0 ? 0x01 : 0x00);
				p.push_back((char)names[i].size());
				p.insert(p.end(), names[i].begin(), names[i].end());
			}
			mdr2(p);
			break;
		}
		case 0x40: mdr2({0x41, 0x01, 0x01, 0x00}); break;                        // VG: on/off supported, no language switch
		case 0x42: mdr2({0x43, 0x01, 0x01, 0x00}); break;
		case 0x46: mdr2({0x47, 0x01, 0x01, (char)voiceGuidance}); break;
		case 0x48: settingSets.push_back("T2 " + hex(b)); voiceGuidance = b[3]; mdr2({0x49, 0x01, 0x01, (char)voiceGuidance}); break;
		default: break;
		}
	}

	void handle(const BtMessage& m)
	{
		if (m.dataType == DATA_TYPE::DATA_MDR_NO2) { handleTable2(m); return; }
		const Buffer& b = m.messageBytes;
		unsigned char cmd = b[0];
		auto mdr = [&](Buffer p) { push(DATA_TYPE::DATA_MDR, p, seq++); };
		char buf[8]; std::snprintf(buf, sizeof buf, "%02x", cmd); sentLog.push_back(buf);
		switch (cmd)
		{
		case 0x06: // capabilities - real WH-1000XM4 list plus VPT and multipoint for coverage
		{
			Buffer p = {0x07, 0x00, 0x00};
			for (int f : {0x71,0x62,0xf5,0x81,0x51,0xa1,0xe1,0xe2,0xd2,0xf6,0xd1,0xf4,0xf3,0x39,0x12,0x13,0x11,0x30,0xc1,0x14,0x22,0x21,0x41,0x38})
				p.push_back((char)f);
			p[2] = (char)(p.size() - 3);
			push(DATA_TYPE::DATA_MDR, p, seq++);
			break;
		}
		case 0x04: { // device info
			std::string v = b[1] == 0x01 ? "WH-1000XM4" : "2.5.1";
			Buffer p = {0x05, b[1], (char)v.size()};
			p.insert(p.end(), v.begin(), v.end());
			push(DATA_TYPE::DATA_MDR, p, seq++);
			break;
		}
		case 0x10: push(DATA_TYPE::DATA_MDR, {0x11, 0x00, 70, 0x00}, seq++); break;       // 70 %, not charging
		case 0x18: push(DATA_TYPE::DATA_MDR, {0x19, 0x00, 0x02}, seq++); break;           // AAC
		case 0x14: push(DATA_TYPE::DATA_MDR, {0x15, 0x00, 0x02, 0x01}, seq++); break;     // DSEE Extreme active
		case 0x50: { // EQ capability: 6 bands, 21 steps, 3 presets
			Buffer p = {0x51, 0x01, 6, 21, 3};
			for (auto pr : std::vector<std::pair<int, std::string>>{{0x00, "Off"}, {0x10, "Bright"}, {0xa0, "Manual"}}) {
				p.push_back((char)pr.first); p.push_back((char)pr.second.size());
				p.insert(p.end(), pr.second.begin(), pr.second.end());
			}
			push(DATA_TYPE::DATA_MDR, p, seq++);
			break;
		}
		case 0x5a: // extended info: Clear Bass + 400 Hz, 1 kHz, 2.5 kHz, 6.3 kHz, 16 kHz (as 15662 Hz)
			push(DATA_TYPE::DATA_MDR, {0x5b, 0x01, 6, 0x10, 0x00, 0x01, 0x01, 0x01, (char)0x90, 0x01, 0x03, (char)0xe8,
				0x01, 0x09, (char)0xc4, 0x01, 0x18, (char)0x9c, 0x01, 0x3d, 0x2e}, seq++);
			break;
		case 0x52: push(DATA_TYPE::DATA_MDR, {0x53, 0x01, 0x00}, seq++); break;
		case 0x56: notifyEq(0x57); break;
		case 0x58: {
			std::string hex;
			for (size_t i = 1; i < b.size(); i++) { char h[4]; std::snprintf(h, sizeof h, "%02x ", (unsigned char)b[i]); hex += h; }
			eqSets.push_back(hex);
			if ((unsigned char)b[2] == 0xff) { eqPreset = 0xa0; eqBands.assign(b.begin() + 4, b.end()); }
			else { eqPreset = b[2]; eqBands = eqPreset == 0x00 ? std::vector<unsigned char>(6, 10) : std::vector<unsigned char>{10, 12, 11, 10, 13, 14}; }
			notifyEq(0x59);
			break;
		}
		case 0x60: // NC/ASM capability: DUAL_SINGLE_OFF, ASM level adjustment, NORMAL+VOICE 20 steps
			push(DATA_TYPE::DATA_MDR, {0x61, 0x02, 0x02, 0x01, 0x01, 0x02, 0x00, 20, 0x01, 20}, seq++);
			break;
		case 0x82:
			push(DATA_TYPE::DATA_MDR, {(char)0x83, 0x01, 0x00, (char)optStatus}, seq++);
			break;
		case 0x84: // optimizer start/cancel
			optStatus = b[3] ? 0x10 : 0x00;
			push(DATA_TYPE::DATA_MDR, {(char)0x85, 0x01, 0x00, (char)optStatus}, seq++);
			break;
		case 0x66:
			if (respondToNc)
				push(DATA_TYPE::DATA_MDR, {(char)0x67, 0x02, (char)ncEffect, (char)ncSettingType, (char)dualSingle, 0x01, (char)asmId, (char)asmLevel}, seq++);
			break;
		case 0x68: // NC set
			{ char hex[64]; std::snprintf(hex, sizeof hex, "%02x %02x %02x %02x %02x %02x %02x", (unsigned char)b[1], (unsigned char)b[2], (unsigned char)b[3], (unsigned char)b[4], (unsigned char)b[5], (unsigned char)b[6], (unsigned char)b[7]); received.push_back(hex); }
			ncEffect = b[2] ? 1 : 0; ncSettingType = b[3]; dualSingle = b[4]; asmId = b[6]; asmLevel = b[7];
			notifyNc();
			break;
		case 0x46:
			push(DATA_TYPE::DATA_MDR, {0x47, b[1], (char)(b[1] == 1 ? vpt : position)}, seq++);
			break;
		case 0xf6:
			if (b[1] == 0x05) mdr({(char)0xf7, 0x05, 0x00, (char)s2c});
			if (b[1] == 0x03) mdr({(char)0xf7, 0x03, 0x00, (char)pauseWhenRemoved});
			if (b[1] == 0x04) mdr({(char)0xf7, 0x04, 0x01, (char)apoActive, (char)apoSelect});
			if (b[1] == 0x06) mdr({(char)0xf7, 0x06, 0x01, (char)customKeyPreset});
			break;
		case 0xf8:
			if (b[1] == 0x05) break;
			settingSets.push_back(hex(b));
			if (b[1] == 0x03) { pauseWhenRemoved = b[3]; mdr({(char)0xf9, 0x03, 0x00, b[3]}); }
			if (b[1] == 0x04) { apoActive = b[3]; apoSelect = b[4]; mdr({(char)0xf9, 0x04, 0x01, b[3], b[4]}); }
			if (b[1] == 0x06) { customKeyPreset = b[3]; mdr({(char)0xf9, 0x06, 0x01, b[3]}); }
			break;
		case 0xfa:
			push(DATA_TYPE::DATA_MDR, {(char)0xfb, 0x05, 0x00, (char)sens, (char)voice, (char)offTime}, seq++);
			break;
		// --- device settings ---
		case 0xf0:
			if (b[1] == 0x03) mdr({(char)0xf1, 0x03, 0x00});
			if (b[1] == 0x04) mdr({(char)0xf1, 0x04, 0x02, 0x10, 0x11});
			if (b[1] == 0x06) // one custom button: NC/AMB, voice assistant, Google Assistant
				mdr({(char)0xf1, 0x06, 0x01, 0x02, 0x01, 0x00, 0x03,
					0x00, 0x01, 0x00, 0x01,
					0x30, 0x01, 0x00, 0x30,
					0x31, 0x02, 0x00, 0x32, 0x01, 0x33});
			break;
		case 0xf2:
			if (b[1] == 0x03 || b[1] == 0x04) mdr({(char)0xf3, b[1], 0x00});
			if (b[1] == 0x06) mdr({(char)0xf3, 0x06, 0x01, 0x00});
			break;
		case 0xe0:
			if (b[1] == 0x01) mdr({(char)0xe1, 0x01, 0x00});
			if (b[1] == 0x02) mdr({(char)0xe1, 0x02, 0x02, 0x00});
			break;
		case 0xe2: mdr({(char)0xe3, b[1], 0x00}); break;
		case 0xe6: mdr({(char)0xe7, b[1], 0x00, (char)(b[1] == 0x01 ? connMode : dsee)}); break;
		case 0xe8:
			settingSets.push_back(hex(b));
			if (b[1] == 0x01) connMode = b[3]; else dsee = b[3];
			mdr({(char)0xe9, b[1], 0x00, b[3]});
			break;
		case 0xd0:
			if ((unsigned char)b[1] == 0xd1)
				mdr(withString({(char)0xd1, (char)0xd1, 0x02}, "TOUCH_PANEL_SETTING") + Buffer{0x00, 0x01});
			if ((unsigned char)b[1] == 0xd2) {
				Buffer p = withString({(char)0xd1, (char)0xd2, 0x01}, "Mode");
				p.push_back(0x00); p.push_back(0x02); p.push_back(0x02);
				p = withString(p + Buffer{0x01}, "A"); p.push_back(0x00);
				p = withString(p + Buffer{0x01}, "B"); p.push_back(0x00);
				mdr(p);
			}
			break;
		case 0xd2: mdr({(char)0xd3, b[1], 0x00}); break;
		case 0xd6: mdr({(char)0xd7, b[1], (char)((unsigned char)b[1] == 0xd1 ? 0x01 : 0x02), (char)((unsigned char)b[1] == 0xd1 ? touchPanel : gsList)}); break;
		case 0xd8:
			settingSets.push_back(hex(b));
			if ((unsigned char)b[1] == 0xd1) touchPanel = b[3]; else gsList = b[3];
			mdr({(char)0xd9, b[1], b[2], b[3]});
			break;
		case 0x94: break; // alerts enabled
		case 0x98: alertAnswers.push_back(hex(b)); break;
		case 0x22: poweredOff = b.size() == 3 && b[1] == 0x00 && b[2] == 0x01; break;
		default:
			break;
		}
	}

	void push(DATA_TYPE t, Buffer payload, unsigned char s)
	{
		Buffer pkt = CommandSerializer::packageDataForBt(payload, t, s);
		{
			std::lock_guard lk(mtx);
			bytes.insert(bytes.end(), pkt.begin(), pkt.end());
		}
		cv.notify_all();
	}

	std::mutex mtx;
	std::condition_variable cv;
	std::deque<char> bytes;
	std::atomic<bool> connected{false};
	unsigned char seq = 0;
};

int main()
{
	auto fakeOwned = std::make_unique<FakeHeadset>();
	FakeHeadset* fake = fakeOwned.get();
	BluetoothWrapper bt(std::move(fakeOwned));
	Headphones hp(bt);
	std::atomic<int> callbacks{0};
	hp.setStateChangedCallback([&] { callbacks++; });

	bt.connect("x");
	std::atomic<bool> running{true};
	Listener listener(hp, bt, running);
	std::thread lt(&Listener::listen, &listener);

	hp.resetState();
	auto t0 = std::chrono::steady_clock::now();
	bool complete = hp.syncState(std::chrono::milliseconds(3000));
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
	std::printf("sync took %lld ms\n", (long long)ms);
	CHECK(complete);
	CHECK(ms < 2500);

	unsigned caps = hp.getCapabilities();
	CHECK(caps & DEVICE_CAPABILITIES::NC_ASM);
	CHECK(caps & DEVICE_CAPABILITIES::VPT);
	CHECK(caps & DEVICE_CAPABILITIES::SPEAK_TO_CHAT);
	CHECK(caps & DEVICE_CAPABILITIES::MULTI_POINT);
	CHECK(caps & DEVICE_CAPABILITIES::OPTIMIZER);

	HeadphonesSettings s = hp.getSettings();
	CHECK(s.ambientSoundControl);
	CHECK(s.asmLevel == 13); // wire ambient 12 -> legacy 13
	CHECK(hp.getAmbientSteps() == 20);
	CHECK(hp.supportsFunction(0x11)); // battery
	CHECK(!hp.supportsFunction(0x99));
	CHECK(hp.getSupportedFunctions().size() == 24);
	CHECK(hp.getLoadedState() & STATE_NC_ASM_CAPABILITY);
	CHECK(s.focusOnVoice);
	CHECK(s.vptType == 0);
	CHECK(s.surroundPosition == SOUND_POSITION_PRESET::FRONT);
	CHECK(s.speakToChat == S2C_TOGGLE::ACTIVE);
	CHECK(s.s2cSensitivity == 2);
	CHECK(s.s2cVoice);
	CHECK(s.s2cOffTime == 3);
	CHECK(!hp.isChanged()); // reading state must not queue anything to send

	auto devices = hp.getDevices();
	CHECK(devices.size() == 3);
	CHECK(devices.size() == 3 && devices[1].name == "MacBook" && devices[2].name == "Phone");
	CHECK(hp.getConnectedDevices().first == 1);
	CHECK(hp.getConnectedDevices().second == 0);
	CHECK(callbacks > 0);

	DeviceStatus ds = hp.getDeviceStatus();
	CHECK(ds.modelName == "WH-1000XM4");
	CHECK(ds.firmwareVersion == "2.5.1");
	CHECK(ds.batteryLevel == 70 && !ds.charging);
	CHECK(ds.hasCodec && ds.codec == AUDIO_CODEC::AAC);
	CHECK(ds.hasUpscaling && ds.upscalingType == UPSCALING_TYPE::DSEE_EXTREME && ds.upscalingActive);
	fake->sendRaw(DATA_TYPE::DATA_MDR, {0x13, 0x00, 65, 0x01}); // battery notify: 65 %, charging
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	CHECK(hp.getDeviceStatus().batteryLevel == 65 && hp.getDeviceStatus().charging);
	hp.setStateFromReply(BtMessage{DATA_TYPE::DATA_MDR, 0, {0x05, 0x01, (char)200, 'X'}}); // length beyond payload
	CHECK(hp.getDeviceStatus().modelName == "X");

	EqCapability eq = hp.getEqCapability();
	CHECK(eq.available && eq.customizable && eq.enabled);
	CHECK(eq.bandCount == 6 && eq.levelSteps == 21);
	CHECK(eq.presets.size() == 3 && eq.presets[1].id == 0x10 && eq.presets[1].name == "Bright");
	CHECK(eq.clearBassIndex == 0);
	CHECK(eq.bandLabels.size() == 6 && eq.bandLabels[1] == "400" && eq.bandLabels[2] == "1k" && eq.bandLabels[3] == "2.5k" && eq.bandLabels[4] == "6.3k" && eq.bandLabels[5] == "15.6k");
	CHECK(s.eqPreset == 0x10 && s.eqBands == (std::vector<unsigned char>{10, 12, 11, 10, 13, 14}));
	CHECK(hp.getLoadedState() & STATE_EQ);
	// Select preset Off -> device answers with flat bands
	hp.setEqPreset(0x00);
	hp.setChanges();
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	CHECK(fake->eqSets.back() == "01 00 00 ");
	CHECK(hp.getSettings().eqPreset == 0x00 && hp.getSettings().eqBands == std::vector<unsigned char>(6, 10));
	// Edit bands -> "Manual"
	hp.setEqBands({15, 10, 10, 10, 10, 5});
	hp.setChanges();
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	CHECK(fake->eqSets.back() == "01 ff 06 0f 0a 0a 0a 0a 05 ");
	CHECK(hp.getSettings().eqPreset == 0xa0);
	// Out-of-range steps are clamped; wrong band count is ignored
	hp.setEqBands({99, 10, 10, 10, 10, 10});
	CHECK(hp.getSettings().eqBands[0] == 20);
	hp.setEqBands({10, 10});
	CHECK(hp.getSettings().eqBands.size() == 6);
	// Picking a preset drops a pending band edit
	hp.setEqPreset(0x10);
	CHECK(hp.getSettings().eqBands[0] == 15);
	hp.setChanges();
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	CHECK(fake->eqSets.back() == "01 10 00 ");
	CHECK(!hp.isChanged());
	// EQ disabled notification
	hp.setStateFromReply(BtMessage{DATA_TYPE::DATA_MDR, 0, {0x55, 0x01, 0x01}});
	CHECK(!hp.getEqCapability().enabled);
	hp.setStateFromReply(BtMessage{DATA_TYPE::DATA_MDR, 0, {0x59, 0x01, 0x10, 0x09, 1}}); // truncated
	CHECK(hp.getSettings().eqBands.size() == 6);

	// --- Device settings ---
	std::vector<std::pair<unsigned char, bool>> alerts;
	std::mutex alertMtx;
	hp.setAlertCallback([&](unsigned char msg, bool choice) { std::lock_guard g(alertMtx); alerts.push_back({msg, choice}); });
	{
		DeviceSettingsCapability sc = hp.getSettingsCapability();
		CHECK(sc.pauseWhenRemoved);
		CHECK(sc.autoPowerOffElements == (std::vector<unsigned char>{0x10, 0x11}));
		CHECK(sc.assignableKeys.size() == 1 && sc.assignableKeys[0].key == 0x02 && sc.assignableKeys[0].keyType == 0x01);
		CHECK(sc.assignableKeys.size() == 1 && sc.assignableKeys[0].presets == (std::vector<unsigned char>{0x00, 0x30, 0x31}));
		CHECK(sc.connectionMode && sc.upscaling && sc.upscalingType == UPSCALING_TYPE::DSEE_EXTREME);
		CHECK(sc.voiceGuidance);
		CHECK(sc.general[0].available && !sc.general[0].isList && sc.general[0].title.enumName && sc.general[0].title.name == "TOUCH_PANEL_SETTING");
		CHECK(sc.general[1].available && sc.general[1].isList && sc.general[1].items.size() == 2 && sc.general[1].items[1].name == "B");
		CHECK(!sc.general[2].available);
		CHECK(hp.getSetting(SETTING::PAUSE_WHEN_REMOVED) == 1);
		CHECK(hp.getSetting(SETTING::AUTO_POWER_OFF) == 0x11);
		CHECK(hp.getSetting(SETTING::CONNECTION_MODE) == 0);
		CHECK(hp.getSetting(SETTING::UPSCALING) == 1);
		CHECK(hp.getSetting(SETTING::VOICE_GUIDANCE) == 1);
		CHECK(hp.getSetting(SETTING::GENERAL_SETTING_1) == 1);
		CHECK(hp.getSetting(SETTING::GENERAL_SETTING_2) == 1);
		CHECK(!hp.getSetting(SETTING::GENERAL_SETTING_3).has_value());
		CHECK(hp.getAssignablePresets() == std::vector<unsigned char>{0x00});
		CHECK(!hp.isChanged());

		// Settings that haven't been reported can't be set
		hp.setSetting(SETTING::GENERAL_SETTING_3, 1);
		CHECK(!hp.isChanged());

		hp.setSetting(SETTING::PAUSE_WHEN_REMOVED, 0);
		hp.setSetting(SETTING::AUTO_POWER_OFF, 0x10);
		hp.setSetting(SETTING::CONNECTION_MODE, 1);
		hp.setSetting(SETTING::UPSCALING, 0);
		hp.setSetting(SETTING::VOICE_GUIDANCE, 0);
		hp.setSetting(SETTING::GENERAL_SETTING_1, 0);
		hp.setSetting(SETTING::GENERAL_SETTING_2, 0);
		hp.setAssignablePresets({0x31});
		CHECK(hp.isChanged());
		hp.setChanges();
		std::this_thread::sleep_for(std::chrono::milliseconds(150));
		std::vector<std::string> sets = fake->settingSets;
		auto sent = [&](const std::string& h) { return std::find(sets.begin(), sets.end(), h) != sets.end(); };
		CHECK(sent("f8 03 00 00 "));
		CHECK(sent("f8 04 01 10 01 ")); // keeps the last timeout (30 min) as select time
		CHECK(sent("e8 01 00 01 "));
		CHECK(sent("e8 02 00 00 "));
		CHECK(sent("T2 48 01 01 00 "));  // voice guidance goes over table 2
		CHECK(sent("d8 d1 01 00 "));     // boolean general setting
		CHECK(sent("d8 d2 02 00 "));     // list general setting
		CHECK(sent("f8 06 01 31 "));
		CHECK(!hp.isChanged());
		CHECK(hp.getSetting(SETTING::VOICE_GUIDANCE) == 0);
		CHECK(hp.getAssignablePresets() == std::vector<unsigned char>{0x31});
		CHECK(hp.getSettings().vptType == 0); // table-2 0x49 must not be read as a VPT notification

		// Device asks for confirmation; the app answers from a non-listener thread
		fake->sendRaw(DATA_TYPE::DATA_MDR, {(char)0x99, 0x01, 0x06, 0x01});
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		{
			std::lock_guard g(alertMtx);
			CHECK(alerts.size() == 1 && alerts[0].first == 0x06 && alerts[0].second);
		}
		hp.answerAlert(0x06, true);
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		CHECK(fake->alertAnswers.size() == 1 && fake->alertAnswers[0] == "98 01 06 01 ");

		// Device-side change (e.g. from the phone app) shows up
		fake->sendRaw(DATA_TYPE::DATA_MDR, {(char)0xd9, (char)0xd1, 0x01, 0x01});
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		CHECK(hp.getSetting(SETTING::GENERAL_SETTING_1) == 1);

		// Malformed settings packets
		hp.setStateFromReply(BtMessage{DATA_TYPE::DATA_MDR, 0, {(char)0xf1, 0x06, 0x05, 0x02}});
		hp.setStateFromReply(BtMessage{DATA_TYPE::DATA_MDR, 0, {(char)0xd1, (char)0xd1, 0x02, (char)200, 'x'}});
		hp.setStateFromReply(BtMessage{DATA_TYPE::DATA_MDR, 0, {(char)0xf7, 0x04, 0x01}});
		hp.setStateFromReply(BtMessage{DATA_TYPE::DATA_MDR_NO2, 0, {0x47}});
		CHECK(hp.getSettingsCapability().general[0].available);
		CHECK(hp.getSettingsCapability().assignableKeys.size() == 1);

		hp.powerOff();
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		CHECK(fake->poweredOff);
	}

	// User changes level -> sent -> device echoes -> UI value sticks.
	hp.setAsmLevel(5);
	CHECK(hp.isChanged());
	hp.setChanges();
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	CHECK(fake->asmLevel == 4); // legacy 5 -> ambient level 4 on the wire
	CHECK(fake->received.back() == "02 11 02 00 01 01 04"); // echoes DUAL_SINGLE_OFF, NC off, VOICE
	CHECK(!hp.isChanged());
	CHECK(hp.getSettings().asmLevel == 5);

	// A device notification must not clobber a change that is still pending.
	hp.setAsmLevel(7);
	hp.setStateFromReply(BtMessage{DATA_TYPE::DATA_MDR, 0, {(char)0x69, 0x02, 0x01, 0x02, 0x00, 0x01, 0x00, 15}});
	CHECK(hp.getSettings().asmLevel == 7);
	CHECK(hp.isChanged());
	hp.setChanges();
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	CHECK(fake->asmLevel == 6);
	CHECK(hp.getSettings().asmLevel == 7);

	// Button pressed on the headphones -> UI follows.
	fake->pressNcButtonTurnOff();
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	CHECK(!hp.getSettings().ambientSoundControl);
	CHECK(!hp.isChanged());

	// Noise cancelling (dual) maps to level 0, single to level 1.
	hp.setStateFromReply(BtMessage{DATA_TYPE::DATA_MDR, 0, {(char)0x69, 0x02, 0x01, 0x02, 0x02, 0x01, 0x00, 0x00}});
	CHECK(hp.getSettings().ambientSoundControl && hp.getSettings().asmLevel == 0);
	hp.setStateFromReply(BtMessage{DATA_TYPE::DATA_MDR, 0, {(char)0x69, 0x02, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00}});
	CHECK(hp.getSettings().asmLevel == 1);
	// The real XM4 reply captured on 2026-11-07 (NC on, dual)
	hp.setStateFromReply(BtMessage{DATA_TYPE::DATA_MDR, 0, {0x67, 0x02, 0x01, 0x02, 0x02, 0x01, 0x00, 0x00}});
	CHECK(hp.getSettings().asmLevel == 0 && hp.getSettings().ambientSoundControl);
	// Wind noise reduction is sent with ambient value 0, not 1
	hp.setAsmLevel(1);
	hp.setChanges();
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	CHECK(fake->received.back() == "02 11 02 01 01 00 00");
	// Max ambient level reaches N (20) on the wire
	hp.setAsmLevel(21);
	hp.setChanges();
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	CHECK(fake->asmLevel == 20);
	// Speak-to-Chat: real RET layout, and preview notifications don't flip the toggle
	hp.setStateFromReply(BtMessage{DATA_TYPE::DATA_MDR, 0, {(char)0xf7, 0x05, 0x00, 0x01}});
	CHECK(hp.getSettings().speakToChat == S2C_TOGGLE::ACTIVE);
	hp.setStateFromReply(BtMessage{DATA_TYPE::DATA_MDR, 0, {(char)0xf9, 0x05, 0x02, 0x00}});
	CHECK(hp.getSettings().speakToChat == S2C_TOGGLE::ACTIVE);
	hp.setStateFromReply(BtMessage{DATA_TYPE::DATA_MDR, 0, {(char)0xf9, 0x05, 0x01, 0x00}});
	CHECK(hp.getSettings().speakToChat == S2C_TOGGLE::INACTIVE);
	// Optimizer: status in byte 3; byte 2 = 0x00 means "enabled", not "optimizing"
	CHECK(hp.getSettings().optimizerState == OPTIMIZER_STATE::IDLE);
	hp.setOptimizerState(OPTIMIZER_STATE::OPTIMIZING);
	hp.setChanges();
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	CHECK(fake->optStatus == 0x10);
	CHECK(hp.getSettings().optimizerState == OPTIMIZER_STATE::OPTIMIZING);
	hp.setStateFromReply(BtMessage{DATA_TYPE::DATA_MDR, 0, {(char)0x85, 0x01, 0x00, 0x11}}); // finished
	CHECK(hp.getSettings().optimizerState == OPTIMIZER_STATE::IDLE);

	// Malformed / truncated packets must not crash or change state.
	hp.setStateFromReply(BtMessage{DATA_TYPE::DATA_MDR, 0, {0x37, 0x01, 0x05, 'A', 'B'}});
	hp.setStateFromReply(BtMessage{DATA_TYPE::DATA_MDR, 0, {(char)0x67, 0x02}});
	hp.setStateFromReply(BtMessage{DATA_TYPE::DATA_MDR, 0, {(char)0xfb}});
	hp.setStateFromReply(BtMessage{DATA_TYPE::DATA_MDR, 0, {}});
	hp.setStateFromReply(BtMessage{DATA_TYPE::DATA_MDR, 0, {0x07, 0x00, (char)0xff, 0x11}}); // count larger than payload
	hp.setStateFromReply(BtMessage{DATA_TYPE::DATA_MDR, 0, {0x61, 0x02, 0x02, 0x01, 0x01, 0x09, 0x00}}); // truncated capability
	CHECK(hp.getSettings().asmLevel == 21); // unchanged by the malformed packets
	CHECK(hp.getAmbientSteps() == 20);

	// A change made while a command is in flight must still be sent (markSent, not fulfill).
	hp.setAsmLevel(9);
	hp.setAmbientSoundControl(true);
	std::thread t([&] { hp.setChanges(); });
	hp.setAsmLevel(14); // races with the send above
	t.join();
	if (hp.isChanged()) hp.setChanges();
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	CHECK(fake->asmLevel == 13);
	CHECK(hp.getSettings().asmLevel == 14);

	// Sync with a feature that never answers -> returns false at the timeout, no hang.
	fake->respondToNc = false;
	hp.resetState();
	t0 = std::chrono::steady_clock::now();
	complete = hp.syncState(std::chrono::milliseconds(1500));
	ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
	CHECK(!complete);
	CHECK(ms < 2500);
	CHECK(!(hp.getLoadedState() & STATE_NC_ASM));
	CHECK(hp.getLoadedState() & STATE_S2C);

	// Disconnect -> listener exits instead of spinning.
	running = false;
	bt.disconnect();
	lt.join();
	std::printf("listener exited\n");

	std::printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", failures);
	return failures ? 1 : 0;
}
