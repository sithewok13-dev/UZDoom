/* The touch controls' game side (IOSGame.cpp, from the owner's Raze port's
   ios/IOSGame.h): what UZDoom's main loop, ticcmd and ticker call. Built
   with DOOM_TOUCH (the iOS app, and the Linux test rig). */
#pragma once

#include <stdint.h>
#include "IOSTouch.h"

// D_DoomLoop, once a frame after the frame's events came in (I_StartTic,
// D_ProcessEvents) and before it draws: tells the overlay what the game is
// doing and what the player has, carries out what the player asked for, and
// feeds the look (as the mouse) and tilt aiming (into the view's angles)
void IOSGame_Frame();
// G_BuildTiccmd, once a tic: the stick into the joystick's axes
// (*pfAxisForward: + forward, *pfAxisSide: + left, both -1..1, as
// buttonMap.ButtonAnalog gives them), the buttons into the ticcmd's
// (BT_ATTACK...), NEXT WPN / PREV WPN and the weapon wheel's pick into the
// weapon selection (SendWeaponSlot, SendItemUse). Returns how fast the
// stick moves: 1 running (the run marker, or Always Run), 0 walking, -1 the
// stick isn't in use (a pad's stick goes as cl_analog_run says)
int IOSGame_Ticcmd(float* pfAxisForward, float* pfAxisSide, uint32_t* pulButtons);
// G_DoLoadLevel: a level was started, restarted or loaded
void IOSGame_LevelStarted();
// P_CheckTickerPaused: the game holds still (the weapon wheel or the list of
// levels is open, the CAMERA's selfie, Action Freeze or cinematic burst, or
// "Switch game"), as under the menu: no "Paused", the music plays on
bool IOSGame_HoldsGame();
// The status bar: where the HUD's screen edges are, in pixels of the 2D
// drawer (x0, y0, x1, y1): the screen less its rounded corners and a notch
void IOSGame_HudFrame(double w, double h, double* pFrame);
