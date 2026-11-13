/*
** IOSGame.cpp: the touch controls' game side (see IOSGame.h, IOSTouch.h),
** for DOOM in UZDoom; from the owner's Raze port's ios/IOSGame.cpp
**
** Once a frame it tells the overlay what the game is doing (playing, the
** console, paused, or nothing to show), what the player has for the weapon
** wheel (the player's weapon slots, so a mod's weapons come too), and
** carries out what the player asked for (the menu, the console, the
** automap, quick save and load, the cheat). The look goes in as the
** mouse's movement (UZDoom's mouse sensitivity and invert settings apply),
** tilt aiming straight into the view's angles; once a tic the stick, the
** buttons and the weapon picks go into the ticcmd, whatever keys are bound.
*/

#include <string.h>
#include <sys/stat.h>
#include <math.h>
#include <dirent.h>
#include <strings.h>
#include <algorithm>
#include <vector>

#include "IOSGame.h"
#include "IOSCamera.h"
#include "IOSPicker.h"

#include <SDL.h>
SDL_Window* IOS_GetGameWindow(); // sdlglvideo.cpp: the game's window (OpenGL ES or Vulkan)

#include "doomstat.h"
#include "d_player.h"
#include "d_event.h"
#include "d_eventbase.h"
#include "d_main.h"
#include "g_game.h"
#include "g_level.h"
#include "g_levellocals.h"
#include "gi.h"
#include "c_console.h"
#include "c_dispatch.h"
#include "c_cvars.h"
#include "menu.h"
#include "keydef.h"
#include "p_setup.h"
#include "printf.h"
#include "a_weapons.h"
#include "r_state.h"
#include "r_data/sprites.h"
#include "texturemanager.h"
#include "bitmap.h"
#include "savegamemanager.h"
#include "i_interface.h"
#include "vm.h"
#include "actorinlines.h"
#include "s_sound.h"
#include "am_map.h"
#include "m_misc.h"
#include "v_video.h"
#include "sbar.h"
#include "i_time.h"

EXTERN_CVAR(Float, m_sensitivity_x)
EXTERN_CVAR(Float, m_sensitivity_y)
EXTERN_CVAR(Bool, invertmouse)
EXTERN_CVAR(Bool, invertmousex)
EXTERN_CVAR(Int, quicksaverotationcount)
EXTERN_CVAR(Int, quicksavenum)
EXTERN_CVAR(Bool, cl_run)
EXTERN_CVAR(Int, screenblocks)

extern const AActor* SendItemUse; // g_game.cpp: the item to use in the next ticcmd ("use <class>")
extern int chatmodeon;

// What the overlay shows: the game being played (no menu, no console, no
// typing a message), the console over it, the game paused with the pause
// key, or nothing (menus, intermissions, the finale, the title map, demos,
// loading)
// "Switch game" is over the game (GAMES in MENU's tray): it holds still
static bool switchingGame = false;
static void SwitchClosed(int bPicked)
{
	switchingGame = false;
}

static int TouchMode()
{
	// (the app in the background: nothing, so the weapon wheel or the level
	// list closes, picking nothing, and the game isn't held when it's back)
	extern bool AppActive;
	if (!AppActive) return IOSTOUCH_HIDDEN;
	if (gamestate != GS_LEVEL || menuactive != MENU_Off || demoplayback || gameaction != ga_nothing) return IOSTOUCH_HIDDEN;
	if (ConsoleState == c_down || ConsoleState == c_falling) return IOSTOUCH_CONSOLE;
	if (chatmodeon) return IOSTOUCH_HIDDEN;
	if (paused) return IOSTOUCH_PAUSED;
	return IOSTOUCH_GAMEPLAY;
}

static player_t* Player()
{
	player_t* p = &players[consoleplayer];
	return p->mo != nullptr ? p : nullptr;
}

//---------------------------------------------------------------------------
//
// The cheat's list of levels: the game's own, in MAPINFO's order (add-ons'
// maps too, as they are loaded), the ones whose map is there
//
//---------------------------------------------------------------------------

struct CheatLevel
{
	FString name; // "E1M1: Hangar", "MAP01: Entryway" (as the game names it)
	FString map;  // its map's lump
};
static TArray<CheatLevel> cheatLevels;

static void FindLevels()
{
	cheatLevels.Clear();
	for (unsigned i = 0; i < wadlevelinfos.Size(); i++)
	{
		auto& info = wadlevelinfos[i];
		if (info.MapName.IsEmpty() || !P_CheckMapData(info.MapName.GetChars())) continue;
		bool dup = false;
		for (auto& l : cheatLevels) if (l.map.CompareNoCase(info.MapName) == 0) dup = true;
		if (dup) continue;
		FString name = info.LookupLevelName();
		// (the name with its map in front, where the game's name hasn't it)
		if (name.IsEmpty()) name = info.MapName;
		else if (strnicmp(name.GetChars(), info.MapName.GetChars(), info.MapName.Len()) != 0) name = FStringf("%s: %s", info.MapName.GetChars(), name.GetChars());
		cheatLevels.Push({ name, info.MapName });
	}
}

int IOSGame_LevelInfo(int iLevel, char* strName, int ctName, char* strFile, int ctFile)
{
	if (iLevel < 1 || iLevel > (int)cheatLevels.Size()) return 0;
	snprintf(strName, ctName, "%s", cheatLevels[iLevel - 1].name.GetChars());
	snprintf(strFile, ctFile, "%s", cheatLevels[iLevel - 1].map.GetChars());
	return 1;
}

int IOSGame_CurrentLevel()
{
	for (unsigned i = 0; i < cheatLevels.Size(); i++)
	{
		if (cheatLevels[i].map.CompareNoCase(primaryLevel->MapName) == 0) return i + 1;
	}
	return 0;
}

//---------------------------------------------------------------------------
//
// The cheat (sithewok), on UZDoom's own cheats: god mode and "give all"
// (weapons, ammo, keys, armour, the backpack, full health), again at each
// new level while it is on; off, god mode goes off, the rest is kept
//
//---------------------------------------------------------------------------

static bool cheatOn = false;
static int cheatGiveIn = -1;  // frames until the cheat gives (again: a new level); -1 none
static int godAskedTic = -100; // (god is a toggle that comes in the next tics: not asked again meanwhile)

// The cheat works in a single-player level being played, where the game's
// own cheats do (CheckCheatmode: sv_cheats in a network game, nightmare...)
static bool CheatAllowed()
{
	return !netgame && gamestate == GS_LEVEL && usergame && Player() != nullptr && !CheckCheatmode(false, true);
}

// Why not, for the message (nullptr: it is allowed)
static const char* CheatRefusal()
{
	if (netgame) return "No cheat in a network game";
	if (gamestate != GS_LEVEL || !usergame || Player() == nullptr) return "The cheat works in a level being played";
	if (CheckCheatmode(false, true)) return "The game doesn't allow cheats here";
	return nullptr;
}

static bool GodOn()
{
	auto p = Player();
	return p && (p->cheats & CF_GODMODE) != 0;
}

static void SetGod(bool on)
{
	if (GodOn() == on || gametic - godAskedTic < 4) return;
	godAskedTic = gametic;
	C_DoCommand("god");
}

static void CheatGive()
{
	SetGod(true);
	C_DoCommand("give all");
}

// The cheat keeps every ammo the player has full (as "give all" left it)
static void CheatAmmoFull()
{
	auto p = Player();
	if (!p || p->health <= 0) return;
	for (AActor* item = p->mo->Inventory; item; item = item->Inventory)
	{
		if (!item->IsKindOf(NAME_Ammo)) continue;
		const int max = item->IntVar(NAME_MaxAmount);
		if (max > 0 && item->IntVar(NAME_Amount) < max) item->IntVar(NAME_Amount) = max;
	}
}

static void CheatFrame()
{
	if (!cheatOn || !CheatAllowed()) return;
	if (cheatGiveIn < 0) CheatAmmoFull();
	if (cheatGiveIn > 0 && --cheatGiveIn == 0)
	{
		cheatGiveIn = -1;
		CheatGive();
	}
	// god mode lost on the way (a new game from the level list): back on
	else if (cheatGiveIn < 0 && !GodOn()) SetGod(true);
}

// The word typed in the console: "sithewok" switches the cheat, "sithewok
// on" / "sithewok off" set it (the overlay says so)
CCMD(sithewok)
{
	bool want = !IOSTouch_CheatOn();
	if (argv.argc() >= 2) want = !stricmp(argv[1], "on");
	if (want != (IOSTouch_CheatOn() != 0))
	{
		IOSTouch_CheatTyped();
		Printf("sithewok: %s\n", IOSTouch_CheatOn() ? "on (god mode, everything given, ammo kept full; NOCLIP in MENU's tray)" :
			want ? (CheatRefusal() ? CheatRefusal() : "not now") : "off (god mode off)");
	}
}

//---------------------------------------------------------------------------
//
// The weapon wheel: the player's weapon slots in the order NEXT WPN goes
// through them (slot 0 to 9, each in its order), the weapons the game has
// (one whose pickup sprite isn't in the loaded game, as DOOM's super
// shotgun, isn't), each with its name, its ammo and its ammo's colour
//
//---------------------------------------------------------------------------

static PClassActor* wheelClasses[IOSTOUCH_WPN_MAX];
static int wheelCount = 0;
static unsigned int wheelList = 0;

// Whether the loaded game has a weapon: its pickup sprite (its Spawn state's
// frame) is in it, or it has none (DOOM's fist)
static bool GameHasWeapon(PClassActor* cls)
{
	auto def = GetDefaultByType(cls);
	if (!def) return false;
	FState* st = def->SpawnState;
	if (!st || st->sprite == 0 || (unsigned)st->sprite >= sprites.Size()) return true;
	return sprites[st->sprite].numframes > st->GetFrame();
}

static void BuildWheelList(player_t* p)
{
	wheelCount = 0;
	unsigned int h = 2166136261u;
	for (int slot = 0; slot < NUM_WEAPON_SLOTS && wheelCount < IOSTOUCH_WPN_MAX; slot++)
	{
		for (int i = 0; i < p->weapons.SlotSize(slot) && wheelCount < IOSTOUCH_WPN_MAX; i++)
		{
			PClassActor* cls = p->weapons.GetWeapon(slot, i);
			if (!cls || !GameHasWeapon(cls)) continue;
			bool dup = false;
			for (int k = 0; k < wheelCount; k++) if (wheelClasses[k] == cls) dup = true;
			if (dup) continue;
			wheelClasses[wheelCount++] = cls;
			h = (h ^ (unsigned)(uintptr_t)cls) * 16777619u;
		}
	}
	wheelList = (h ^ (unsigned)wheelCount) | 1u; // (never 0)
}

// The slice's colour: the kind of ammo, DOOM's four by their classes (a
// mod's ammo that derives from them too), any other by its name
static int AmmoColour(PClassActor* ammo)
{
	if (!ammo) return IOSTOUCH_WCOL_NONE;
	static const struct { const char* cls; int col; } known[] = {
		{ "Clip", IOSTOUCH_WCOL_BULLETS }, { "Shell", IOSTOUCH_WCOL_SHELLS },
		{ "RocketAmmo", IOSTOUCH_WCOL_ROCKETS }, { "Cell", IOSTOUCH_WCOL_ELECTRICITY },
	};
	for (auto& k : known)
	{
		auto c = PClass::FindActor(k.cls);
		if (c && ammo->IsDescendantOf(c)) return k.col;
	}
	unsigned h = 0;
	for (const char* s = ammo->TypeName.GetChars(); *s; s++) h = h * 31 + (unsigned char)*s;
	return 1 + (int)(h % (IOSTOUCH_WCOL_COUNT - 1));
}

// Weapon.CheckAmmo(EitherFire, false): what NEXT WPN would switch to
static bool WeaponHasAmmo(AActor* weap)
{
	IFVIRTUALPTRNAME(weap, NAME_Weapon, CheckAmmo)
	{
		VMValue params[] = { weap, 2 /* EitherFire */, false, false, -1 };
		int rv = 1;
		VMReturn ret(&rv);
		VMCall(func, params, countof(params), &ret, 1);
		return rv != 0;
	}
	return true;
}

static int WheelIndexOf(AActor* weap)
{
	if (!weap || weap == (AActor*)WP_NOCHANGE) return -1;
	for (int i = 0; i < wheelCount; i++) if (weap->GetClass() == wheelClasses[i]) return i;
	// (a powered-up weapon: its sister's slice)
	auto sister = weap->PointerVar<AActor>(NAME_SisterWeapon);
	if (sister) for (int i = 0; i < wheelCount; i++) if (sister->GetClass() == wheelClasses[i]) return i;
	return -1;
}

static void FillWeapons(IOSTouchWeapons* pw)
{
	auto p = Player();
	if (!p) return;
	BuildWheelList(p);
	pw->bValid = 1;
	pw->bCanSelect = p->playerstate == PST_LIVE && p->health > 0 && gamestate == GS_LEVEL;
	pw->ctWeapons = wheelCount;
	pw->uList = wheelList;
	pw->bInfiniteAmmo = (dmflags & DF_INFINITE_AMMO) != 0;
	AActor* wanted = (p->PendingWeapon && p->PendingWeapon != (AActor*)WP_NOCHANGE) ? p->PendingWeapon : p->ReadyWeapon;
	pw->iWanted = WheelIndexOf(wanted);
	for (int i = 0; i < wheelCount; i++)
	{
		PClassActor* cls = wheelClasses[i];
		auto def = GetDefaultByType(cls);
		FString tag = def->GetTag();
		tag.ToUpper();
		// (DOOM's fist is "Brass Knuckles" in the game's own text: the wheel
		// says what it is)
		if (cls->TypeName == FName("Fist")) tag = "FIST";
		snprintf(pw->astrName[i], IOSTOUCH_WPN_NAME, "%s", tag.GetChars());
		PClassActor* ammoType = def->PointerVar<PClassActor>(NAME_AmmoType1);
		pw->aiColour[i] = AmmoColour(ammoType);
		pw->aiAmmo[i] = pw->aiMaxAmmo[i] = -1;
		AActor* weap = p->mo->FindInventory(cls);
		if (ammoType)
		{
			AActor* ammo = p->mo->FindInventory(ammoType);
			pw->aiAmmo[i] = ammo ? ammo->IntVar(NAME_Amount) : 0;
			pw->aiMaxAmmo[i] = ammo ? ammo->IntVar(NAME_MaxAmount) : GetDefaultByType(ammoType)->IntVar(NAME_MaxAmount);
		}
		if (weap)
		{
			pw->ulOwned |= 1u << i;
			if (WeaponHasAmmo(weap)) pw->ulReady |= 1u << i;
		}
	}
}

// A weapon picked on the wheel: used as "use <class>" would (SendItemUse:
// the weapon's Use selects it, in the next ticcmd)
static void PickWeapon(int i)
{
	auto p = Player();
	if (!p || i < 0 || i >= wheelCount) return;
	AActor* weap = p->mo->FindInventory(wheelClasses[i]);
	if (weap) SendItemUse = weap;
}

//---------------------------------------------------------------------------
//
// The weapon wheel's pictures, from the game's own art (the player's data,
// read now: nothing of it is in the app): the weapon's pickup sprite, else
// its inventory icon, else its Ready state's sprite (DOOM's fist: PUNGA0);
// cropped to its pixels and scaled to fit IOSTOUCH_ICON_MAX: down by
// averaging, up by a whole number (sharp pixels)
//
//---------------------------------------------------------------------------

static FGameTexture* StateTexture(FState* st)
{
	if (!st || st->sprite == 0 || (unsigned)st->sprite >= sprites.Size()) return nullptr;
	const spritedef_t& sd = sprites[st->sprite];
	if (st->GetFrame() >= sd.numframes) return nullptr;
	FTextureID id = SpriteFrames[sd.spriteframes + st->GetFrame()].Texture[0];
	return id.isValid() ? TexMan.GetGameTexture(id) : nullptr;
}

// (pbHand: it is the first-person sprite, an arm reaching in from the
// bottom left, as DOOM's fist: MakeIcon keeps its hand end)
static FGameTexture* WeaponTexture(PClassActor* cls, bool* pbHand = nullptr)
{
	if (pbHand) *pbHand = false;
	auto def = GetDefaultByType(cls);
	if (!def) return nullptr;
	FGameTexture* tex = StateTexture(def->SpawnState);
	if (!tex)
	{
		FTextureID icon = def->TextureIDVar(NAME_Icon);
		if (icon.isValid()) tex = TexMan.GetGameTexture(icon);
	}
	// no pickup or icon (DOOM's fist): the first frame of its punch, the fist
	// clenched (PUNGB0), else its Ready frame (PUNGA0, the open hand)
	if (!tex && (tex = StateTexture(def->FindState(NAME_Fire))) != nullptr && pbHand) *pbHand = true;
	if (!tex && (tex = StateTexture(def->FindState(NAME_Ready))) != nullptr && pbHand) *pbHand = true;
	if (tex && tex->isValid() && tex->GetTexture() && tex->GetTexelWidth() > 0 && tex->GetTexelHeight() > 0) return tex;
	return nullptr;
}

static bool MakeIcon(FGameTexture* tex, TArray<uint8_t>& out, int& ow, int& oh, bool bHand = false)
{
	if (!tex) return false;
	FBitmap bmp = tex->GetTexture()->GetBgraBitmap(nullptr);
	const int w = bmp.GetWidth(), h = bmp.GetHeight(), pitch = bmp.GetPitch();
	const uint8_t* px = bmp.GetPixels();
	if (!px || w <= 0 || h <= 0) return false;
	// (cropped to what isn't see-through)
	int x0 = w, y0 = h, x1 = -1, y1 = -1;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			if (px[y * pitch + x * 4 + 3] > 8)
			{
				x0 = min(x0, x); x1 = max(x1, x);
				y0 = min(y0, y); y1 = max(y1, y);
			}
	if (x1 < x0 || y1 < y0) return false;
	// (an arm reaching in: its hand end, at most 1.6 times as wide as high,
	// cropped again to what isn't see-through there)
	if (bHand && x1 - x0 + 1 > (y1 - y0 + 1) * 8 / 5)
	{
		x0 = x1 + 1 - (y1 - y0 + 1) * 8 / 5;
		int ny0 = h, ny1 = -1;
		for (int y = y0; y <= y1; y++)
			for (int x = x0; x <= x1; x++)
				if (px[y * pitch + x * 4 + 3] > 8) { ny0 = min(ny0, y); ny1 = max(ny1, y); }
		if (ny1 >= ny0) { y0 = ny0; y1 = ny1; }
	}
	const int cw = x1 - x0 + 1, ch = y1 - y0 + 1, big = max(cw, ch);
	if (big <= IOSTOUCH_ICON_MAX)
	{
		const int k = max(1, IOSTOUCH_ICON_MAX / big);
		ow = cw * k;
		oh = ch * k;
		out.Resize(ow * oh * 4);
		for (int y = 0; y < oh; y++)
			for (int x = 0; x < ow; x++)
			{
				const uint8_t* p = px + (y0 + y / k) * pitch + (x0 + x / k) * 4;
				uint8_t* q = &out[(y * ow + x) * 4];
				q[0] = p[2]; q[1] = p[1]; q[2] = p[0]; q[3] = p[3]; // (BGRA to RGBA)
			}
		return true;
	}
	ow = max(1, cw * IOSTOUCH_ICON_MAX / big);
	oh = max(1, ch * IOSTOUCH_ICON_MAX / big);
	out.Resize(ow * oh * 4);
	for (int y = 0; y < oh; y++)
		for (int x = 0; x < ow; x++)
		{
			// the average of the texels under it, weighted by their alpha
			const int sx0 = x0 + x * cw / ow, sx1 = x0 + max((x + 1) * cw / ow, x * cw / ow + 1);
			const int sy0 = y0 + y * ch / oh, sy1 = y0 + max((y + 1) * ch / oh, y * ch / oh + 1);
			double r = 0, g = 0, b = 0, a = 0;
			int n = 0;
			for (int sy = sy0; sy < sy1; sy++)
				for (int sx = sx0; sx < sx1; sx++, n++)
				{
					const uint8_t* p = px + sy * pitch + sx * 4;
					const double al = p[3];
					r += p[2] * al; g += p[1] * al; b += p[0] * al; a += al;
				}
			uint8_t* q = &out[(y * ow + x) * 4];
			if (a > 0)
			{
				q[0] = (uint8_t)(r / a + 0.5); q[1] = (uint8_t)(g / a + 0.5); q[2] = (uint8_t)(b / a + 0.5);
			}
			else q[0] = q[1] = q[2] = 0;
			q[3] = (uint8_t)(a / max(n, 1) + 0.5);
		}
	return true;
}

// Once a frame: the pictures the overlay wants (each once per list)
static void FeedIcons()
{
	TArray<uint8_t> px;
	int w = 0, h = 0;
	const unsigned wanted = IOSTouch_WantedIcons();
	for (int i = 0; i < IOSTOUCH_WPN_MAX; i++)
	{
		if (!(wanted & (1u << i))) continue;
		bool hand = false;
		FGameTexture* tex = i < wheelCount ? WeaponTexture(wheelClasses[i], &hand) : nullptr;
		if (tex && MakeIcon(tex, px, w, h, hand)) IOSTouch_SetWeaponIcon(i, px.Data(), w, h);
		else IOSTouch_SetWeaponIcon(i, nullptr, 0, 0); // (its name shows)
	}
}

//---------------------------------------------------------------------------
//
// CHEAT's round icon, made at run time from the player's own DOOM data: the
// wall MWALL4_1 (the demon's face in a pentagram: DOOM, DOOM II, TNT and
// Plutonia have it alike), its eyes lit, the wall's own star painted out
// and DOOM's star and circle drawn again over it evenly, cut round to the
// circle, which is the button's edge. The recipe is the owner's approved
// mockup's (Python, Pillow), step for step: the eyes' outline was traced
// by the owner; Pillow's blurs, resizing and compositing are followed
// closely so the picture matches it. Off: grey, at 60%.
//
//---------------------------------------------------------------------------

namespace {
struct CheatPic
{
	int w = 0, h = 0, c = 0;
	std::vector<uint8_t> p;
	CheatPic() = default;
	CheatPic(int w_, int h_, int c_, uint8_t v = 0) : w(w_), h(h_), c(c_), p(size_t(w_) * h_ * c_, v) {}
	uint8_t* at(int x, int y) { return &p[(size_t(y) * w + x) * c]; }
	const uint8_t* at(int x, int y) const { return &p[(size_t(y) * w + x) * c]; }
};

inline uint8_t Clip8(double v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : int(v + 0.5)); }

// Pillow's GaussianBlur: three box blurs each way, of a fractional radius,
// edges repeated, rounded to whole values after each
void CheatBoxLine(const uint8_t* in, uint8_t* out, int n, int stride, double r)
{
	const int ri = int(r);
	const double frac = r - ri, div = r * 2 + 1;
	std::vector<double> line(n);
	for (int i = 0; i < n; i++) line[i] = in[size_t(i) * stride];
	auto get = [&](int i) { return line[i < 0 ? 0 : i >= n ? n - 1 : i]; };
	double s = 0; // (a running sum of the window)
	for (int k = -ri; k <= ri; k++) s += get(k);
	for (int i = 0; i < n; i++)
	{
		out[size_t(i) * stride] = Clip8((s + (get(i - ri - 1) + get(i + ri + 1)) * frac) / div);
		s += get(i + ri + 1) - get(i - ri);
	}
}

void CheatGaussian(CheatPic& im, double radius)
{
	const int passes = 3;
	const double sigma2 = radius * radius / passes;
	const double L = sqrt(12.0 * sigma2 + 1.0);
	const double l = floor((L - 1.0) / 2.0);
	double a = (2 * l + 1) * (l * (l + 1) - 3 * sigma2);
	a /= 6 * (sigma2 - (l + 1) * (l + 1));
	const double r = l + a;
	for (int ch = 0; ch < im.c; ch++)
	{
		for (int pass = 0; pass < passes; pass++)
			for (int y = 0; y < im.h; y++) CheatBoxLine(im.at(0, y) + ch, im.at(0, y) + ch, im.w, im.c, r);
		for (int pass = 0; pass < passes; pass++)
			for (int x = 0; x < im.w; x++) CheatBoxLine(im.at(x, 0) + ch, im.at(x, 0) + ch, im.h, im.w * im.c, r);
	}
}

// Pillow's LANCZOS resize (alpha premultiplied for RGBA, as Pillow does):
// across, then down, whole values in between
double CheatLanczos(double x)
{
	auto sinc = [](double v) { return v == 0.0 ? 1.0 : sin(M_PI * v) / (M_PI * v); };
	return (x >= -3.0 && x < 3.0) ? sinc(x) * sinc(x / 3.0) : 0.0;
}

void CheatResamplePass(const CheatPic& in, CheatPic& out, bool across)
{
	const int inSize = across ? in.w : in.h, outSize = across ? out.w : out.h;
	const double scale = double(inSize) / outSize, fs = std::max(scale, 1.0), support = 3.0 * fs;
	const int other = across ? in.h : in.w;
	for (int o = 0; o < outSize; o++)
	{
		const double centre = (o + 0.5) * scale;
		int x0 = int(centre - support + 0.5), x1 = int(centre + support + 0.5);
		x0 = std::max(x0, 0);
		x1 = std::min(x1, inSize);
		std::vector<double> k(x1 - x0);
		double ww = 0;
		for (int x = x0; x < x1; x++) ww += (k[x - x0] = CheatLanczos((x - centre + 0.5) / fs));
		for (auto& v : k) v /= ww;
		for (int j = 0; j < other; j++)
			for (int ch = 0; ch < in.c; ch++)
			{
				double s = 0;
				for (int x = x0; x < x1; x++) s += k[x - x0] * (across ? in.at(x, j) : in.at(j, x))[ch];
				(across ? out.at(o, j) : out.at(j, o))[ch] = Clip8(s);
			}
	}
}

CheatPic CheatResize(const CheatPic& src, int ow, int oh)
{
	CheatPic in = src;
	if (in.c == 4)
		for (size_t i = 0; i < in.p.size(); i += 4)
			for (int ch = 0; ch < 3; ch++) in.p[i + ch] = uint8_t((in.p[i + ch] * in.p[i + 3] + 127) / 255);
	CheatPic mid(ow, in.h, in.c), out(ow, oh, in.c);
	CheatResamplePass(in, mid, true);
	CheatResamplePass(mid, out, false);
	if (out.c == 4)
		for (size_t i = 0; i < out.p.size(); i += 4)
		{
			const int al = out.p[i + 3];
			for (int ch = 0; ch < 3; ch++) out.p[i + ch] = al ? Clip8(out.p[i + ch] * 255.0 / al) : 0;
		}
	return out;
}

// src over dst (RGBA, not premultiplied), as Pillow's alpha_composite
void CheatOver(uint8_t* dst, const uint8_t* src)
{
	const double sa = src[3] / 255.0, da = dst[3] / 255.0, oa = sa + da * (1 - sa);
	if (oa <= 0) { dst[0] = dst[1] = dst[2] = dst[3] = 0; return; }
	for (int ch = 0; ch < 3; ch++) dst[ch] = Clip8((src[ch] * sa + dst[ch] * da * (1 - sa)) / oa);
	dst[3] = Clip8(oa * 255);
}

bool CheatInPoly(const double (*poly)[2], int n, double x, double y)
{
	bool in = false;
	for (int i = 0, j = n - 1; i < n; j = i++)
	{
		if (((poly[i][1] > y) != (poly[j][1] > y)) &&
			x < (poly[j][0] - poly[i][0]) * (y - poly[i][1]) / (poly[j][1] - poly[i][1]) + poly[i][0]) in = !in;
	}
	if (in) return true;
	// (Pillow's polygon also fills the pixels its outline passes through)
	for (int i = 0, j = n - 1; i < n; j = i++)
	{
		const double vx = poly[i][0] - poly[j][0], vy = poly[i][1] - poly[j][1], l2 = vx * vx + vy * vy;
		double t = l2 > 0 ? ((x - poly[j][0]) * vx + (y - poly[j][1]) * vy) / l2 : 0;
		t = t < 0 ? 0 : t > 1 ? 1 : t;
		if (hypot(x - poly[j][0] - t * vx, y - poly[j][1] - t * vy) <= 0.5) return true;
	}
	return false;
}
} // namespace

// The circle (DOOM's own, measured from the wall) and the star's points
static const double CHEAT_CX = 64.54, CHEAT_CY = 63.6, CHEAT_R = 52.1, CHEAT_CW = 4.2;

// The wall made into the icon at 128 x 128 (RGBA): its eyes lit, its star
// redrawn. False: the wall isn't the one expected.
static bool CheatWallIcon(FGameTexture* tex, CheatPic& on)
{
	if (!tex || !tex->GetTexture()) return false;
	FBitmap bmp = tex->GetTexture()->GetBgraBitmap(nullptr);
	if (bmp.GetWidth() != 128 || bmp.GetHeight() != 128 || !bmp.GetPixels()) return false;
	CheatPic src(128, 128, 4);
	for (int y = 0; y < 128; y++)
		for (int x = 0; x < 128; x++)
		{
			const uint8_t* q = bmp.GetPixels() + y * bmp.GetPitch() + x * 4;
			uint8_t* d = src.at(x, y);
			d[0] = q[2]; d[1] = q[1]; d[2] = q[0]; d[3] = 255;
		}

	// the eyes: the owner's outline of the left one, and the same mirrored
	static const double eye[8][2] = { {38,50},{42,51},{45,54},{47,58},{47.5,63.5},{44,61.5},{41,58.5},{38.5,55} };
	double eye2[8][2];
	for (int i = 0; i < 8; i++) { eye2[i][0] = 126 - eye[i][0]; eye2[i][1] = eye[i][1]; }
	CheatPic eyes(128, 128, 1);
	for (int y = 0; y < 128; y++)
		for (int x = 0; x < 128; x++)
			if (CheatInPoly(eye, 8, x, y) || CheatInPoly(eye2, 8, x, y)) *eyes.at(x, y) = 255;

	// lit: each eye pixel's brightness through a fire ramp, a red glow round them
	static const struct { double v; int c[3]; } ramp[] = {
		{0,{90,0,0}}, {40,{190,15,5}}, {80,{255,60,10}}, {120,{255,150,40}}, {170,{255,230,140}} };
	CheatPic lit = src, glow(128, 128, 1);
	for (int y = 0; y < 128; y++)
		for (int x = 0; x < 128; x++)
		{
			if (!*eyes.at(x, y)) continue;
			uint8_t* d = lit.at(x, y);
			const double v = std::min(255.0, (d[0] + d[1] + d[2]) / 3.0 * 1.25);
			int k = 0;
			while (k < 3 && v > ramp[k + 1].v) k++;
			if (v > ramp[4].v) { for (int ch = 0; ch < 3; ch++) d[ch] = ramp[4].c[ch]; }
			else
			{
				const double t = (v - ramp[k].v) / (ramp[k + 1].v - ramp[k].v);
				for (int ch = 0; ch < 3; ch++) d[ch] = uint8_t(int(ramp[k].c[ch] + (ramp[k + 1].c[ch] - ramp[k].c[ch]) * t));
			}
			*glow.at(x, y) = 255;
		}
	CheatGaussian(glow, 2.0);
	for (int y = 0; y < 128; y++)
		for (int x = 0; x < 128; x++)
		{
			if (*eyes.at(x, y)) continue;
			const uint8_t g[4] = { 255, 40, 10, uint8_t(std::min(140, *glow.at(x, y) * 2)) };
			CheatOver(lit.at(x, y), g);
		}

	// the wall's own star painted out with the stone round it, a ring at a time
	std::vector<bool> star(128 * 128, false), known(128 * 128);
	for (int y = 0; y < 128; y++)
		for (int x = 0; x < 128; x++)
		{
			const uint8_t* s = src.at(x, y);
			if (!(s[0] > 1.45 * s[1] && s[0] > 1.45 * s[2] && s[0] > 30)) continue;
			for (int dy = -1; dy <= 1; dy++)
				for (int dx = -1; dx <= 1; dx++)
					if (x + dx >= 0 && x + dx < 128 && y + dy >= 0 && y + dy < 128) star[(y + dy) * 128 + x + dx] = true;
		}
	std::vector<double> fill(128 * 128 * 3);
	for (int i = 0; i < 128 * 128; i++)
	{
		known[i] = !star[i];
		for (int ch = 0; ch < 3; ch++) fill[i * 3 + ch] = lit.p[i * 4 + ch];
	}
	for (int it = 0; it < 12; it++)
	{
		std::vector<bool> nk = known;
		bool any = false;
		for (int y = 0; y < 128; y++)
			for (int x = 0; x < 128; x++)
			{
				if (known[y * 128 + x]) continue;
				any = true;
				double s[3] = { 0, 0, 0 };
				int n = 0;
				for (int yy = std::max(0, y - 1); yy < std::min(128, y + 2); yy++)
					for (int xx = std::max(0, x - 1); xx < std::min(128, x + 2); xx++)
						if (known[yy * 128 + xx])
						{
							for (int ch = 0; ch < 3; ch++) s[ch] += fill[(yy * 128 + xx) * 3 + ch];
							n++;
						}
				if (!n) continue;
				for (int ch = 0; ch < 3; ch++) fill[(y * 128 + x) * 3 + ch] = s[ch] / n;
				nk[y * 128 + x] = true;
			}
		if (!any) break;
		known = nk;
	}
	CheatPic clean = lit;
	for (int i = 0; i < 128 * 128; i++)
		for (int ch = 0; ch < 3; ch++) clean.p[i * 4 + ch] = uint8_t(std::min(255.0, std::max(0.0, fill[i * 3 + ch])));

	// DOOM's star (its five lines, as on the wall) and circle drawn again, 8
	// times larger and brought down: thin (1.1) inside the circle, bold
	// (2.0) outside, easing over 5 pixels; the circle 4.2 wide, inwards
	const int S = 8, N = 128 * S;
	CheatPic lines(N, N, 1);
	static const double TL[2] = {1,2}, TR[2] = {126,2}, LL[2] = {-2,82}, RR[2] = {129,82}, BB[2] = {64,124};
	const double* seg[5][2] = { {TL, RR}, {TR, LL}, {LL, RR}, {TL, BB}, {TR, BB} };
	for (auto& sg : seg)
		for (int i = 0; i <= 600; i++)
		{
			const double t = i / 600.0;
			const double x = sg[0][0] + (sg[1][0] - sg[0][0]) * t, y = sg[0][1] + (sg[1][1] - sg[0][1]) * t;
			const double inside = CHEAT_R - hypot(x - CHEAT_CX, y - CHEAT_CY);
			const double k = std::max(0.0, std::min(1.0, (inside + 2.5) / 5.0));
			const double rr = (2.0 + (1.1 - 2.0) * k) * S / 2;
			const double cx = x * S, cy = y * S;
			for (int py = std::max(0, int(floor(cy - rr))); py <= std::min(N - 1, int(ceil(cy + rr))); py++)
				for (int px = std::max(0, int(floor(cx - rr))); px <= std::min(N - 1, int(ceil(cx + rr))); px++)
					if ((px - cx) * (px - cx) + (py - cy) * (py - cy) <= rr * rr) *lines.at(px, py) = 255;
		}
	{
		const double x0 = (CHEAT_CX - CHEAT_R) * S, x1 = (CHEAT_CX + CHEAT_R) * S, y0 = (CHEAT_CY - CHEAT_R) * S, y1 = (CHEAT_CY + CHEAT_R) * S;
		const double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, R = (x1 - x0) / 2, w = int(CHEAT_CW * S);
		for (int py = 0; py < N; py++)
			for (int px = 0; px < N; px++)
			{
				const double d = hypot(px - cx, py - cy);
				if (d <= R && d > R - w) *lines.at(px, py) = 255;
			}
	}
	CheatPic al = CheatResize(lines, 128, 128);

	// in DOOM's red over the stone, never over the lit eyes
	on = clean;
	for (int y = 0; y < 128; y++)
		for (int x = 0; x < 128; x++)
		{
			if (*eyes.at(x, y)) continue;
			const uint8_t c[4] = { 225, 25, 15, *al.at(x, y) };
			CheatOver(on.at(x, y), c);
		}
	return true;
}

// The button: cut round to the circle (whose outer edge is the button's
// edge), a faint red glow round it while on; off grey at 60%; size pixels
// wide (the button's width / IOSTOUCH_CHEAT_ICON_FILL: the glow round it), sharpened
// a little as the mockup's (Pillow's UnsharpMask 1, 70%, 2)
static CheatPic CheatButton(const CheatPic& on, int size, bool lit)
{
	const int S = 8;
	const double R = CHEAT_R - 0.15;
	const int bx0 = int((CHEAT_CX - R) * S), by0 = int((CHEAT_CY - R) * S), bx1 = int((CHEAT_CX + R) * S), by1 = int((CHEAT_CY + R) * S);
	const int n = bx1 - bx0;
	(void)by1;
	const int big = int(n * 1.16), o0 = (big - n) / 2;
	CheatPic out(big, big, 4);
	if (lit)
	{
		const int rw = std::max(2, int(n * 0.04));
		CheatPic gl(big, big, 1);
		const double c = o0 + n / 2.0, Ro = n / 2.0;
		for (int y = 0; y < big; y++)
			for (int x = 0; x < big; x++)
			{
				const double d = hypot(x - c, y - c);
				if (d <= Ro && d > Ro - rw * 2) *gl.at(x, y) = 255;
			}
		CheatGaussian(gl, n * 0.025);
		for (size_t i = 0; i < gl.p.size(); i++)
		{
			uint8_t* d = &out.p[i * 4];
			d[0] = 255; d[1] = 40; d[2] = 10; d[3] = uint8_t(gl.p[i] * 2 / 3);
		}
	}
	const double mc = (n - 1) / 2.0;
	for (int y = 0; y < n; y++)
		for (int x = 0; x < n; x++)
		{
			if ((x - mc) * (x - mc) + (y - mc) * (y - mc) > mc * mc + mc) continue; // (Pillow's ellipse 0..n-1)
			const uint8_t* s = on.at((x + bx0) / S, (y + by0) / S);
			uint8_t px[4] = { s[0], s[1], s[2], 255 };
			if (!lit)
			{
				const uint8_t g = uint8_t((s[0] * 19595 + s[1] * 38470 + s[2] * 7471 + 0x8000) >> 16);
				px[0] = px[1] = px[2] = g;
				px[3] = 255 * 60 / 100;
			}
			CheatOver(out.at(x + o0, y + o0), px);
		}
	CheatPic res = CheatResize(out, size, size);
	// UnsharpMask(radius 1, 70%, threshold 2), every channel
	CheatPic blur = res;
	CheatGaussian(blur, 1.0);
	for (size_t i = 0; i < res.p.size(); i++)
	{
		const int diff = int(res.p[i]) - int(blur.p[i]);
		if (abs(diff) >= 2) res.p[i] = Clip8(res.p[i] + diff * 70 / 100.0);
	}
	return res;
}

// The icons, on and off, size x size (false: no MWALL4_1, or not DOOM's)
static bool MakeCheatRoundIcons(int size, TArray<uint8_t>& on, TArray<uint8_t>& off, CheatPic* pWall = nullptr)
{
	FTextureID id = TexMan.CheckForTexture("MWALL4_1", ETextureType::Wall, FTextureManager::TEXMAN_Overridable | FTextureManager::TEXMAN_TryAny);
	if (!id.isValid()) return false;
	CheatPic wall;
	if (!CheatWallIcon(TexMan.GetGameTexture(id), wall)) return false;
	if (pWall) *pWall = wall;
	CheatPic a = CheatButton(wall, size, true), b = CheatButton(wall, size, false);
	on.Resize(unsigned(a.p.size()));
	memcpy(on.Data(), a.p.data(), a.p.size());
	off.Resize(unsigned(b.p.size()));
	memcpy(off.Data(), b.p.data(), b.p.size());
	return true;
}

// CHEAT's picture: the round pentagram icon above, from the player's own
// data; in a game without that wall, the doomguy's grinning god-mode face
// from the status bar (STFGOD0; else his plain face, STFST01), made as the
// wheel's icons are; asked once
static void FeedCheatIcon()
{
	if (!IOSTouch_WantsCheatIcon()) return;
	{
		TArray<uint8_t> on, off;
		const int size = IOSTouch_CheatIconPixels();
		if (size > 8 && MakeCheatRoundIcons(size, on, off))
		{
			IOSTouch_SetCheatRoundIcons(on.Data(), off.Data(), size);
			return;
		}
	}
	static const char* const names[] = { "STFGOD0", "STFST01", "STFST00" };
	for (auto name : names)
	{
		FTextureID id = TexMan.CheckForTexture(name, ETextureType::Any);
		if (!id.isValid()) continue;
		TArray<uint8_t> px;
		int w = 0, h = 0;
		if (MakeIcon(TexMan.GetGameTexture(id), px, w, h))
		{
			IOSTouch_SetCheatIcon(px.Data(), w, h);
			return;
		}
	}
	IOSTouch_SetCheatIcon(nullptr, 0, 0); // (CHEAT says CHEAT)
}

// The cheat coming on: DOOM II's Icon of Sin spitting a cube (DSBOSPIT,
// "brain/spit"), whole, from the player's own data; in DOOM, which hasn't
// it, from a doom2.wad beside the game's own IWAD (the app's Documents) if
// there is one (read from that file, never bundled); else the game's
// power-up pickup (DSGETPOW). Off is silent.
static FSoundID cheatSpit = NO_SOUND;      // (the sound made from doom2.wad's lump)
static TArray<uint8_t> cheatSpitData;     // ...its DMX lump
static bool cheatSpitLooked = false;

// DSBOSPIT from a doom2.wad in the IWAD's folder (any letter case), read
// from the WAD's own directory
static bool ReadSpitFromDoom2(TArray<uint8_t>& data, FString& from)
{
	const int iwad = fileSystem.GetIwadNum();
	const char* full = iwad >= 0 ? fileSystem.GetResourceFileFullName(iwad) : nullptr;
	if (!full || !*full) return false;
	FString dir = full;
	const ptrdiff_t slash = dir.LastIndexOf('/');
	dir = slash >= 0 ? FString(dir.GetChars(), slash) : FString(".");
	DIR* d = opendir(dir.GetChars());
	if (!d) return false;
	FString path;
	while (dirent* e = readdir(d))
		if (!strcasecmp(e->d_name, "doom2.wad")) { path.Format("%s/%s", dir.GetChars(), e->d_name); break; }
	closedir(d);
	if (path.IsEmpty()) return false;
	FILE* f = fopen(path.GetChars(), "rb");
	if (!f) return false;
	bool ok = false;
	uint8_t head[12];
	if (fread(head, 1, 12, f) == 12 && (!memcmp(head, "IWAD", 4) || !memcmp(head, "PWAD", 4)))
	{
		const uint32_t count = head[4] | head[5] << 8 | head[6] << 16 | uint32_t(head[7]) << 24;
		const uint32_t dirAt = head[8] | head[9] << 8 | head[10] << 16 | uint32_t(head[11]) << 24;
		if (count < 100000 && fseek(f, dirAt, SEEK_SET) == 0)
		{
			TArray<uint8_t> dirData(count * 16, true);
			if (fread(dirData.Data(), 16, count, f) == count)
				for (uint32_t i = 0; i < count && !ok; i++)
				{
					const uint8_t* en = &dirData[i * 16];
					if (strncasecmp((const char*)en + 8, "DSBOSPIT", 8)) continue;
					const uint32_t pos = en[0] | en[1] << 8 | en[2] << 16 | uint32_t(en[3]) << 24;
					const uint32_t size = en[4] | en[5] << 8 | en[6] << 16 | uint32_t(en[7]) << 24;
					if (size < 8 || size > (4u << 20) || fseek(f, pos, SEEK_SET) != 0) break;
					data.Resize(size);
					ok = fread(data.Data(), 1, size, f) == size;
				}
		}
	}
	fclose(f);
	if (ok) from = path;
	return ok;
}

// The doom2.wad sound made playable: a sound of its own (its lump number
// only marks it as one that exists: the data is loaded here, as the sound
// engine loads a DMX lump; again if the engine let it go)
static FSoundID SpitFromDoom2()
{
	if (!cheatSpitLooked)
	{
		cheatSpitLooked = true;
		FString from;
		if (ReadSpitFromDoom2(cheatSpitData, from)) Printf(PRINT_NONOTIFY, "iOS: the cheat's sound: DSBOSPIT from %s\n", from.GetChars());
		else cheatSpitData.Clear();
	}
	if (cheatSpitData.Size() < 8 || !soundEngine) return NO_SOUND;
	if (cheatSpit == NO_SOUND || soundEngine->GetSfx(cheatSpit)->name != FName("ios/cheatspit"))
	{
		const int mark = fileSystem.CheckNumForName("PLAYPAL");
		if (mark < 0) return NO_SOUND;
		cheatSpit = soundEngine->AddSoundLump("ios/cheatspit", mark, 0);
	}
	sfxinfo_t* sfx = soundEngine->GetWritableSfx(cheatSpit);
	if (!sfx->data.isValid() && GSnd && !GSnd->IsNull())
	{
		uint8_t* p = cheatSpitData.Data();
		const int len = (int)cheatSpitData.Size();
		const int dmxlen = p[4] | p[5] << 8 | p[6] << 16 | p[7] << 24;
		if (p[0] == 3 && p[1] == 0 && dmxlen <= len - 8)
		{
			int rate = p[2] | p[3] << 8;
			if (rate == 0) rate = 11025;
			sfx->data = GSnd->LoadSoundRaw(p + 8, dmxlen, rate, 1, 8, -1);
		}
		else sfx->data = GSnd->LoadSound(p, len, -1, -1);
	}
	sfx->lumpnum = fileSystem.CheckNumForName("PLAYPAL");
	return cheatSpit;
}

// Which: "brain/spit" (the game's own), the doom2.wad one, or the pickup
static const char* CheatSoundName(FSoundID* pid)
{
	FSoundID id = S_FindSound("brain/spit");
	if (soundEngine && soundEngine->isValidSoundId(id)) { *pid = id; return "brain/spit (DSBOSPIT, the game's own)"; }
	id = SpitFromDoom2();
	if (id != NO_SOUND) { *pid = id; return "DSBOSPIT from doom2.wad beside the IWAD"; }
	*pid = S_FindSound("misc/p_pkup");
	return "misc/p_pkup (DSGETPOW: no DSBOSPIT)";
}

static void CheatSound()
{
	FSoundID id;
	const char* which = CheatSoundName(&id);
	Printf(PRINT_NONOTIFY, "iOS: the cheat's sound: %s\n", which);
	S_Sound(CHAN_7, CHANF_UI, id, 1.f, ATTN_NONE);
}

//---------------------------------------------------------------------------
//
// Quick save and quick load: the rotating quick saves (G_DoQuickSave: no
// menu, no question, whatever quicksaverotation and saveloadconfirmation
// say), and the newest of them
//
//---------------------------------------------------------------------------

static bool QuickSave()
{
	auto p = Player();
	if (netgame || !usergame || !p || p->health <= 0 || gamestate != GS_LEVEL)
	{
		IOSTouch_ShowMessage("Can't save now", 2.0);
		return false;
	}
	G_DoQuickSave();
	IOSTouch_ShowMessage("Quick save", 1.5);
	return true;
}

static FString NewestQuickSave()
{
	FString best;
	time_t tBest = 0;
	const int count = max((int)quicksaverotationcount, 1);
	for (int n = 0; n < count && n < 100; n++)
	{
		auto name = G_BuildSaveName(FStringf("quick%02d", n).GetChars());
		struct stat st;
		if (stat(name.GetChars(), &st) != 0) continue;
		if (best.IsEmpty() || st.st_mtime > tBest || (st.st_mtime == tBest && n == quicksavenum))
		{
			best = name;
			tBest = st.st_mtime;
		}
	}
	return best;
}

static bool QuickLoad()
{
	if (netgame) return false;
	auto name = NewestQuickSave();
	if (name.IsEmpty())
	{
		IOSTouch_ShowMessage("No quick save yet (hold SAVE)", 3.0);
		return false;
	}
	G_LoadGame(name.GetChars());
	IOSTouch_ShowMessage("Quick load", 1.5);
	return true;
}

//---------------------------------------------------------------------------
//
// Once a frame
//
//---------------------------------------------------------------------------

static void PostKey(int key)
{
	event_t ev = {};
	ev.type = EV_KeyDown;
	ev.data1 = key;
	D_PostEvent(&ev);
	ev.type = EV_KeyUp;
	D_PostEvent(&ev);
}

// JUMP and CROUCH show where the level allows them (IsJumpingAllowed:
// dmflags, then MAPINFO's nojump / nocrouch). UZDoom allows both by default,
// also in DOOM, which never had them; so in DOOM (and Chex Quest) they show
// only once turned on for good (dmflags' "Yes": sv_jump / sv_crouch 2, the
// Gameplay options' Allow Jump / Allow Crouch), and in the other games
// wherever the level allows them
static bool CanJump()
{
	if (!primaryLevel->IsJumpingAllowed()) return false;
	return !(gameinfo.gametype & GAME_DoomChex) || (dmflags & DF_YES_JUMP);
}

static bool CanCrouch()
{
	if (!primaryLevel->IsCrouchingAllowed()) return false;
	return !(gameinfo.gametype & GAME_DoomChex) || (dmflags & DF_YES_CROUCH);
}

// Levels started, restarted or loaded (G_DoLoadLevel), and the count the
// frame last saw
static unsigned int levelStarts = 0, levelStartsSeen = 0;
static unsigned int framesDrawn = 0;

void IOSGame_LevelStarted()
{
	levelStarts++;
	IOSCamera_Abort(); // (a level started or loaded: the CAMERA puts everything back first)
}

// The look, as the mouse moved: PostMouseMove's sums (UZDoom's mouse
// sensitivity and invert settings), as floats, so a slow drag isn't lost
static void FeedLook(float dx, float dy)
{
	if (dx == 0.f && dy == 0.f) return;
	event_t ev = {};
	ev.x = dx * m_sensitivity_x;
	ev.y = -dy * m_sensitivity_y;
	if (invertmousex) ev.x = -ev.x;
	if (invertmouse) ev.y = -ev.y;
	if (ev.x != 0.f || ev.y != 0.f)
	{
		ev.type = EV_Mouse;
		D_PostEvent(&ev);
	}
}

// Tilt aiming: degrees (+ left, + up) straight into the view's angles (in
// G_AddViewAngle's units, 65536 a turn; what's left of a unit kept)
static void FeedGyro(float yaw, float pitch)
{
	static double yawLeft = 0, pitchLeft = 0;
	yawLeft += -yaw * 65536.0 / 360.0; // (G_AddViewAngle: + turns right)
	pitchLeft += pitch * 65536.0 / 360.0; // (G_AddViewPitch: + looks up)
	const int iy = (int)yawLeft, ip = (int)pitchLeft;
	yawLeft -= iy;
	pitchLeft -= ip;
	if (iy) G_AddViewAngle(iy, true);
	if (ip) G_AddViewPitch(ip, true);
}

void IOSGame_Frame()
{
	framesDrawn++;
	const int mode = TouchMode();
	auto p = Player();

	// a level started, restarted or loaded
	if (levelStarts != levelStartsSeen)
	{
		levelStartsSeen = levelStarts;
		FindLevels();
		if (cheatOn) cheatGiveIn = 10; // (the cheat gives again, once the level is going)
	}

	IOSTouchWeapons wpn = {};
	IOSTouchHud hud = {};
	if (mode != IOSTOUCH_HIDDEN && p)
	{
		FillWeapons(&wpn);
		hud.bValid = 1;
		hud.bCanJump = CanJump();
		hud.bCanCrouch = CanCrouch();
		hud.bNoclip = (p->cheats & (CF_NOCLIP | CF_NOCLIP2)) != 0;
		hud.bAutomap = automapactive;
	}
	hud.bRunOn = cl_run;
	hud.bGames = IOSPicker_GameCount() > 1;
	hud.bNoSaves = netgame; // (QUICK SAVE says when it can't save; QUICK LOAD works dead too)
	hud.bCheatAllowed = CheatAllowed();
	hud.strCheatRefusal = hud.bCheatAllowed ? nullptr : CheatRefusal();
	const bool levelPick = !netgame && gamestate == GS_LEVEL && p != nullptr;
	if (levelPick && cheatLevels.Size() == 0) FindLevels();
	hud.ctLevels = levelPick ? cheatLevels.Size() : 0;
	hud.uWorld = levelStarts;
	// the HUD CLASSIC: where DOOM's status bar starts (FIRE and its arc keep
	// above it), while it shows (the screen size kept for it)
	{
		IOSTouchHudLayout hl;
		IOSTouch_GetHudLayout(&hl);
		if (hl.iStyle == IOSTOUCH_HUD_CLASSIC && StatusBar && screen && screenblocks <= 10 && screen->GetHeight() > 0)
			hud.fBarTop = float(StatusBar->GetTopOfStatusbar()) / screen->GetHeight();
	}
	IOSTouch_SetWeapons(&wpn);
	if (mode != IOSTOUCH_HIDDEN)
	{
		FeedIcons();
		FeedCheatIcon();
	}

	const int req = IOSTouch_Update(IOS_GetGameWindow(), mode, &hud, framesDrawn);

	if (req & IOSTOUCH_REQ_MENU)
	{
		if (mode == IOSTOUCH_CONSOLE) C_ToggleConsole();
		else PostKey(KEY_ESCAPE);
	}
	if (req & IOSTOUCH_REQ_CONSOLE) C_ToggleConsole();
	// GAMES: "Switch game" over the game, held still behind it (Cancel: back)
	if ((req & IOSTOUCH_REQ_GAMES) && !switchingGame)
	{
		switchingGame = true;
#ifdef DOOM_IOS
		IOSPicker_PresentSwitch(SwitchClosed);
#else
		Printf("rig: Switch game (no picker on the rig)\n");
		SwitchClosed(0);
#endif
	}
	if ((req & IOSTOUCH_REQ_QUICKSAVE) && mode == IOSTOUCH_GAMEPLAY) QuickSave();
	else if ((req & IOSTOUCH_REQ_QUICKLOAD) && mode == IOSTOUCH_GAMEPLAY) QuickLoad();
	// SAVE / LOAD tapped: the game's Save Game / Load Game menus
	if ((req & IOSTOUCH_REQ_SAVEMENU) && mode == IOSTOUCH_GAMEPLAY) C_DoCommand("menu_save");
	else if ((req & IOSTOUCH_REQ_LOADMENU) && mode == IOSTOUCH_GAMEPLAY) C_DoCommand("menu_load");
	// the tray's ALWAYS RUN: DOOM's own Always Run
	if (req & IOSTOUCH_REQ_RUN)
	{
		cl_run = !cl_run;
		IOSTouch_ShowMessage(cl_run ? "Always Run on" : "Always Run off: slide up onto the stick's ^^ to run", 2.5);
	}
	if ((req & IOSTOUCH_REQ_RESUME) && paused) C_DoCommand("pause");
	if ((req & IOSTOUCH_REQ_MAP) && mode == IOSTOUCH_GAMEPLAY) C_DoCommand("togglemap");
	// (noclip before the cheat goes off)
	if ((req & IOSTOUCH_REQ_NOCLIP) && CheatAllowed()) C_DoCommand("noclip");
	if (req & IOSTOUCH_REQ_CHEATON)
	{
		cheatOn = true;
		CheatSound();
		if (CheatAllowed())
		{
			CheatGive();
			cheatGiveIn = -1;
		}
	}
	if (req & IOSTOUCH_REQ_CHEATOFF)
	{
		cheatOn = false;
		cheatGiveIn = -1;
		if (CheatAllowed() && GodOn())
		{
			godAskedTic = -100;
			SetGod(false);
		}
	}
	if ((req & IOSTOUCH_REQ_LEVEL) && levelPick)
	{
		const int i = IOSTouch_TakeLevelPick();
		if (i >= 1 && i <= (int)cheatLevels.Size() && gameaction == ga_nothing)
		{
			// as "map <lump>" does: straight there, no intermission, as a new
			// game (without the cheat: the pistol, as the map starts; with it,
			// everything is given again)
			Printf(PRINT_NONOTIFY, "iOS: level %d picked: %s\n", i, cheatLevels[i - 1].map.GetChars());
			C_DoCommand(FStringf("map %s", cheatLevels[i - 1].map.GetChars()).GetChars());
			IOSTouch_ShowMessage(FStringf("Going to %s%s", cheatLevels[i - 1].name.GetChars(), cheatOn ? "" : ": a fresh start").GetChars(), 3.0);
		}
		else
		{
			Printf(PRINT_NONOTIFY, "iOS: level %d not picked (ga %d)\n", i, (int)gameaction);
			IOSTouch_CheatLevelFailed();
		}
	}
	CheatFrame();
	// the CAMERA: a level being played, single player, nothing over it
	IOSCamera_Frame(mode == IOSTOUCH_GAMEPLAY && !netgame && !switchingGame);

	// The look (as the mouse: dropped by the game while it is paused or the
	// menu or console is up) and tilt aiming, taken every frame so nothing
	// piles up to spin the view later
	float dx, dy, yaw, pitch;
	IOSTouch_TakeLook(&dx, &dy);
	IOSTouch_TakeGyro(&yaw, &pitch);
	if (mode == IOSTOUCH_GAMEPLAY && !paused && !IOSGame_HoldsGame())
	{
		FeedLook(dx, dy);
		FeedGyro(yaw, pitch);
	}
}

//---------------------------------------------------------------------------
//
// Once a tic, in G_BuildTiccmd
//
//---------------------------------------------------------------------------

static unsigned int ulLastButtons = 0;
static int iLastPick = -1;

int IOSGame_Ticcmd(float* pfAxisForward, float* pfAxisSide, uint32_t* pulButtons)
{
	IOSTouchInput in;
	IOSTouch_ReadInput(&in);
	// the stick, as a joystick's (forward +, the side axis + to the left)
	*pfAxisForward = clamp(*pfAxisForward + in.fMoveY, -1.f, 1.f);
	*pfAxisSide = clamp(*pfAxisSide - in.fMoveX, -1.f, 1.f);
	// how fast the stick goes: running on the run marker or with Always Run
	// (cl_run), else walking (whatever cl_analog_run says for a pad's stick)
	int run = -1;
	if (in.fMoveX != 0.f || in.fMoveY != 0.f) run = (in.bRun || cl_run) ? 1 : 0;
	const unsigned b = in.ulButtons;
	if (b & IOSTOUCH_FIRE) *pulButtons |= BT_ATTACK;
	if (b & IOSTOUCH_USE) *pulButtons |= BT_USE;
	if (b & IOSTOUCH_JUMP) *pulButtons |= BT_JUMP;
	if (b & IOSTOUCH_CROUCH) *pulButtons |= BT_CROUCH;
	// NEXT WPN / PREV WPN: once per press (a quick tap is reported for two
	// tics), as weapnext / weapprev
	const unsigned down = b & ~ulLastButtons;
	ulLastButtons = b;
	if (down & IOSTOUCH_NEXTWEAPON) SendWeaponSlot = WST_NEXT;
	else if (down & IOSTOUCH_PREVWEAPON) SendWeaponSlot = WST_PREV;
	// the wheel's pick: once (it is reported for two tics)
	if (in.iSelectWeapon >= 0 && in.iSelectWeapon != iLastPick) PickWeapon(in.iSelectWeapon);
	iLastPick = in.iSelectWeapon;
	return run;
}

bool IOSGame_HoldsGame()
{
	return IOSTouch_HoldsGame() != 0 || IOSCamera_HoldsGame() || switchingGame;
}

// The settings written now: "Switch game" closes the app (ios/IOSPicker.mm)
extern "C" void IOSGame_WriteConfig(void)
{
	M_SaveDefaults(nullptr);
}

// The picture's place in the window (IOSPhotos: the buttons drawn over the
// HUD photo): the whole screen, in its pixels (x, y, w, h, and the window's
// width and height)
extern "C" int IOSGame_LastPictureRect(int r[6])
{
	if (!screen) return 0;
	r[0] = r[1] = 0;
	r[2] = r[4] = screen->GetWidth();
	r[3] = r[5] = screen->GetHeight();
	return 1;
}

// The HUD: SPLIT and STRIP draw their pills as the full-screen HUD (the
// Doom status bar's ZScript), NONE nothing, CLASSIC as usual
int IOSGame_HudState()
{
	IOSTouchHudLayout hl;
	IOSTouch_GetHudLayout(&hl);
	if (hl.iStyle == IOSTOUCH_HUD_SPLIT || hl.iStyle == IOSTOUCH_HUD_STRIP) return HUD_Fullscreen;
	if (hl.iStyle == IOSTOUCH_HUD_NONE) return HUD_None;
	return -1;
}

bool IOSGame_HudFills()
{
	return IOSGame_HudState() >= 0;
}

double IOSGame_HudValue(int which, double w, double h)
{
	IOSTouchHudLayout hl;
	IOSTouch_GetHudLayout(&hl);
	switch (which)
	{
	case 0: return hl.iStyle;
	case 1: return hl.bFace;
	case 2: return hl.fPillH * h;
	case 3: return hl.fPillBottom * h;
	case 4: return hl.fLeft * w;
	case 5: return hl.fRight * w;
	case 6: return hl.fStripHalf * w;
	}
	return 0;
}

void IOSGame_HudFrame(double w, double h, double* pFrame)
{
	float f[4];
	IOSTouch_GetHudFrame(f);
	pFrame[0] = f[0] * w;
	pFrame[1] = f[1] * h;
	pFrame[2] = f[2] * w;
	pFrame[3] = f[3] * h;
}

#ifndef DOOM_IOS
// (the rig has no picker: GAMES shows, as with two games found, unless
// IOSRIG_GAMES says how many)
extern "C" int IOSPicker_GameCount(void)
{
	const char* n = getenv("IOSRIG_GAMES");
	return n ? atoi(n) : 2;
}

// The CAMERA's commands: ios_cam photo | selfie | burst | freeze | cinema |
// end | orbit y p | pan r d | zoom f | kinds n | selfies n | hash | status |
// gather n
CCMD(ios_cam)
{
	FString cmd;
	for (int i = 1; i < argv.argc(); i++)
	{
		if (i > 1) cmd += ' ';
		cmd += argv[i];
	}
	IOSCamera_RigCommand(cmd.GetChars());
}

//---------------------------------------------------------------------------
//
// The Linux test rig's own (DOOM_TOUCH_RIG_SOURCE, ios/rig/IOSTouchRig.c):
// its touches from a console command, and the player's state for its log
//
//---------------------------------------------------------------------------

extern "C" void IOSTouchRig_Command(const char* cmd);

// ios_rig <the rig's command>: a touch, a look at the overlay's state...
CCMD(ios_rig)
{
	FString cmd;
	for (int i = 1; i < argv.argc(); i++)
	{
		if (i > 1) cmd += ' ';
		cmd += argv[i];
	}
	IOSTouchRig_Command(cmd.GetChars());
}

// The app going to the background and back, as iOS's app state does it
// (sdlglvideo.cpp's IOS_SetAppActive, iOS only): ios_rigactive 0 / 1
void S_SetSoundPaused(int state);
CCMD(ios_rigactive)
{
	extern bool AppActive;
	const bool active = argv.argc() < 2 || atoi(argv[1]) != 0;
	Printf("rig: app %s\n", active ? "in front" : "in the background");
	S_SetSoundPaused(active ? 1 : 0);
	AppActive = active;
	if (!active) IOSTouch_Hide();
}

// The player as the rig's checks need it
CCMD(ios_rigpos)
{
	auto p = Player();
	if (!p) { Printf("rigpos: no player\n"); return; }
	auto mo = p->mo;
	AActor* armor = mo->FindInventory(NAME_BasicArmor);
	AActor* ammo = p->ReadyWeapon ? p->ReadyWeapon->PointerVar<AActor>(NAME_Ammo1) : nullptr;
	// the ceilings d, d + 8 and d + 16 units ahead (ios_rigpos [d]; 32 if
	// not given: a door opening there rises)
	double ceil[3];
	const double d = argv.argc() > 1 ? atof(argv[1]) : 32.;
	for (int k = 0; k < 3; k++)
	{
		const DVector2 ahead = mo->Pos().XY() + mo->Angles.Yaw.ToVector(d + 8. * k);
		ceil[k] = primaryLevel->PointInSector(ahead)->ceilingplane.ZatPoint(ahead);
	}
	int weapons = 0;
	for (int i = 0; i < wheelCount; i++) if (mo->FindInventory(wheelClasses[i])) weapons++;
	int keys = 0;
	for (AActor* item = mo->Inventory; item; item = item->Inventory) if (item->IsKindOf(NAME_Key)) keys++;
	AActor* backpack = mo->FindInventory(PClass::FindActor("BackpackItem"), true);
	Printf("rigpos tic %d maptime %d gs %d mode %d menu %d con %d paused %d held %d map %s pos %.1f %.1f %.1f yaw %.3f pitch %.3f "
		"health %d armor %d god %d noclip %d weapon %s ammo %d weapons %d/%d keys %d backpack %d automap %d aheadceil %.1f %.1f %.1f\n",
		gametic, primaryLevel->maptime, (int)gamestate, TouchMode(), (int)menuactive, (int)ConsoleState, paused, (int)IOSTouch_HoldsGame(),
		primaryLevel->MapName.GetChars(), mo->X(), mo->Y(), mo->Z(), mo->Angles.Yaw.Degrees(), mo->Angles.Pitch.Degrees(),
		p->health, armor ? armor->IntVar(NAME_Amount) : 0, (int)GodOn(), (int)((p->cheats & (CF_NOCLIP | CF_NOCLIP2)) != 0),
		p->ReadyWeapon ? p->ReadyWeapon->GetClass()->TypeName.GetChars() : "-", ammo ? ammo->IntVar(NAME_Amount) : -1,
		weapons, wheelCount, keys, backpack ? 1 : 0, (int)(automapactive != 0), ceil[0], ceil[1], ceil[2]);
}

// The wheel's list as the overlay gets it
CCMD(ios_rigweapons)
{
	IOSTouchWeapons w = {};
	FillWeapons(&w);
	Printf("rigweapons valid %d select %d count %d wanted %d infinite %d\n", w.bValid, w.bCanSelect, w.ctWeapons, w.iWanted, w.bInfiniteAmmo);
	for (int i = 0; i < w.ctWeapons; i++)
	{
		auto tex = WeaponTexture(wheelClasses[i]);
		Printf("rigweapon %d %s \"%s\" colour %d owned %d ready %d ammo %d/%d icon %s\n", i, wheelClasses[i]->TypeName.GetChars(), w.astrName[i],
			w.aiColour[i], (w.ulOwned >> i) & 1, (w.ulReady >> i) & 1, w.aiAmmo[i], w.aiMaxAmmo[i], tex ? tex->GetName().GetChars() : "none");
	}
}

// The cheat's list of levels
CCMD(ios_riglevels)
{
	if (cheatLevels.Size() == 0) FindLevels();
	for (unsigned i = 0; i < cheatLevels.Size(); i++) Printf("riglevel %u %s | %s\n", i + 1, cheatLevels[i].name.GetChars(), cheatLevels[i].map.GetChars());
	Printf("riglevels %u, current %d\n", cheatLevels.Size(), IOSGame_CurrentLevel());
}

// CHEAT's round icon (on, off) at a size (default: the overlay's) as raw
// files (4-byte width, height, then RGBA) in argv[1], and the 128 x 128
// picture it is cut from; and which sound coming on plays
CCMD(ios_rigcheaticon)
{
	if (argv.argc() < 2) return;
	const int size = argv.argc() >= 3 ? atoi(argv[2]) : IOSTouch_CheatIconPixels();
	TArray<uint8_t> on, off;
	CheatPic wall;
	const uint64_t t0 = I_msTime();
	const bool made = MakeCheatRoundIcons(size, on, off, &wall);
	Printf("rigcheaticon: made in %d ms\n", int(I_msTime() - t0));
	if (!made)
	{
		Printf("rigcheaticon: no MWALL4_1 (the face's fallback)\n");
		return;
	}
	auto put = [&](const char* name, const uint8_t* px, int w, int h) {
		FILE* f = fopen(FStringf("%s/%s.rgba", argv[1], name).GetChars(), "wb");
		if (!f) return;
		int32_t wh[2] = { w, h };
		fwrite(wh, 4, 2, f);
		fwrite(px, 1, size_t(w) * h * 4, f);
		fclose(f);
	};
	put(FStringf("cheat-on-%d", size).GetChars(), on.Data(), size, size);
	put(FStringf("cheat-off-%d", size).GetChars(), off.Data(), size, size);
	put("cheat-wall-128", wall.p.data(), 128, 128);
	Printf("rigcheaticon: %d px written\n", size);
}

CCMD(ios_rigcheatsound)
{
	FSoundID id;
	Printf("rigcheatsound: %s\n", CheatSoundName(&id));
	if (soundEngine && id != NO_SOUND)
		Printf("rigcheatsound: sound %d loaded %d (sound system %s)\n", id.index(), (int)soundEngine->GetSfx(id)->data.isValid(), GSnd && !GSnd->IsNull() ? "on" : "off");
}

// The wheel's pictures as raw files (4-byte width, height, then RGBA) in argv[1]
CCMD(ios_rigicons)
{
	if (argv.argc() < 2) return;
	for (int i = 0; i < wheelCount; i++)
	{
		TArray<uint8_t> px;
		int w = 0, h = 0;
		bool hand = false;
		FGameTexture* tex = WeaponTexture(wheelClasses[i], &hand);
		if (!MakeIcon(tex, px, w, h, hand)) continue;
		FILE* f = fopen(FStringf("%s/w%02d.rgba", argv[1], i).GetChars(), "wb");
		if (!f) continue;
		int32_t wh[2] = { w, h };
		fwrite(wh, 4, 2, f);
		fwrite(px.Data(), 1, px.Size(), f);
		fclose(f);
	}
}
#endif
