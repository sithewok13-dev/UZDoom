// The game picker (ios/IOSPicker.mm): a card for each main game found in
// Files > On My iPhone > DOOM (DOOM, DOOM II, TNT, Plutonia), held 0.5 s (a
// ring) for its add-ons (SIGIL, No Rest for the Living, ...).
// (From the owner's Raze port, ios/IOSPicker.mm, without LAN games and
// "Switch game".)
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define IOSPICKER_UNAVAILABLE (-2)	// nothing to show (i_ios.mm shows a plain list)

// At start: the picker until a game is picked. names: the games UZDoom
// found (its names, "DOOM 2: KEX Edition"), iLast: the one played last.
// Returns the game's index, or -1 (none: quit), or IOSPICKER_UNAVAILABLE.
// For an add-on, pszFiles gets the files to load with the game (full paths,
// separated by '\n', in load order); otherwise it is empty.
int IOSPicker_Run(const char* const* names, int ctNames, int iLast, char* pszFiles, int ctFilesSize);

#ifdef __cplusplus
}
#endif
