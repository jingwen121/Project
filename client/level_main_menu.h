#pragma once

// ---------------------------------------------------------------------------- 
// This function loads all necessary assets in MainMenu 
// It should be called once before the start of the level 
// It loads assets like textures, meshes and music files etc�  
// ---------------------------------------------------------------------------- 
void MainMenu_Load();

// ---------------------------------------------------------------------------- 
// This function initializes all necessary variables in MainMenu 
// It should be called once before the start of the level 
// It loads variables of type int, float, double etc�  
// ---------------------------------------------------------------------------- 
void MainMenu_Initialize();

// ---------------------------------------------------------------------------- 
// This function updates all assets and variables in MainMenu
// It should be called whenever level is being updated
// It runs the game logic for the level etc�  
// ---------------------------------------------------------------------------- 
/******************************************************************************/

void MainMenu_Update();

// ---------------------------------------------------------------------------- 
// This function renders all necessary assets in MainMenu 
// It should be called whenever the level update is called
// It draw assets onto meshes like textures etc�  
// ---------------------------------------------------------------------------- 
void MainMenu_Draw();

// ---------------------------------------------------------------------------- 
// This function frees all assets in MainMenu 
// It should be called once when its the end of the level 
// It frees assets like textures, meshes and music files etc�  
// ---------------------------------------------------------------------------- 
void MainMenu_Free();

// ---------------------------------------------------------------------------- 
// This function unloads all assets in MainMenu 
// It should be called once when its the end of the level and not in restart gamestate 
// It unloads assets like textures, meshes and music files etc�  
// ---------------------------------------------------------------------------- 
void MainMenu_Unload();