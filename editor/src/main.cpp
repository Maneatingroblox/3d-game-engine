#include "editor/EditorApp.h"

#if FW_PLATFORM_WINDOWS
#include <windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    fw::EditorApp app;
    app.Run("Forgeworks Map Maker", 1920, 1080, true);
    return 0;
}
#else
int main() { return 0; }
#endif
