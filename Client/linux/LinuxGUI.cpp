#include "LinuxGUI.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <stdio.h>
#include "Constants.h"
#include "TimedMessageQueue.h"
#include "CrossPlatformGUI.h"

static void glfw_error_callback(int error, const char *description)
{
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

void EnterGUIMainLoop(BluetoothWrapper bt)
{
    // Setup Dear ImGui binding
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // init GUI
    CrossPlatformGUI GUI = CrossPlatformGUI(std::move(bt));

    // Setup window
    glfwSetErrorCallback(glfw_error_callback);

    // GLEW usa GLX y necesita X11/XWayland.
    // Forzar X11 incluso dentro de sesiones Wayland.
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);

    if (!glfwInit())
    {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return;
    }

    const char *glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow *window = glfwCreateWindow(
        GUI_WIDTH,
        GUI_HEIGHT,
        APP_NAME,
        NULL,
        NULL
    );

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    glewExperimental = GL_TRUE;

    GLenum err = glewInit();
    if (err != GLEW_OK)
    {
        fprintf(stderr,
            "GLEW error: %s\n",
            glewGetErrorString(err));
        return;
    }

    ImGuiIO &io = ImGui::GetIO();
    (void)io;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    ImVec4 clear_color = COLOR_BG_DARK;

    // Main loop
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();

        ImGui::NewFrame();

        GUI.performGUIPass();

        if (glfwWindowShouldClose(window))
        {
            GUI.cleanup();
            break;
        }

        ImGui::Render();

        int display_w, display_h;

        glfwGetFramebufferSize(
            window,
            &display_w,
            &display_h
        );

        glViewport(
            0,
            0,
            display_w,
            display_h
        );

        glClearColor(
            clear_color.x,
            clear_color.y,
            clear_color.z,
            clear_color.w
        );

        glClear(
            GL_COLOR_BUFFER_BIT |
            GL_DEPTH_BUFFER_BIT
        );

        ImGui_ImplOpenGL3_RenderDrawData(
            ImGui::GetDrawData()
        );

        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
};
