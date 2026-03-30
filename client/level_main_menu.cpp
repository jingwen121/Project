#include "level_main_menu.h"
#include "GameStateManager.h"
#include "GSMList.h"
#include "AEEngine.h"
#include <string>

std::string g_lobbyName;
static s8 g_mainMenuFont = -1;

static void MainMenu_HandleInput()
{
    if (AEInputCheckTriggered(AEVK_RETURN))
    {
        if (g_lobbyName.empty())
            g_lobbyName = "Player";
        next = GS_LEVEL1;
        return;
    }
    if (AEInputCheckTriggered(AEVK_BACK))
    {
        if (!g_lobbyName.empty())
            g_lobbyName.pop_back();
        return;
    }
    if (g_lobbyName.size() < 15)
    {
        for (int i = 0; i < 26; ++i)
        {
            u8 key = static_cast<u8>(AEVK_A + i);
            if (AEInputCheckTriggered(key))
            {
                bool shift = AEInputCheckCurr(AEVK_LSHIFT) || AEInputCheckCurr(AEVK_RSHIFT);
                char c = static_cast<char>('A' + i);
                char out = shift ? c : static_cast<char>(c + ('a' - 'A'));
                g_lobbyName.push_back(out);
            }
        }
        for (int i = 0; i < 10; ++i)
        {
            u8 key = static_cast<u8>(AEVK_0 + i);
            if (AEInputCheckTriggered(key))
            {
                char c = static_cast<char>('0' + i);
                g_lobbyName.push_back(c);
            }
        }
        if (AEInputCheckTriggered(AEVK_SPACE))
            g_lobbyName.push_back(' ');
    }
}

void MainMenu_Load(){
    g_mainMenuFont = AEGfxCreateFont("C:\\Windows\\Fonts\\arial.ttf", 32);
}

void MainMenu_Initialize(){
    g_lobbyName = "Player";
}


void MainMenu_Update(){
    MainMenu_HandleInput();
}

void MainMenu_Draw(){
    AEGfxSetBackgroundColor(0.05f, 0.05f, 0.1f);
    if (g_mainMenuFont >= 0)
    {
        AEGfxSetBlendMode(AE_GFX_BM_BLEND);
        AEGfxPrint(g_mainMenuFont, "Spaceships Lobby", -0.30f, 0.40f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
        std::string nameLine = "Name: " + g_lobbyName;
        AEGfxPrint(g_mainMenuFont, nameLine.c_str(), -0.30f, 0.20f, 0.9f, 0.9f, 0.9f, 0.9f, 1.0f);
        AEGfxPrint(g_mainMenuFont, "Type to edit, Enter to join", -0.30f, 0.05f, 0.7f, 0.8f, 0.8f, 1.0f, 1.0f);
        AEGfxPrint(g_mainMenuFont, "Backspace deletes", -0.30f, -0.05f, 0.5f, 0.8f, 0.8f, 0.8f, 1.0f);
    }
}

void MainMenu_Free(){
}
 
void MainMenu_Unload(){
    if (g_mainMenuFont >= 0)
    {
        AEGfxDestroyFont(g_mainMenuFont);
        g_mainMenuFont = -1;
    }
}