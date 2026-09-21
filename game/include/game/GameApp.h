#pragma once
// The standalone game runtime shell: shows a main menu (New/Continue/
// Settings/Quit), loads a .fwscene and hands off to Engine::Play(), and
// provides a pause menu (Esc) that can resume, reopen settings, or return to
// the main menu. No editor code is linked into this executable - this is
// what ships to players.

#include "engine/core/Application.h"
#include "engine/core/Engine.h"
#include "engine/core/Settings.h"
#include "engine/render/RenderTypes.h"
#include "engine/render/SoftwareRenderer.h"
#include <string>

namespace fw {

enum class GameUIState { MainMenu, Settings, Playing, Paused };

class GameApp : public Application {
public:
    void SetStartupScene(const std::string& path) { m_StartupScene = path; }
    // --play: skip the main menu and start the startup scene immediately.
    void SetStartPlaying(bool play) { m_AutoPlay = play; }

protected:
    bool OnInit() override;
    void OnUpdate(float dt) override;
    void OnRender() override;
    // CPU presentation path (--software / automatic fallback): rasterizes the
    // scene into the software canvas so the game window keeps showing the world
    // without Direct3D (the menu UI is drawn on top by Application).
    void OnSoftwareRender(SoftCanvas& canvas) override;
    void OnImGui() override;
    void OnShutdown() override;
    void OnResize(int width, int height) override;

private:
    // Camera/settings shared by the GPU and CPU paths.
    RenderCamera MakeSceneCamera(float aspect);
    RenderSettings MakeSceneSettings();

    SoftwareImage m_SoftwareImage;

private:
    void DrawMainMenu();
    // In-game HUD (crosshair + status line) shown while playing.
    void DrawHud();
    void DrawSettingsMenu();
    void DrawPauseMenu();
    void StartGame(const std::string& scenePath);
    void ReturnToMainMenu();

    Engine m_Engine;
    GameUIState m_UIState = GameUIState::MainMenu;
    GameUIState m_ReturnStateAfterSettings = GameUIState::MainMenu;
    std::string m_StartupScene = "assets/scenes/default.fwscene";
    bool m_AutoPlay = false;
};

} // namespace fw
