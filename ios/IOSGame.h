// IOSGame: the game side of the iOS touch controls (from the owner's Raze
// port, ios/IOSGame.h)
#pragma once

// The status bar: where the HUD's screen edges are, in pixels of the 2D
// drawer (x0, y0, x1, y1): the screen less its rounded corners and a notch
void IOSGame_HudFrame(double w, double h, double* pFrame);
