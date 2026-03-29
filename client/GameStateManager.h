#pragma once

typedef void(*FP)(void);

extern int current, previous, next;

extern FP fpLoad, fpInitialize, fpUpdate, fpDraw, fpFree, fpUnload;

// ---------------------------------------------------------------------------- 
// This function initializes all game states to be the starting state 
// It should be called once before the start of the system with System_Initialize 
// It initializes the system game state to be the starting game state 
// ---------------------------------------------------------------------------- 
void GSM_Initialize(int startingState);

// ---------------------------------------------------------------------------- 
// This function updates all game states to the new game starts 
// It should be called before the changing of the level 
// It updates function pointers to the respective level functions to be called 
// ---------------------------------------------------------------------------- 
void GSM_Update();