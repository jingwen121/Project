// GameStateManager.cpp
// Manages game state transitions and assigns lifecycle callbacks for the active scene.
// 'current', 'previous', and 'next' are coordinated by the main loop.
// GSM_Update selects the appropriate function pointers for the active state.
#include "GameStateManager.h"
#include "GSMList.h"
#include "level_1.h"
#include "level_main_menu.h"

// Tracks current, previous, and next requested game states
int current = 0, previous = 0, next = 0;

// Lifecycle function pointers for the active game state (scene)
FP fpLoad = nullptr, fpInitialize = nullptr, fpUpdate = nullptr, fpDraw = nullptr, fpFree = nullptr, fpUnload = nullptr;

// Sets the starting state; current, previous, and next align to this value so no transition occurs on first frame.
void GSM_Initialize(int startingState)
{
    current = previous = next = startingState;
}

// Updates function pointers to point at the implementation for the current state.
// Call after updating 'current' to reflect the selected scene.
void GSM_Update()
{
    switch (current)
    {
    case GS_LEVEL1:
        fpLoad = Level1_Load;
        fpInitialize = Level1_Initialize;
        fpUpdate = Level1_Update;
        fpDraw = Level1_Draw;
        fpFree = Level1_Free;
        fpUnload = Level1_Unload;
        break;
    case GS_MAIN_MENU:
        fpLoad = MainMenu_Load;
        fpInitialize = MainMenu_Initialize;
        fpUpdate = MainMenu_Update;
        fpDraw = MainMenu_Draw;
        fpFree = MainMenu_Free;
        fpUnload = MainMenu_Unload;
        break;
        
    case GS_RESTART:
    case GS_QUIT:
    default:
        break;
    }
}