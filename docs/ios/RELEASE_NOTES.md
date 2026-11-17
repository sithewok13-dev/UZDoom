<!--
  The GitHub release text for an ios-v* release, made by a manual run of
  .github/workflows/ios.yml on branch ios with release=<tag>
  (gh workflow run ios.yml --ref ios -f release=ios-v0.1). The workflow
  publishes this file as the release body, as it is in the commit it builds,
  with DOOM.ipa attached. Update it before each release run: the run fails
  before building unless this file has a "### What's in <tag>" heading line
  for that tag (for example "### What's in ios-v0.2"), written exactly like
  that and on a line of its own. A tag without a '-' (ios-v0.2) is marked as
  the latest release; one with a '-' (ios-v0.3-beta1) becomes a prerelease.
  Links must be absolute: relative links don't resolve on a release page.
  Keep each paragraph and list item on one line: a release page turns every
  line break into a visible one.
-->

**DOOM (UZDoom) for iOS v0.1**: an unofficial iPhone and iPad build of [UZDoom](https://github.com/UZDoom/UZDoom), the open-source DOOM engine that carries on ZDoom and GZDoom, with on-screen touch controls. It plays The Ultimate DOOM, DOOM II, Final DOOM (TNT: Evilution and The Plutonia Experiment) and their add-ons.

**You need your own copy of DOOM; no game files are included.** The **DOOM + DOOM II** re-release ([Steam](https://store.steampowered.com/app/2280/DOOM__DOOM_II/) or [GOG](https://www.gog.com/game/doom_doom_ii)) has every game the app plays. The classic .wad files from older editions work too.

**One app, DOOM** (`DOOM.ipa`): when it opens, it shows a card for each game it finds in your files. Hold a card for its add-ons.

**New here?** *Known limitations*, *Requirements* and *Install* are near the bottom of this page, just above the download.

### What's in ios-v0.1

The first numbered release. What you get:

- **A game picker.** A card each for DOOM, DOOM II, TNT and PLUTONIA, from the files you copied. Hold a card for its add-ons: **SIGIL**, **SIGIL II**, **No Rest for the Living**, **The Master Levels** and **Legacy of Rust**. Each add-on keeps its own saves.
- **Touch controls made for DOOM.** A move stick wherever your left thumb lands, drag anywhere else to look, FIRE with USE, JUMP and CROUCH on an arc around it, and MAP for the automap.
- **A weapon wheel.** Slide off NEXT WPN or PREV WPN and every weapon you carry fans out, drawn with the game's own pickup pictures and ammo counts. The game holds still while it's open.
- **Your choice of HUD.** Six looks: two new ones with DOOM's own face and red numbers in small dark pills (SPLIT and STRIP), DOOM's classic status bar, two cut-down versions of it, or nothing at all. Three sizes, and a slider for how see-through it is.
- **A camera.** Take photos, bursts and selfies: the doomguy poses with up to 11 of the nearest monsters, in eight arrangements. Action Freeze stops time so you can fly the camera round a fight, and a cinematic burst takes five shots. Photos go straight to your Photos app. The game is put back exactly as it was afterwards.
- **The sithewok cheat.** Its button is a demon's face in a pentagram, made from the game's own wall. It turns on god mode with every weapon, keeps your ammo full and gives you NOCLIP. It comes on with the Icon of Sin's spit from DOOM II.
- **LEVEL.** Jump to any level of the game or add-on from a list.
- **Tilt aiming (GYRO)**, **ALWAYS RUN**, look speed (**LOOK**), and **60 or 120 frames a second** on screens that can show it.
- **LAN multiplayer**, new and barely tested: co-op or deathmatch for 2 to 8 players on the same Wi-Fi (see *Known limitations*).
- **Vulkan graphics** on Apple's Metal, through MoltenVK.

---

### How to play: every control explained

```
 NEXT  PREV  MAP       CAMERA  CHEAT  SAVE  LOAD  MENU
 WPN   WPN
 (pickup and key messages)

                                               JUMP
   left thumb: the move stick           USE        FIRE
   anywhere else: drag to look                CROUCH
 [face | health | armour]          [ammo | keys]
```

#### Moving and looking

- **Move:** put your left thumb down anywhere on the left side of the screen (about the left 40%). The stick appears where it lands.
- **Run:** slide your thumb up onto the small ^^ circle above the stick to run straight ahead. Or switch on ALWAYS RUN in the tray (below) and the stick always runs.
- **Look:** drag anywhere else on the screen. Dragging on FIRE and the buttons around it looks too, so you can shoot and turn with one thumb.

#### Buttons

- **FIRE** (bottom right): hold to shoot.
- **USE** (left of FIRE): open doors, press switches.
- **JUMP** and **CROUCH**: only shown where the level allows them. Classic DOOM doesn't jump or crouch, so they stay hidden there unless you turn them on in the game's options.
- **MAP** (top left): the automap. Tap again to close it.
- **SAVE / LOAD** (top right): a tap opens DOOM's Save Game or Load Game menu. Hold for 0.3 s (a ring fills) to quick save or quick load. Slide off to cancel.
- **MENU:** a tap opens DOOM's own menu. Hold it 0.45 s for the tray (below).

#### The weapon wheel (NEXT WPN and PREV WPN)

- A tap picks the next or previous weapon.
- Slide 10 pt off either button and the wheel opens at once: slide onto a weapon and let go. Or hold 0.3 s and it opens for you to tap.
- One slice per weapon you have, with the game's own pickup picture (the fist as a clenched fist) and its ammo. The gap at the bottom cancels. A small buzz for each slice.
- In single player, the game holds still while the wheel is open.

#### Quick save and quick load: why the short hold

A plain tap on SAVE or LOAD opens the full menu, so a stray tap never throws away your progress. The 0.3 s hold with its ring is the fast way.

#### MENU and its hidden tray

Hold MENU for 0.45 s. A row of buttons opens:

- **LOOK:** how fast a drag turns you ("LOOK 1x"). Tap to step through 0.5x to 3x; hold to go back to 1x.
- **ALWAYS RUN:** on, the stick always runs; off, slide onto the ^^ circle to run.
- **SENS** and **GYRO:** tilt aiming (below).
- **HUD:** opens a panel. LOOK picks the HUD's look (SPLIT, STRIP, CLASSIC SPLIT, CLASSIC STRIP, CLASSIC or NONE). SIZE: small, medium or large. FACE: the doomguy's face on or off (SPLIT and STRIP). HUD and BUTTONS: sliders for how solid the HUD and the touch buttons are. Tap a row's name to reset that row; hold HUD for 0.45 s to reset everything.
- **FPS:** tap to show or hide the frame rate; hold for 0.45 s to switch between 60 and 120 frames a second.
- **CAMERA** (camera symbol): shows or hides the CAMERA button. Hold it for the camera's options.
- **CHEAT:** shows the CHEAT button and turns the cheat on (below).
- **LEVEL:** a list of the game's levels; tap one to go there. Single player, or the host of a network game.
- **NOCLIP:** walk through walls, while the cheat is on.
- **GAMES:** when you have more than one game or add-on. Pick another one: the app closes, and the next time you open it (within 10 minutes) it goes straight there. Allow notifications and a "Tap to start" message brings you back.
- **Keyboard:** opens the game's console, with the iPhone's keyboard.
- The build number is under the tray (for example "build 37 · Nov 16").

Settings like LOOK, GYRO, HUD and FPS are remembered.

#### Tilt aiming (gyro)

- **GYRO** in the tray: OFF (the default), TOUCH (aims by tilting while either thumb is on the screen; lift both and the view stays put) or ALWAYS.
- **SENS:** how strongly the tilt turns you: 1.0, 1.5 (the default), 2.0 or 3.0.

#### The camera: photos, selfies, bursts and Action Freeze

The CAMERA button (top, in the middle) does it all:

- **Tap:** a photo.
- **Hold 0.5 s:** a selfie. The doomguy stands in the best spot with the nearest monsters (up to 11) brought in around him, standing or walking, facing the lens, in one of eight arrangements (huddle, peek, rows, corner, tiny, totem, pyramid, lift).
- **Slide sideways:** a burst of 10 photos, 0.2 s apart.
- **Slide down:** Action Freeze. Time stops: one finger flies the camera round, two fingers move it and pinch to zoom (up to 8x). The shutter takes clean photos; X goes back.
- **Slide up:** a cinematic burst of 5 shots.
- Hold the tray's CAMERA for its options: what a photo shows (just the game, with the HUD, or with the HUD and buttons), 1, 3 or 5 selfies, and Action Freeze on or off.
- Photos go to your Photos app (it only asks to add photos, never to read them). If that's not allowed, they go to Files > On My iPhone > DOOM > Screenshots.
- A selfie, burst or freeze changes nothing in the game: every monster goes back exactly where it was. Selfies and Action Freeze are single player only.

#### Network games (LAN)

- On the game picker, tap **LAN multiplayer**, then **Host a LAN game** or **Join a LAN game**. iOS asks to find devices on your local network: say yes.
- **Host:** pick the game, then Co-op, Deathmatch or Deathmatch with items coming back, how many players (2 to 8, you too) and the skill. The game starts when everyone is in, or when you tap Start now.
- **Join:** the app looks for games on your Wi-Fi and lists them; tap one and that game opens. Or type the address the host's screen shows.
- Everyone needs the same game files.
- In deathmatch there are no monsters and the exits do nothing. The host changes the level with LEVEL in the tray; everyone goes, and each player taps the score screen to go on.
- In a network game nothing holds the game still, and the camera's selfies and Action Freeze, the cheat and LEVEL (except for the host) are off.

#### The cheat (sithewok)

- Tap **CHEAT** in the tray: the CHEAT button appears and the cheat comes on, with the Icon of Sin's spit (in DOOM only if `doom2.wad` is there too; otherwise the power-up sound).
- Hold the CHEAT button 0.5 s to switch it off (grey) or on (gold). A plain tap does nothing, so you can't hit it by mistake.
- Typing `sithewok` in the console also switches it.
- While it's on (single player): god mode and every weapon, again at each new level, and your ammo kept full. NOCLIP shows in the tray.
- The cheat starts off each time you open the app; whether its button shows is remembered.

#### Menus, leaving the app, and the console

- In DOOM's menus and between levels, the touch buttons step aside: tap the menu items themselves. A BACK button takes you up a menu.
- The console (the keyboard button in the tray) takes UZDoom's commands and the classic cheats, for example `god`, `give all` or `map e1m3`.
- To leave, swipe up to the home screen as with any app. Save first.
- Everything the game prints goes to Files > On My iPhone > DOOM > `doom-log.txt`, new each time the app opens. It helps with bug reports.

---

### Known limitations

**Sorry about the bugs.** This is an unofficial beta, and you will find bugs. Thank you for trying it anyway, and please tell us what goes wrong (see *Found a bug?* below).

- **Multiplayer may well be broken.** LAN games are brand new in this release. They have only been tried on a computer, with two copies of the game talking to each other on one machine. They have never been tried between two iPhones or iPads, so they may not find each other, may fall out of step, or may not work at all. Sorry: it's being worked on. There is no internet play.
- **Tested on one iPhone only.** Most of the app has been played on the developer's own iPhone in test builds, and the rest on a computer with the real game files and simulated touches. iPads and other iPhone models have never been tried.
- **Game controllers, keyboards and mice** have not been tried.
- **No app icon yet**: the app shows a plain icon.
- UZDoom starts one game per run, so switching games with GAMES closes the app and you open it again.

### Requirements

- An iPhone or iPad with **iOS 15 or newer**.
- Your own DOOM game files (below).
- A way to sideload apps: **SideStore**, **AltStore** (Classic), **Feather**, **Sideloadly** or a signing service. With a free Apple ID, SideStore and AltStore must re-sign the app every 7 days, and you can have up to 3 sideloaded apps at once.

### Install

1. **Get the app.** Easiest: on your iPhone, open https://sithewok13-dev.github.io/doom/ and tap **Add to SideStore** or **Add to AltStore**, then install DOOM from there; updates come the same way. Or download `DOOM.ipa` from this page and sign it with your tool.
2. **Open DOOM once.** With no game files yet it says "No game files found." and tells you where they go. That's expected: tap Exit.
3. **Find your game files** on your computer: in the DOOM + DOOM II install folder (on Steam, the `rerelease` folder inside it). You want `doom.wad` (DOOM), `doom2.wad` (DOOM II), `tnt.wad` and `plutonia.wad` (Final DOOM), and any add-ons: `sigil.wad`, `sigil2.wad`, `nerve.wad` (No Rest for the Living), `masterlevels.wad`, and `id1.wad` with `id1-res.wad` (Legacy of Rust).
4. **Copy them into Files > On My iPhone > DOOM**, straight into that folder, not into a folder inside it. AirDrop to Files, a USB cable with Finder or iTunes (File Sharing, DOOM), or iCloud Drive then Files all work.
5. **Open DOOM again** and tap a game's card.

Updating the app keeps your files and saves if you use the same tool and the same Apple ID. Your saves are in Files > On My iPhone > DOOM > UZDoom > Savegames. Copy that folder somewhere safe before you delete or reinstall the app: deleting the app deletes them.

**Found a bug?** Please report it here, not to the upstream UZDoom project, and say you're on **ios-v0.1** (and the build number under the MENU tray): https://github.com/sithewok13-dev/UZDoom/issues

---

**Thank you.** This app is only possible because of the people who made DOOM and kept it alive:

- **id Software**, for DOOM and DOOM II, and for releasing DOOM's source code in 1997.
- **Marisa "Randi" Heit**, who created ZDoom; **Christoph "Graf Zahl" Oelckers**, who created GZDoom; **Rachael "Eruanna" Alexanderson** and everyone who has looked after GZDoom and now **UZDoom**; and all the contributors listed in [CONTRIBUTORS](https://github.com/UZDoom/UZDoom/blob/trunk/CONTRIBUTORS). UZDoom: https://github.com/UZDoom/UZDoom
- **Sam Lantinga and the SDL contributors** ([SDL](https://www.libsdl.org/)), the **Khronos Group** and the MoltenVK contributors ([MoltenVK](https://github.com/KhronosGroup/MoltenVK)), **Chris Robinson** ([OpenAL Soft](https://openal-soft.org/)), and the authors of FluidSynth, libsndfile, Ogg, Vorbis, FLAC, Opus and libvpx.
- **Nightdive Studios** and **id Software** for the DOOM + DOOM II re-release, and **John Romero** for SIGIL and SIGIL II.

**Licences:** UZDoom and this port are under the [GNU GPL version 3 or later](https://www.gnu.org/licenses/gpl-3.0.html); the source is at https://github.com/sithewok13-dev/UZDoom (branch `ios`). SDL2: zlib licence. MoltenVK: Apache 2.0. OpenAL Soft, FluidSynth and libsndfile: GNU LGPL. Ogg, Vorbis, FLAC, Opus and libvpx: BSD-style licences. The UZDoom icon shown in the SideStore / AltStore source is by Carlos "Cardboard Marty" Sanchez, © the UZDoom Team, [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/).

Not affiliated with or endorsed by id Software, Bethesda, ZeniMax, Microsoft, Nightdive Studios or the UZDoom team. DOOM is a trademark of id Software.
