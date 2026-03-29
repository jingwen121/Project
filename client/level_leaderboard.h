#pragma once

// ---------------------------------------------------------------------------- 
// This function loads all necessary assets in Leaderboard 
// It should be called once before the start of the level 
// It loads assets like textures, meshes and music files etc�  
// ---------------------------------------------------------------------------- 
void Leaderboard_Load();

// ---------------------------------------------------------------------------- 
// This function initializes all necessary variables in Leaderboard 
// It should be called once before the start of the level 
// It loads variables of type int, float, double etc�  
// ---------------------------------------------------------------------------- 
void Leaderboard_Initialize();

// ---------------------------------------------------------------------------- 
// This function updates all assets and variables in Leaderboard
// It should be called whenever level is being updated
// It runs the game logic for the level etc�  
// ---------------------------------------------------------------------------- 
/******************************************************************************/

void Leaderboard_Update();

// ---------------------------------------------------------------------------- 
// This function renders all necessary assets in Leaderboard 
// It should be called whenever the level update is called
// It draw assets onto meshes like textures etc�  
// ---------------------------------------------------------------------------- 
void Leaderboard_Draw();

// ---------------------------------------------------------------------------- 
// This function frees all assets in Leaderboard 
// It should be called once when its the end of the level 
// It frees assets like textures, meshes and music files etc�  
// ---------------------------------------------------------------------------- 
void Leaderboard_Free();

// ---------------------------------------------------------------------------- 
// This function unloads all assets in Leaderboard 
// It should be called once when its the end of the level and not in restart gamestate 
// It unloads assets like textures, meshes and music files etc�  
// ---------------------------------------------------------------------------- 
void Leaderboard_Unload();