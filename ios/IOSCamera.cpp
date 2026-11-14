/*
** IOSCamera.cpp: the CAMERA's game side for DOOM in UZDoom (see
** IOSCamera.h): photos, bursts, selfies (group photos), Action Freeze and
** the cinematic burst, after the owner's Wolf3D port's IOSCamera.cpp and
** Raze port's ioscamera.cpp.
**
** The photo view: while a selfie, Action Freeze or the cinematic burst is
** on, the game holds still (no tic runs: P_CheckTickerPaused, as under the
** menu) and the 3D view is drawn from a camera of its own instead of the
** player's eyes (R_SetupFrame: IOSCamera_View), without the weapon or the
** status bar. What it changes -- monsters brought close, turned to the lens
** and posed on a frame of their own animations, others and shots hidden,
** the player's own body shown, the lens's field of view -- is applied in
** IOSCamera_RenderBegin and put back exactly in IOSCamera_RenderEnd, every
** frame. Actors are never moved in the level's lists: one posed elsewhere
** is drawn in the sector it is posed in (IOSCamera_ThingsIn, from the
** renderer's RenderThings), with its own position, angles, frame and render
** flags as kept and put back.
**
** Lengths are map units; angles radians, DOOM's way: 0 east, turning left
** (counter-clockwise), y north (forward is cos, sin). CAM_S is the size the
** Wolf3D port's arrangements were measured in (its tiles).
*/

#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <vector>
#include <algorithm>

#include "doomstat.h"
#include "d_player.h"
#include "g_game.h"
#include "g_level.h"
#include "g_levellocals.h"
#include "c_console.h"
#include "menu.h"
#include "r_state.h"
#include "r_utility.h"
#include "r_data/sprites.h"
#include "p_trace.h"
#include "p_local.h"
#include "m_random.h"
#include "actorinlines.h"
#include "am_map.h"
#include "v_video.h"
#include "d_event.h"
#ifdef IOSTOUCH_RIGHOOKS
#include "serializer.h"
#endif

#include "IOSCamera.h"
#include "IOSCapture.h"

#define CAM_PI 3.14159265358979323846
// The Wolf3D port's tile, for its arrangements' distances
#define CAM_S 60.0

// A burst: CAM_BURST_MAX photos, CAM_BURST_GAP apart
#define CAM_BURST_MAX 10
#define CAM_BURST_GAP 0.2
// A photo the game hasn't taken in this long is dropped (a menu came up)
#define CAM_PHOTO_TIMEOUT 0.5
// A selfie's or cinematic burst's last photo stays this long, and either
// gives up when its photos can't be taken for CAM_GIVE_UP
#define CAM_SHOW_TIME 0.8
#define CAM_GIVE_UP 4.0
// Selfies: the doomguy and the nearest CAM_GROUP_MAX - 1 monsters within CAM_GROUP_RANGE
#define CAM_GROUP_RANGE 1600.0
#define CAM_GROUP_MAX 12
// How near the camera keeps to a wall, the floor and the ceiling
#define CAM_WALL_GAP 12.0
#define CAM_PLANE_GAP 6.0
// Action Freeze: nearest and furthest from its focus, its zoom
#define CAM_FREEZE_NEAR 28.0
#define CAM_FREEZE_FAR 960.0
#define CAM_FREEZE_ZOOM 8.0
#define CAM_PITCH_MAX (40.0 * CAM_PI / 180.0)
#define CINEMA_SHOTS 5
// One on another's shoulders stands this share of the one below's height
#define CAM_SHOULDERS 0.78
// The height lines are traced at above the floor (over steps and stairs)
#define CAM_KNEE 28.0

enum { STAGE_NONE = 0, STAGE_SELFIE, STAGE_FREEZE, STAGE_CINEMA };

#ifdef IOSTOUCH_RIGHOOKS
#define CAMLOG(...) do { printf("cam: " __VA_ARGS__); fflush(stdout); } while (0)
#else
#define CAMLOG(...) do {} while (0)
#endif

// ------------------------------------------------------------ small things

static double Now()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

// The camera's own random numbers (the game's are never touched)
static unsigned randState = 2463534242u;
static unsigned Rand()
{
	randState ^= randState << 13;
	randState ^= randState >> 17;
	randState ^= randState << 5;
	return randState;
}
static double Rand01() { return (Rand() & 0xFFFFFF) / (double)0x1000000; }
static double RandIn(double a, double b) { return a + (b - a) * Rand01(); }

typedef DVector2 V2;
static V2 Fwd(double yaw) { return V2(cos(yaw), sin(yaw)); }
static V2 Right(double yaw) { return V2(sin(yaw), -cos(yaw)); }
static double Dot(V2 a, V2 b) { return a.X * b.X + a.Y * b.Y; }
static double Len(V2 a) { return a.Length(); }
static double YawTo(V2 from, V2 to) { return atan2(to.Y - from.Y, to.X - from.X); }
static double Wrap(double a) { while (a > CAM_PI) a -= 2 * CAM_PI; while (a < -CAM_PI) a += 2 * CAM_PI; return a; }
static V2 PosOf(const AActor *a) { return a->Pos().XY(); }

// ------------------------------------------------------------ the options

static int optKinds = IOSCAPTURE_CLEAN, optShots = 3, optFreeze = 1;
extern "C" int IOSCamera_GetKinds(void) { return optKinds; }
extern "C" void IOSCamera_SetKinds(int k) { k &= IOSCAPTURE_ALL; optKinds = k ? k : IOSCAPTURE_CLEAN; }
extern "C" int IOSCamera_GetSelfieShots(void) { return optShots; }
extern "C" void IOSCamera_SetSelfieShots(int n) { optShots = (n == 1 || n == 5) ? n : 3; }
extern "C" int IOSCamera_GetFreezeOn(void) { return optFreeze; }
extern "C" void IOSCamera_SetFreezeOn(int on) { optFreeze = on != 0; }

static player_t *Player()
{
	if (gamestate != GS_LEVEL || !playeringame[consoleplayer]) return nullptr;
	player_t *p = &players[consoleplayer];
	return p->mo ? p : nullptr;
}

// ------------------------------------------------------------ the level

static FLevelLocals *Level() { return primaryLevel; }
static double FloorAt(V2 p) { sector_t *s = Level()->PointInSector(p); return s->floorplane.ZatPoint(p); }
static double CeilAt(V2 p) { sector_t *s = Level()->PointInSector(p); return s->ceilingplane.ZatPoint(p); }

// How far from a towards b before a wall (or a step too high, or a closed
// door) at z (absolute; NAN: CAM_KNEE above the floor at a)
static double ClearDist(V2 a, V2 b, double z = NAN)
{
	const V2 d = b - a;
	const double len = Len(d);
	if (len < 1e-6) return 0;
	sector_t *sec = Level()->PointInSector(a);
	const double fl = sec->floorplane.ZatPoint(a), ce = sec->ceilingplane.ZatPoint(a);
	if (std::isnan(z)) z = fl + CAM_KNEE;
	z = std::max(fl + 1.0, std::min(ce - 1.0, z));
	FTraceResults res;
	const DVector3 dir(d.X / len, d.Y / len, 0);
	if (!Trace(DVector3(a.X, a.Y, z), sec, dir, len, 0, ML_BLOCKING | ML_BLOCKEVERYTHING, nullptr, res, TRACE_NoSky))
		return len;
	return res.Distance;
}
static bool ClearLine(V2 a, V2 b, double z = NAN) { return ClearDist(a, b, z) >= Len(b - a) - 0.5; }

// A spot something h high and r round can stand on, near the floor at
// floorNear (no more than a short flight of steps above or below it)
static bool Roomy(V2 p, double r, double h, double floorNear)
{
	sector_t *sec = Level()->PointInSector(p);
	const double fl = sec->floorplane.ZatPoint(p), ce = sec->ceilingplane.ZatPoint(p);
	if (ce - fl < h + 2 || fabs(fl - floorNear) > 96) return false;
	for (int i = 0; i < 8; i++)
	{
		const double a = i * CAM_PI / 4;
		if (!ClearLine(p, p + V2(cos(a), sin(a)) * r)) return false;
	}
	return true;
}

// ------------------------------------------------------------ the view

struct CamView
{
	DVector3 pos;
	double yaw;
	double pitch; // + looking down
	double zoom;  // the lens (1: none)
};

static bool camOn = false;      // the photo view draws the 3D view
static CamView cam;
static bool showFigure = false; // the player's body drawn where he stands
static int stage = STAGE_NONE;
static double lastFovDeg = 90.0;

// The view's shape: tangents of half its height and width at zoom 1 (UZDoom
// keeps the height of a 4:3 view at its field of view)
static double TanV() { return tan(lastFovDeg * CAM_PI / 360.0) * 0.75; }
static double TanH()
{
	const double w = screen ? screen->GetWidth() : 4, h = screen ? screen->GetHeight() : 3;
	return TanV() * (h > 0 ? w / h : 4.0 / 3.0);
}

// The player's view as the game last drew it
static bool eyeKnown = false;
static DVector3 eyePos;
static double eyeYaw = 0, eyePitch = 0;

void IOSCamera_View(FRenderViewpoint &vp)
{
	if (!camOn)
	{
		if (vp.camera && vp.camera->player == &players[consoleplayer])
		{
			eyeKnown = true;
			eyePos = vp.Pos;
			eyeYaw = vp.Angles.Yaw.Radians();
			eyePitch = vp.Angles.Pitch.Radians();
		}
		return;
	}
	vp.Pos = cam.pos;
	vp.Angles.Yaw = DAngle::fromRad(cam.yaw);
	vp.Angles.Pitch = DAngle::fromRad(cam.pitch);
	vp.Angles.Roll = nullAngle;
	vp.sector = vp.ViewLevel ? vp.ViewLevel->PointInRenderSubsector(cam.pos)->sector : vp.sector;
	vp.showviewer = showFigure;
	vp.bForceNoViewer = !showFigure;
}

double IOSCamera_Fov(double fovDeg)
{
	lastFovDeg = fovDeg;
	if (!camOn || fabs(cam.zoom - 1.0) < 1e-4) return fovDeg;
	return 2.0 * atan(tan(fovDeg * CAM_PI / 360.0) / cam.zoom) * 180.0 / CAM_PI;
}

bool IOSCamera_HidesWeapon() { return stage != STAGE_NONE; }
bool IOSCamera_HidesHud() { return camOn; }
bool IOSCamera_HoldsGame() { return stage != STAGE_NONE; }
extern "C" int IOSCamera_Stage(void) { return stage; }
extern "C" int IOSCamera_IsFrozen(void) { return stage == STAGE_FREEZE; }

// ------------------------------------------------------------ people

struct Person
{
	AActor *act;
	double dist;
	// the pose: where (on the floor there, plus lift), which frame (NULL:
	// not posed: hidden)
	bool posed;
	V2 at;
	double lift;
	FState *frame;
	double yawFix; // (a little turn off the lens)
};
static std::vector<Person> people;
static std::vector<AActor *> hidden;

static bool Alive(const AActor *a)
{
	return (a->flags3 & MF3_ISMONSTER) && (a->flags & MF_SHOOTABLE) && !(a->flags & MF_CORPSE) && a->health > 0;
}

static bool FrameShows(const FState *st)
{
	if (!st || st->sprite == 0 || (unsigned)st->sprite >= sprites.Size()) return false;
	const spritedef_t &sd = sprites[st->sprite];
	if (st->GetFrame() >= sd.numframes) return false;
	return SpriteFrames[sd.spriteframes + st->GetFrame()].Texture[0].isValid();
}

// Its standing frame, or now and then a step of its walk (never an attack,
// pain or death frame: Spawn and See only)
static FState *PoseFrame(AActor *a, bool step)
{
	if (step)
	{
		FState *f = a->SeeState;
		if (FrameShows(f))
		{
			if (Rand01() < 0.5 && f->NextState && f->NextState->sprite == f->sprite && FrameShows(f->NextState)) f = f->NextState;
			return f;
		}
	}
	if (FrameShows(a->SpawnState)) return a->SpawnState;
	if (FrameShows(a->SeeState)) return a->SeeState;
	return FrameShows(a->state) ? a->state : nullptr;
}

static bool StillThere(const AActor *a)
{
	auto it = Level()->GetThinkerIterator<AActor>();
	while (AActor *b = it.Next())
		if (b == a) return true;
	return false;
}

// ------------------------------------------------------------ render: apply, put back

struct Kept
{
	AActor *act;
	DVector3 pos, prev;
	DRotator angles, prevAngles;
	FState *state;
	int sprite;
	uint8_t frame;
	ActorRenderFlags renderflags;
	sector_t *sector;
	subsector_t *subsector;
	double floorz, ceilingz;
	double floorclip;
};
static std::vector<Kept> kept;
static bool applied = false;

static void Keep(AActor *a)
{
	Kept k;
	k.act = a;
	k.pos = a->Pos();
	k.prev = a->Prev;
	k.angles = a->Angles;
	k.prevAngles = a->PrevAngles;
	k.state = a->state;
	k.sprite = a->sprite;
	k.frame = a->frame;
	k.renderflags = a->renderflags;
	k.sector = a->Sector;
	k.subsector = a->subsector;
	k.floorz = a->floorz;
	k.ceilingz = a->ceilingz;
	k.floorclip = a->Floorclip;
	kept.push_back(k);
}

// Posed: where it is drawn (not linked there), turned, on frame f
static void SetPose(AActor *a, V2 at, double lift, double yaw, FState *f)
{
	sector_t *sec = Level()->PointInSector(at);
	const double fl = sec->floorplane.ZatPoint(at);
	const DVector3 pos(at.X, at.Y, fl + lift);
	a->SetXYZ(pos); // (the position only: no links change)
	a->Prev = pos;
	a->Angles.Yaw = DAngle::fromRad(yaw);
	a->Angles.Pitch = nullAngle;
	a->Angles.Roll = nullAngle;
	a->PrevAngles = a->Angles;
	a->Sector = sec;
	a->subsector = Level()->PointInRenderSubsector(at);
	a->floorz = fl;
	a->ceilingz = sec->ceilingplane.ZatPoint(at);
	a->Floorclip = 0;
	a->renderflags &= ~RF_INVISIBLE;
	if (f)
	{
		a->state = f;
		a->sprite = f->sprite;
		a->frame = f->GetFrame();
		if (f->GetFullbright()) a->renderflags |= RF_FULLBRIGHT;
		else a->renderflags &= ~RF_FULLBRIGHT;
	}
}

void IOSCamera_RenderBegin()
{
	applied = false;
	if (!camOn) return;
	player_t *p = Player();
	if (!p) return;
	applied = true;
	kept.clear();
	// the people posed, or hidden
	for (size_t i = 0; i < people.size(); i++)
	{
		Person &q = people[i];
		Keep(q.act);
		if (q.posed) SetPose(q.act, q.at, q.lift, YawTo(q.at, cam.pos.XY()) + q.yawFix, q.frame);
		else q.act->renderflags |= RF_INVISIBLE;
	}
	for (size_t i = 0; i < hidden.size(); i++)
	{
		Keep(hidden[i]);
		hidden[i]->renderflags |= RF_INVISIBLE;
	}
	// the doomguy where he stands (as the game last drew him), seen as
	// anyone else is
	AActor *mo = p->mo;
	Keep(mo);
	mo->Prev = mo->Pos();
	mo->PrevAngles = mo->Angles;
	if (showFigure) mo->renderflags &= ~RF_INVISIBLE;
}

void IOSCamera_RenderEnd()
{
	if (!applied) return;
	applied = false;
	for (size_t i = kept.size(); i-- > 0;)
	{
		Kept &k = kept[i];
		AActor *a = k.act;
		a->SetXYZ(k.pos);
		a->Prev = k.prev;
		a->Angles = k.angles;
		a->PrevAngles = k.prevAngles;
		a->state = k.state;
		a->sprite = k.sprite;
		a->frame = k.frame;
		a->renderflags = k.renderflags;
		a->Sector = k.sector;
		a->subsector = k.subsector;
		a->floorz = k.floorz;
		a->ceilingz = k.ceilingz;
		a->Floorclip = k.floorclip;
	}
	kept.clear();
}

void IOSCamera_ThingsIn(sector_t *sec, void (*fn)(AActor *, void *), void *ctx)
{
	if (!applied) return;
	for (size_t i = 0; i < people.size(); i++)
		if (people[i].posed && people[i].act->Sector == sec) fn(people[i].act, ctx);
}

// ------------------------------------------------------------ the player's eyes

static bool ReadEye()
{
	player_t *p = Player();
	if (!p) return false;
	if (!eyeKnown)
	{
		eyePos = DVector3(p->mo->Pos().XY(), p->viewz);
		eyeYaw = p->mo->Angles.Yaw.Radians();
		eyePitch = p->mo->Angles.Pitch.Radians();
	}
	return true;
}
static V2 EyeAt() { return eyePos.XY(); }
static double EyeFloor() { return FloorAt(EyeAt()); }

// How far ahead along yaw before a wall (at most max)
static double FreeAhead(V2 from, double yaw, double max) { return ClearDist(from, from + Fwd(yaw) * max); }

// The living monsters, nearest first
static void FindPeople(double range, size_t max)
{
	people.clear();
	player_t *pl = Player();
	auto it = Level()->GetThinkerIterator<AActor>();
	while (AActor *a = it.Next())
	{
		if (!Alive(a) || a == pl->mo || a->player) continue;
		Person q;
		memset(&q, 0, sizeof(q));
		q.act = a;
		q.dist = Len(PosOf(a) - EyeAt());
		if (q.dist > range) continue;
		people.push_back(q);
	}
	std::sort(people.begin(), people.end(), [](const Person &a, const Person &b) { return a.dist < b.dist; });
	if (people.size() > max) people.resize(max);
}

// ------------------------------------------------------------ selfies

enum { THEME_HUDDLE = 0, THEME_PEEK, THEME_CLASS, THEME_CORNER, THEME_TINY, THEME_TOTEM, THEME_PYRAMID, THEME_LIFT, THEMES };
static const char *const themeNames[THEMES] = { "huddle", "peek", "class", "corner", "tiny", "totem", "pyramid", "lift" };
static const int themeWeights[THEMES] = { 18, 14, 14, 10, 10, 10, 10, 10 };
static const int themeLeast[THEMES] = { 1, 3, 3, 2, 3, 2, 3, 3 };
static int lastTheme = -1;
static double groupYaw = 0, groupRoom = 0;
static int ctTaken = 0, ctShots = 0;
static double tStage = 0, tShown = 0, tWaited = 0;
static bool asked = false;
static const char *cantWhy = nullptr;

// The way to take it: ahead if there is room, else the roomiest way round
static bool PickGroupHeading()
{
	double best = -1;
	const V2 eye = EyeAt();
	for (int i = 0; i < 16; i++)
	{
		const int k = (i + 1) / 2;
		const double yaw = eyeYaw + ((i & 1) ? 1 : -1) * k * (2 * CAM_PI / 16);
		const double room = FreeAhead(eye, yaw, 8.0 * CAM_S);
		const V2 mid = eye + Fwd(yaw) * fmin(room * 0.6, 2.0 * CAM_S);
		const double side = fmin(ClearDist(mid, mid + Right(yaw) * (1.5 * CAM_S)), ClearDist(mid, mid - Right(yaw) * (1.5 * CAM_S)));
		const double score = fmin(room, 5.0 * CAM_S) + side * 0.6 - k * 0.12 * CAM_S;
		if (i == 0 && room >= 3.0 * CAM_S)
		{
			groupYaw = yaw;
			groupRoom = room;
			return true;
		}
		if (score > best)
		{
			best = score;
			groupYaw = yaw;
			groupRoom = room;
		}
	}
	return groupRoom >= 1.2 * CAM_S;
}

// A solid thing (a lamp, a barrel, a column) on that spot
static bool NearSolidThing(V2 p, double r)
{
	auto it = Level()->GetThinkerIterator<AActor>();
	while (AActor *a = it.Next())
	{
		if (!(a->flags & MF_SOLID) || (a->flags3 & MF3_ISMONSTER) || a->player) continue;
		if (Len(PosOf(a) - p) < r + a->radius) return true;
	}
	return false;
}

// Person i posed at (lateral, depth) in the camera's view (CAM_S units),
// lift above the floor: nudged in, out or to the middle where that spot has
// no room
static bool PlaceAt(size_t i, double lat, double depth, double lift, bool step = false)
{
	if (i >= people.size()) return false;
	static const double dk[] = { 1.0, 0.88, 1.14, 0.76, 1.3 };
	static const double lk[] = { 1.0, 0.7, 0.4 };
	const V2 f = Fwd(cam.yaw), r = Right(cam.yaw), eye = cam.pos.XY();
	Person &q = people[i];
	const double rad = q.act->radius, hgt = q.act->Height;
	for (int a = 0; a < 3; a++)
	{
		for (int b = 0; b < 5; b++)
		{
			const double d = depth * dk[b] * CAM_S;
			if (d < 0.75 * CAM_S || d > groupRoom - rad - 8) continue;
			const V2 p = eye + f * d + r * (lat * lk[a] * CAM_S);
			if (!Roomy(p, rad * 0.6, hgt + lift, EyeFloor()) || !ClearLine(eye, p) || NearSolidThing(p, rad * 0.8))
			{
#ifdef IOSTOUCH_RIGHOOKS
				if (getenv("IOSCAM_WHY"))
					CAMLOG("    %d no room at %.0f,%.0f (d %.0f): roomy %d clear %d thing %d fl %.0f eyefl %.0f\n", (int)i, p.X, p.Y, d,
						(int)Roomy(p, rad * 0.6, hgt + lift, EyeFloor()), (int)ClearLine(eye, p), (int)NearSolidThing(p, rad * 0.8), FloorAt(p), EyeFloor());
#endif
				continue;
			}
			q.posed = true;
			q.at = p;
			q.lift = lift;
			q.frame = PoseFrame(q.act, step || Rand01() < 0.15);
			q.yawFix = RandIn(-0.25, 0.25);
			if (!q.frame) q.posed = false;
			return q.posed;
		}
	}
	return false;
}

// A loose row (n people from first) across the view at depth, spacing
// apart (CAM_S units; wider for DOOM's wider monsters: shoulder to shoulder
// at least)
static void Row(size_t first, size_t n, double depth, double spacing, double lift, double latMid = 0)
{
	double widest = 0;
	for (size_t j = first; j < first + n && j < people.size(); j++) widest = std::max(widest, people[j].act->radius);
	spacing = std::max(spacing, widest * 2.1 / CAM_S);
	for (size_t j = 0; j < n; j++)
		PlaceAt(first + j, latMid + (j - (n - 1) * 0.5) * spacing + RandIn(-0.05, 0.05), depth + RandIn(-0.12, 0.12), lift);
}

static int PickTheme(size_t n)
{
	int total = 0;
	for (int t = 0; t < THEMES; t++)
		if (n >= (size_t)themeLeast[t] && t != lastTheme) total += themeWeights[t];
	int r = total > 0 ? (int)(Rand() % total) : 0;
	for (int t = 0; t < THEMES; t++)
	{
		if (n < (size_t)themeLeast[t] || t == lastTheme) continue;
		if ((r -= themeWeights[t]) < 0) return t;
	}
	return THEME_HUDDLE;
}

// The camera at height h above the floor (kept under the ceiling), looking
// at height at above the floor d away (+ down)
static void CamHeight(double h, double at, double d)
{
	const V2 e = cam.pos.XY();
	const double fl = FloorAt(e), ce = CeilAt(e);
	cam.pos.Z = std::max(fl + CAM_PLANE_GAP, std::min(ce - CAM_PLANE_GAP, fl + h));
	cam.pitch = atan2(cam.pos.Z - (fl + at), std::max(d, 1.0));
}

static double Tallest(size_t from, size_t n)
{
	double h = 56;
	for (size_t j = from; j < from + n && j < people.size(); j++) h = std::max(h, people[j].act->Height);
	return h;
}

static void Theme(int t)
{
	const size_t n = people.size();
	const double tanH = TanH();
	const double eyeH = eyePos.Z - EyeFloor();
	cam.pos.Z = eyePos.Z;
	cam.pitch = 0;
	cam.zoom = 1.0;
	switch (t)
	{
	default:
	case THEME_HUDDLE:
	{
		const size_t front = std::min<size_t>(n, 4), back = std::min<size_t>(n - front, 4);
		const double d = RandIn(1.5, 1.9);
		Row(0, front, d, 0.5, 0);
		Row(front, back, d + 0.55, 0.5, 4);
		CamHeight(eyeH, Tallest(0, n) * 0.55, d * CAM_S);
		break;
	}
	case THEME_PEEK:
	{
		const double d = 1.15;
		PlaceAt(0, -d * tanH * 0.8, d, 0);
		PlaceAt(1, d * tanH * 0.8, d, 0);
		Row(2, std::min<size_t>(n - 2, 4), 2.2, 0.55, 0);
		CamHeight(eyeH, Tallest(0, n) * 0.6, 2.0 * CAM_S);
		break;
	}
	case THEME_CLASS:
	{
		const size_t front = std::min<size_t>((n + 1) / 2, 4), back = std::min<size_t>(n - front, 5);
		Row(0, front, 1.8, 0.6, 0);
		Row(front, back, 2.6, 0.6, 12);
		CamHeight(eyeH + 8, Tallest(0, n) * 0.6, 2.2 * CAM_S);
		break;
	}
	case THEME_CORNER:
	{
		const double side = Rand01() < 0.5 ? -1 : 1, d = 2.0;
		const size_t m = std::min<size_t>(n, 5);
		for (size_t j = 0; j < m; j++)
			PlaceAt(j, side * (d * tanH * 0.55 - j * 0.32), d + j * 0.25 + RandIn(-0.1, 0.1), 0);
		CamHeight(eyeH, Tallest(0, m) * 0.55, d * CAM_S);
		break;
	}
	case THEME_TINY:
	{
		const double far = fmin(groupRoom / CAM_S - 0.4, 6.5), d = 1.15;
		PlaceAt(2, RandIn(-0.2, 0.2), far, 0);
		PlaceAt(0, -d * tanH * 0.6, d, 0);
		PlaceAt(1, d * tanH * 0.6, d, 0);
		CamHeight(eyeH, Tallest(0, 3) * 0.6, far * CAM_S);
		break;
	}
	case THEME_TOTEM:
	{
		const double d = 2.6;
		const size_t m = std::min<size_t>(n, 3);
		double lift = 0, top = 0;
		for (size_t j = 0; j < m; j++)
		{
			if (PlaceAt(j, 0, d, lift)) top = lift + people[j].act->Height;
			if (people[j].posed) lift += people[j].act->Height * CAM_SHOULDERS;
		}
		// (the first's place: the others stand on it, exactly)
		for (size_t j = 1; j < m; j++)
			if (people[j].posed && people[0].posed) people[j].at = people[0].at + Fwd(cam.yaw) * (0.5 * j);
		if (n > 3) PlaceAt(3, -0.7, d, 0);
		if (n > 4) PlaceAt(4, 0.7, d, 0);
		cam.zoom = 0.8;
		CamHeight(std::max(eyeH, top * 0.6), top * 0.5, d * CAM_S);
		break;
	}
	case THEME_PYRAMID:
	{
		const double d = 3.0;
		static const double lat[6] = { -0.5, 0, 0.5, -0.25, 0.25, 0 };
		static const int below[6] = { -1, -1, -1, 0, 1, 3 };
		const size_t m = std::min<size_t>(n, 6);
		double lifts[6] = { 0, 0, 0, 0, 0, 0 }, top = 0;
		for (size_t j = 0; j < m; j++)
		{
			const int b = below[j];
			lifts[j] = b < 0 ? 0 : lifts[b] + people[b].act->Height * CAM_SHOULDERS;
			if (PlaceAt(j, lat[j], d, lifts[j])) top = std::max(top, lifts[j] + people[j].act->Height);
		}
		// (all on one line across: the upper ones on the shoulders below)
		if (people[0].posed || people[1].posed)
		{
			const Person &base = people[1].posed ? people[1] : people[0];
			const double baseDepth = Dot(base.at - cam.pos.XY(), Fwd(cam.yaw));
			for (size_t j = 0; j < m; j++)
				if (people[j].posed)
					people[j].at = cam.pos.XY() + Fwd(cam.yaw) * (baseDepth + 0.5 * (j / 3 + (j >= 5))) + Right(cam.yaw) * (lat[j] * CAM_S);
		}
		cam.zoom = 0.75;
		CamHeight(std::max(eyeH, top * 0.6), top * 0.5, d * CAM_S);
		break;
	}
	case THEME_LIFT:
	{
		const size_t up = Rand() % std::min<size_t>(n, 3);
		PlaceAt(up, 0, 2.1, 32);
		size_t k = 0;
		for (size_t j = 0; j < std::min<size_t>(n, 6); j++)
		{
			if (j == up) continue;
			PlaceAt(j, ((k & 1) ? 1 : -1) * (0.3 + 0.5 * (k / 2)), 1.8 + RandIn(-0.1, 0.1), 0);
			k++;
		}
		CamHeight(eyeH - 6, Tallest(0, n) * 0.7, 2.0 * CAM_S);
		cam.pitch -= 0.08;
		break;
	}
	}
}

// Everyone else hidden: the monsters not in it, the shots, and whatever
// stands in front of a face
static void HideOthers()
{
	hidden.clear();
	const V2 f = Fwd(cam.yaw), r = Right(cam.yaw), eye = cam.pos.XY();
	player_t *pl = Player();
	auto it = Level()->GetThinkerIterator<AActor>();
	while (AActor *a = it.Next())
	{
		if (a == pl->mo || (a->renderflags & RF_INVISIBLE) || a->sprite == 0) continue;
		bool inGroup = false;
		for (size_t i = 0; i < people.size() && !inGroup; i++) inGroup = people[i].act == a;
		if (inGroup) continue;
		bool hide = Alive(a) || (a->flags & MF_MISSILE);
		if (!hide)
		{
			const V2 d = PosOf(a) - eye;
			const double depth = Dot(d, f), lat = Dot(d, r);
			for (size_t i = 0; i < people.size() && !hide; i++)
			{
				const Person &q = people[i];
				if (!q.posed) continue;
				const double qd = Dot(q.at - eye, f), ql = Dot(q.at - eye, r);
				hide = depth > 0 && depth < qd + 8 && fabs(lat - ql) < q.act->radius + a->radius + 12;
			}
		}
		if (hide) hidden.push_back(a);
	}
}

static bool Pose()
{
	int t = PickTheme(people.size());
	for (int attempt = 0; attempt < 3; attempt++)
	{
		for (size_t i = 0; i < people.size(); i++) people[i].posed = false;
		Theme(t);
		size_t posed = 0;
		for (size_t i = 0; i < people.size(); i++) posed += people[i].posed;
		if (posed >= std::min<size_t>(people.size(), (size_t)themeLeast[t]))
		{
			lastTheme = t;
			HideOthers();
			CAMLOG("selfie %s: %d of %d posed, yaw %.0f, room %.0f, zoom %.2f\n", themeNames[t], (int)posed, (int)people.size(),
				cam.yaw * 180 / CAM_PI, groupRoom, cam.zoom);
#ifdef IOSTOUCH_RIGHOOKS
			for (size_t i = 0; i < people.size(); i++)
				if (people[i].posed)
					CAMLOG("  %s at %.0f,%.0f lift %.0f, %.0f from the lens; was %.0f,%.0f\n", people[i].act->GetClass()->TypeName.GetChars(), people[i].at.X,
						people[i].at.Y, people[i].lift, Len(people[i].at - cam.pos.XY()), people[i].act->X(), people[i].act->Y());
#endif
			return true;
		}
		t = attempt == 0 ? THEME_HUDDLE : THEME_CORNER;
	}
	return false;
}

static bool SelfieBegin()
{
	FindPeople(CAM_GROUP_RANGE, CAM_GROUP_MAX - 1);
	// the doomguy himself, first: the arrangements give the first the best
	// place (nearest the lens, at the front); alone when no one is around
	{
		Person me;
		memset(&me, 0, sizeof(me));
		me.act = Player()->mo;
		people.insert(people.begin(), me);
	}
	if (!PickGroupHeading())
	{
		cantWhy = "No room for a group photo here";
		return false;
	}
	cam.pos = eyePos;
	cam.yaw = groupYaw;
	cam.pitch = 0;
	cam.zoom = 1;
	camOn = true;
	showFigure = true; // (the doomguy is in it)
	lastTheme = -1;
	if (!Pose())
	{
		cantWhy = "No room for a group photo here";
		return false;
	}
	return true;
}

// ------------------------------------------------------------ Action Freeze and the orbit

static DVector3 focus;
static double orbYaw, orbPitch, orbDist, orbDistNow, distStart;
static bool smooth = true;

// The camera where the orbit puts it, kept out of walls, floors and ceilings
static bool PlaceOrbit(bool snap)
{
	const V2 fxy = focus.XY();
	const double horizWant = orbDist * cos(orbPitch);
	const V2 back = Fwd(orbYaw) * -1.0;
	double room = ClearDist(fxy, fxy + back * (horizWant + CAM_WALL_GAP), focus.Z) - CAM_WALL_GAP;
	double d = fmin(room, horizWant);
	const double fl0 = FloorAt(fxy);
	while (d > CAM_FREEZE_NEAR * 0.5 && !Roomy(fxy + back * d, 8, 16, fl0)) d -= 4;
	d = fmax(d, CAM_FREEZE_NEAR * 0.5);
	if (snap || !smooth || d < orbDistNow) orbDistNow = d;
	else orbDistNow += (d - orbDistNow) * 0.2;
	const V2 eye = fxy + back * orbDistNow;
	const double fl = FloorAt(eye), ce = CeilAt(eye);
	if (ce - fl < 2 * CAM_PLANE_GAP) return false;
	cam.pos = DVector3(eye.X, eye.Y, std::max(fl + CAM_PLANE_GAP, std::min(ce - CAM_PLANE_GAP, focus.Z + orbDistNow * tan(orbPitch))));
	cam.yaw = orbYaw;
	cam.pitch = atan2(cam.pos.Z - focus.Z, std::max(orbDistNow, 8.0));
	camOn = true;
	// (the doomguy, unless the camera is in his head)
	showFigure = Len(eye - EyeAt()) > 24;
	return true;
}

// What the player was looking at: a monster near the middle of the view, or
// where the view meets the level
static void FindFocus()
{
	const V2 eye = EyeAt();
	FindPeople(1100.0, 64);
	for (size_t i = 0; i < people.size(); i++)
	{
		AActor *a = people[i].act;
		const V2 p = PosOf(a);
		const double ang = fabs(Wrap(YawTo(eye, p) - eyeYaw));
		if (ang < 0.2 && people[i].dist > 48 && ClearLine(eye, p, eyePos.Z))
		{
			focus = DVector3(p.X, p.Y, a->Z() + a->Height * 0.5);
			orbDist = Len(p - eye);
			people.clear();
			return;
		}
	}
	people.clear();
	const double ahead = FreeAhead(eye, eyeYaw, 6.0 * CAM_S) - 32;
	if (ahead < 64)
	{
		// (up against a wall: round the doomguy, from behind him)
		player_t *pl = Player();
		focus = DVector3(eye.X, eye.Y, pl->mo->Z() + pl->mo->Height * 0.5);
		orbDist = 120;
		return;
	}
	orbDist = std::max(64.0, std::min(ahead, 3.0 * CAM_S));
	const V2 f = eye + Fwd(eyeYaw) * orbDist;
	focus = DVector3(f.X, f.Y, FloorAt(f) + 32);
}

static bool FreezeBegin()
{
	FindFocus();
	orbYaw = eyeYaw;
	orbPitch = 0.12;
	orbDistNow = orbDist;
	distStart = orbDist;
	cam.zoom = 1;
	return PlaceOrbit(true);
}

extern "C" void IOSCamera_FreezeOrbit(float yawDeg, float pitchDeg)
{
	if (stage != STAGE_FREEZE) return;
	const double y0 = orbYaw, p0 = orbPitch;
	const double k = 1.0 / std::max(cam.zoom, 1.0); // (zoomed in, a slide turns less)
	orbYaw += yawDeg * k * CAM_PI / 180;
	orbPitch = std::max(-CAM_PITCH_MAX, std::min(CAM_PITCH_MAX, orbPitch + pitchDeg * k * CAM_PI / 180));
	if (!PlaceOrbit(false))
	{
		orbYaw = y0;
		orbPitch = p0;
		PlaceOrbit(true);
	}
}

extern "C" void IOSCamera_FreezePan(float right, float down)
{
	if (stage != STAGE_FREEZE) return;
	// what is seen follows the fingers: the focus moves across the view
	const double span = 2.0 * std::max(orbDistNow, 24.0) * TanV() / std::max(cam.zoom, 1.0);
	const V2 fxy = focus.XY();
	const V2 to = fxy - Right(orbYaw) * (right * span);
	const double room = ClearDist(fxy, to, focus.Z);
	const double len = Len(to - fxy);
	V2 nxy = fxy;
	if (len > 1e-6)
	{
		const double k = std::max(0.0, std::min(1.0, (room - CAM_WALL_GAP) / len));
		nxy = fxy + (to - fxy) * k;
	}
	const double fl = FloorAt(nxy), ce = CeilAt(nxy);
	focus = DVector3(nxy.X, nxy.Y, std::max(fl + 4, std::min(ce - 4, focus.Z + down * span)));
	PlaceOrbit(false);
}

extern "C" float IOSCamera_FreezeZoomNow(void) { return stage == STAGE_FREEZE ? (float)cam.zoom : 1.0f; }

extern "C" void IOSCamera_FreezeZoom(float factor)
{
	if (stage != STAGE_FREEZE || factor <= 0) return;
	// one scale: above 1 the lens's zoom; below, how far the camera has
	// backed off from where the freeze began
	double z = cam.zoom > 1.0 ? cam.zoom : distStart / std::max(orbDist, 0.01);
	z = std::max(distStart / CAM_FREEZE_FAR, std::min(CAM_FREEZE_ZOOM, z * factor));
	if (z >= 1.0)
	{
		cam.zoom = z;
		orbDist = distStart;
	}
	else
	{
		cam.zoom = 1.0;
		orbDist = distStart / z;
	}
	PlaceOrbit(false);
}

// ------------------------------------------------------------ the cinematic burst

static double cineYaw[CINEMA_SHOTS];

static bool CinemaBegin()
{
	const V2 eye = EyeAt();
	player_t *pl = Player();
	FindPeople(800.0, 64);
	AActor *target = nullptr;
	for (size_t i = 0; i < people.size() && !target; i++)
	{
		const V2 p = PosOf(people[i].act);
		if (fabs(Wrap(YawTo(eye, p) - eyeYaw)) < 0.9 && ClearLine(eye, p, eyePos.Z)) target = people[i].act;
	}
	double start, step;
	if (target)
	{
		const V2 p = PosOf(target), mid = (eye + p) * 0.5;
		focus = DVector3(mid.X, mid.Y, (pl->mo->Z() + target->Z()) * 0.5 + 28);
		orbDist = std::max(176.0, std::min(Len(p - eye) * 0.9, 480.0));
		start = 60 * CAM_PI / 180;
		step = 30 * CAM_PI / 180;
	}
	else
	{
		focus = DVector3(eye.X, eye.Y, pl->mo->Z() + 28);
		orbDist = 160;
		start = 60 * CAM_PI / 180;
		step = 40 * CAM_PI / 180;
	}
	people.clear();
	// the side with the most room
	double room[2] = { 0, 0 };
	for (int side = 0; side < 2; side++)
		for (int i = 0; i < 4; i++)
		{
			const double y = eyeYaw + (side ? -1 : 1) * (start + step * i);
			room[side] += std::min(ClearDist(focus.XY(), focus.XY() - Fwd(y) * orbDist, focus.Z), orbDist);
		}
	const int sgn = room[0] >= room[1] ? 1 : -1;
	for (int i = 0; i < 4; i++) cineYaw[i + 1] = eyeYaw + sgn * (start + step * i);
	orbPitch = 0.3;
	cam.zoom = 1;
	smooth = false;
	return true;
}

// Whatever stands between the camera and the doomguy (a lamp, a barrel)
// left out of the cinematic burst's shot
static void HideBeforeFigure()
{
	hidden.clear();
	const V2 f = Fwd(cam.yaw), r = Right(cam.yaw), eye = cam.pos.XY();
	const double bd = Dot(EyeAt() - eye, f), bl = Dot(EyeAt() - eye, r);
	player_t *pl = Player();
	auto it = Level()->GetThinkerIterator<AActor>();
	while (AActor *a = it.Next())
	{
		if (a == pl->mo || (a->renderflags & RF_INVISIBLE) || a->sprite == 0 || Alive(a)) continue;
		const V2 d = PosOf(a) - eye;
		if (Dot(d, f) > 0 && Dot(d, f) < bd - 8 && fabs(Dot(d, r) - bl) < a->radius + 28) hidden.push_back(a);
	}
}

static void CinemaPlace(int shot)
{
	if (shot == 0)
	{
		camOn = false; // (the first: what the player saw, without the weapon)
		return;
	}
	// (a little further round where the camera would be on top of him)
	for (int k = 0; k < 4; k++)
	{
		orbYaw = cineYaw[shot] + (k & 1 ? -1 : 1) * ((k + 1) / 2) * 0.35;
		if (PlaceOrbit(true) && orbDistNow >= std::min(96.0, orbDist * 0.7))
		{
			HideBeforeFigure();
			return;
		}
	}
	orbYaw = cineYaw[shot];
	PlaceOrbit(true);
	HideBeforeFigure();
}

// ------------------------------------------------------------ stages

static double tPhotoAsked = 0;
static int ctBurst = 0;
static double tBurstNext = 0;
static bool burst = false;

static void EndBurst()
{
	if (!burst) return;
	burst = false;
	IOSPhotos_SetBatch(0);
}

#ifdef IOSTOUCH_RIGHOOKS
static unsigned RigHash();
#endif

static void EndStage()
{
#ifdef IOSTOUCH_RIGHOOKS
	const bool bHash = stage != STAGE_NONE;
#endif
	if (stage == STAGE_FREEZE) IOSPhotos_FreezeShown(0);
	if (stage == STAGE_SELFIE || stage == STAGE_CINEMA) IOSPhotos_SetBatch(0);
	if (stage != STAGE_NONE) IOSCapture_NoteFreeze(Now() - tStage);
	stage = STAGE_NONE;
	camOn = false;
	showFigure = false;
	smooth = true;
	people.clear();
	hidden.clear();
#ifdef IOSTOUCH_RIGHOOKS
	if (bHash) CAMLOG("state hash after %08x\n", RigHash());
#endif
}

extern "C" void IOSCamera_FreezeEnd(void)
{
	if (stage != STAGE_FREEZE) return;
	IOSCapture_Cancel();
	EndStage();
}

extern "C" int IOSCamera_NumTaken(void) { return ctTaken; }

void IOSCamera_Abort()
{
	EndBurst();
	IOSCapture_Cancel();
	EndStage();
	tPhotoAsked = 0;
	eyeKnown = false;
}

// Why not here (NULL: it can be had)
static const char *CantHere()
{
	if (netgame || multiplayer) return "single";
	player_t *p = Player();
	if (!p || p->playerstate == PST_DEAD || p->health <= 0 || automapactive || gameaction != ga_nothing) return "here";
	if (p->camera && p->camera != p->mo) return "here";
	return nullptr;
}

static void Say(const char *why, int what)
{
	static const char *const names[] = { "", "Selfies", "Action Freeze", "The cinematic burst" };
	char msg[96];
	if (why && !strcmp(why, "single"))
		snprintf(msg, sizeof(msg), "%s %s for single player", names[what], what == STAGE_SELFIE ? "are" : "is");
	else if (what == STAGE_SELFIE)
		snprintf(msg, sizeof(msg), "Can't take a selfie here");
	else
		snprintf(msg, sizeof(msg), "Can't freeze here");
	IOSCamera_Message(msg);
}

extern "C" void IOSCamera_Photo(void)
{
	if (stage == STAGE_SELFIE || stage == STAGE_CINEMA) return;
	// in Action Freeze: a clean photo of the view
	const int kinds = stage == STAGE_FREEZE ? IOSCAPTURE_CLEAN : optKinds;
	if (IOSCapture_Request(kinds, IOSCAPTURE_MODE_PHOTO)) tPhotoAsked = Now();
	else IOSCamera_Message("Still saving the last photo");
}

extern "C" void IOSCamera_Burst(void)
{
	if (stage != STAGE_NONE || burst) return;
	burst = true;
	ctBurst = 0;
	tBurstNext = Now();
	IOSPhotos_SetBatch(1);
}

static bool StartHeld(int which)
{
	EndBurst();
	if (stage != STAGE_NONE) return false;
	if (const char *why = CantHere())
	{
		Say(why, which);
		return false;
	}
	if (which != STAGE_FREEZE && !IOSCapture_HasRoom(IOSCAPTURE_CLEAN, 1))
	{
		IOSCamera_Message("Still saving the last photo");
		return false;
	}
	if (!ReadEye()) return false;
	IOSCapture_Cancel(); // (a photo not taken yet would be of the held view)
#ifdef IOSTOUCH_RIGHOOKS
	CAMLOG("state hash before %08x\n", RigHash());
#endif
	randState ^= (unsigned)(Now() * 1000.0);
	if (!randState) randState = 2463534242u;
	stage = which; // (held from here: no tic runs)
	tStage = Now();
	ctTaken = 0;
	asked = false;
	tShown = tWaited = 0;
	cantWhy = nullptr;
	const bool ok = which == STAGE_SELFIE ? SelfieBegin() : which == STAGE_FREEZE ? FreezeBegin() : CinemaBegin();
	if (!ok)
	{
		EndStage();
		if (cantWhy) IOSCamera_Message(cantWhy);
		else Say(nullptr, which);
		return false;
	}
	if (which == STAGE_SELFIE)
	{
		ctShots = optShots;
		if (ctShots > 1) IOSPhotos_SetBatch(1);
	}
	if (which == STAGE_CINEMA)
	{
		ctShots = CINEMA_SHOTS;
		IOSPhotos_SetBatch(1);
		CinemaPlace(0);
	}
	if (which == STAGE_FREEZE) IOSPhotos_FreezeShown(1);
	return true;
}

extern "C" void IOSCamera_Selfie(void) { StartHeld(STAGE_SELFIE); }
extern "C" void IOSCamera_Cinematic(void) { StartHeld(STAGE_CINEMA); }

extern "C" void IOSCamera_Freeze(void)
{
	if (stage == STAGE_FREEZE)
	{
		IOSCamera_FreezeEnd();
		return;
	}
	if (!optFreeze)
	{
		IOSCamera_Message("Action Freeze is off (CAMERA's options)");
		return;
	}
	StartHeld(STAGE_FREEZE);
}

extern "C" int IOSCamera_IsBusy(void) { return stage != STAGE_NONE || burst || IOSCapture_IsPending(); }

// A selfie's or the cinematic burst's photos, one a frame as they are taken
static void TickShots(double now)
{
	if (tShown > 0)
	{
		if (now - tShown > CAM_SHOW_TIME) EndStage();
		return;
	}
	if (asked)
	{
		if (IOSCapture_IsPending())
		{
			if (now - tWaited > CAM_GIVE_UP)
			{
				IOSCamera_Message("Couldn't take the photos");
				IOSCapture_Cancel();
				EndStage();
			}
			return;
		}
		asked = false;
		ctTaken++;
		if (ctTaken >= ctShots)
		{
			tShown = now;
			IOSPhotos_SetBatch(0);
			return;
		}
		if (stage == STAGE_SELFIE) Pose();
		else CinemaPlace(ctTaken);
		tWaited = 0;
	}
	if (!IOSCapture_Request(IOSCAPTURE_CLEAN, stage == STAGE_CINEMA ? IOSCAPTURE_MODE_BURST : IOSCAPTURE_MODE_SELFIE))
	{
		if (tWaited == 0) tWaited = now;
		if (now - tWaited > CAM_GIVE_UP)
		{
			IOSCamera_Message("Couldn't take the photos");
			EndStage();
		}
		return;
	}
	tWaited = now;
	asked = true;
}

void IOSCamera_Frame(bool bPlay)
{
	static bool init = false;
	if (!init)
	{
		init = true;
		IOSPhotos_Init();
	}
	const double now = Now();
	if (!bPlay)
	{
		if (stage != STAGE_NONE || burst || IOSCapture_IsPending()) IOSCamera_Abort();
		eyeKnown = false;
		return;
	}
	if (stage != STAGE_NONE && CantHere())
	{
		IOSCapture_Cancel();
		EndStage();
	}
	// the people still there (a load or a level change ends it first; this
	// is in case)
	for (size_t i = 0; i < people.size(); i++)
	{
		if (!StillThere(people[i].act))
		{
			IOSCapture_Cancel();
			EndStage();
			break;
		}
	}
	if (tPhotoAsked > 0 && !IOSCapture_IsPending()) tPhotoAsked = 0;
	if (tPhotoAsked > 0 && now - tPhotoAsked > CAM_PHOTO_TIMEOUT && stage != STAGE_SELFIE && stage != STAGE_CINEMA)
	{
		IOSCapture_Cancel();
		tPhotoAsked = 0;
	}
	if (burst && now >= tBurstNext)
	{
		if (ctBurst >= CAM_BURST_MAX) EndBurst();
		else if (IOSCapture_Request(IOSCAPTURE_CLEAN, IOSCAPTURE_MODE_BURST))
		{
			ctBurst++;
			tBurstNext = now + CAM_BURST_GAP;
		}
	}
	if (stage == STAGE_SELFIE || stage == STAGE_CINEMA) TickShots(now);
	if (stage == STAGE_FREEZE) PlaceOrbit(false);
}

// ------------------------------------------------------------ the frames

void IOSCamera_FrameBegin(int w, int h)
{
	IOSCapture_FrameBegin(w, h);
}
void IOSCamera_AfterScene()
{
	// (the clean photo: the 3D view's window, not the status bar's strip)
	IOSCapture_SceneRect(viewwindowx, viewwindowy, viewwidth, viewheight);
	IOSCapture_AfterScene();
}
extern "C" void IOSCamera_AfterHud(void) { IOSCapture_AfterHud(); }

extern "C" int IOSCapture_ReadFrame(int w, int h, unsigned char *p)
{
	return screen ? screen->IOSReadFrame(w, h, p) : 0;
}

#ifdef IOSTOUCH_RIGHOOKS
// ------------------------------------------------------------ the rig

// Every actor's state, the clock and the random numbers, as one number:
// the same before and after a selfie or a freeze
static unsigned Fnv(unsigned h, const void *p, size_t n)
{
	const unsigned char *b = (const unsigned char *)p;
	for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
	return h;
}

static unsigned RigHash()
{
	unsigned h = 2166136261u;
	int ct = 0;
	auto it = Level()->GetThinkerIterator<AActor>();
	static int dumpNo = 0;
	FILE *dump = getenv("IOSCAM_DUMP") ? fopen(FStringf("%s.%d", getenv("IOSCAM_DUMP"), dumpNo++).GetChars(), "w") : nullptr;
	while (AActor *a = it.Next())
	{
		if (dump)
			fprintf(dump, "%s pos %g %g %g prev %g %g %g ang %g %g pang %g %g st %p spr %d fr %d tics %d hp %d fl %x rf %x vel %g %g %g sec %p ss %p fz %g cz %g fc %g\n",
				a->GetClass()->TypeName.GetChars(), a->X(), a->Y(), a->Z(), a->Prev.X, a->Prev.Y, a->Prev.Z, a->Angles.Yaw.Degrees(), a->Angles.Pitch.Degrees(),
				a->PrevAngles.Yaw.Degrees(), a->PrevAngles.Pitch.Degrees(), (void *)a->state, a->sprite, a->frame, a->tics, a->health, (unsigned)a->flags,
				(unsigned)a->renderflags, a->Vel.X, a->Vel.Y, a->Vel.Z, (void *)a->Sector, (void *)a->subsector, a->floorz, a->ceilingz, a->Floorclip);
		const DVector3 pos = a->Pos();
		h = Fnv(h, &pos, sizeof(pos));
		h = Fnv(h, &a->Prev, sizeof(a->Prev));
		h = Fnv(h, &a->Angles, sizeof(a->Angles));
		h = Fnv(h, &a->PrevAngles, sizeof(a->PrevAngles));
		h = Fnv(h, &a->state, sizeof(a->state));
		h = Fnv(h, &a->sprite, sizeof(a->sprite));
		h = Fnv(h, &a->frame, sizeof(a->frame));
		h = Fnv(h, &a->tics, sizeof(a->tics));
		h = Fnv(h, &a->health, sizeof(a->health));
		// (not the player's MF_PICKUP: UZDoom's own prediction takes it away
		// while the game is held -- as under the menu -- and gives it back
		// as the game goes on: FActorBackup)
		const ActorFlags fl = a->player ? (a->flags & ~MF_PICKUP) : a->flags;
		h = Fnv(h, &fl, sizeof(fl));
		h = Fnv(h, &a->renderflags, sizeof(a->renderflags));
		h = Fnv(h, &a->Vel, sizeof(a->Vel));
		h = Fnv(h, &a->Sector, sizeof(a->Sector));
		h = Fnv(h, &a->subsector, sizeof(a->subsector));
		h = Fnv(h, &a->floorz, sizeof(a->floorz));
		h = Fnv(h, &a->ceilingz, sizeof(a->ceilingz));
		h = Fnv(h, &a->Floorclip, sizeof(a->Floorclip));
		ct++;
	}
	if (dump) fclose(dump);
	h = Fnv(h, &Level()->maptime, sizeof(Level()->maptime));
	// the random number generators, as a save game writes them
	unsigned seeds = 2166136261u;
	{
		FSerializer arc;
		if (arc.OpenWriter(false))
		{
			FRandom::StaticWriteRNGState(arc);
			unsigned len = 0;
			const char *out = arc.GetOutput(&len);
			if (out) seeds = Fnv(seeds, out, len);
		}
	}
	h = Fnv(h, &seeds, sizeof(seeds));
	CAMLOG("(%d actors, gametic %d, maptime %d, rng %08x)\n", ct, gametic, Level()->maptime, seeds);
	return h;
}

void IOSCamera_RigCommand(const char *cmd)
{
	float a = 0, b = 0;
	int n = 0;
	if (!strcmp(cmd, "photo")) IOSCamera_Photo();
	else if (!strcmp(cmd, "selfie")) IOSCamera_Selfie();
	else if (!strcmp(cmd, "burst")) IOSCamera_Burst();
	else if (!strcmp(cmd, "freeze")) IOSCamera_Freeze();
	else if (!strcmp(cmd, "cinema")) IOSCamera_Cinematic();
	else if (!strcmp(cmd, "end")) IOSCamera_FreezeEnd();
	else if (!strcmp(cmd, "options")) IOSCamera_ShowOptions();
	else if (sscanf(cmd, "orbit %f %f", &a, &b) == 2) IOSCamera_FreezeOrbit(a, b);
	else if (sscanf(cmd, "pan %f %f", &a, &b) == 2) IOSCamera_FreezePan(a, b);
	else if (sscanf(cmd, "zoom %f", &a) == 1) IOSCamera_FreezeZoom(a);
	else if (sscanf(cmd, "kinds %d", &n) == 1) IOSCamera_SetKinds(n);
	else if (sscanf(cmd, "selfies %d", &n) == 1) IOSCamera_SetSelfieShots(n);
	else if (sscanf(cmd, "freezeon %d", &n) == 1) IOSCamera_SetFreezeOn(n);
	else if (!strcmp(cmd, "hash")) CAMLOG("state hash %08x\n", RigHash());
	else if (!strcmp(cmd, "status"))
		CAMLOG("stage %d cam %d figure %d people %d hidden %d taken %d busy %d pos %.0f %.0f %.0f yaw %.0f pitch %.1f zoom %.2f\n", stage,
			(int)camOn, (int)showFigure, (int)people.size(), (int)hidden.size(), ctTaken, IOSCamera_IsBusy(), cam.pos.X, cam.pos.Y,
			cam.pos.Z, cam.yaw * 180 / CAM_PI, cam.pitch * 180 / CAM_PI, cam.zoom);
	else if (sscanf(cmd, "gather %d", &n) == 1)
	{
		// (the rig only: the nearest n monsters brought to stand ahead, for
		// selfies where none are near; changes the game, unlike the rest)
		if (!ReadEye()) return;
		FindPeople(100000.0, n);
		const V2 eye = EyeAt();
		const double room = FreeAhead(eye, eyeYaw, 6.0 * CAM_S);
		for (size_t i = 0; i < people.size(); i++)
		{
			const V2 p = eye + Fwd(eyeYaw) * fmin(room - 40, 160.0 + 50.0 * (i / 3)) + Right(eyeYaw) * (((int)(i % 3) - 1) * 56.0);
			people[i].act->UnlinkFromWorld(nullptr);
			people[i].act->SetXYZ(DVector3(p.X, p.Y, FloorAt(p)));
			people[i].act->LinkToWorld(nullptr);
			people[i].act->ClearInterpolation();
		}
		CAMLOG("gathered %d\n", (int)people.size());
		people.clear();
	}
	else
		CAMLOG("unknown command '%s'\n", cmd);
}
#endif
