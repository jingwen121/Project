
// Simple main menu that lets the player type a lobby name and press Enter to join Level 1.
#include "level_main_menu.h"
#include "GameStateManager.h"
#include "GSMList.h"
#include "AEEngine.h"
#include <string>

// Lobby display name typed by user
std::string g_lobbyName;
// Font used to render menu text (-1 means not created yet)
static s8 g_mainMenuFont = -1;

static void MainMenu_HandleInput()
{
    // Enter: confirm name (default "Player" if empty) and transition to Level 1
    if (AEInputCheckTriggered(AEVK_RETURN))
    {
        if (g_lobbyName.empty())
            g_lobbyName = "Player";
        next = GS_LEVEL1;
        return;
    }
    // Backspace: delete last character
    if (AEInputCheckTriggered(AEVK_BACK))
    {
        if (!g_lobbyName.empty())
            g_lobbyName.pop_back();
        return;
    }
    // Typing accepted while name length < 15
    if (g_lobbyName.size() < 15)
    {
        // Letters A-Z; Shift keeps uppercase, otherwise converted to lowercase
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
        // Digits 0-9
        for (int i = 0; i < 10; ++i)
        {
            u8 key = static_cast<u8>(AEVK_0 + i);
            if (AEInputCheckTriggered(key))
            {
                char c = static_cast<char>('0' + i);
                g_lobbyName.push_back(c);
            }
        }
        // Space character
        if (AEInputCheckTriggered(AEVK_SPACE))
            g_lobbyName.push_back(' ');
    }
}

void MainMenu_Load(){
    // Create font used to render main menu text
    g_mainMenuFont = AEGfxCreateFont("C:\\Windows\\Fonts\\arial.ttf", 32);
}

void MainMenu_Initialize(){
    // Default lobby name shown until the user types
    g_lobbyName = "Player";
}


void MainMenu_Update(){
    // Poll input and update lobby name/state transitions
    MainMenu_HandleInput();
}

void MainMenu_Draw(){
    // Background for the menu screen
    AEGfxSetBackgroundColor(0.05f, 0.05f, 0.1f);
    if (g_mainMenuFont >= 0)
    {
        // Enable blending for text
        AEGfxSetBlendMode(AE_GFX_BM_BLEND);
        // Title
        AEGfxPrint(g_mainMenuFont, "Fighting Game Lobby", -0.30f, 0.40f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
        // Current name line
        std::string nameLine = "Name: " + g_lobbyName;
        AEGfxPrint(g_mainMenuFont, nameLine.c_str(), -0.30f, 0.20f, 0.9f, 0.9f, 0.9f, 0.9f, 1.0f);
        // Instructions
        AEGfxPrint(g_mainMenuFont, "Type to edit, Enter to join", -0.30f, 0.05f, 0.7f, 0.8f, 0.8f, 1.0f, 1.0f);
        AEGfxPrint(g_mainMenuFont, "Backspace deletes", -0.30f, -0.05f, 0.5f, 0.8f, 0.8f, 0.8f, 1.0f);
    }
}

void MainMenu_Free(){
    // No transient resources to free in the main menu
}
 
void MainMenu_Unload(){
    // Destroy the font when leaving the menu scene
    if (g_mainMenuFont >= 0)
    {
        AEGfxDestroyFont(g_mainMenuFont);
        g_mainMenuFont = -1;
    }
}