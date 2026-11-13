/* The Linux test rig for the iOS touch controls (DOOM_TOUCH_RIG_SOURCE):
   the overlay's plain C core (ios/IOSTouch.m, built here as C with
   IOSTOUCH_RIG, without UIKit) with a stand-in for its UIKit view: a phone's
   landscape screen (844 x 390 points, the iPhone 14's insets), the overlay's
   once-a-frame work (IOSTouch_Update) and touches typed in UZDoom's console
   (ios_rig <command>, IOSGame.cpp), so a script of console commands drives
   the very code the app runs. What it does and sees goes to stdout, each
   line starting "rig:".

   Commands (x, y in points; a place can be "@NAME", a button's middle, e.g.
   @FIRE, @NEXTWPN, @TRAYLEVEL, or "@NAME+dx,dy"; id: any small number naming
   the finger):
     down <id> <place>          a finger comes down
     move <id> <place>          it moves there (in 4 pt steps, as UIKit's
                                coalesced touches)
     moveby <id> <dx> <dy>      ...by this much
     up <id> | cancel <id>      it lifts (iOS takes it away)
     tapslice <i>               a tap on the open wheel's slice i
     picklevel <i>              a tap on the open level list's row i (1..)
     gyro <yaw> <pitch>         tilt aiming: degrees (+ left, + up), as a frame's
     state                      what shows, the wheel, the tray, the cheat
*/

#define _GNU_SOURCE
#include <time.h>
#include <stdlib.h>
#include <ctype.h>
#include <strings.h>

#include "../IOSTouch.m"

// (IOSRIG_SCREEN="W H left top right bottom" sets another phone: e.g. the
// iPhone 16 Pro Max's "956 440 62 0 62 21", the HUD mockups' screen)
static double _afRigScreen[6] = { 844.0, 390.0, 47.0, 0.0, 47.0, 21.0 };
#define RIG_W _afRigScreen[0]
#define RIG_H _afRigScreen[1]
#define RIG_IN_LEFT _afRigScreen[2]
#define RIG_IN_TOP _afRigScreen[3]
#define RIG_IN_RIGHT _afRigScreen[4]
#define RIG_IN_BOTTOM _afRigScreen[5]

static double IOSTouchRig_Now(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// (no motion sensor in the rig: tilt aiming comes from the "gyro" command)
static void IOSTouch_GyroRunUpdates(int bRun)
{
  (void)bRun;
}

// The HUD's settings (the view's NSUserDefaults): a file, $HOME/.iosrig-hud
// ("<style> <face> <HUD strength> <buttons' strength>"), so a restart keeps
// them as the phone does (the last: the phone's iosTouchOpacity)
static void IOSTouchRig_HudPrefsPath(char *path, size_t size)
{
  const char *home = getenv("HOME");
  snprintf(path, size, "%s/.iosrig-hud", home ? home : ".");
}

static void IOSTouch_LoadHudPrefs(void)
{
  char path[512];
  IOSTouchRig_HudPrefsPath(path, sizeof(path));
  FILE *f = fopen(path, "r");
  if (!f) return;
  int style = 0, face = 1;
  float k = 1.0f, kb = 1.0f;
  const int n = fscanf(f, "%d %d %f %f", &style, &face, &k, &kb);
  if (n >= 2) {
    _iHudStyle = style;
    _bHudFace = face;
  }
  if (n >= 3) _fHudOpacity = IOSTouch_OpacityStep(k);
  if (n >= 4) _fOpacity = IOSTouch_OpacityStep(kb);
  fclose(f);
  printf("rig: hud prefs loaded: style %d face %d hud %.2f buttons %.2f\n", _iHudStyle, _bHudFace, (double)_fHudOpacity, (double)_fOpacity);
}

static void IOSTouch_SaveHudPrefs(void)
{
  char path[512];
  IOSTouchRig_HudPrefsPath(path, sizeof(path));
  FILE *f = fopen(path, "w");
  if (!f) return;
  fprintf(f, "%d %d %.2f %.2f\n", _iHudStyle, _bHudFace, (double)_fHudOpacity, (double)_fOpacity);
  fclose(f);
}

// (the reference pictures' size: the owner's phone's button, 130 px)
int IOSTouch_CheatIconPixels(void)
{
  return (int)(130.0 / IOSTOUCH_CHEAT_ICON_FILL);
}

static int _bRigLaidOut = 0;
static unsigned int _ulRigFrames = 0;

static void IOSTouchRig_Layout(void)
{
  const IOSTouchRect none = { 0, 0, 0, 0, 0 };
  static int bScreenRead = 0;
  if (!bScreenRead) {
    bScreenRead = 1;
    const char *env = getenv("IOSRIG_SCREEN");
    double a[6];
    if (env && sscanf(env, "%lf %lf %lf %lf %lf %lf", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]) == 6) memcpy(_afRigScreen, a, sizeof(a));
  }
  IOSTouch_LayoutButtons(RIG_W, RIG_H, RIG_IN_LEFT, RIG_IN_TOP, RIG_IN_RIGHT, RIG_IN_BOTTOM, 1, &none, &none, &none);
  IOSTouch_SetHudFrame(RIG_W, RIG_H, RIG_IN_LEFT, RIG_IN_TOP, RIG_IN_RIGHT, RIG_IN_BOTTOM);
  _bRigLaidOut = 1;
}

static const char *const _astrModes[] = { "HIDDEN", "GAMEPLAY", "CONSOLE", "PAUSED" };

// As the view's IOSTouch_SetMode: every change starts clean
static void IOSTouchRig_SetMode(int iMode)
{
  if (iMode != _iMode) {
    printf("rig: mode %s -> %s\n", _astrModes[_iMode], _astrModes[iMode]);
    fflush(stdout);
  }
  _iMode = iMode;
  IOSTouch_ResetTouches(iMode == IOSTOUCH_GAMEPLAY);
}

// As the view's IOSTouch_UpdateOverlay, once a frame
int IOSTouch_Update(void *pSDLWindow, int iMode, const IOSTouchHud *pHud, unsigned int ulFramesDrawn)
{
  (void)pSDLWindow;
  const int iRequests = _iRequests;
  _iRequests = 0;
  _ulRigFrames = ulFramesDrawn;
  if (pHud) _hud = *pHud;
  if (!_bRigLaidOut || IOSTouch_LayoutKey() != _iLaidOutKey) IOSTouchRig_Layout();
  if (iMode != _iMode) IOSTouchRig_SetMode(iMode);
  if (iMode != IOSTOUCH_HIDDEN) {
    const double now = CACurrentMediaTime();
    IOSTouch_TickHolds(now);
    IOSTouch_CountFrames(ulFramesDrawn, now);
  }
  if (iRequests) {
    printf("rig: requests 0x%x\n", iRequests);
    fflush(stdout);
  }
  return iRequests;
}

void IOSTouch_Hide(void)
{
  if (_iMode != IOSTOUCH_HIDDEN) IOSTouchRig_SetMode(IOSTOUCH_HIDDEN);
}

// ---------------------------------------------------------------- the commands

static const struct { const char *name; int button; } _aRigNames[] = {
  { "FIRE", BTN_FIRE }, { "CROUCH", BTN_CROUCH }, { "USE", BTN_USE }, { "JUMP", BTN_JUMP },
  { "NEXTWPN", BTN_NEXTWPN }, { "PREVWPN", BTN_PREVWPN }, { "MAP", BTN_MAP }, { "CHEAT", BTN_CHEAT }, { "CAMERA", BTN_CAMERA },
  { "SAVE", BTN_QUICKSAVE }, { "LOAD", BTN_QUICKLOAD }, { "QUICKSAVE", BTN_QUICKSAVE }, { "QUICKLOAD", BTN_QUICKLOAD },
  { "MENU", BTN_MENU }, { "RESUME", BTN_RESUME }, { "TRAYLOOK", BTN_TRAYLOOK }, { "TRAYRUN", BTN_TRAYRUN },
  { "TRAYSENS", BTN_TRAYSENS }, { "TRAYGYRO", BTN_TRAYGYRO }, { "TRAYOPACITY", BTN_TRAYOPACITY },
  { "TRAYHUD", BTN_TRAYHUD }, { "TRAYFACE", BTN_TRAYFACE }, { "TRAYFPS", BTN_TRAYFPS },
  { "TRAYCAM", BTN_TRAYCAM }, { "TRAYCHEAT", BTN_TRAYCHEAT }, { "TRAYLEVEL", BTN_TRAYLEVEL }, { "TRAYNOCLIP", BTN_TRAYNOCLIP },
  { "TRAYGAMES", BTN_TRAYGAMES }, { "TRAYKEYS", BTN_TRAYKEYS },
};

// The fingers: each a touch's identity (only its address is used)
static char _acRigFingers[IOSTOUCH_MAX_TOUCHES];
static double _afRigX[IOSTOUCH_MAX_TOUCHES], _afRigY[IOSTOUCH_MAX_TOUCHES];

static const void *IOSTouchRig_Finger(int id)
{
  if (id < 0 || id >= IOSTOUCH_MAX_TOUCHES) return NULL;
  return &_acRigFingers[id];
}

// "x y", "@NAME" or "@NAME+dx,dy"; returns how many words it took (0: none)
static int IOSTouchRig_Place(char **argv, int argc, double *px, double *py)
{
  if (argc < 1) return 0;
  if (argv[0][0] == '@') {
    char name[32];
    double dx = 0.0, dy = 0.0;
    const char *plus = strpbrk(argv[0] + 1, "+-");
    const size_t n = plus ? (size_t)(plus - argv[0] - 1) : strlen(argv[0] + 1);
    snprintf(name, sizeof(name), "%.*s", (int)n, argv[0] + 1);
    if (plus) sscanf(plus, "%lf,%lf", &dx, &dy);
    for (size_t i = 0; i < sizeof(_aRigNames) / sizeof(_aRigNames[0]); i++) {
      if (!strcasecmp(name, _aRigNames[i].name)) {
        *px = _aButtons[_aRigNames[i].button].x + dx;
        *py = _aButtons[_aRigNames[i].button].y + dy;
        return 1;
      }
    }
    printf("rig: no button %s\n", name);
    return 0;
  }
  if (argc < 2) return 0;
  *px = atof(argv[0]);
  *py = atof(argv[1]);
  return 2;
}

static void IOSTouchRig_MoveTo(int id, double x, double y)
{
  const void *t = IOSTouchRig_Finger(id);
  const double x0 = _afRigX[id], y0 = _afRigY[id];
  const int steps = (int)ceil(hypot(x - x0, y - y0) / 4.0);
  for (int k = 1; k <= steps; k++) {
    const double f = (double)k / steps;
    const double cx = x0 + (x - x0) * f, cy = y0 + (y - y0) * f;
    IOSTouch_TouchPassed(t, cx, cy);
    IOSTouch_TouchMoved(t, cx, cy);
  }
  _afRigX[id] = x;
  _afRigY[id] = y;
}

static const char *IOSTouchRig_ButtonName(int b)
{
  for (size_t i = 0; i < sizeof(_aRigNames) / sizeof(_aRigNames[0]); i++) {
    if (_aRigNames[i].button == b) return _aRigNames[i].name;
  }
  return "?";
}

static void IOSTouchRig_State(void)
{
  printf("rig: state mode %s reading %d tray %d cheat %d/%d levellist %d wheel %d tapmode %d hot %d holds %d fps %d/%d maxfps %d gyro %d sens %.1f"
         " look %.2f opacity %.2f/%d run %d/%d marker %d camera %d trayplaced %d\n",
         _astrModes[_iMode], _bReading, _bTrayOpen, _bShowCheat, _bCheatOn, _bLevelListOpen, _bWheelOpen, _bWheelTapMode,
         IOSTouch_WheelHot(), IOSTouch_HoldsGame(), _bShowFps, _iFpsShown, _iMaxFps, _iGyroMode, (double)_afGyroSens[_iGyroSens],
         (double)_fLookScale, (double)_fOpacity, _bOpacityOpen, _bMoveRun, _hud.bRunOn, _bRunMarker, _bShowCamera, _bTrayLevelPlaced);
  {
    IOSTouchHudLayout hl;
    IOSTouch_GetHudLayout(&hl);
    const IOSTouchRect op = IOSTouch_OpacityPanel();
    printf("rig: opacity buttons %.2f hud %.2f panel %d (%.0f,%.0f)-(%.0f,%.0f) screen %.0fx%.0f\n", (double)_fOpacity, (double)hl.fAlpha,
           _bOpacityOpen && _bTrayOpen, op.x0, op.y0, op.x1, op.y1, RIG_W, RIG_H);
    printf("rig: hud style %d face %d barTop %.3f pills top %.1f bottom %.1f left %.1f right %.1f striphalf %.1f (frac h %.4f bottom %.4f l %.4f r %.4f half %.4f)\n",
           hl.iStyle, hl.bFace, (double)_hud.fBarTop, _fPillTop, _fPillBottom, _fPillLeft, _fPillRight, _fStripHalf, (double)hl.fPillH,
           (double)hl.fPillBottom, (double)hl.fLeft, (double)hl.fRight, (double)hl.fStripHalf);
    printf("rig: buttons");
    for (int i = 0; i < IOSTOUCH_NUM_BUTTONS; i++) {
      printf(" %s(%.1f,%.1f,r%.0f)", IOSTouchRig_ButtonName(i), _aButtons[i].x, _aButtons[i].y, _aButtons[i].radius);
    }
    printf("\n");
  }
  printf("rig: shown");
  for (int i = 0; i < IOSTOUCH_NUM_BUTTONS; i++) {
    if (IOSTouch_IsShown(i)) printf(" %s(%.0f,%.0f)", IOSTouchRig_ButtonName(i), _aButtons[i].x, _aButtons[i].y);
  }
  printf("\n");
  if (_wpn.bValid) {
    printf("rig: weapons %d wanted %d owned 0x%x ready 0x%x select %d\n", _wpn.ctWeapons, _wpn.iWanted, _wpn.ulOwned, _wpn.ulReady, _wpn.bCanSelect);
  }
  if (_bWheelOpen) {
    const IOSTouchWheelMap *m = IOSTouch_WheelMapOf(_iWheelMap);
    for (int i = 0; i < m->ctSlots; i++) {
      static const char *const astrState[] = { "ready", "noammo", "notfound" };
      IOSTouchWheelLook look;
      IOSTouch_WheelSlotLook(i, IOSTouch_WheelHot(), &look);
      printf("rig: slice %d \"%s\" deg %.1f %s count %d icon %d%s%s\n", i, m->aSlots[i].strName, IOSTouch_WheelSlotDeg(m, i),
             astrState[look.iState], look.iCount, _aIcons[i].bHave, look.bWanted ? " in-hand" : "", look.bHot ? " hot" : "");
    }
    IOSTouchWheelMiddle mid;
    IOSTouch_WheelMiddleState(&mid);
    printf("rig: middle \"%s\" \"%s\" \"%s\"\n", mid.strTitle ? mid.strTitle : "", mid.strCount, mid.strHint ? mid.strHint : "");
  }
  if (_strCamMessage[0] && CACurrentMediaTime() - _tCamMessage < _fCamMessageTime) printf("rig: message \"%s\"\n", _strCamMessage);
  fflush(stdout);
}

void IOSTouchRig_Command(const char *cmd)
{
  char buf[256];
  char *argv[16];
  int argc = 0;
  snprintf(buf, sizeof(buf), "%s", cmd ? cmd : "");
  for (char *p = strtok(buf, " \t"); p && argc < 16; p = strtok(NULL, " \t")) argv[argc++] = p;
  if (argc == 0) return;
  if (!_bRigLaidOut) IOSTouchRig_Layout();
  const double now = CACurrentMediaTime();
  const char *op = argv[0];
  const int id = argc >= 2 ? atoi(argv[1]) : 0;
  double x = 0.0, y = 0.0;
  if (!strcmp(op, "down") && IOSTouchRig_Finger(id) && IOSTouchRig_Place(argv + 2, argc - 2, &x, &y)) {
    _afRigX[id] = x;
    _afRigY[id] = y;
    IOSTouch_TouchBegan(IOSTouchRig_Finger(id), x, y, RIG_W, now, now);
    printf("rig: down %d at %.0f,%.0f\n", id, x, y);
  }
  else if (!strcmp(op, "move") && IOSTouchRig_Finger(id) && IOSTouchRig_Place(argv + 2, argc - 2, &x, &y)) {
    IOSTouchRig_MoveTo(id, x, y);
  }
  else if (!strcmp(op, "moveby") && IOSTouchRig_Finger(id) && argc >= 4) {
    IOSTouchRig_MoveTo(id, _afRigX[id] + atof(argv[2]), _afRigY[id] + atof(argv[3]));
  }
  else if ((!strcmp(op, "up") || !strcmp(op, "cancel")) && IOSTouchRig_Finger(id)) {
    IOSTouch_TouchEnded(IOSTouchRig_Finger(id), _afRigX[id], _afRigY[id], !strcmp(op, "cancel"), now);
    printf("rig: %s %d\n", op, id);
  }
  else if (!strcmp(op, "tapslice") && argc >= 2) {
    // the slice's middle, half way out the ring
    const IOSTouchWheelMap *m = IOSTouch_WheelMapOf(_iWheelMap);
    const int i = atoi(argv[1]);
    if (!_bWheelOpen || i < 0 || i >= m->ctSlots) {
      printf("rig: tapslice %d: no such slice (wheel %d)\n", i, _bWheelOpen);
    }
    else {
      const double a = IOSTouch_WheelSlotDeg(m, i) * M_PI / 180.0, r = (_fWheelR0 + _fWheelR1) * 0.5;
      const void *t = IOSTouchRig_Finger(IOSTOUCH_MAX_TOUCHES - 1);
      x = _fWheelCX + r * cos(a);
      y = _fWheelCY - r * sin(a);
      IOSTouch_TouchBegan(t, x, y, RIG_W, now, now);
      IOSTouch_TouchEnded(t, x, y, 0, now);
      printf("rig: tapslice %d \"%s\" at %.0f,%.0f, wheel now %d\n", i, m->aSlots[i].strName, x, y, _bWheelOpen);
    }
  }
  else if (!strcmp(op, "picklevel") && argc >= 2) {
    printf("rig: picklevel %d (list open %d)\n", id, _bLevelListOpen);
    IOSTouch_PickLevel(id);
  }
  else if (!strcmp(op, "gyro") && argc >= 3) {
    // as IOSTouch_GyroFrame hands a frame's turn on (at SENS 1.0x)
    os_unfair_lock_lock(&_lock);
    if (_bReading) {
      _fGyroYaw += (float)atof(argv[1]);
      _fGyroPitch += (float)atof(argv[2]);
    }
    os_unfair_lock_unlock(&_lock);
    printf("rig: gyro %s %s (reading %d)\n", argv[1], argv[2], _bReading);
  }
  else if (!strcmp(op, "opacityslide") && argc >= 3) {
    // OPACITY's panel: its BUTTONS or HUD slider moved to a value and lifted
    // (as the view's: only while the panel is open)
    const int bOpen = _bOpacityOpen && _bTrayOpen;
    if (bOpen && !strcasecmp(argv[1], "hud")) IOSTouch_SetHudOpacity((float)atof(argv[2]), 1);
    else if (bOpen && !strcasecmp(argv[1], "buttons")) {
      _fOpacity = IOSTouch_OpacityStep((float)atof(argv[2]));
      IOSTouch_SaveHudPrefs(); // (the rig's file keeps it too)
    }
    printf("rig: opacityslide %s %s (panel %d) -> buttons %.2f hud %.2f\n", argv[1], argv[2], bOpen, (double)_fOpacity, (double)_fHudOpacity);
  }
  else if (!strcmp(op, "lookstats")) {
    char line[512];
    if (IOSTouch_LookStats(line, sizeof(line))) printf("rig: %s\n", line);
    else printf("rig: look: no drag\n");
  }
  else if (!strcmp(op, "state")) {
    IOSTouchRig_State();
  }
  else {
    printf("rig: what? %s\n", cmd);
  }
  fflush(stdout);
}
