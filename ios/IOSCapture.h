/* The CAMERA's frame grabs (as the Jedi Knight, Serious Sam, Raze and
   Wolf3D ports' captures): a request for the next frame, read back from the
   renderer at two points of it -- once the 3D view and the first-person
   weapon are drawn (clean), and once the status bar and everything else are
   too (HUD) -- and handed to IOSPhotos to save. A budget keeps the images
   waiting to be saved within a few screens' worth. Plain C, on the game's
   thread (the main thread on iOS); the Linux rig drives this very code. */

#ifndef DOOM_IOSCAPTURE_H
#define DOOM_IOSCAPTURE_H

#ifdef __cplusplus
extern "C" {
#endif

/* What a photo is of (the PHOTO option's ticks; a bit mask) */
#define IOSCAPTURE_CLEAN    1 /* the 3D view and the weapon */
#define IOSCAPTURE_HUD      2 /* ...with the status bar */
#define IOSCAPTURE_HUDBTNS  4 /* ...and the touch buttons, as the screen shows */
#define IOSCAPTURE_ALL      7

/* Full-screen images that may wait to be saved at once */
#define IOSCAPTURE_BUDGET_IMAGES 6

/* What a capture is for (a burst's and a selfie's are clean only) */
#define IOSCAPTURE_MODE_PHOTO  0
#define IOSCAPTURE_MODE_BURST  1
#define IOSCAPTURE_MODE_SELFIE 2

/* Asks for these kinds of the next frame: 0 if one is waiting already or
   there is no room in the budget */
int IOSCapture_Request(int kinds, int mode);
int IOSCapture_HasRoom(int kinds, int n);
int IOSCapture_IsPending(void);
void IOSCapture_Cancel(void);
int IOSCapture_IsBusy(void);
/* (IOSPhotos) one capture of `bytes` saved or given up, saveSeconds after it was handed over */
void IOSCapture_Done(long bytes, double saveSeconds);

typedef struct {
  double readSeconds;
  double saveSeconds;
  long peakBytes;
  double freezeSeconds;
} IOSCaptureStats;
void IOSCapture_TakeStats(IOSCaptureStats *pStats);
void IOSCapture_NoteFreeze(double seconds);

/* The game's frame (w x h pixels), in order: FrameBegin before anything of
   it is drawn; AfterScene once the 3D view and the weapon are, before the
   status bar (not called where none is drawn: the photo view); AfterHud
   once everything is, before it goes to the screen. */
void IOSCapture_FrameBegin(int w, int h);
void IOSCapture_AfterScene(void);
void IOSCapture_AfterHud(void);
/* (before AfterScene) the clean photo's part of the frame: the 3D view's
   window, in the frame's pixels, top left first (the whole frame if not
   inside it) */
void IOSCapture_SceneRect(int x, int y, int w, int h);

/* The renderer's side (screen->IOSReadFrame: Vulkan on iOS, OpenGL on the
   rig): the w x h frame drawn so far into p as RGBA, the bottom row first;
   1 if it could */
int IOSCapture_ReadFrame(int w, int h, unsigned char *p);

/* IOSPhotos: takes the images (malloc'd RGBA, top row first, opaque;
   either may be NULL) and frees them. The HUD one is the whole game
   picture (for the buttons drawn over it). */
void IOSPhotos_SaveCapture(unsigned char *pClean, int cleanW, int cleanH, unsigned char *pHud, int hudW, int hudH,
                           int kinds, int mode);
/* While a burst or a selfie of several goes on: one message for them all */
void IOSPhotos_SetBatch(int bOn);

#ifdef __cplusplus
}
#endif

#endif /* DOOM_IOSCAPTURE_H */
