#include "../common/game_protocol.h"
#include "../common/game_math.h"
#include "AEEngine.h"
#include "GameStateManager.h"
#include "GSMList.h"
#include "level_1.h"
#include <cstdio>
#include <string>
#include <windows.h>

#include <array>
#include <atomic>
#include <mutex>
#include <thread>
#include <cstring>
#include <chrono>
#include <cmath>
#include <crtdbg.h>

// wWinMain: Windows entry point for the client executable.
// hInstance: current module handle; hPrevInstance: always null in modern Windows;
// lpCmdLine: command-line (wide); nCmdShow: window show state.
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    // Enable CRT leak checks in Debug.
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // Initialize Alpha Engine (window 1600x900, maxFps=60, vsync=false).
    if (!AESysInit(hInstance, nCmdShow, 1600, 900, 0, 60, FALSE, 0))
        return -1;

    AESysSetWindowTitle("2-4 Player Fighting Game");

    // Initialize Game State Manager, then load/initialize the first state.
    GSM_Initialize(GS_MAIN_MENU);
    GSM_Update();
    if (fpLoad) fpLoad();
    if (fpInitialize) fpInitialize();

    // Main frame loop: start frame, update/draw, handle window close,
    // end frame, and process state transitions (free/unload, load/init).
    while (current != GS_QUIT)
    {
        AESysFrameStart();

        if (fpUpdate) fpUpdate();
        if (fpDraw) fpDraw();

        if (!AESysDoesWindowExist())
            next = GS_QUIT;

        AESysFrameEnd();

        if (next != current)
        {
            if (fpFree) fpFree();
            if (fpUnload) fpUnload();

            previous = current;
            current = next;

            if (current == GS_RESTART)
            {
                next = previous;
                current = previous;
            }

            if (current != GS_QUIT)
            {
                GSM_Update();
                if (fpLoad) fpLoad();
                if (fpInitialize) fpInitialize();
            }
        }
    }

    AESysExit();
    return 0;
}