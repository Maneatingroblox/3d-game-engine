#include "game/GameApp.h"

#if FW_PLATFORM_WINDOWS
#include <windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    fw::GameApp app;
    app.Run("Forgeworks", 1600, 900);
    return 0;
}
#else
int main() { return 0; }
#endif
