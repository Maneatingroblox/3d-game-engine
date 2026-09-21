#include "game/GameApp.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include "engine/platform/Input.h"
#include "engine/audio/AudioEngine.h"
#include "engine/scene/DefaultScene.h"
#include <imgui.h>
#include <filesystem>

namespace fw {

bool GameApp::OnInit() {
    // Resolve content paths relative to the project root, not the working
    // directory, so a double-clicked ForgeworksGame.exe still finds assets/.
    Paths::Initialize();
    FW_LOG_INFO("Project root: %s (from %s)", Paths::ProjectRoot().c_str(), Paths::RootSource().c_str());

    Settings::Get().Load();
    m_Window.SetTitle("Forgeworks");

    m_Engine.Init(false);

    // Make sure the startup scene exists (the runtime must never launch into a
    // blank window just because nothing has been authored yet).
    DefaultScene::EnsureStarterContent(m_StartupScene);
    if (!std::filesystem::exists(Paths::Resolve(m_StartupScene))) {
        m_Engine.NewScene("Default");
        DefaultScene::Build(m_Engine.GetScene(), &m_Engine.GetBrushMap());
    }

    if (m_AutoPlay) StartGame(m_StartupScene);

    FW_LOG_INFO("Game runtime initialized");
    return true;
}

void GameApp::StartGame(const std::string& scenePath) {
    const std::string resolved = Paths::Resolve(scenePath);
    if (!std::filesystem::exists(resolved)) {
        FW_LOG_WARN("Scene '%s' not found - playing the built-in starter scene instead", resolved.c_str());
        m_Engine.NewScene("Default");
        DefaultScene::Build(m_Engine.GetScene(), &m_Engine.GetBrushMap());
        m_Engine.Play();
        m_UIState = GameUIState::Playing;
        Input::Get().SetCursorLocked(true);
        return;
    }
    if (m_Engine.LoadScene(resolved)) {
        m_Engine.Play();
        m_UIState = GameUIState::Playing;
        Input::Get().SetCursorLocked(true);
        FW_LOG_INFO("Playing scene: %s", resolved.c_str());
    } else {
        FW_LOG_ERROR("Failed to load scene: %s", resolved.c_str());
    }
}

void GameApp::ReturnToMainMenu() {
    m_Engine.Stop();
    m_UIState = GameUIState::MainMenu;
    Input::Get().SetCursorLocked(false);
}

void GameApp::OnUpdate(float dt) {
    if (m_UIState == GameUIState::Playing && Input::Get().WasKeyPressed(VK_ESCAPE)) {
        m_Engine.SetPaused(true);
        m_UIState = GameUIState::Paused;
        Input::Get().SetCursorLocked(false);
    } else if (m_UIState == GameUIState::Paused && Input::Get().WasKeyPressed(VK_ESCAPE)) {
        m_Engine.SetPaused(false);
        m_UIState = GameUIState::Playing;
        Input::Get().SetCursorLocked(true);
    }

    if (m_UIState == GameUIState::Playing) {
        m_Engine.Tick(dt);
    }
}

void GameApp::OnRender() {
    // The menu states also render the scene behind the UI (the starter scene is
    // loaded on startup), so the window is never an empty black rectangle.
    if (m_Engine.GetScene().Registry().view<IDComponent>().size() == 0) return;

    RenderCamera rc;
    const float aspect = (float)m_Device.Width() / std::max(1, m_Device.Height());
    if (Entity cam = m_Engine.GetScene().PrimaryCamera()) {
        auto& tc = cam.Get<TransformComponent>();
        auto& cc = cam.Get<CameraComponent>();
        rc.position = vec3(tc.worldMatrix[3]);
        rc.view = glm::inverse(tc.worldMatrix);
        rc.proj = MakeProjectionMatrix(cc.fovDeg, aspect, cc.nearClip, cc.farClip);
    } else {
        // No camera in the scene: keep a sane default so rendering still works.
        rc = MakeCamera(vec3(9, 6, 12), 36.0f, -20.0f, 60.0f, aspect, 0.05f, 2000.0f);
    }

    RenderSettings settings;
    auto skyView = m_Engine.GetScene().Registry().view<SkyLightComponent>();
    if (!skyView.empty()) {
        auto& sky = m_Engine.GetScene().Registry().get<SkyLightComponent>(skyView.front());
        settings.ambientColor = sky.ambientColor;
        settings.ambientIntensity = sky.ambientIntensity;
    }
    m_Renderer->RenderScene(m_Engine.GetScene(), rc, settings);
}

void GameApp::DrawMainMenu() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(360, 300));
    ImGui::Begin("Forgeworks", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);
    ImGui::SetWindowFontScale(1.3f);
    ImGui::Text("FORGEWORKS");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 10));

    if (ImGui::Button("Play", ImVec2(-1, 40))) StartGame(m_StartupScene);
    ImGui::Dummy(ImVec2(0, 4));
    if (ImGui::Button("Settings", ImVec2(-1, 40))) {
        m_ReturnStateAfterSettings = GameUIState::MainMenu;
        m_UIState = GameUIState::Settings;
    }
    ImGui::Dummy(ImVec2(0, 4));
    if (ImGui::Button("Quit", ImVec2(-1, 40))) m_Running = false;
    ImGui::End();
}

void GameApp::DrawSettingsMenu() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(480, 420));
    ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);

    auto& settings = Settings::Get();
    if (ImGui::BeginTabBar("SettingsTabs")) {
        if (ImGui::BeginTabItem("Video")) {
            ImGui::InputInt("Width", &settings.video.resolutionWidth);
            ImGui::InputInt("Height", &settings.video.resolutionHeight);
            const char* modes[] = { "Windowed", "Borderless", "Fullscreen" };
            int mode = (int)settings.video.windowMode;
            if (ImGui::Combo("Window Mode", &mode, modes, 3)) settings.video.windowMode = (WindowMode)mode;
            ImGui::Checkbox("VSync", &settings.video.vsync);
            ImGui::SliderFloat("Render Scale", &settings.video.renderScale, 0.5f, 2.0f);
            ImGui::SliderFloat("Field of View", &settings.video.fov, 60.0f, 120.0f);
            ImGui::Checkbox("Bloom", &settings.video.bloom);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Audio")) {
            ImGui::SliderFloat("Master Volume", &settings.audio.masterVolume, 0.0f, 1.0f);
            ImGui::SliderFloat("Music Volume", &settings.audio.musicVolume, 0.0f, 1.0f);
            ImGui::SliderFloat("SFX Volume", &settings.audio.sfxVolume, 0.0f, 1.0f);
            ImGui::Checkbox("Mute on Focus Loss", &settings.audio.muteOnFocusLoss);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Controls")) {
            ImGui::SliderFloat("Mouse Sensitivity", &settings.controls.mouseSensitivity, 0.1f, 5.0f);
            ImGui::Checkbox("Invert Y", &settings.controls.invertY);
            ImGui::Separator();
            ImGui::TextDisabled("Key bindings:");
            for (auto& [action, key] : settings.controls.keyBindings) {
                ImGui::Text("%s : %d", action.c_str(), key);
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::Dummy(ImVec2(0, 10));
    if (ImGui::Button("Save & Back", ImVec2(-1, 32))) {
        settings.Save();
        if (m_Engine.GetAudioEngine()) m_Engine.GetAudioEngine()->SetMasterVolume(settings.audio.masterVolume);
        m_UIState = m_ReturnStateAfterSettings;
    }
    ImGui::End();
}

void GameApp::DrawPauseMenu() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(320, 260));
    ImGui::Begin("Paused", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);
    if (ImGui::Button("Resume", ImVec2(-1, 36))) {
        m_Engine.SetPaused(false);
        m_UIState = GameUIState::Playing;
        Input::Get().SetCursorLocked(true);
    }
    if (ImGui::Button("Settings", ImVec2(-1, 36))) {
        m_ReturnStateAfterSettings = GameUIState::Paused;
        m_UIState = GameUIState::Settings;
    }
    if (ImGui::Button("Quit to Main Menu", ImVec2(-1, 36))) ReturnToMainMenu();
    ImGui::End();
}

void GameApp::OnImGui() {
    switch (m_UIState) {
        case GameUIState::MainMenu: DrawMainMenu(); break;
        case GameUIState::Settings: DrawSettingsMenu(); break;
        case GameUIState::Paused: DrawPauseMenu(); break;
        case GameUIState::Playing: break;
    }
}

void GameApp::OnResize(int width, int height) { FW_UNUSED(width); FW_UNUSED(height); }

void GameApp::OnShutdown() {
    m_Engine.Shutdown();
}

} // namespace fw
