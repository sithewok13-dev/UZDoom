// IOSPicker: the game picker shown at launch (iOS)
#pragma once

struct WadStuff;

// (the picker couldn't be shown: i_ios.mm shows a plain list instead)
#define IOSPICKER_UNAVAILABLE -2

// Shows the games found in Files > On My iPhone > DOOM and waits for a
// choice: the index into wads, -1 to quit, or IOSPICKER_UNAVAILABLE
int IOSPicker_Run(WadStuff* wads, int numwads, int defaultiwad);
