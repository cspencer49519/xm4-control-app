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
        this->_listenerActive = false;
        this->_bt.disconnect();

        if(this->_listenerFuture.valid())
        {
            this->_listenerFuture.wait_for(
                std::chrono::milliseconds(LISTENER_TIMEOUT_MS)
            );
        }

        this->_listener.reset();
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

void CrossPlatformGUI::_drawBattery()
{
    ImGui::TextColored(COLOR_TEXT, "[Battery: N/A]");
}

void CrossPlatformGUI::_drawDeviceDiscovery()
{
    static std::vector<BluetoothDevice>
        connectedDevices;

    static int selectedDevice=-1;
    static auto connectStart = std::chrono::steady_clock::now();

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
            selectedDevice=-1;
            this->_listenerActive = false;
            this->_listener.reset();

            this->_bt.disconnect();

            if(this->_listenerFuture.valid())
            {
                this->_listenerFuture.wait_for(
                    std::chrono::milliseconds(LISTENER_TIMEOUT_MS)
                );
            }
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
            &selectedDevice,
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
        auto elapsed = std::chrono::steady_clock::now() - connectStart;
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
                selectedDevice!=-1
            )
            {
                this->_connectedDevice=
                    connectedDevices[
                        selectedDevice
                    ];

                connectStart = std::chrono::steady_clock::now();

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

                    if(
                        this
                        ->_connectedDevice
                        .name
                        ==
                        "WH-1000XM4"
                    )
                    {
                        this
                        ->_headphones
                        .queryState();
                    }
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
        selectedDevice=-1;

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
        ImGui::TextDisabled("(0 = Max NC, 19 = Max Ambient)");

        float progress = this->_asmLevel / 19.0f;
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.15f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.0f, 0.52f, 0.84f, 1.0f));
        ImGui::ProgressBar(progress, ImVec2(-1, 4));
        ImGui::PopStyleColor(2);

        ImGui::SliderInt(
            "##ambient",
            &this->_asmLevel,
            0,
            19
        );

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
    static int soundPosition=0;
    static int vptType=0;

    ImGui::TextDisabled(
        "Only one mode can be active"
    );

    ImGui::Spacing();

    if(
    ImGui::Combo(
        "Position",
        &soundPosition,
        "Off\0Front Left\0Front Right\0Front\0Rear Left\0Rear Right\0\0"
    ))
    {
        vptType=0;
    }

    if(
    ImGui::Combo(
        "Environment",
        &vptType,
        "Off\0Outdoor Festival\0Arena\0Concert Hall\0Club\0\0"
    ))
    {
        soundPosition=0;
    }

    this->_headphones
    .setSurroundPosition(
        SOUND_POSITION_PRESET_ARRAY[
            soundPosition
        ]
    );

    this->_headphones
    .setVptType(
        vptType
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

		static bool S2Ctoggle_check = false;
		static int S2C_Sens = Sens_Auto;
		static bool S2C_Voice = 0;
		static int S2C_AutoOff = Time_Short;

		const char* Sens_name = (S2C_Sens >= 0 && S2C_Sens < Sens_count) ? Sens_hints[S2C_Sens] : "Unknown";
		const char* Time_name = (S2C_AutoOff >= 0 && S2C_AutoOff < Time_count) ? Time_hints[S2C_AutoOff] : "Unknown";

		ImGui::Checkbox("Speak To Chat", &S2Ctoggle_check);

		if (S2Ctoggle_check)
		{
			ImGui::Indent();
			ImGui::SliderInt("Sensitivity", &S2C_Sens, 0, Sens_count - 1, Sens_name);
			ImGui::SliderInt("Auto close", &S2C_AutoOff, 0, Time_count - 1, Time_name);
			ImGui::Checkbox("Voice passthrough", &S2C_Voice);
			ImGui::Unindent();
		}

		if (S2Ctoggle_check)
			this->_headphones.setS2CToggle(S2C_TOGGLE::ACTIVE);
		else
			this->_headphones.setS2CToggle(S2C_TOGGLE::INACTIVE);

		this->_headphones.setS2COptions(S2C_Sens, S2C_Voice, S2C_AutoOff);
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
    static int commandLinger=0;
    static auto sendStart = std::chrono::steady_clock::now();

    if(this->_sendCommandFuture.ready())
    {
        commandLinger=0;

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
        auto elapsed = std::chrono::steady_clock::now() - sendStart;
        if (elapsed > std::chrono::milliseconds(FUTURE_TIMEOUT_MS))
        {
            this->_mq.addMessage("Command timed out");
            this->_sendCommandFuture = SingleInstanceFuture<void>();
        }
        else if (
            commandLinger++
            > (FPS / 10)
        )
        {
            this->_drawSpinner("Sending...");
        }
    }
    else if(
        this->_headphones
        .isChanged()
    )
    {
        sendStart = std::chrono::steady_clock::now();
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

    char* fileData=
    new char[
    sizeof(
    CascadiaCodeTTF
    )];

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
        reinterpret_cast<void*>(
            fileData
        ),
        sizeof(
            CascadiaCodeTTF
        ),
        FONT_SIZE
    );

    IM_ASSERT(font!=NULL);
}
