#include "CrossPlatformGUI.h"

bool CrossPlatformGUI::performGUIPass()
{
    //ImGui::NewFrame();

    bool open = true;

    //-----------------------------------------
    // estilo
    //-----------------------------------------

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

    ImGui::Separator();

    if(this->_bt.isConnected())
    {
        ImGui::Text("Status:");
        ImGui::SameLine();

        ImGui::TextColored(
            ImVec4(
                0.2f,
                0.9f,
                0.3f,
                1.0f
            ),
            "CONNECTED"
        );

        ImGui::Text(
            "Device: %s",
            this->_connectedDevice.name.c_str()
        );
    }
    else
    {
        ImGui::Text("Status:");
        ImGui::SameLine();

        ImGui::TextColored(
            ImVec4(
                0.9f,
                0.2f,
                0.2f,
                1.0f
            ),
            "DISCONNECTED"
        );
    }

    ImGui::Spacing();

    ImGui::Separator();

    ImGui::Text("Connection");

    ImGui::Separator();

    this->_drawErrors();

    this->_drawDeviceDiscovery();

    //-----------------------------------------

    if(this->_bt.isConnected())
{
    unsigned int cap =
        this->_headphones
        .getCapabilities();

    if(cap & DEVICE_CAPABILITIES::NC_ASM)
    {
        ImGui::Spacing();
        ImGui::Separator();

        ImGui::Text(
            "Noise Control"
        );

        ImGui::Separator();

        this->_drawASMControls();
    }

    if(cap & DEVICE_CAPABILITIES::VPT)
    {
        ImGui::Spacing();
        ImGui::Separator();

        ImGui::Text(
            "Spatial Audio"
        );

        ImGui::Separator();

        this->_drawSurroundControls();
    }

    if(cap & DEVICE_CAPABILITIES::SPEAK_TO_CHAT)
    {
        ImGui::Spacing();
        ImGui::Separator();

        this->_drawSpeakToChat();
    }

    if(cap & DEVICE_CAPABILITIES::MULTI_POINT)
    {
        ImGui::Spacing();
        ImGui::Separator();

        this->_drawMultiPointConn();
    }

    if(cap & DEVICE_CAPABILITIES::OPTIMIZER)
    {
        ImGui::Spacing();
        ImGui::Separator();

        this->_drawOptimizerButton();
    }

    this->_setHeadphoneSettings();
    }
    ImGui::Spacing();

    ImGui::Separator();

    ImGui::TextDisabled(
        "Unofficial Sony client"
    );

    ImGui::End();

    if(!open)
    {
        // detener listener primero
        this->_listener.reset();

        // cerrar bluetooth para desbloquear reads
        this->_bt.disconnect();

        // no esperar eternamente
        if(this->_listenerFuture.valid())
        {
            this->_listenerFuture.wait_for(
                std::chrono::milliseconds(200)
            );
        }

        return false;
    }

    //ImGui::Render();

    return open;
    }


void CrossPlatformGUI::_drawErrors()
{
    if (this->_mq.begin() != this->_mq.end())
    {
        ImGui::TextColored(
            ImVec4(1.0f,0.3f,0.3f,1.0f),
            "Errors:"
        );

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

void CrossPlatformGUI::_drawDeviceDiscovery()
{
    static std::vector<BluetoothDevice>
        connectedDevices;

    static int selectedDevice=-1;

    //---------------------------------
    // Ya conectado
    //---------------------------------

    if(this->_bt.isConnected())
    {
        ImGui::Text(
            "Connected to:"
        );

        ImGui::SameLine();

        ImGui::TextColored(
            ImVec4(
                0.2f,
                0.8f,
                0.3f,
                1.0f
            ),
            "%s",
            this->_connectedDevice
            .name.c_str()
        );

        if(
            ImGui::Button(
                "Disconnect",
                ImVec2(
                    150,
                    35
                )
            )
        )
        {
            selectedDevice=-1;
            this->_listener.reset();

            this->_bt.disconnect();

            if(this->_listenerFuture.valid())
            {
                this->_listenerFuture.wait_for(
                    std::chrono::milliseconds(200)
                );
            }

        }

        return;
    }

    //---------------------------------
    // Lista dispositivos
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
    // Estado conexión
    //---------------------------------

    if(
        this->_connectFuture
        .valid()
    )
    {
        if(
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
        else
        {
            ImGui::Text(
                "Connecting..."
            );
        }
    }
    else
    {
        if(
            ImGui::Button(
                "Connect",
                ImVec2(
                    150,
                    35
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

                this->_connectFuture
                .setFromAsync(
                [this]()
                {
                    this->_bt.connect(
                        this
                        ->_connectedDevice
                        .mac
                    );

                    //--------------------------------
                    // crear listener
                    //--------------------------------

                    this->_listener=
                    std::make_unique<
                        Listener
                    >(
                        this
                        ->_headphones,
                        this
                        ->_bt
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

                    //--------------------------------

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
                150,
                35
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
    // Obtener resultados
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

        ImGui::Text(
            "Environment Level"
        );

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
		if (ImGui::Button("Optimize"))
		{
			this->_headphones.setOptimizerState(OPTIMIZER_STATE::OPTIMIZING);
		}
	}
	else {
		if (ImGui::Button("Optimizing... (Press To Cancel)"))
		{
			this->_headphones.setOptimizerState(OPTIMIZER_STATE::IDLE);
		}
	}
}

void CrossPlatformGUI::_drawSpeakToChat()
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

	if (ImGui::CollapsingHeader("Speak To Chat Controls", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Checkbox("Speak To Chat", &S2Ctoggle_check);

		if (S2Ctoggle_check)
		{
			if (S2Ctoggle_check){
				// ImGui::SameLine();
				ImGui::SliderInt("Speak to chat sensitivity", &S2C_Sens, 0, Sens_count - 1, Sens_name);
				ImGui::SliderInt("Speak to chat Auto close", &S2C_AutoOff, 0, Time_count - 1, Time_name);
				ImGui::Checkbox("Voice passthrough", &S2C_Voice);
			}
			else {
			}
		}
	}

	if (S2Ctoggle_check)
		this->_headphones.setS2CToggle(S2C_TOGGLE::ACTIVE);
	else
		this->_headphones.setS2CToggle(S2C_TOGGLE::INACTIVE);

	this->_headphones.setS2COptions(S2C_Sens, S2C_Voice, S2C_AutoOff);

}

void CrossPlatformGUI::_drawMultiPointConn()
{
	auto devices = this->_headphones.getDevices();
	auto connectedDevices = this->_headphones.getConnectedDevices();

	int dev1 = connectedDevices.first;
	int dev2 = connectedDevices.second;
	int old_dev1 = dev1;
	int old_dev2 = dev2;
	
	if(ImGui::CollapsingHeader("Connected Devices", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (devices[dev1].name!="")
		{
			ImGui::Text(devices[dev1].name.c_str());
			ImGui::SameLine();
			if (ImGui::Button("Disconnect device 1"))
			{
				this->_headphones.setMultiPointConnection(1, 0);
			}
		}

		if (devices[dev1].name=="" || devices[dev2].name=="")
		{
			// const char* combo_preview_value = devices[dev1].name // &(devices[dev1].name);
			if (ImGui::BeginCombo("Connect", ""))
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

					// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
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
			ImGui::Text(devices[dev2].name.c_str());
			ImGui::SameLine();
			if (ImGui::Button("Disconnect device 2"))
			{
				this->_headphones.setMultiPointConnection(2,0);				
			}
		}
		
	}

}

void CrossPlatformGUI::_setHeadphoneSettings()
{
    static int commandLinger=0;

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

                excString=
                "Disconnected: ";
            }

            this->_mq.addMessage(
                excString+
                exc.what()
            );
        }
    }
    else if(
        this->_sendCommandFuture
        .valid()
    )
    {
        if(
        commandLinger++
        >(FPS/10)
        )
        {
            ImGui::Text(
                "Sending..."
            );
        }
    }
    else if(
        this->_headphones
        .isChanged()
    )
    {
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