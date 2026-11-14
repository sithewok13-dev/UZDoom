# DOOM for iOS (UZDoom): notes for Claude Code sessions

An unofficial iPhone/iPad build of UZDoom (the GZDoom family) playing DOOM,
DOOM II, Final DOOM (TNT, Plutonia) and the re-release's add-ons, app name
**DOOM**, bundle id `io.github.sithewok13.doom`. The owner plays it on an
iPhone, isn't a programmer, and reads everything on a phone. Their DOOM
data (the KEX re-release's and the classic WADs) is private: release
`doom-data` (`doom-data.zip`) of `sithewok13-dev/jk-files`, fetched with
`gh api` (REST, `.../releases/assets/<id>` with `Accept:
application/octet-stream`) into a folder outside the repo, for the rig only.

- **Build branch:** `ios`. `master` is left as upstream (UZDoom).
- **Renderer:** Vulkan on Metal (MoltenVK, bundled), OpenGL ES as the
  fallback; drawn at two thirds of the screen's pixels (`vid_scalefactor`
  0.67); vsync on. SDL2 backend, no ZWidget windows on iOS (`d_main.cpp`).
- **Game picker** (`ios/IOSPicker.mm`): a card for each main game found in
  Files > On My iPhone > DOOM; held 0.5 s, its add-ons (SIGIL, NRFTL...,
  loaded with `-file`; an add-on keeps its saves in a folder of its own).
  It always shows at start. **GAMES** in MENU's tray (more than one game or
  add-on found) puts the same cards over the game with Cancel; a pick can't
  start there (UZDoom starts one game a run), so the pick is kept
  (NSUserDefaults), the settings written (`IOSGame_WriteConfig`), a "Tap to
  start <game>" notification asked for and the app closes; the next start
  within 10 minutes goes straight to it (without notifications, an alert
  says to open DOOM again). From the Raze port.
- **Touch controls** (`ios/IOSTouch.m`/`.h`: C core + UIKit view, from the
  Raze port's; brought up to the Wolf3D port's, whose overlay came from this
  one; `ios/IOSGame.cpp` is the game side). Modes: GAMEPLAY, CONSOLE (only
  MENU and its tray), PAUSED (RESUME), HIDDEN (menus, intermissions,
  loading: touches are SDL's mouse clicks; the menus' BACK is in
  `menu.zs`). Buttons act on the ticcmd directly (`G_BuildTiccmd`,
  `IOSGame_Ticcmd`), whatever keys are bound; the look goes in as mouse
  counts (so `m_sensitivity_x/y` and invert apply) times LOOK; tilt aiming
  straight into the view's angles.
  - Bottom right: FIRE, USE (left of it) and JUMP (above it) on its arc,
    CROUCH beside it; JUMP and CROUCH only where the level allows them (in
    DOOM only with sv_jump / sv_crouch 2). Dragging on them also looks.
  - Top left: NEXT WPN, PREV WPN, MAP (the automap, lit while open).
  - Top middle: CAMERA. Then CHEAT (cheat mode), SAVE, LOAD, MENU.
  - The weapon wheel: one slice per weapon of the player's slots, the
    pickup sprites from the loaded game (the fist: its punch's clenched
    frame, PUNGB0, cropped to the fist), ammo counts, colours by ammo.
- **The cheat (`sithewok`):** the tray's CHEAT shows the CHEAT button and
  switches the cheat on, or tapped again leaves cheat mode; CHEAT held
  0.5 s (gold ring) = on (gold, the doomguy's god-mode face in colour) /
  off (grey); a tap does nothing. Its picture is made at run time from the
  player's own wall `MWALL4_1` (the demon's face in a pentagram: DOOM, DOOM
  II, TNT, Plutonia), the owner's approved recipe ported from Python
  (`CheatWallIcon`/`CheatButton` in `IOSGame.cpp`): eyes lit, the wall's
  star painted out and redrawn evenly, cut round so its red circle is the
  button's edge (no ring drawn; off: grey at 60%); without that wall the
  god-mode face `STFGOD0` (else `STFST01`) with the gold/grey ring.
  Coming on plays DOOM II's Icon of Sin cube spit (`brain/spit`, DSBOSPIT)
  whole; in DOOM (no DSBOSPIT) read at run time from a `doom2.wad` beside
  the IWAD (the app's Documents) if there is one, else the power-up pickup
  (`misc/p_pkup`). Off is silent. Typing `sithewok` in the console toggles it. While on
  (single player, where the game's cheats work): god mode and `give all`,
  again at each new level, ammo kept full; the tray's NOCLIP (the game's
  noclip). Off each launch; the button's showing is remembered.
- **HUD** (the tray's HUD opens its panel, below; looks: SPLIT, STRIP,
  CLASSIC SPLIT, CLASSIC STRIP, CLASSIC, NONE, default SPLIT; FACE on/off,
  the pills only (the classic bars always have the face); SIZE: the pills
  small / medium / large (30.6 / 39.8 / 51.7 pt, default medium), the
  classic bars small (0.75) / normal (default); NSUserDefaults
  `iosTouchHud`, `iosTouchHudFace`, `iosTouchHudSize`, `iosTouchHudBarSize`;
  the style numbers are kept: CSPLIT 4, CSTRIP 5): CLASSIC SPLIT / STRIP
  are DOOM's status bar over the whole view without the wide STBAR's stone
  sides (`DoomStatusBar.DrawIOSBar`: clipped; SPLIT cut at 178, the face's
  edge, to the HUD's corners with `drawOffset`; small = the bar drawn in a
  320/0.75 x 200/0.75 space), its top told back (`IOSHudBarTop`) so FIRE
  keeps above it. SPLIT,
  STRIP, the classic strips and NONE fill the screen with the view (`setblocks` 11, as the
  CAMERA does; `IOSGame_HudState` in `D_Display`); SPLIT/STRIP draw dark
  rounded pills of DOOM's own graphics in the Doom status bar's ZScript
  (`DoomStatusBar.DrawIOSHud`, sizes from `BaseStatusBar.IOSHud` =
  `IOSTouch_GetHudLayout`: medium: an 83 px pill at 2000x921 (39.8 of 440 pt,
  1.3x the first mockups'; SIZE above),
  scaled with the screen's height): SPLIT bottom left [face] | health |
  armour, bottom right ammo | keys, FIRE and its arc lifted 14 pt above
  them; STRIP one pill in the middle, FIRE and its arc lifted above it too; CLASSIC the status bar, FIRE and its arc above it (its top
  from the game, `fBarTop`). The face is the status bar's mugshot; the
  numbers are the status bar's own digits in its own red (as CLASSIC; the
  owner's wish; armour without a %), health under 25 pulsing brighter. The
  HUD's strength is OPACITY's panel's HUD slider (`iosTouchHudOpacity`,
  20%-100%, default 100%): it fades the pills and their contents, and
  CLASSIC's status bar (`Alpha`; it dims over the view's border, the view
  doesn't reach under it).
- **Game messages:** pickups and the centre messages at DOOM's own size
  for the screen's height (scale height / 200, `active_con_scaletext` in
  `sbar.h`; about 2x the engine's own on a phone; owner's choice). The
  pickup lines start below NEXT WPN | PREV WPN | MAP, from NEXT WPN's left
  edge (`fMsgTop`/`fMsgLeft` of the HUD layout, `c_notifybuffer.cpp`).
  Pickup messages only go to the console (`PRINT_NONOTIFY` in
  `a_pickups.cpp`, the owner's choice); the key and other centre messages
  still show.
- **LEVEL** (tray, any single-player level, cheat or not; gold while the
  cheat is on): the game's maps in MAPINFO's order (add-ons' too), the game
  held still while open; a tap is `map <lump>`: a new game at that map
  ("Going to ...: a fresh start"; with the cheat, everything given again).
- **CAMERA** (`ios/IOSCamera.cpp` the game side, `IOSCapture.cpp` the
  frame grabs, `IOSPhotos.mm` the iOS saving and options, from the Raze and
  Wolf3D ports): tap = photo (the tray's CAMERA held opens its options:
  PHOTO kinds clean / HUD / HUD + buttons, SELFIES 1/3/5, FREEZE on/off),
  hold 0.5 s (ring) = selfie, slide sideways = burst (10, 0.2 s apart),
  slide down = Action Freeze (one finger orbits, two move and pinch to zoom
  to 8x; the shutter takes clean photos; X back), slide up = the cinematic
  burst (5). The tray's CAMERA (camera symbol) shows/hides it. Photos are
  the render's own pixels (Vulkan: `VulkanRenderDevice::IOSReadFrame`, as
  the Raze port's; OpenGL on the rig), clean = the 3D view's window with
  the weapon; saved to Photos (add-only) else Files > DOOM > Screenshots.
  Selfies: the doomguy himself (first: the best place; shown with
  `showviewer`, alone if no one is near) and the nearest 11 monsters
  brought in front of the player's eyes,
  standing (Spawn) or walking (See) frames, facing the lens, an arrangement
  per photo (huddle, peek, rows, corner, tiny, totem, pyramid, lift);
  other living monsters, shots and whatever stands in front of a face
  hidden. Freeze and cinema draw the doomguy (the player's own sprite,
  `showviewer`). How nothing changes: the photo view's camera
  (`R_SetupFrame` hook), poses, hiding (`RF_INVISIBLE`) and lens are
  applied in `IOSCamera_RenderBegin` and put back in `RenderEnd` around
  `RenderView` every frame; posed actors keep their links and are drawn
  in their posed sector by `HWDrawInfo::RenderThings` (`IOSCamera_ThingsIn`);
  the game holds still (`P_CheckTickerPaused`); the photo view fills the
  screen (`setblocks` 11 while it's on). Single player only. Lengths are
  map units; the arrangements were measured in the Wolf3D port's tiles
  (`CAM_S`, 60 units).
- **Frame rate:** FPS in the tray (held: 60 <-> 120). The game ticks at 35
  Hz and the renderer interpolates. The pace (`ios/IOSPacer.h`, called as
  `IOS_FrameCap` at the top of `D_DoomLoop`, before the frame's time and the
  touches are read): at 60 a frame starts every 1/60 s and asks the screen
  to show it one period later (VK_GOOGLE_display_timing: MoltenVK's
  presentAtTime, in `vk_framebuffer.cpp`), the starts steered onto the
  screen's refreshes by the real shown times; at 120 plain vsync. Build 14
  slept after reading the time and the touches, and vsync on a 120 Hz screen
  showed a frame at one refresh or the next: judder at "60.0 fps". The look
  is resampled by the touches' own timestamps (`IOSTouch_LookTake`; build
  14's recording turned 6, 18, 6, 18 px a frame), UIKit's coalesced
  touches each a sample at its own time (`IOSTouch_TouchPassed`), the lag
  up to 45 ms when touches come in batches (build 23's log: half the drag
  frames without a new touch for minutes, until Control Center was
  pulled down). The pacer's wait serves the main run loop
  (`CFRunLoopRunInMode` in `IOS_FrameCap`, the last 1 ms slept) so UIKit
  can hand touches over while the game waits. While the game is held
  still (wheel, LEVEL list, CAMERA, GAMES) or paused, frames don't wait for
  a tic. Log every 5 s of play (`IOS_PerfFrame`, `i_ios.mm`): fps and a
  frame's ms (its turn's wait, tics, input, drawing, Vulkan's waits), and
  "shown every ... ms (min, max, spread): n uneven, n late", the screen's
  real present times. `ios/rig/IOSPacerSim.cpp` runs the pacer against a
  pretend 120/60 Hz screen (build 14's way: a third of frames uneven).
  MoltenVK reports the screen's fastest refresh (8.33 ms) while at FPS 60
  frames show every 16.67 ms: a plan that starts over (after the app comes
  back) could sit on the refresh between and stay "late" every frame; the
  pacer now moves a steadily late plan by the whole refresh (at most a
  frame period each time the pace starts over). After the "shown" line, a
  look line while there was a drag (`IOSTouch_LookStats`): drag frames,
  ones with no new touches, no turn, playback past the newest touch,
  touches per frame, the newest touch's age, the lag, the longest gap.
- **iOS code:** `ios/`: `i_ios.mm` (main, paths, the picker's fallback, the
  error alert, the log, MoltenVK, frame cap and log), `IOSPicker.mm`,
  `IOSTouch.m`/`.h`, `IOSGame.cpp`/`.h`, `IOSCamera.cpp`/`.h`,
  `IOSCapture.cpp`/`.h`, `IOSPhotos.mm`, `Info.plist.in`, `build_deps.sh`.
  Engine changes are `#ifdef DOOM_IOS` (the app) or `DOOM_TOUCH` (the touch
  controls and the CAMERA: iOS and the rig), marked "Added (iOS...)".
- **CI:** `.github/workflows/ios.yml` (macos-15, newest Xcode): deps cached,
  host tools, the iOS build, `DOOM.ipa`, a check that fails if a .wad is in
  the app, and on pushes to `ios` the public `ios-latest` prerelease (body's
  first line "Build <run> · <YYYY-MM-DD>"). Pushes to other `ios-*`
  branches build without publishing (use one to compile-check Objective-C).
  The build stamp ("build 80 · Nov 9") shows under the open MENU tray.

## Testing without a Mac (the Linux rig)

- Build: `cmake -S . -B <dir> -G Ninja -DCMAKE_BUILD_TYPE=Release
  -DDOOM_TOUCH_RIG_SOURCE=$PWD/ios/rig/IOSTouchRig.c -DNO_OPENAL=ON
  -DFORCE_NO_LTO=ON -DGCEM_INCLUDE_DIR=<gcem>/include` (fluidsynth's gcem
  can't be downloaded through the proxy: `git clone` it). About 6 minutes
  on 4 cores. The rig defines `IOSTOUCH_RIGHOOKS`.
- Run under `xvfb-run -s "-screen 0 1688x780x24"` with `-iwad <wad> -warp
  1 -width 1688 -height 780 +vid_fullscreen 0 -nosound -exec <cfg>`,
  `HOME=<scratch>`, `IOSCAM_DIR=<photos>`. Vulkan runs on llvmpipe there.
  `IOSRIG_SCREEN="956 440 62 0 62 21"` sets the rig's phone (points, insets;
  default the iPhone 14's 844x390); the HUD's settings are kept in
  `$HOME/.iosrig-hud` ("style face hud buttons size barsize", the phone's
  NSUserDefaults); with HUD's panel open, `ios_rig opacityslide
  <buttons|hud> <0.2-1>` moves a slider, `ios_rig hudpick <row 0-2>
  <choice>` taps a choice, `ios_rig hudreset <row 0-4>` a row's name;
  `ios_rig lookstats` prints the look line.
  `wait` only works inside one line: join the script's lines with `;`.
- Commands: `ios_rig down/move/up <id> <x y | @BUTTON>` (`@MENU`,
  `@TRAYLOOK`, `@TRAYCHEAT`, `@SAVE`, `@CAMERA`...), `ios_rig tapslice
  <i>`, `ios_rig picklevel <i>`, `ios_rig gyro <yaw> <pitch>`, `ios_rig
  state`; `ios_rigpos` (the player), `ios_rigweapons`, `ios_riglevels`,
  `ios_rigicons <dir>`, `ios_rigcheaticon <dir> [px]` (CHEAT's icons as
  raw RGBA), `ios_rigcheatsound`, `ios_rigactive 0/1`; `ios_cam photo | selfie |
  burst | freeze | cinema | end | orbit y p | pan r d | zoom f | kinds n |
  selfies n | hash | status | gather n` (gather moves monsters: rig only).
  `ios_cam hash` (every actor, the map time, the random numbers) is
  printed before and after each selfie, freeze and cinematic burst
  (`IOSCAM_DUMP=<file>` writes every actor's fields too). Holds need
  `wait 25` or so (the rig's frames are slower than the phone's).
- Objective-C (the overlay's view, `i_ios.mm`, the picker, `IOSPhotos.mm`)
  only compiles in CI. Gameplay on the phone is only tested by the owner.

## Rules

- **Game data:** never commit, upload, bundle or publish WADs or any game
  files (IWADs, add-on WADs, the re-release's files, zips), here or
  anywhere public. Work on local copies only, outside the repository.
- **Commits:**
  - Identity `Claude <noreply@anthropic.com>` (already configured); never
    the owner's email or name. Subject `iOS: ...`, then a plain wrapped body.
  - End with the attribution trailer lines your session provides. Apart from
    those, no AI model names in commits, code or docs.
  - No PRs unless asked. Never stash, reset or check out over a working tree
    that another job may be editing. Push with `git push -u origin ios`
    (pull first); never force-push, never push tags.
- **Releases:** no versioned release yet; each push to `ios` updates the
  public `ios-latest` prerelease (the owner's go). Before any other public
  release or public text, build a preview and wait for the owner's explicit
  go.
- **Touch controls standard** (shared with the owner's other ports; keep it
  the same):
  - Move stick where the left thumb lands; drag elsewhere to look.
  - SAVE / LOAD: tap = DOOM's Save Game / Load Game menu; a 0.3 s hold with
    a ring = quick save / quick load. Sliding off cancels; SAVE wins a tie.
  - MENU: tap = game menu; hold 0.45 s = tray with LOOK, ALWAYS RUN, SENS,
    GYRO, HUD (its panel), FPS, CAMERA, CHEAT, LEVEL (single player, in a level),
    NOCLIP (the cheat on), GAMES (more than one game), keyboard (the
    console), and the build stamp under it.
  - LOOK: the drag's look speed, shown on it ("LOOK 1x"); a tap steps 0.5,
    0.75, 1, 1.25, 1.5, 2, 2.5, 3x (round to 0.5x); held 0.45 s (ring)
    back to 1x with a toast and a haptic. Remembered (`iosTouchLookScale`).
  - ALWAYS RUN: DOOM's `cl_run`. Off: the run marker (^^ in a small circle
    above the stick, joined by a line) shows with the stick; the thumb slid
    onto it runs straight ahead (a haptic tick). On: no marker, the stick
    always runs. The stick walks otherwise (not `cl_analog_run`).
  - GYRO: OFF (default) / TOUCH (aims while either thumb is down; lifting
    both freezes the view) / ALWAYS. SENS 1.0/1.5/2.0/3.0 (the gyro's
    only), default 1.5. Remembered.
  - HUD (replaces OPACITY, FACE and SIZE; the owner's design, for all the
    ports): its label is the look ("HUD\nSPLIT", "CLASSIC\nSPLIT"); tap =
    a panel under the tray (`IOSTouch_OpacityPanel`, `layoutHudPanel`), a
    row each: LOOK (the six looks), SIZE (not for CLASSIC or NONE), FACE
    (SPLIT and STRIP only), HUD (the HUD's opacity: `iosTouchHudOpacity`)
    and BUTTONS (the controls': `iosTouchOpacity`), the sliders 20% to 100%
    in 5% steps, live, saved on lift; a choice takes effect at once; a tap
    on a row's name = that row back to its default (toast, haptic); hold
    HUD 0.45 s (ring) = everything back (SPLIT, medium, face on, both
    100%; toast, haptic). The tray and the wheel stay at full.
  - FPS: tap = show/hide the FPS readout; held 0.45 s (ring) = 60 <-> 120
    ("FPS\n60", default 60) with a toast and a haptic. Remembered.
  - NEXT WPN / PREV WPN: tap = next/previous; a 10 pt slide opens the
    weapon wheel at once; a 0.3 s hold opens it to tap. The gap at the
    bottom cancels. The game holds still while a wheel is open. A haptic
    tick per slice.
  - HUD and buttons stay clear of the corners, the notch or Dynamic Island,
    and the home bar.
- **Testing:** say plainly what was and wasn't tested. The owner tests on
  the device.
- **Lean mode:** small contexts, no full log or file dumps
  (grep/sed/head/tail), reuse builds.
- **Talking to the owner:** plain words, short messages, direct links, and
  honesty about mistakes and untested things. If they skip a question,
  choose a sensible default and say what you chose.
