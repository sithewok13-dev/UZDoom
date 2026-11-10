/* The CAMERA (photos, bursts, selfies, Action Freeze, the cinematic burst),
   as the owner's Jedi Knight, Serious Sam, Dark Forces, Raze and Wolf3D
   ports have it, for DOOM in UZDoom. IOSCamera.cpp is the game's side,
   IOSCapture.cpp takes the frames, IOSPhotos.mm saves them on iOS
   (ios/rig/IOSPhotosRig.cpp on the Linux rig).

   - A photo (CAMERA tapped): the next frame, of each kind ticked in the
     options: clean (the 3D view and the weapon: no status bar, no
     buttons), with the status bar, with the status bar and the buttons.
   - A burst (CAMERA slid sideways): 10 clean photos, 0.2 s apart.
   - A selfie (CAMERA held 0.5 s): a group photo from the player's phone,
     with no player in it: the nearest monsters are brought in close and
     pose together, all facing the lens in their standing frame (now and
     then a step of their walk; never an attack, pain or death frame), an
     arrangement per photo (a huddle, faces peeking in at the frame's edge,
     rows, a corner, a tiny one far off between two faces, a totem or a
     pyramid on shoulders, one held up high); everyone else, and every
     shot, is hidden. 1, 3 or 5 photos (the options). Nobody about: "No one
     around for a group photo".
   - Action Freeze (CAMERA slid down): the game holds still and a camera
     turns round what the player was looking at: one finger orbits it, two
     move it and pinch to zoom (the lens zooms in up to 8x; pinched in past
     1x the camera backs off); it keeps out of walls. The doomguy stands
     where the player is, drawn with the game's own player sprites. The
     shutter takes clean photos; X goes back.
   - The cinematic burst (CAMERA slid up): the game holds still, five
     photos as the camera sweeps round the action (the first is exactly
     what the player saw, without the weapon), then the game goes on.

   Nothing in the game changes: the photo view's camera, the poses, moves
   and hiding (positions, angles, frames, render flags, the sector an actor
   is drawn in -- never its links in the level) and the lens's field of
   view are applied only while the view is drawn and put back exactly right
   after, every frame; the game holds still meanwhile (no tic runs: as under
   the menu), and the camera's random numbers are its own. Single player
   only. */

#ifndef DOOM_IOSCAMERA_H
#define DOOM_IOSCAMERA_H

#ifdef __cplusplus
extern "C" {
#endif

/* What the overlay calls (IOSTouch.m) */
void IOSCamera_Photo(void);       /* CAMERA tapped (in Action Freeze: the shutter) */
void IOSCamera_Selfie(void);      /* CAMERA held IOSTOUCH_SELFIE_HOLD */
void IOSCamera_Burst(void);       /* CAMERA slid sideways */
void IOSCamera_Freeze(void);      /* CAMERA slid down (in Action Freeze: out of it) */
void IOSCamera_Cinematic(void);   /* CAMERA slid up */
int IOSCamera_IsBusy(void);       /* a selfie, a freeze, a cinematic burst, a burst or a photo waiting: CAMERA dimmed, no new press */
int IOSCamera_Stage(void);        /* 0 none, 1 selfie, 2 Action Freeze, 3 cinematic burst: the overlay hides its buttons */
void IOSCamera_ShowOptions(void); /* the tray's CAMERA held: the options (IOSPhotos) */

/* The options (IOSPhotos keeps them in the app's settings): what a tap
   takes (IOSCAPTURE_* bits), photos per selfie (1, 3, 5), FREEZE on */
int IOSCamera_GetKinds(void);
void IOSCamera_SetKinds(int kinds);
int IOSCamera_GetSelfieShots(void);
void IOSCamera_SetSelfieShots(int n);
int IOSCamera_GetFreezeOn(void);
void IOSCamera_SetFreezeOn(int on);

/* Action Freeze's moves (IOSPhotos' view on iOS; the rig's commands):
   degrees round the focus (+yaw to the left, +pitch from higher up); the
   focus moved across the view (fractions of the view's height: right,
   down); the pinch (> 1 in) */
int IOSCamera_IsFrozen(void);
void IOSCamera_FreezeOrbit(float yawDeg, float pitchDeg);
void IOSCamera_FreezePan(float right, float down);
void IOSCamera_FreezeZoom(float factor);
float IOSCamera_FreezeZoomNow(void);
void IOSCamera_FreezeEnd(void);
int IOSCamera_NumTaken(void);     /* photos so far in this selfie or cinematic burst */

/* The host's side (IOSPhotos.mm, or the rig's) */
void IOSPhotos_Init(void);              /* once: the remembered options */
void IOSPhotos_FreezeShown(int bOn);    /* Action Freeze's controls on or off */
void IOSCamera_Message(const char *str);/* a line for the player (the overlay's message line) */

#ifdef __cplusplus
}

/* The game's side: what IOSGame.cpp and UZDoom's renderer call */
struct FRenderViewpoint;
class AActor;
struct sector_t;
// Once a frame from IOSGame_Frame (bPlay: a level being played, single
// player, no menu or console): the bursts, the selfie's and cinema's
// photos, the freeze camera; anything else ends whatever the camera was
// doing
void IOSCamera_Frame(bool bPlay);
// P_CheckTickerPaused: the game holds still (a selfie, Action Freeze, cinema)
bool IOSCamera_HoldsGame();
// A save, a load, a level change: everything ends now
void IOSCamera_Abort();
// D_Display, around the 3D view (RenderView): the photo view's poses,
// moves and hiding applied, then everything put back
void IOSCamera_RenderBegin();
void IOSCamera_RenderEnd();
// D_Display: the lens's field of view (degrees in, degrees out)
double IOSCamera_Fov(double fovDeg);
// R_SetupFrame, once the player's view is worked out: the photo view's
// camera instead (position, angles, sector; the player's own body shown)
void IOSCamera_View(FRenderViewpoint &vp);
// The renderer: no first-person weapon, no status bar in the photo view
bool IOSCamera_HidesWeapon();
bool IOSCamera_HidesHud();
// HWDrawInfo::RenderThings: the posed people drawn in the sector they are
// posed in (fn for each one there; not linked into it)
void IOSCamera_ThingsIn(sector_t *sec, void (*fn)(AActor *, void *), void *ctx);
// The frame's grabs (D_Display; the renderer's Update): its size first,
// then once the 3D view is drawn (not in the photo view), then once
// everything is
void IOSCamera_FrameBegin(int w, int h);
void IOSCamera_AfterScene();
extern "C" void IOSCamera_AfterHud(void);
#ifdef IOSTOUCH_RIGHOOKS
// The rig's "ios_cam ..." commands
void IOSCamera_RigCommand(const char *cmd);
#endif
#endif

#endif /* DOOM_IOSCAMERA_H */
