#include "CrossPlatformGUI.h"

constexpr auto LISTENER_TIMEOUT_MS = 2000;
constexpr auto FUTURE_TIMEOUT_MS = 8000;

int CrossPlatformGUI::_spinnerFrame()
{
    return static_cast<int>(ImGui::GetTime() * 6.0f) & 3;
}

void CrossPlatformGUI::_drawSpinner(const char* label)
{
    static const char chars[] = { '|', '/', '-', '\\' };
    int frame = _spinnerFrame();
    char buf[64];
    snprintf(buf, sizeof(buf), "%c %s", chars[frame], label);
    ImGui::Text("%s", buf);
}

bool CrossPlatformGUI::performGUIPass()
{
    bool open = true;

    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = 0.0f;
    style.FrameRounding = 8.0f;
    style.GrabRounding = 8.0f;

    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;

    style.WindowPadding = ImVec2(20,20);
    style.FramePadding = ImVec2(12,8);
    style.ItemSpacing = ImVec2(10,12);

	ImVec2 size = ImGui::GetIO().DisplaySize;

	ImGui::SetNextWindowPos(
		ImVec2(0,0),
		ImGuiCond_Always
	);

	ImGui::SetNextWindowSize(
		size,
		ImGuiCond_Always
	);

    ImGui::Begin(
        "MainWindow",
        &open,

        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings
    );

    //-----------------------------------------
    // Header
    //-----------------------------------------

    ImGui::SetWindowFontScale(1.4f);

    ImGui::Text(
        "Sony WH Headphones"
    );

    ImGui::SetWindowFontScale(1.0f);

    if(this->_bt.isConnected())
    {
        ImGui::SameLine();
        ImGui::Text("  ");
        ImGui::SameLine();
        ImGui::TextColored(COLOR_SUCCESS, "[Connected]");

        ImGui::SameLine();
        ImGui::Dummy(ImVec2(20,0));
        ImGui::SameLine();
        this->_drawBattery();

        ImGui::Text(
            "Device: %s",
            this->_connectedDevice.name.c_str()
        );
    }
    else
    {
        ImGui::SameLine();
        ImGui::Text("  ");
        ImGui::SameLine();
        ImGui::TextColored(COLOR_ERROR, "[Disconnected]");
    }

    ImGui::Dummy(ImVec2(0, SECTION_SPACING));

    //-----------------------------------------
    // Connection
    //-----------------------------------------

    if (ImGui::CollapsingHeader("Connection", ImGuiTreeNodeFlags_DefaultOpen))
    {
        this->_drawErrors();
        this->_drawDeviceDiscovery();
    }

    //-----------------------------------------
    // Device controls (only when connected)
    //-----------------------------------------

    if(this->_bt.isConnected())
    {
        if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_None))
        {
            unsigned int cap = this->_headphones.getCapabilities();

            if (cap & (DEVICE_CAPABILITIES::NC_ASM | DEVICE_CAPABILITIES::VPT |
                       DEVICE_CAPABILITIES::SPEAK_TO_CHAT | DEVICE_CAPABILITIES::OPTIMIZER))
            {
                if (ImGui::BeginTabItem("Audio"))
                {
                    if(cap & DEVICE_CAPABILITIES::NC_ASM)
                    {
                        if (ImGui::CollapsingHeader("Noise Control", ImGuiTreeNodeFlags_DefaultOpen))
                            this->_drawASMControls();
                    }

                    if(cap & DEVICE_CAPABILITIES::VPT)
                    {
                        if (ImGui::CollapsingHeader("Spatial Audio", ImGuiTreeNodeFlags_DefaultOpen))
                            this->_drawSurroundControls();
                    }

                    if(cap & DEVICE_CAPABILITIES::SPEAK_TO_CHAT)
                        this->_drawSpeakToChat();

                    if(cap & DEVICE_CAPABILITIES::OPTIMIZER)
                    {
                        ImGui::Dummy(ImVec2(0, SECTION_SPACING));
                        this->_drawOptimizerButton();
                    }

                    ImGui::EndTabItem();
                }
            }

            if(cap & DEVICE_CAPABILITIES::MULTI_POINT)
            {
                if (ImGui::BeginTabItem("Devices"))
                {
                    this->_drawMultiPointConn();
                    ImGui::EndTabItem();
                }
            }

            ImGui::EndTabBar();
        }
    }

    //-----------------------------------------
    // Commands
    //-----------------------------------------

    this->_setHeadphoneSettings();

    //-----------------------------------------
    // Footer
    //-----------------------------------------

    ImGui::Dummy(ImVec2(0, SECTION_SPACING));

    ImGui::TextDisabled(
        "Unofficial Sony client"
    );

    ImGui::End();

    if(!open)
    {
        this->cleanup();
        return false;
    }

    return open;
}


void CrossPlatformGUI::_drawErrors()
{
    if (this->_mq.begin() != this->_mq.end())
    {
        ImGui::TextColored(COLOR_ERROR, "Errors:");

        ImGui::Spacing();

        for (auto&& message : this->_mq)
        {
            ImGui::BulletText(
                "%s",
                message.message.c_str()
            );
        }

        ImGui::Spacing();
    }
}

void CrossPlatformGUI::cleanup()
{
    this->_listenerActive = false;
    this->_bt.disconnect();

    if(this->_listenerFuture.valid())
    {
        this->_listenerFuture.wait_for(
            std::chrono::milliseconds(LISTENER_TIMEOUT_MS)
        );
    }

    this->_listener.reset();
}

void CrossPlatformGUI::_drawBattery()
{
    // Prefer the level reported over the protocol; fall back to the connector (BlueZ D-Bus on Linux).
    DeviceStatus status = this->_headphones.getDeviceStatus();
    int level = status.batteryLevel >= 0 ? status.batteryLevel : this->_bt.getBatteryLevel();
    if (level >= 0)
        ImGui::TextColored(COLOR_TEXT, "[Battery: %d%%]", level);
    else
        ImGui::TextColored(COLOR_TEXT, "[Battery: N/A]");
}

void CrossPlatformGUI::_drawDeviceDiscovery()
{
    auto& connectedDevices = this->_discoveredDevices;

    //---------------------------------
    // Already connected
    //---------------------------------

    if(this->_bt.isConnected())
    {
        ImGui::TextColored(COLOR_CONNECTED, "Connected to: %s",
            this->_connectedDevice.name.c_str());

        if(
            ImGui::Button(
                "Disconnect",
                ImVec2(
                    BUTTON_WIDTH,
                    BUTTON_HEIGHT
                )
            )
        )
        {
            this->_selectedDevice = -1;
            this->cleanup();
        }

        return;
    }

    //---------------------------------
    // Device list
    //---------------------------------

    ImGui::Text(
        "Select a device:"
    );

    ImGui::Spacing();

    int temp=0;

    for(
        const auto& device:
        connectedDevices
    )
    {
        ImGui::RadioButton(
            device.name.c_str(),
            &this->_selectedDevice,
            temp++
        );
    }

    ImGui::Spacing();

    //---------------------------------
    // Connection state
    //---------------------------------

    if(
        this->_connectFuture
        .valid()
    )
    {
        bool timedOut = false;
        auto elapsed = std::chrono::steady_clock::now() - this->_connectStart;
        if (elapsed > std::chrono::milliseconds(FUTURE_TIMEOUT_MS))
        {
            timedOut = true;
        }

        if (
            this->_connectFuture
            .ready()
        )
        {
            try
            {
                this->_connectFuture
                .get();
                this->resetUIState();
            }
            catch(
                const RecoverableException&
                exc
            )
            {
                if(
                    exc.shouldDisconnect
                )
                {
                    this->_bt
                    .disconnect();
                }

                this->_mq
                .addMessage(
                    exc.what()
                );
            }
        }
        else if (timedOut)
        {
            this->_mq.addMessage("Connection timed out");
            this->_connectFuture = SingleInstanceFuture<void>();
        }
        else
        {
            this->_drawSpinner("Connecting...");
        }
    }
    else
    {
        if(
            ImGui::Button(
                "Connect",
                ImVec2(
                    BUTTON_WIDTH,
                    BUTTON_HEIGHT
                )
            )
        )
        {
            if(
                this->_selectedDevice != -1
            )
            {
                this->_connectedDevice=
                    connectedDevices[
                        this->_selectedDevice
                    ];

                this->_connectStart = std::chrono::steady_clock::now();

                this->_connectFuture
                .setFromAsync(
                [this]()
                {
                    this->_bt.connect(
                        this
                        ->_connectedDevice
                        .mac
                    );

                    this->_listenerActive = true;
                    this->_listener=
                    std::make_unique<
                        Listener
                    >(
                        this
                        ->_headphones,
                        this
                        ->_bt,
                        this
                        ->_listenerActive
                    );

                    this
                    ->_listenerFuture=
                    std::async(
                        std::launch
                        ::async,

                        &Listener
                        ::listen,

                        this
                        ->_listener
                        .get()
                    );

                    this
                    ->_headphones
                    .queryState();
                });
            }
        }
    }

    ImGui::SameLine();

    //---------------------------------
    // Refresh
    //---------------------------------

    if(
        ImGui::Button(
            "Refresh",
            ImVec2(
                BUTTON_WIDTH,
                BUTTON_HEIGHT
            )
        ) &&
        !this
        ->_connectedDevicesFuture
        .valid()
    )
    {
        this->_selectedDevice = -1;

        this
        ->_connectedDevicesFuture
        .setFromAsync(
        [this]()
        {
            return
            this
            ->_bt
            .getConnectedDevices();
        });
    }

    //---------------------------------
    // Get results
    //---------------------------------

    if(
        this
        ->_connectedDevicesFuture
        .valid()
    )
    {
        if(
            this
            ->_connectedDevicesFuture
            .ready()
        )
        {
            try
            {
                connectedDevices=
                this
                ->_connectedDevicesFuture
                .get();
            }
            catch(
                const RecoverableException&
                exc
            )
            {
                this
                ->_mq
                .addMessage(
                    exc.what()
                );
            }
        }
    }
}

void CrossPlatformGUI::_drawASMControls()
{
    ImGui::Checkbox(
        "Enable Ambient Sound",
        &this->_ambientSoundControl
    );

    if(this->_ambientSoundControl)
    {
        ImGui::Indent();

        ImGui::Text("Environment Level");
        ImGui::SameLine();
        const int maxLevel = this->_headphones.getAmbientSteps() + 1;
        ImGui::TextDisabled("%d/%d", this->_asmLevel, maxLevel);

        float w = ImGui::GetContentRegionAvail().x;
        float h = 20.0f;
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float fill = (w > 0.0f) ? this->_asmLevel / (float)maxLevel : 0.0f;
        float r = h * 0.5f;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImU32 bgCol = ImColor(0.10f, 0.15f, 0.30f, 1.00f);
        ImU32 fillCol = ImColor(COLOR_ACCENT);
        ImU32 thumbCol = IM_COL32(255, 255, 255, 255);

        dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), bgCol, r);
        if (fill > 0.0f)
            dl->AddRectFilled(pos, ImVec2(pos.x + w * fill, pos.y + h), fillCol, r);
        dl->AddCircleFilled(ImVec2(pos.x + w * fill, pos.y + h * 0.5f), 7.0f, thumbCol);

        ImGui::InvisibleButton("##asmBar", ImVec2(w, h));
        if (ImGui::IsItemActive())
        {
            float mx = ImGui::GetMousePos().x - pos.x;
            int val = (int)((mx / w) * (float)maxLevel + 0.5f);
            if (val < 0) val = 0;
            if (val > maxLevel) val = maxLevel;
            this->_asmLevel = val;
        }

        ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + h + ImGui::GetStyle().ItemSpacing.y));

        if(
        this->_headphones
        .isFocusOnVoiceAvailable()
        )
        {
            ImGui::Checkbox(
                "Focus on Voice",
                &this->_focusOnVoice
            );
        }
        else
        {
            ImGui::TextDisabled(
                "Focus on Voice unavailable"
            );
        }

        ImGui::Unindent();
    }

    this->_headphones
        .setAmbientSoundControl(
            this->_ambientSoundControl
        );

    this->_headphones
        .setAsmLevel(
            this->_asmLevel
        );

    this->_headphones
        .setFocusOnVoice(
            this->_focusOnVoice
        );
}

void CrossPlatformGUI::resetUIState()
{
    this->_ambientSoundControl = true;
    this->_focusOnVoice = false;
    this->_asmLevel = 0;
}


void CrossPlatformGUI::_drawSurroundControls()
{
    ImGui::TextDisabled(
        "Only one mode can be active"
    );

    ImGui::Spacing();

    if(
    ImGui::Combo(
        "Position",
        &this->_soundPosition,
        "Off\0Front Left\0Front Right\0Front\0Rear Left\0Rear Right\0\0"
    ))
    {
        this->_vptType = 0;
    }

    if(
    ImGui::Combo(
        "Environment",
        &this->_vptType,
        "Off\0Outdoor Festival\0Arena\0Concert Hall\0Club\0\0"
    ))
    {
        this->_soundPosition = 0;
    }

    this->_headphones
    .setSurroundPosition(
        SOUND_POSITION_PRESET_ARRAY[
            this->_soundPosition
        ]
    );

    this->_headphones
    .setVptType(
        this->_vptType
    );
}

void CrossPlatformGUI::_drawOptimizerButton()
{
	if (this->_headphones.getOptimizerState() == OPTIMIZER_STATE::IDLE)
	{
		if (ImGui::Button("Optimize", ImVec2(BUTTON_WIDTH, BUTTON_HEIGHT)))
		{
			this->_headphones.setOptimizerState(OPTIMIZER_STATE::OPTIMIZING);
		}
	}
	else {
		if (ImGui::Button("Cancel Optimization", ImVec2(BUTTON_WIDTH, BUTTON_HEIGHT)))
		{
			this->_headphones.setOptimizerState(OPTIMIZER_STATE::IDLE);
		}
	}
}

void CrossPlatformGUI::_drawSpeakToChat()
{
	if (ImGui::CollapsingHeader("Speak To Chat", ImGuiTreeNodeFlags_DefaultOpen))
	{
		enum Sensitivity { Sens_Auto, Sens_High, Sens_Low, Sens_count};
		const char* Sens_hints[Sens_count] = { "Auto", "High", "Low" };

		enum AutoOff { Time_Short, Time_Std, Time_Long, Time_Inf, Time_count};
		const char* Time_hints[Time_count] = { "Short", "Standard", "Long", "Off" };

		const char* Sens_name = (this->_s2cSensitivity >= 0 && this->_s2cSensitivity < Sens_count) ? Sens_hints[this->_s2cSensitivity] : "Unknown";
		const char* Time_name = (this->_s2cAutoOff >= 0 && this->_s2cAutoOff < Time_count) ? Time_hints[this->_s2cAutoOff] : "Unknown";

		ImGui::Checkbox("Speak To Chat", &this->_s2cToggle);

		if (this->_s2cToggle)
		{
			ImGui::Indent();
			ImGui::SliderInt("Sensitivity", &this->_s2cSensitivity, 0, Sens_count - 1, Sens_name);
			ImGui::SliderInt("Auto close", &this->_s2cAutoOff, 0, Time_count - 1, Time_name);
			ImGui::Checkbox("Voice passthrough", &this->_s2cVoice);
			ImGui::Unindent();
		}

		if (this->_s2cToggle)
			this->_headphones.setS2CToggle(S2C_TOGGLE::ACTIVE);
		else
			this->_headphones.setS2CToggle(S2C_TOGGLE::INACTIVE);

		this->_headphones.setS2COptions(this->_s2cSensitivity, this->_s2cVoice, this->_s2cAutoOff);
	}
}

void CrossPlatformGUI::_drawMultiPointConn()
{
	auto devices = this->_headphones.getDevices();
	auto connectedDevices = this->_headphones.getConnectedDevices();

	int dev1 = connectedDevices.first;
	int dev2 = connectedDevices.second;

	if(ImGui::CollapsingHeader("Connected Devices", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (devices[dev1].name!="")
		{
			ImGui::BulletText("%s", devices[dev1].name.c_str());
			ImGui::SameLine();
			if (ImGui::Button("Disconnect"))
			{
				this->_headphones.setMultiPointConnection(1, 0);
			}
		}

		if (devices[dev1].name=="" || devices[dev2].name=="")
		{
			if (ImGui::BeginCombo("Connect device", ""))
			{
				for (int i = 0; i < devices.size(); i++)
				{
					if (i==dev2 || i==dev1)
						continue;

					const bool is_selected = false;
					if (ImGui::Selectable(devices[i].name.c_str(), is_selected))
					{
						if (devices[dev1].name=="")
						{
							dev1 = i;
							this->_headphones.setMultiPointConnection(1, dev1);
						}
						else {
							dev2 = i;
							this->_headphones.setMultiPointConnection(2, dev2);
						}
					}

					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			ImGui::SameLine();
			if (devices[dev1].name=="")
				ImGui::Text("Device 1");
			else
				ImGui::Text("Device 2");
		}

		if (devices[dev2].name!="")
		{
			ImGui::BulletText("%s", devices[dev2].name.c_str());
			ImGui::SameLine();
			if (ImGui::Button("Disconnect##2"))
			{
				this->_headphones.setMultiPointConnection(2,0);
			}
		}
	}
}

void CrossPlatformGUI::_setHeadphoneSettings()
{
    if(this->_sendCommandFuture.ready())
    {
        this->_commandLinger = 0;

        try
        {
            this->_sendCommandFuture.get();
        }
        catch(
            const RecoverableException&
            exc
        )
        {
            std::string excString;

            if(exc.shouldDisconnect)
            {
                this->_bt.disconnect();
                excString = "Disconnected: ";
            }

            this->_mq.addMessage(
                excString +
                exc.what()
            );
        }
    }
    else if(
        this->_sendCommandFuture
        .valid()
    )
    {
        auto elapsed = std::chrono::steady_clock::now() - this->_sendStart;
        if (elapsed > std::chrono::milliseconds(FUTURE_TIMEOUT_MS))
        {
            this->_mq.addMessage("Command timed out");
            this->_sendCommandFuture = SingleInstanceFuture<void>();
        }
        else if (
            this->_commandLinger++
            > (FPS / 10)
        )
        {
            this->_drawSpinner("Sending...");
        }
    }
    else if(
        this->_bt.isConnected() &&
        this->_headphones
        .isChanged()
    )
    {
        this->_sendStart = std::chrono::steady_clock::now();
        this->_sendCommandFuture
        .setFromAsync(
        [=,this]()
        {
            return this
            ->_headphones
            .setChanges();
        });
    }
}


CrossPlatformGUI::CrossPlatformGUI(
BluetoothWrapper bt
)
:
_bt(std::move(bt)),
_headphones(_bt)
{
    ImGui::StyleColorsDark();

    ImGuiIO& io=
        ImGui::GetIO();

    this->_mq=
        TimedMessageQueue(
            GUI_MAX_MESSAGES
        );

    this
    ->_connectedDevicesFuture
    .setFromAsync(
    [this]()
    {
        return this->_bt
        .getConnectedDevices();
    });

    io.IniFilename=nullptr;

    io.WantSaveIniSettings=
        false;

    io.FontGlobalScale = 1.0f;

    void* fileData =
        ImGui::MemAlloc(
            sizeof(
                CascadiaCodeTTF
            )
        );

    memcpy(
        fileData,
        CascadiaCodeTTF,
        sizeof(
            CascadiaCodeTTF
        )
    );

    ImFont* font=
    io.Fonts
    ->AddFontFromMemoryTTF(
        fileData,
        sizeof(
            CascadiaCodeTTF
        ),
        FONT_SIZE
    );

    IM_ASSERT(font!=NULL);
}
