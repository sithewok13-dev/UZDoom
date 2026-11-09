// IOSGame.cpp: the game side of the iOS touch controls.
// For now only the HUD frame, the whole screen; the touch controls (the
// overlay, ios/IOSTouch.m) replace IOSTouch_GetHudFrame with the screen
// less its corners and a notch.
#include <string.h>
#include "IOSGame.h"

extern "C" void IOSTouch_GetHudFrame(float afFrame[4]);

#ifndef DOOM_IOSTOUCH_OVERLAY
extern "C" void IOSTouch_GetHudFrame(float afFrame[4])
{
	const float full[4] = { 0.f, 0.f, 1.f, 1.f };
	memcpy(afFrame, full, sizeof(full));
}
#endif

void IOSGame_HudFrame(double w, double h, double* pFrame)
{
	float f[4];
	IOSTouch_GetHudFrame(f);
	pFrame[0] = f[0] * w;
	pFrame[1] = f[1] * h;
	pFrame[2] = f[2] * w;
	pFrame[3] = f[3] * h;
}
