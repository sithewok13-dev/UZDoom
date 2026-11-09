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

#include "IOSGame.h"

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

EXTERN_CVAR(Float, m_sensitivity_x)
EXTERN_CVAR(Float, m_sensitivity_y)
EXTERN_CVAR(Bool, invertmouse)
EXTERN_CVAR(Bool, invertmousex)
EXTERN_CVAR(Int, quicksaverotationcount)
EXTERN_CVAR(Int, quicksavenum)

extern const AActor* SendItemUse; // g_game.cpp: the item to use in the next ticcmd ("use <class>")
extern int chatmodeon;

// What the overlay shows: the game being played (no menu, no console, no
// typing a message), the console over it, the game paused with the pause
// key, or nothing (menus, intermissions, the finale, the title map, demos,
// loading)
static int TouchMode()
{
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

static void CheatFrame()
{
	if (!cheatOn || !CheatAllowed()) return;
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
		Printf("sithewok: %s\n", IOSTouch_CheatOn() ? "on (god mode, everything given; LEVEL and NOCLIP in MENU's tray)" :
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

static FGameTexture* WeaponTexture(PClassActor* cls)
{
	auto def = GetDefaultByType(cls);
	if (!def) return nullptr;
	FGameTexture* tex = StateTexture(def->SpawnState);
	if (!tex)
	{
		FTextureID icon = def->TextureIDVar(NAME_Icon);
		if (icon.isValid()) tex = TexMan.GetGameTexture(icon);
	}
	if (!tex) tex = StateTexture(def->FindState(NAME_Ready));
	if (tex && tex->isValid() && tex->GetTexture() && tex->GetTexelWidth() > 0 && tex->GetTexelHeight() > 0) return tex;
	return nullptr;
}

static bool MakeIcon(FGameTexture* tex, TArray<uint8_t>& out, int& ow, int& oh)
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
		if (i < wheelCount && MakeIcon(WeaponTexture(wheelClasses[i]), px, w, h)) IOSTouch_SetWeaponIcon(i, px.Data(), w, h);
		else IOSTouch_SetWeaponIcon(i, nullptr, 0, 0); // (its name shows)
	}
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
		IOSTouch_ShowMessage("No quick save yet (hold QUICK SAVE)", 3.0);
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
	}
	hud.bNoSaves = netgame; // (QUICK SAVE says when it can't save; QUICK LOAD works dead too)
	hud.bCheatAllowed = CheatAllowed();
	hud.strCheatRefusal = hud.bCheatAllowed ? nullptr : CheatRefusal();
	const bool levelPick = !netgame && gamestate == GS_LEVEL && p != nullptr;
	if (levelPick && cheatLevels.Size() == 0) FindLevels();
	hud.ctLevels = levelPick ? cheatLevels.Size() : 0;
	hud.uWorld = levelStarts;
	IOSTouch_SetWeapons(&wpn);
	if (mode != IOSTOUCH_HIDDEN) FeedIcons();

	const int req = IOSTouch_Update(IOS_GetGameWindow(), mode, &hud, framesDrawn);

	if (req & IOSTOUCH_REQ_MENU)
	{
		if (mode == IOSTOUCH_CONSOLE) C_ToggleConsole();
		else PostKey(KEY_ESCAPE);
	}
	if (req & IOSTOUCH_REQ_CONSOLE) C_ToggleConsole();
	if ((req & IOSTOUCH_REQ_QUICKSAVE) && mode == IOSTOUCH_GAMEPLAY) QuickSave();
	else if ((req & IOSTOUCH_REQ_QUICKLOAD) && mode == IOSTOUCH_GAMEPLAY) QuickLoad();
	if ((req & IOSTOUCH_REQ_RESUME) && paused) C_DoCommand("pause");
	if ((req & IOSTOUCH_REQ_MAP) && mode == IOSTOUCH_GAMEPLAY) C_DoCommand("togglemap");
	// (noclip before the cheat goes off)
	if ((req & IOSTOUCH_REQ_NOCLIP) && CheatAllowed()) C_DoCommand("noclip");
	if (req & IOSTOUCH_REQ_CHEATON)
	{
		cheatOn = true;
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
			// as "map <lump>" does: straight there, no intermission
			Printf(PRINT_NONOTIFY, "iOS: level %d picked: %s\n", i, cheatLevels[i - 1].map.GetChars());
			C_DoCommand(FStringf("map %s", cheatLevels[i - 1].map.GetChars()).GetChars());
		}
		else
		{
			Printf(PRINT_NONOTIFY, "iOS: level %d not picked (ga %d)\n", i, (int)gameaction);
			IOSTouch_CheatLevelFailed();
		}
	}
	CheatFrame();

	// The look (as the mouse: dropped by the game while it is paused or the
	// menu or console is up) and tilt aiming, taken every frame so nothing
	// piles up to spin the view later
	float dx, dy, yaw, pitch;
	IOSTouch_TakeLook(&dx, &dy);
	IOSTouch_TakeGyro(&yaw, &pitch);
	if (mode == IOSTOUCH_GAMEPLAY && !paused && !IOSTouch_HoldsGame())
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

void IOSGame_Ticcmd(float* pfAxisForward, float* pfAxisSide, uint32_t* pulButtons)
{
	IOSTouchInput in;
	IOSTouch_ReadInput(&in);
	// the stick, as a joystick's (forward +, the side axis + to the left)
	*pfAxisForward = clamp(*pfAxisForward + in.fMoveY, -1.f, 1.f);
	*pfAxisSide = clamp(*pfAxisSide - in.fMoveX, -1.f, 1.f);
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
}

bool IOSGame_HoldsGame()
{
	return IOSTouch_HoldsGame() != 0;
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

// The wheel's pictures as raw files (4-byte width, height, then RGBA) in argv[1]
CCMD(ios_rigicons)
{
	if (argv.argc() < 2) return;
	for (int i = 0; i < wheelCount; i++)
	{
		TArray<uint8_t> px;
		int w = 0, h = 0;
		if (!MakeIcon(WeaponTexture(wheelClasses[i]), px, w, h)) continue;
		FILE* f = fopen(FStringf("%s/w%02d.rgba", argv[1], i).GetChars(), "wb");
		if (!f) continue;
		int32_t wh[2] = { w, h };
		fwrite(wh, 4, 2, f);
		fwrite(px.Data(), 1, px.Size(), f);
		fclose(f);
	}
}
#endif
