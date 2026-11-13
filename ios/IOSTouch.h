/* On-screen touch controls for iOS, for DOOM in UZDoom (DOOM, DOOM II,
   Final DOOM and their add-ons, and mods): the controls standard of the
   Jedi Knight and Serious Sam ports, ported from the owner's Raze port's
   IOSTouch.m (itself from Serious Sam's).

   A UIKit overlay (IOSTouch.m) sits on top of SDL's game view while a level
   is being played. It turns touches into:
     - a floating move stick under the left thumb (anywhere in the left 42%
       of the screen): analog movement, up the screen is forward; with
       Always Run off, a run marker (^^) above it: the thumb slid up onto it
       runs straight ahead (as in the Jedi Knight and Wolf3D ports)
     - drag-to-look anywhere else, fed in as mouse movement (so UZDoom's
       mouse sensitivity and invert settings apply), at the tray's LOOK speed
     - tilt aiming (MENU's tray's GYRO: OFF, TOUCH, ALWAYS; SENS 1.0x, 1.5x,
       2.0x, 3.0x, the gyro's own), straight into the player's view angles
     - buttons. Bottom right: FIRE, with USE and JUMP on an arc around it
       and CROUCH as the secondary button beside FIRE (JUMP and CROUCH only
       where the level lets the player jump / crouch: not in classic DOOM).
       Dragging on FIRE, the arc and CROUCH also looks. Top left: NEXT WPN,
       PREV WPN and MAP (the automap). Top middle: CAMERA (IOSCamera.h).
       Top right: CHEAT (cheat mode), SAVE and LOAD (a tap: the game's Save
       Game / Load Game menu; held 0.3 s with a ring: quick save / quick
       load) and MENU (a tap: the game's menu; held 0.45 s: the tray).
     - NEXT WPN and PREV WPN: a quick tap is the next / previous weapon; a
       10 pt slide opens the weapon wheel at once, a 0.3 s hold opens it to
       tap. One slice per weapon in the player's weapon slots (DOOM's own
       order: fist, chainsaw, pistol, shotgun, super shotgun where the game
       has it, chaingun, rocket launcher, plasma rifle, BFG 9000), with its
       pickup sprite from the loaded game and its ammo. The game holds still
       while it is open.
     - MENU's tray: LOOK (0.5x to 3x), ALWAYS RUN, SENS, GYRO, OPACITY,
       HUD (SPLIT, STRIP, CLASSIC, NONE), FACE (in the HUD), FPS (60/120),
       CAMERA, CHEAT, LEVEL (single player, in a level),
       NOCLIP (the cheat on), GAMES (more than one game found: "Switch
       game") and the keyboard (the console, for typing cheats and
       commands), with the build stamp under it.
   Buttons act on the player directly (UZDoom's ticcmd), whatever keys are
   bound. In menus, intermissions, the finale and while a level loads the
   overlay hides and touches reach SDL as mouse clicks.

   Threads: UZDoom runs its game and its input on the main thread, so every
   call here is the main thread's. */

#ifndef DOOM_IOSTOUCH_H
#define DOOM_IOSTOUCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* What the game is doing (IOSTouch_Update), which decides what shows */
enum {
  IOSTOUCH_HIDDEN = 0, /* menus, intermissions, the finale, loading: touches are mouse clicks */
  IOSTOUCH_GAMEPLAY,   /* a level is being played */
  IOSTOUCH_CONSOLE,    /* the console is open over the game: only MENU (and its tray) */
  IOSTOUCH_PAUSED,     /* the game is paused (the pause key): RESUME and MENU */
};

/* What the game says each frame besides the weapons */
typedef struct IOSTouchHud {
  int bValid;
  int bCanJump;        /* the level lets the player jump (JUMP shows) */
  int bCanCrouch;      /* ...and crouch (CROUCH shows) */
  int bNoSaves;        /* no QUICK SAVE / QUICK LOAD (the game can't save now) */
  int bCheatAllowed;   /* the cheat works here (single player, a level being played, cheats allowed) */
  const char *strCheatRefusal; /* ...and if not, why (a static string, or NULL) */
  int bNoclip;         /* the game's noclip is on now */
  int bRunOn;          /* Always Run (cl_run) is on: the tray's ALWAYS RUN lit, no run marker */
  int bAutomap;        /* the automap is open (MAP lit) */
  int bGames;          /* more than one game found: the tray's GAMES shows */
  int ctLevels;        /* the levels the tray's LEVEL lists (0: none) */
  unsigned int uWorld; /* changes with every level started or loaded */
  float afHudFrame[4]; /* (filled by the overlay: IOSTouch_GetHudFrame) */
  float fBarTop;       /* the HUD CLASSIC: the top of DOOM's status bar, a fraction of the screen's
                          height from the top (0: none shows); FIRE and its arc keep above it */
} IOSTouchHud;

/* What the player asked for, carried out by the game (IOSTouch_Update's result) */
enum {
  IOSTOUCH_REQ_MENU      = 1 << 0, /* Escape: the menu (with the console open: just close the console) */
  IOSTOUCH_REQ_CONSOLE   = 1 << 1, /* open or close the console (MENU's tray) */
  IOSTOUCH_REQ_QUICKSAVE = 1 << 2,
  IOSTOUCH_REQ_QUICKLOAD = 1 << 3,
  IOSTOUCH_REQ_RESUME    = 1 << 4, /* unpause */
  IOSTOUCH_REQ_CHEATON   = 1 << 5, /* the cheat came on: god mode on, everything given */
  IOSTOUCH_REQ_CHEATOFF  = 1 << 6, /* the cheat went off: god mode off (what it gave is kept) */
  IOSTOUCH_REQ_LEVEL     = 1 << 7, /* the list of levels: go to IOSTouch_TakeLevelPick() */
  IOSTOUCH_REQ_NOCLIP    = 1 << 8, /* the cheat: toggle the game's noclip */
  IOSTOUCH_REQ_MAP       = 1 << 9, /* MAP: open or close the automap */
  IOSTOUCH_REQ_RUN       = 1 << 10, /* the tray's ALWAYS RUN: cl_run on or off */
  IOSTOUCH_REQ_SAVEMENU  = 1 << 11, /* SAVE tapped: the game's Save Game menu */
  IOSTOUCH_REQ_LOADMENU  = 1 << 12, /* LOAD tapped: the game's Load Game menu */
  IOSTOUCH_REQ_GAMES     = 1 << 13, /* the tray's GAMES: "Switch game" (IOSPicker_PresentSwitch) */
};

/* Buttons as the game reads them (IOSTouchInput.ulButtons) */
enum {
  IOSTOUCH_FIRE       = 1 << 0,
  IOSTOUCH_USE        = 1 << 1, /* open / use */
  IOSTOUCH_JUMP       = 1 << 2,
  IOSTOUCH_CROUCH     = 1 << 3,
  IOSTOUCH_NEXTWEAPON = 1 << 4,
  IOSTOUCH_PREVWEAPON = 1 << 5,
  IOSTOUCH_NUMBUTTONS = 6
};
typedef struct IOSTouchInput {
  float fMoveX;           /* move stick: -1..1, right is + */
  float fMoveY;           /* -1..1, forward is + */
  int bRun;               /* the stick's thumb on its run marker: running (straight ahead) */
  unsigned int ulButtons; /* IOSTOUCH_FIRE... held (or a quick tap) for this read */
  int iSelectWeapon;      /* -1 none, else the wheel's pick (an index into IOSTouchWeapons' list):
                             reported for two reads, then -1 for at least one, never in a read with
                             NEXT/PREV or the read after one */
} IOSTouchInput;

/* The weapon wheel: what the player has, as the game tells it every frame
   (before IOSTouch_Update). The game makes the list (UZDoom: the player's
   weapon slots, in the order NEXT WPN goes through them); weapon numbers are
   indexes into it, 0..ctWeapons-1. */
#define IOSTOUCH_WPN_MAX 14
#define IOSTOUCH_WPN_NAME 32
/* The slices' colours, by the kind of ammo the weapon takes */
enum { IOSTOUCH_WCOL_NONE = 0, IOSTOUCH_WCOL_SHELLS, IOSTOUCH_WCOL_BULLETS, IOSTOUCH_WCOL_ROCKETS, IOSTOUCH_WCOL_GRENADES,
       IOSTOUCH_WCOL_NAPALM, IOSTOUCH_WCOL_SNIPER, IOSTOUCH_WCOL_ELECTRICITY, IOSTOUCH_WCOL_CANNONBALLS, IOSTOUCH_WCOL_COUNT };
typedef struct IOSTouchWeapons {
  int bValid;            /* the player and its weapons exist */
  int bCanSelect;        /* alive, in a level: it would take a pick now */
  int ctWeapons;         /* the list's length (0: no wheel; the taps still step through the weapons) */
  unsigned int uList;    /* changes whenever the list does (another game, a mod): the wheel's icons are asked again */
  char astrName[IOSTOUCH_WPN_MAX][IOSTOUCH_WPN_NAME]; /* each weapon's name, as the game tags it */
  int aiColour[IOSTOUCH_WPN_MAX];                     /* IOSTOUCH_WCOL_* */
  int iWanted;           /* the weapon in hand, or being switched to (-1: none of the list) */
  unsigned int ulOwned;  /* bit w: weapon w owned */
  unsigned int ulReady;  /* bit w: owned and it has ammo for it -- what the game would switch to */
  int bInfiniteAmmo;     /* no counts shown */
  int aiAmmo[IOSTOUCH_WPN_MAX];     /* the ammo count for weapon w, -1: none to show */
  int aiMaxAmmo[IOSTOUCH_WPN_MAX];
} IOSTouchWeapons;

/* The pictures (IOSGame.cpp makes them from the game's own art, as RGBA,
   not premultiplied, at most IOSTOUCH_ICON_MAX square): the weapons whose
   wheel icon the overlay still wants (bit w; each asked for once per list,
   until it comes), each one handed over (NULL: there is none, the slice
   shows the name) */
#define IOSTOUCH_ICON_MAX 128
unsigned int IOSTouch_WantedIcons(void);
void IOSTouch_SetWeaponIcon(int iWeapon, const unsigned char *pubRGBA, int iWidth, int iHeight);

/* Once per frame: shows or hides the overlay for iMode, follows the SDL
   window (an SDL_Window *). ulFramesDrawn: the game's count of frames drawn,
   which the FPS readout counts. Returns the IOSTOUCH_REQ_* the player asked
   for since the last call. */
int IOSTouch_Update(void *pSDLWindow, int iMode, const IOSTouchHud *pHud, unsigned int ulFramesDrawn);
/* Where the HUD lays itself out: the screen less its rounded corners, the
   home indicator and a notch, as x0, y0, x1, y1 fractions of the screen. The
   whole screen until IOSTouch_Update has seen SDL's view. */
void IOSTouch_GetHudFrame(float afFrame[4]);
/* The HUD (MENU's tray's HUD and FACE, remembered; IOSGame.cpp and the
   Doom status bar's ZScript draw it): SPLIT, two pills at the bottom (you
   on the left; the gun and the keys on the right: FIRE and its arc move up
   clear of it), STRIP (one pill in the middle), CLASSIC (DOOM's status
   bar: FIRE and its arc keep above it) or NONE; FACE, the doomguy's face in
   the pills. Where the pills go, in fractions of the screen: */
enum { IOSTOUCH_HUD_SPLIT = 0, IOSTOUCH_HUD_STRIP, IOSTOUCH_HUD_CLASSIC, IOSTOUCH_HUD_NONE, IOSTOUCH_HUD_COUNT };
typedef struct IOSTouchHudLayout {
  int iStyle;          /* IOSTOUCH_HUD_* */
  int bFace;           /* the face in the pills */
  float fAlpha;        /* how strongly the HUD shows (the tray's HUD held: a slider), 0.2..1 */
  float fPillH;        /* a pill's height (of the screen's height) */
  float fPillBottom;   /* the pills' bottom edge (of the height, from the top) */
  float fLeft, fRight; /* SPLIT's pills' outer edges (of the width) */
  float fStripHalf;    /* STRIP: its pill reaches at most this far either side of the middle (of the width),
                          clear of the buttons */
  float fMsgTop;       /* the game's pickup messages start here (of the height): below the top-left buttons */
  float fMsgLeft;      /* ...lined up with NEXT WPN's left edge (of the width) */
} IOSTouchHudLayout;
void IOSTouch_GetHudLayout(IOSTouchHudLayout *pLayout);
/* The frame rate asked for in the tray (FPS held): 60 or 120. The screen is
   asked for it. */
int IOSTouch_MaxFps(void);
/* Hide now (a level is loading, or the app is going to the background), and
   stop tilt aiming's motion updates; the next update shows it again */
void IOSTouch_Hide(void);
/* Once per game tic: the stick and the buttons. A button tapped too quickly
   for any tic to see it is reported for two reads, then up for one. */
void IOSTouch_ReadInput(IOSTouchInput *pInput);
/* The stick alone */
void IOSTouch_ReadMove(float *pfMoveX, float *pfMoveY);
/* Look movement since the last call, in mouse counts (+x right, +y down),
   at the tray's LOOK speed */
void IOSTouch_TakeLook(float *pfDX, float *pfDY);
/* The look's timing since the last call, as a line for the play log (the
   drag frames, frames with no new touches or no turn, touches per frame,
   the newest touch's age, the lag, the longest gap between touches); 0 if
   there was no drag in that time */
int IOSTouch_LookStats(char *str, int size);
/* Tilt aiming since the last call, in degrees for the view to turn (+ left)
   and tilt (+ up), straight into the player's view angles */
void IOSTouch_TakeGyro(float *pfYaw, float *pfPitch);

/* Every frame before IOSTouch_Update: what the player has, for the weapon
   wheel. A wheel open while the game couldn't take a pick (or whose list
   changed) closes, picking nothing. */
void IOSTouch_SetWeapons(const IOSTouchWeapons *pw);
/* Whether the weapon wheel (or the cheat's list of levels) is open in a
   level being played: the game holds still (as under the menu: game sounds
   pause, the music plays on, no "Paused") */
int IOSTouch_HoldsGame(void);
/* The cheat (sithewok): the word typed in the console (it switches the
   cheat on or off), and the level picked (1..ctLevels; 0: none) once
   IOSTOUCH_REQ_LEVEL comes */
void IOSTouch_CheatTyped(void);
int IOSTouch_CheatOn(void);
int IOSTouch_TakeLevelPick(void);
void IOSTouch_CheatLevelFailed(void); /* the level picked couldn't be started */
/* CHEAT's picture, from the game's own art (RGBA as the wheel's icons): the
   overlay asks once (IOSTouch_WantsCheatIcon); NULL: none, CHEAT says CHEAT */
int IOSTouch_WantsCheatIcon(void);
void IOSTouch_SetCheatIcon(const unsigned char *pubRGBA, int iWidth, int iHeight);
/* ...or, where the game has DOOM's pentagram wall (MWALL4_1), the round
   icon made of it, on and off (iSize x iSize RGBA, not premultiplied): its
   red circle is the button's edge (no ring drawn round it), the button
   IOSTOUCH_CHEAT_ICON_FILL of the picture's width; IOSTouch_CheatIconPixels:
   the picture's width wanted (the button's, in the screen's pixels, divided
   by IOSTOUCH_CHEAT_ICON_FILL) */
#define IOSTOUCH_CHEAT_ICON_FILL (1.0 / 1.16)
#define IOSTOUCH_CHEAT_ICON_MAX 512
int IOSTouch_CheatIconPixels(void);
void IOSTouch_SetCheatRoundIcons(const unsigned char *pubOn, const unsigned char *pubOff, int iSize);
/* A line of news over the game for fSeconds */
void IOSTouch_ShowMessage(const char *str, double fSeconds);

/* Provided by the game (IOSGame.cpp): the cheat's list of levels (iLevel
   1..ctLevels: its name and its map's lump; 0 if there is none), and the
   one being played (1.., 0: none of them) */
int IOSGame_LevelInfo(int iLevel, char *strName, int ctName, char *strFile, int ctFile);
int IOSGame_CurrentLevel(void);

#ifdef __cplusplus
}
#endif

#endif /* DOOM_IOSTOUCH_H */
