// The game picker (ios/IOSPicker.mm): a card for each main game found in
// Files > On My iPhone > DOOM (DOOM, DOOM II, TNT, Plutonia), held 0.5 s (a
// ring) for its add-ons (SIGIL, No Rest for the Living, ...).
// "Switch game" (GAMES in MENU's tray) puts the same cards over the game:
// a pick closes the app with a "Tap to start <game>" notification, and the
// next start (within 10 minutes) goes straight to it. (From the owner's
// Raze port, ios/IOSPicker.mm, without LAN games.)
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
// The game picked with "Switch game" before the app closed (in the last 10
// minutes, and still there) is taken without showing anything.
int IOSPicker_Run(const char* const* names, int ctNames, int iLast, char* pszFiles, int ctFilesSize);
// How many games and add-ons the picker has (GAMES shows with more than one)
int IOSPicker_GameCount(void);
// "Switch game" over the game being played. Cancel: pfnDone(0). A pick: the
// settings are written (IOSGame_WriteConfig), the pick kept, a "Tap to
// start" notification asked for and the app closes; pfnDone isn't called.
void IOSPicker_PresentSwitch(void (*pfnDone)(int bPicked));
void IOSGame_WriteConfig(void); // (IOSGame.cpp)

#ifdef __cplusplus
}
#endif
