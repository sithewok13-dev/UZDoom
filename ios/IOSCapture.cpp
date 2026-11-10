// The CAMERA's frame grabs: see IOSCapture.h (from the owner's Raze port's
// iosCapture.cpp). Plain C in a C++ file.


#include "IOSCapture.h"
#include "IOSCamera.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

static int _iWanted = 0;            // the kinds the next frame should take (0: none waiting)
static int _iMode = IOSCAPTURE_MODE_PHOTO;
static long _slBytesSaving = 0;     // handed to IOSPhotos, not saved yet
static int _ctSaving = 0;
static unsigned char *_pClean = NULL;
static int _iW = 0, _iH = 0;        // this frame's size (FrameBegin)
// The clean photo's part of it (the 3D view's window, without the status
// bar's strip; top left first) and its size once cut
static int _iSceneX = 0, _iSceneY = 0, _iSceneW = 0, _iSceneH = 0;
static int _iCleanW = 0, _iCleanH = 0;
static int _bSceneSeen = 0;         // AfterScene ran in this frame
static IOSCaptureStats _stats;

static double IOSCapture_Now(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

// One full-screen image (the last frame's size)
static long IOSCapture_ImageBytes(void)
{
  return (_iW > 0 && _iH > 0) ? (long)_iW * _iH * 4 : 0;
}

// The full-screen images a capture of these kinds holds until it is saved:
// the clean one and the one with the HUD (read back here), and the HUD with
// the buttons drawn on (a third, made by IOSPhotos from the HUD one)
static int IOSCapture_NumImages(int kinds)
{
  return ((kinds & IOSCAPTURE_CLEAN) ? 1 : 0) + ((kinds & (IOSCAPTURE_HUD | IOSCAPTURE_HUDBTNS)) ? 1 : 0)
         + ((kinds & IOSCAPTURE_HUDBTNS) ? 1 : 0);
}

int IOSCapture_HasRoom(int kinds, int n)
{
  const long image = IOSCapture_ImageBytes();
  if (image <= 0) return 0;
  return _slBytesSaving + image * IOSCapture_NumImages(kinds & IOSCAPTURE_ALL) * n <= image * IOSCAPTURE_BUDGET_IMAGES;
}

int IOSCapture_Request(int kinds, int mode)
{
  kinds &= IOSCAPTURE_ALL;
  if (mode != IOSCAPTURE_MODE_PHOTO) kinds = IOSCAPTURE_CLEAN;
  if (!kinds || _iWanted || !IOSCapture_HasRoom(kinds, 1)) return 0;
  _iWanted = kinds;
  _iMode = mode;
  return 1;
}

int IOSCapture_IsPending(void)
{
  return _iWanted != 0;
}

void IOSCapture_Cancel(void)
{
  free(_pClean);
  _pClean = NULL;
  _iWanted = 0;
}

int IOSCapture_IsBusy(void)
{
  return _iWanted || _ctSaving;
}

void IOSCapture_Done(long bytes, double saveSeconds)
{
  if (_ctSaving > 0) _ctSaving--;
  _slBytesSaving -= bytes;
  if (_slBytesSaving < 0 || !_ctSaving) _slBytesSaving = 0;
  _stats.saveSeconds = saveSeconds;
}

void IOSCapture_TakeStats(IOSCaptureStats *pStats)
{
  if (pStats) *pStats = _stats;
  _stats.saveSeconds = 0;
  _stats.peakBytes = _slBytesSaving;
  _stats.freezeSeconds = 0;
}

void IOSCapture_NoteFreeze(double seconds)
{
  _stats.freezeSeconds = seconds;
}

// Hands what was taken to IOSPhotos
static void IOSCapture_Hand(unsigned char *pHud)
{
  unsigned char *pClean = _pClean;
  const int kinds = _iWanted;
  _pClean = NULL;
  _iWanted = 0;
  _ctSaving++;
  // (the same count IOSPhotos hands back to IOSCapture_Done)
  _slBytesSaving += (long)_iCleanW * _iCleanH * 4 * (pClean ? 1 : 0)
                    + (long)_iW * _iH * 4 * ((pHud ? 1 : 0) + ((pHud && (kinds & IOSCAPTURE_HUDBTNS)) ? 1 : 0));
  if (_slBytesSaving > _stats.peakBytes) _stats.peakBytes = _slBytesSaving;
  IOSPhotos_SaveCapture(pClean, _iCleanW, _iCleanH, pHud, _iW, _iH, kinds, _iMode);
}

// The frame as drawn so far: RGBA, the top row first, fully opaque
static unsigned char *IOSCapture_Read(void)
{
  const double t0 = IOSCapture_Now();
  const size_t row = (size_t)_iW * 4;
  unsigned char *p = (unsigned char *)malloc(row * _iH);
  unsigned char *tmp = (unsigned char *)malloc(row);
  if (!p || !tmp || !IOSCapture_ReadFrame(_iW, _iH, p)) {
    free(p);
    free(tmp);
    return NULL;
  }
  // GL's rows come bottom up
  for (int y = 0; y < _iH / 2; y++) {
    unsigned char *a = p + row * y;
    unsigned char *b = p + row * (_iH - 1 - y);
    memcpy(tmp, a, row);
    memcpy(a, b, row);
    memcpy(b, tmp, row);
  }
  free(tmp);
  for (size_t i = 3; i < row * _iH; i += 4) p[i] = 255;
  _stats.readSeconds = IOSCapture_Now() - t0;
  return p;
}

// The clean one cut to the 3D view's window (p: the whole frame, top row
// first; freed): a new image, _iCleanW x _iCleanH
static unsigned char *IOSCapture_CutScene(unsigned char *p)
{
  const int x0 = _iSceneX, y0 = _iSceneY, w = _iSceneW, h = _iSceneH;
  if (!p || (x0 == 0 && y0 == 0 && w == _iW && h == _iH)) {
    _iCleanW = _iW;
    _iCleanH = _iH;
    return p;
  }
  unsigned char *q = (unsigned char *)malloc((size_t)w * h * 4);
  if (!q) {
    free(p);
    return NULL;
  }
  for (int y = 0; y < h; y++) memcpy(q + (size_t)y * w * 4, p + ((size_t)(y0 + y) * _iW + x0) * 4, (size_t)w * 4);
  free(p);
  _iCleanW = w;
  _iCleanH = h;
  return q;
}

void IOSCapture_SceneRect(int x, int y, int w, int h)
{
  if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > _iW || y + h > _iH) {
    x = y = 0;
    w = _iW;
    h = _iH;
  }
  _iSceneX = x;
  _iSceneY = y;
  _iSceneW = w;
  _iSceneH = h;
}

static void IOSCapture_Failed(void)
{
  IOSCapture_Cancel();
  IOSCamera_Message("Couldn't take the photos");
}

void IOSCapture_FrameBegin(int w, int h)
{
  _iW = w;
  _iH = h;
  _iSceneX = _iSceneY = 0;
  _iSceneW = w;
  _iSceneH = h;
  _iCleanW = w;
  _iCleanH = h;
  _bSceneSeen = 0;
  // a clean one read in a frame that never got as far as the HUD
  free(_pClean);
  _pClean = NULL;
}

void IOSCapture_AfterScene(void)
{
  if (_bSceneSeen) return; // (once a frame: the first view drawn)
  _bSceneSeen = 1;
  if (!(_iWanted & IOSCAPTURE_CLEAN) || _iW <= 0 || _iH <= 0) return;
  _pClean = IOSCapture_CutScene(IOSCapture_Read());
  if (!_pClean) {
    IOSCapture_Failed();
    return;
  }
  // The clean one alone needs nothing more of this frame
  if (_iWanted == IOSCAPTURE_CLEAN) IOSCapture_Hand(NULL);
}

void IOSCapture_AfterHud(void)
{
  if (!_iWanted || _iW <= 0 || _iH <= 0) return;
  if (!_bSceneSeen) {
    // No HUD was drawn in this frame (it is turned off, or a photo view):
    // the whole frame is the clean one, and the HUD one too
    unsigned char *p = IOSCapture_Read();
    if (!p) {
      IOSCapture_Failed();
      return;
    }
    if (_iWanted & (IOSCAPTURE_HUD | IOSCAPTURE_HUDBTNS)) {
      unsigned char *q = NULL;
      if (_iWanted & IOSCAPTURE_CLEAN) {
        q = (unsigned char *)malloc((size_t)_iW * _iH * 4);
        if (!q) {
          free(p);
          IOSCapture_Failed();
          return;
        }
        memcpy(q, p, (size_t)_iW * _iH * 4);
      }
      _pClean = q;
      IOSCapture_Hand(p);
    }
    else {
      _pClean = p;
      IOSCapture_Hand(NULL);
    }
    return;
  }
  if (!(_iWanted & (IOSCAPTURE_HUD | IOSCAPTURE_HUDBTNS))) return;
  if ((_iWanted & IOSCAPTURE_CLEAN) && !_pClean) return; // (not read: wait for the next frame)
  unsigned char *pHud = IOSCapture_Read();
  if (!pHud) {
    IOSCapture_Failed();
    return;
  }
  IOSCapture_Hand(pHud);
}

