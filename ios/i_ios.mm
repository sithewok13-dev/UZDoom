/*
** i_ios.mm: the iOS pieces the SDL backend expects from the Mac files
** (from the owner's Raze port, ios/i_ios.mm)
**
** - I_PickIWad_Cocoa: the game picker (ios/IOSPicker.mm), a plain list if
**   that can't be shown
** - LAN games: the picker's "LAN multiplayer": which game, Host (mode,
**   players, skill) or Join (the hosts found on the Wi-Fi, or a typed
**   address), then UZDoom's own -host / -join; and IOSLobby, the box while
**   waiting for the players (i_net.cpp's lobby on the phone)
** - Mac_I_FatalError: an alert with the error before the app ends
** - the log in Files (doom-log.txt), MoltenVK, the game window's scene
**
** The picker and the alert run before or outside the game loop, so they
** show UIKit and spin the main run loop until they are answered.
*/

#define GLES_SILENCE_DEPRECATION 1 // (OpenGL ES is the fallback renderer; its diagnostics stay)
#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#import <AVFoundation/AVFoundation.h>
#include "SDL.h"
#include "SDL_syswm.h"

#include "cmdlib.h"
#include "i_interface.h"
#include "m_argv.h"
#include "c_cvars.h"
#include "IOSPicker.h"
#include "IOSNet.h"
#include "IOSLobby.h"
#include "IOSGame.h"



static UIViewController* TopViewController()
{
	UIWindow* key = nil;
	for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
	{
		if (![scene isKindOfClass:UIWindowScene.class]) continue;
		// (never a hidden window: HideOwnWindow's can stay key, and an alert
		// put on it is never seen: a black screen)
		for (UIWindow* w in ((UIWindowScene*)scene).windows)
		{
			if (w.isKeyWindow && !w.hidden) { key = w; break; }
		}
		if (!key)
			for (UIWindow* w in ((UIWindowScene*)scene).windows)
				if (!w.hidden && w.rootViewController) key = w; // (the frontmost shown)
		if (key) break;
	}
	UIViewController* vc = key.rootViewController;
	while (vc.presentedViewController) vc = vc.presentedViewController;
	return vc;
}

// What to put an alert on: the top of what's on screen, or, with nothing
// on screen yet (the game window comes later), a plain black window of our
// own (HideOwnWindow takes it away again)
static UIWindow* ownWindow = nil;

static UIViewController* PresentingController()
{
	UIViewController* vc = TopViewController();
	if (vc == nil)
	{
		UIWindowScene* windowScene = nil;
		for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
			if ([scene isKindOfClass:UIWindowScene.class]) { windowScene = (UIWindowScene*)scene; break; }
		ownWindow = windowScene ? [[UIWindow alloc] initWithWindowScene:windowScene]
		                        : [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
		ownWindow.rootViewController = [UIViewController new];
		ownWindow.rootViewController.view.backgroundColor = UIColor.blackColor;
		[ownWindow makeKeyAndVisible];
		vc = ownWindow.rootViewController;
	}
	return vc;
}

static void HideOwnWindow()
{
	if (ownWindow)
	{
		// Out of the way again before the game makes its own window
		ownWindow.hidden = YES;
		ownWindow = nil;
	}
}

static void SpinRunLoop(double seconds)
{
	@autoreleasepool
	{
		[[NSRunLoop mainRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:seconds]];
	}
}

// Shows an alert (an iPad's action sheet gets an anchor); false when it
// couldn't be shown
static bool Present(UIAlertController* alert)
{
	UIViewController* vc = PresentingController();
	if (vc == nil) return false;
	if (alert.popoverPresentationController)
	{
		alert.popoverPresentationController.sourceView = vc.view;
		alert.popoverPresentationController.sourceRect = CGRectMake(CGRectGetMidX(vc.view.bounds), CGRectGetMidY(vc.view.bounds), 0, 0);
		alert.popoverPresentationController.permittedArrowDirections = 0;
	}
	[vc presentViewController:alert animated:YES completion:nil];
	return alert.presentingViewController != nil;
}

// Waits (up to a second) until a closed alert is really off the screen, so
// the next one can be shown
static void WaitGone(UIAlertController* alert)
{
	for (int i = 0; i < 20 && alert.presentingViewController != nil; i++) SpinRunLoop(0.05);
}

// Shows the alert and waits for an answer; returns the chosen action's index
// (-1: it couldn't be shown)
static int RunAlert(UIAlertController* alert, NSArray<NSString*>* titles, int cancelIndex)
{
	__block int chosen = -1;
	for (NSUInteger i = 0; i < titles.count; i++)
	{
		const int index = (int)i;
		UIAlertActionStyle style = (index == cancelIndex) ? UIAlertActionStyleCancel : UIAlertActionStyleDefault;
		[alert addAction:[UIAlertAction actionWithTitle:titles[i] style:style handler:^(UIAlertAction*) { chosen = index; }]];
	}
	if (!Present(alert))
	{
		HideOwnWindow();
		return -1;
	}
	while (chosen < 0) SpinRunLoop(0.05);
	WaitGone(alert);
	HideOwnWindow();
	return chosen;
}

static UIAlertController* Sheet(NSString* title, NSString* message)
{
	return [UIAlertController alertControllerWithTitle:title message:message preferredStyle:UIAlertControllerStyleActionSheet];
}

//---------------------------------------------------------------------------
//
// LAN games: the questions after "LAN multiplayer" (from the owner's Raze
// port); the game itself is UZDoom's own network game (-host / -join)
//
//---------------------------------------------------------------------------

// Hosting: mode, players, skill. false: back
static bool ChooseHost(FArgs* args)
{
	const int m = RunAlert(Sheet(@"Host a LAN game", @"Which kind of game?"),
		@[ @"Co-op", @"Deathmatch", @"Deathmatch (items respawn)", @"Back" ], 3);
	if (m < 0 || m >= 3) return false;

	NSMutableArray<NSString*>* titles = [NSMutableArray array];
	for (int n = 2; n <= 8; n++) [titles addObject:[NSString stringWithFormat:@"%d players", n]];
	[titles addObject:@"Back"];
	const int p = RunAlert(Sheet(@"Host a LAN game", @"How many players, you too? The game starts when they are all here, or when you tap Start now."), titles, 7);
	if (p < 0 || p >= 7) return false;

	const int k = RunAlert(Sheet(@"Host a LAN game", @"Skill"),
		@[ @"I'm too young to die", @"Hey, not too rough", @"Hurt me plenty", @"Ultra-Violence", @"Nightmare!", @"Back" ], 5);
	if (k < 0 || k >= 5) return false;

	IOSNet_TouchNetwork();	// (iOS asks for the Local Network permission now)
	args->AppendRawArg("-host");
	args->AppendRawArg(FStringf("%d", p + 2).GetChars());
	if (m == 1) args->AppendRawArg("-deathmatch");
	else if (m == 2) args->AppendRawArg("-altdeath");
	if (m >= 1)
	{
		// deathmatch: no monsters, and the exits do nothing (sv_noexit; the
		// player isn't killed for it on iOS: p_spec.cpp), so the level only
		// changes by the host's LEVEL
		args->AppendRawArg("-nomonsters");
		args->AppendRawArg("+sv_noexit");
		args->AppendRawArg("1");
	}
	args->AppendRawArg("-skill");
	args->AppendRawArg(FStringf("%d", k + 1).GetChars());
	printf("LAN game: hosting, mode %d, %d players, skill %d\n", m, p + 2, k + 1);
	return true;
}

// Joining: the hosts found on the Wi-Fi, or a typed address. Gives the
// address and the host's game (nil: typed, the game not known). false: back
static bool ChooseJoin(NSString** pAddress, NSString** pGame)
{
	IOSNet_TouchNetwork();
	for (;;)
	{
		// the search (1.5 s, off the main thread; a box meanwhile)
		static IOSNetHost hosts[IOSNET_MAXHOSTS];	// (static: the search block fills it)
		__block int ctHosts = -1;
		UIAlertController* busy = [UIAlertController alertControllerWithTitle:@"Join a LAN game" message:@"Looking for games on this Wi-Fi…" preferredStyle:UIAlertControllerStyleAlert];
		const bool shown = Present(busy);
		dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
			int n = IOSNet_Scan(hosts, IOSNET_MAXHOSTS, 1.5);
			dispatch_async(dispatch_get_main_queue(), ^{ ctHosts = n; });
		});
		while (ctHosts < 0) SpinRunLoop(0.05);
		if (shown)
		{
			[busy dismissViewControllerAnimated:NO completion:nil];
			WaitGone(busy);
		}
		HideOwnWindow();

		NSMutableArray<NSString*>* titles = [NSMutableArray array];
		for (int i = 0; i < ctHosts; i++)
		{
			[titles addObject:[NSString stringWithFormat:@"%s · %s · %d of %d", hosts[i].strGame, hosts[i].strMode,
				hosts[i].ctPresent, hosts[i].ctWanted]];
		}
		const int iType = (int)titles.count;
		[titles addObject:@"Type an address"];
		[titles addObject:@"Search again"];
		[titles addObject:@"Back"];
		NSString* msg = ctHosts > 0 ? @"Tap a game to join it. Its game opens here by itself (you need the same game files as the host)."
			: @"No game found. The host must be waiting for players, on this Wi-Fi.";
		const int pick = RunAlert(Sheet(@"Join a LAN game", msg), titles, iType + 2);
		if (pick < 0 || pick == iType + 2) return false;
		if (pick == iType + 1) continue;

		if (pick < ctHosts)
		{
			*pAddress = [NSString stringWithUTF8String:hosts[pick].strAddress];
			*pGame = [NSString stringWithUTF8String:hosts[pick].strGame];
		}
		else
		{
			UIAlertController* ask = [UIAlertController alertControllerWithTitle:@"Join a LAN game"
				message:@"The host's address (it shows on the host's screen while it waits)" preferredStyle:UIAlertControllerStyleAlert];
			NSString* last = [NSUserDefaults.standardUserDefaults stringForKey:@"DoomLanLastAddress"];
			[ask addTextFieldWithConfigurationHandler:^(UITextField* field) {
				field.placeholder = @"192.168.1.20";
				field.text = last;
				field.keyboardType = UIKeyboardTypeNumbersAndPunctuation;
				field.autocorrectionType = UITextAutocorrectionTypeNo;
			}];
			const int ok = RunAlert(ask, @[ @"Join", @"Back" ], 1);
			NSString* typed = [ask.textFields.firstObject.text stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
			if (ok != 0 || typed.length == 0) continue;
			*pAddress = typed;
			*pGame = nil;
		}
		[NSUserDefaults.standardUserDefaults setObject:*pAddress forKey:@"DoomLanLastAddress"];
		return true;
	}
}

// The picker's games (labels), and Back last
static NSMutableArray<NSString*>* LanGames(int* pCount)
{
	NSMutableArray<NSString*>* games = [NSMutableArray array];
	int ctGames = 0;
	for (const char* label; (label = IOSPicker_EntryLabel(ctGames)) != NULL; ctGames++)
		[games addObject:[NSString stringWithUTF8String:label] ?: @"?"];
	*pCount = ctGames;
	return games;
}

// "LAN multiplayer": host or join first. The host picks the game; a guest
// picks a host, and that host's game opens here (matched by its picker
// name, which the host's answer carries; a typed address asks which game).
// The game's index (its add-on's files in pszFiles), or -1: back to the
// picker
static int ChooseLan(FArgs* args, char* pszFiles, int ctFiles)
{
	for (;;)
	{
		const int c = RunAlert(Sheet(@"LAN multiplayer", @"A game with others on this Wi-Fi"), @[ @"Host a LAN game", @"Join a LAN game", @"Back" ], 2);
		if (c < 0 || c == 2) return -1;

		int ctGames = 0;
		NSMutableArray<NSString*>* games = LanGames(&ctGames);
		if (c == 0)
		{
			// hosting: which game, then mode, players, skill
			[games addObject:@"Back"];
			const int g = RunAlert(Sheet(@"Host a LAN game", @"Which game? (Everyone needs the same game files.)"), games, ctGames);
			if (g < 0 || g >= ctGames) continue;
			if (!ChooseHost(args)) continue;
			IOSGame_SetNetName(games[g].UTF8String);
			return IOSPicker_EntryPick(g, pszFiles, ctFiles);
		}

		// joining: the host's game, found among this phone's
		NSString* address = nil;
		NSString* game = nil;
		if (!ChooseJoin(&address, &game)) continue;
		int g = -1;
		if (game)
		{
			for (int i = 0; i < ctGames && g < 0; i++)
				if ([games[i] isEqualToString:game]) g = i;
			for (int i = 0; i < ctGames && g < 0; i++)
				if ([games[i] caseInsensitiveCompare:game] == NSOrderedSame) g = i;
			if (g < 0)
			{
				RunAlert([UIAlertController alertControllerWithTitle:@"Join a LAN game"
					message:[NSString stringWithFormat:@"This phone doesn't have %@. Copy the same game files as the host's into Files > On My iPhone > DOOM, then try again.", game]
					preferredStyle:UIAlertControllerStyleAlert], @[ @"OK" ], 0);
				continue;
			}
		}
		else
		{
			[games addObject:@"Back"];
			g = RunAlert(Sheet(@"Join a LAN game", @"Which game is the host playing?"), games, ctGames);
			if (g < 0 || g >= ctGames) continue;
		}
		args->AppendRawArg("-join");
		args->AppendRawArg(address.UTF8String);
		printf("LAN game: joining %s (%s)\n", address.UTF8String, games[g].UTF8String);
		IOSGame_SetNetName(games[g].UTF8String);
		return IOSPicker_EntryPick(g, pszFiles, ctFiles);
	}
}

//---------------------------------------------------------------------------
//
// LAN games: the box while waiting for the players (IOSLobby.h: i_net.cpp's
// lobby calls, instead of the ZWidget window)
//
//---------------------------------------------------------------------------

namespace IOSLobby
{
	static UIAlertController* box = nil;
	static bool bHost = false, bCancelled = false, bStart = false;
	static FString message;
	static TArray<FString> names;	// by client
	static int present = 1, wanted = 2;

	static NSString* BoxText()
	{
		NSMutableString* text = [NSMutableString string];
		if (bHost)
		{
			char mine[64] = "";
			IOSNet_WifiAddress(mine, sizeof(mine));
			[text appendFormat:@"%d of %d players here.", present, wanted];
			[text appendFormat:@"\n\nThis phone's address: %s\n(the others: LAN multiplayer, Join)", mine[0] ? mine : "no Wi-Fi?"];
		}
		else
		{
			NSString* what = [NSString stringWithUTF8String:message.GetChars()] ?: @"";
			if ([what hasPrefix:@"Contacting"]) [text appendString:@"Looking for the host…\n(it must be waiting for players, on this Wi-Fi)"];
			else [text appendFormat:@"%d of %d players here: waiting for the host to start.", present, wanted];
		}
		NSMutableArray<NSString*>* here = [NSMutableArray array];
		for (unsigned i = 0; i < names.Size(); i++)
			if (names[i].IsNotEmpty()) [here addObject:[NSString stringWithUTF8String:names[i].GetChars()] ?: @"?"];
		if (here.count) [text appendFormat:@"\n\n%@", [here componentsJoinedByString:@", "]];
		return text;
	}

	static void CloseBox()
	{
		if (box)
		{
			if (box.presentingViewController) [box dismissViewControllerAnimated:NO completion:nil];
			WaitGone(box);
			[box release];
			box = nil;
		}
		HideOwnWindow();
	}

	// The box shown, its text up to date (again after Start now closed it,
	// while the game gets going)
	static void ShowBox()
	{
		NSString* text = BoxText();
		if (box && box.presentingViewController == nil && !box.isBeingPresented)
		{
			[box release];
			box = nil;
		}
		if (box == nil)
		{
			box = [[UIAlertController alertControllerWithTitle:(bHost ? @"Hosting a LAN game" : @"Joining a LAN game") message:text
				preferredStyle:UIAlertControllerStyleAlert] retain];
			if (bHost && !bStart)
				[box addAction:[UIAlertAction actionWithTitle:@"Start now" style:UIAlertActionStyleDefault handler:^(UIAlertAction*) { bStart = true; }]];
			if (!bStart)
				[box addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:^(UIAlertAction*) { bCancelled = true; }]];
			Present(box);
		}
		else if (![box.message isEqualToString:text]) box.message = text;
	}

	void NetInit(const char* msg, bool host)
	{
		bHost = host;
		bCancelled = bStart = false;
		message = msg ? msg : "";
		names.Clear();
		present = 1;
		ShowBox();
	}

	void NetMessage(const char* msg)
	{
		message = msg ? msg : "";
		if (box) ShowBox();
	}

	void NetConnect(int client, const char* name, unsigned flags, int status)
	{
		if (client < 0) return;
		if ((unsigned)client >= names.Size()) names.Resize(client + 1);
		names[client] = name ? name : "?";
		if (box) ShowBox();
	}

	void NetUpdate(int client, int status) {}

	void NetDisconnect(int client)
	{
		if (client >= 0 && (unsigned)client < names.Size()) names[client] = "";
		if (box) ShowBox();
	}

	void NetProgress(int cur, int limit)
	{
		present = cur;
		wanted = limit;
		if (box) ShowBox();
	}

	void NetDone() { CloseBox(); }
	void NetClose() { CloseBox(); }
	bool ShouldStartNet() { return bStart; }
	int GetNetKickClient() { return -1; }
	int GetNetBanClient() { return -1; }

	// The lobby's wait: its callback about 10 times a second until it's done
	// (true) or Cancel (false)
	bool NetLoop(bool (*loopCallback)(void*), void* data)
	{
		for (;;)
		{
			if (loopCallback(data)) return true;
			if (bCancelled)
			{
				CloseBox();
				return false;
			}
			ShowBox();
			SpinRunLoop(0.1);
		}
	}
}

// The game picker (i_system.cpp's I_PickIWad on iOS): the picker's cards,
// a plain list if they can't be shown. An add-on's files are loaded with
// the game (-file). false: none picked (the app ends).
bool I_PickIWad_IOS(FStartupSelectionInfo& info)
{
	const TArray<WadStuff>& wads = *info.Wads;
	const int numwads = (int)wads.Size();
	TArray<const char*> names;
	for (int i = 0; i < numwads; i++) names.Push(wads[i].Name.GetChars());
	TArray<const char*> paths;
	for (int i = 0; i < numwads; i++) paths.Push(wads[i].Path.GetChars());
	IOSPicker_SetPaths(paths.Data(), numwads);
	static char files[4096];
	int chosen;
	for (;;)
	{
		chosen = IOSPicker_Run(names.Data(), numwads, info.DefaultIWAD, files, sizeof(files));
		if (chosen != IOSPICKER_LAN) break;
		// Added (LAN games): which game, host or join (Back: the picker again)
		chosen = ChooseLan(info.Args, files, sizeof(files));
		if (chosen >= 0) break;
	}
	if (chosen == IOSPICKER_UNAVAILABLE)
	{
		files[0] = 0;
		NSMutableArray<NSString*>* titles = [NSMutableArray array];
		for (int i = 0; i < numwads; i++)
		{
			NSString* name = [NSString stringWithUTF8String:wads[i].Name.GetChars()];
			[titles addObject:name ? name : @"?"];
		}
		[titles addObject:@"Quit"];
		chosen = RunAlert(Sheet(@"DOOM", @"Choose a game"), titles, numwads);
		if (chosen == numwads) chosen = -1;
	}
	printf("Game picker: chose %d of %d\n", chosen, numwads);
	if (chosen < 0 || chosen >= numwads) return false;
	info.DefaultIWAD = chosen;
	// (no extra arguments kept from earlier runs: only this pick's add-on)
	info.DefaultArgs = "";
	info.bSaveArgs = false;
	if (files[0])
	{
		info.Args->AppendRawArg("-file");
		for (char* f = strtok(files, "\n"); f; f = strtok(nullptr, "\n")) info.Args->AppendRawArg(f);
	}
	return true;
}

void Mac_I_FatalError(const char* errortext)
{
	NSString* text = [NSString stringWithUTF8String:errortext ? errortext : ""];
	if (text == nil) text = [NSString stringWithCString:errortext encoding:NSISOLatin1StringEncoding];

	UIAlertController* alert = [UIAlertController alertControllerWithTitle:@"DOOM: fatal error"
		message:text
		preferredStyle:UIAlertControllerStyleAlert];
	RunAlert(alert, @[ @"Exit" ], 0);
}

// Everything the engine prints goes to Files > On My iPhone > DOOM >
// doom-log.txt (replaced at each launch), so a problem on the phone can be
// read and sent. Runs before main().
#include <stdio.h>
#include <time.h>
#include <unistd.h>
__attribute__((constructor(101))) static void IOS_LogToFile()
{
	NSString* docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
	if (docs == nil) return;
	const char* path = [[docs stringByAppendingPathComponent:@"doom-log.txt"] fileSystemRepresentation];
	if (freopen(path, "w", stdout)) setvbuf(stdout, NULL, _IOLBF, 0);
	dup2(fileno(stdout), fileno(stderr));
	setvbuf(stderr, NULL, _IONBF, 0);
}

// Start-up diagnostics (called from SwapBuffers for the first frames): what
// the GL frame buffer holds and which windows UIKit shows, into the log.
#include <OpenGLES/ES3/gl.h>
#import <OpenGLES/EAGL.h>
#include <vector>
#include "IOSPacer.h"
extern "C" void IOS_FrameReport(int frame, int w, int h)
{
	GLint fb = 0, vp[4] = {0, 0, 0, 0};
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
	glGetIntegerv(GL_VIEWPORT, vp);
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	unsigned char px[4] = {0, 0, 0, 0};
	if (w > 0 && h > 0) glReadPixels(w / 2, h / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	GLenum err = glGetError();
	if (frame == 0)
	{
		// SDL's own idea of the screen's frame buffer, to compare with ours
		SDL_SysWMinfo info;
		SDL_VERSION(&info.version);
		SDL_Window* sw = SDL_GL_GetCurrentWindow();
		if (sw && SDL_GetWindowWMInfo(sw, &info) && info.subsystem == SDL_SYSWM_UIKIT)
		{
			printf("SDL screen: framebuffer %u, colorbuffer %u, resolve %u\n",
				info.info.uikit.framebuffer, info.info.uikit.colorbuffer, info.info.uikit.resolveFramebuffer);
			UIView* v = info.info.uikit.window.rootViewController.view;
			printf("SDL view: %s, frame %.0f,%.0f %.0fx%.0f, hidden %d, alpha %.2f, layer %s opaque %d, superview %s\n",
				class_getName(v.class), v.frame.origin.x, v.frame.origin.y, v.frame.size.width, v.frame.size.height,
				v.hidden, v.alpha, class_getName(v.layer.class), v.layer.opaque, v.superview ? class_getName(v.superview.class) : "none");

		}
	}
	printf("Frame %d: drawable %dx%d, fb %d (status 0x%x), viewport %d,%d %dx%d, centre pixel %d,%d,%d, GL error 0x%x\n",
		frame, w, h, (int)fb, status, vp[0], vp[1], vp[2], vp[3], px[0], px[1], px[2], err);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
	for (UIWindow* win in UIApplication.sharedApplication.windows)
	{
		printf("  window %s: %s%s%s level %.0f, %.0fx%.0f, root %s\n", class_getName(win.class),
			win.isKeyWindow ? "key, " : "", win.hidden ? "hidden" : "shown", win.windowScene ? ", in a scene" : ", NO scene", win.windowLevel,
			win.bounds.size.width, win.bounds.size.height,
			win.rootViewController ? class_getName(win.rootViewController.class) : "none");
	}
#pragma clang diagnostic pop
	fflush(stdout);
}

// SDL 2 makes its UIWindow with initWithFrame:, outside any scene: make sure
// it is in the app's window scene, key and shown. (On the owner's phone it
// already was; kept as a safeguard for newer iOS versions.)
void IOS_AttachWindowToScene(SDL_Window* window)
{
	// Sound plays with the silent switch on too, and the game takes over the
	// sound: other apps' music stops while it plays (the owner's choice; the
	// playback category without mixing; set before the game opens its sound)
	[[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayback mode:AVAudioSessionModeDefault
		options:0 error:nil];
	[[AVAudioSession sharedInstance] setActive:YES error:nil];
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_UIKIT || !info.info.uikit.window) return;
	UIWindow* w = info.info.uikit.window;
	UIWindowScene* windowScene = nil;
	for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
	{
		if ([scene isKindOfClass:UIWindowScene.class]) { windowScene = (UIWindowScene*)scene; break; }
	}
	printf("Game window: scene %s, attaching to %s\n", w.windowScene ? "set" : "none", windowScene ? "the app's scene" : "nothing (no scene found)");
	if (windowScene && w.windowScene != windowScene)
	{
		w.windowScene = windowScene;
		w.frame = windowScene.coordinateSpace.bounds;
	}
	[w makeKeyAndVisible];
}


// Vulkan (iOS): MoltenVK, the app carries it in Frameworks/ (the
// workflow puts it there): SDL loads it before the game's window is made
// with Vulkan, and the renderer takes vkGetInstanceProcAddr from SDL
#include "SDL_vulkan.h"

extern "C" bool IOS_LoadVulkan()
{
	NSString* path = [[NSBundle mainBundle].privateFrameworksPath stringByAppendingPathComponent:@"MoltenVK.framework/MoltenVK"];
	if (![[NSFileManager defaultManager] fileExistsAtPath:path])
	{
		printf("iOS: MoltenVK is not in the app: OpenGL ES\n");
		return false;
	}
	if (SDL_Vulkan_LoadLibrary(path.UTF8String) != 0)
	{
		printf("iOS: MoltenVK did not load (%s): OpenGL ES\n", SDL_GetError());
		return false;
	}
	printf("iOS: MoltenVK loaded: Vulkan on Metal\n");
	fflush(stdout);
	return true;
}

extern "C" void* IOS_VulkanGetInstanceProcAddr()
{
	return SDL_Vulkan_GetVkGetInstanceProcAddr();
}

extern "C" int IOSTouch_MaxFps(void);
extern "C" int IOSTouch_LookStats(char* str, int size);
static uint64_t IOS_Ns()
{
	return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}
static uint64_t iosPresentNs = 0;

// The frame's times, logged every 5 s of play (as the Raze port's): frames
// a second, and a frame's milliseconds: the wait for its turn (FPS 60's
// pace), the tics (with any wait for one, and why: cl_capfps, pauseext,
// r_NoInterpolate), the input and the touch controls, and drawing (Vulkan: its own work,
// its waits for a screen image and for the GPU)
int iosWaitReasons = 0; // (d_net.cpp's TryRunTics: bits of why it waited for a tic)
extern uint64_t iosVkAcquireNs, iosVkGpuWaitNs; // (vk_commandbuffer.cpp)
static IOSPacer iosPacer;
extern "C" void IOS_PerfFrame(uint64_t capNs, uint64_t ticsNs, uint64_t inputNs, uint64_t drawNs, bool inLevel)
{
	static uint64_t tStart = 0, sumCap = 0, sumTics = 0, sumInput = 0, sumDraw = 0, sumAcq = 0, sumGpu = 0, worst = 0;
	static int frames = 0, waits = 0, reasons = 0;
	const uint64_t now = IOS_Ns();
	const uint64_t acq = iosVkAcquireNs, gpu = iosVkGpuWaitNs;
	iosVkAcquireNs = iosVkGpuWaitNs = 0;
	const int why = iosWaitReasons;
	iosWaitReasons = 0;
	if (!inLevel) { tStart = 0; return; }
	if (tStart == 0)
	{
		char unused[512];
		tStart = now; frames = waits = reasons = 0; sumCap = sumTics = sumInput = sumDraw = sumAcq = sumGpu = worst = 0;
		IOSPacer_TakeStats(iosPacer);
		IOSTouch_LookStats(unused, sizeof(unused)); // (the look line starts with the window too)
		return;
	}
	frames++;
	if (why & 1) waits++;
	reasons |= why;
	sumCap += capNs;
	sumTics += ticsNs;
	sumInput += inputNs;
	sumDraw += drawNs;
	sumAcq += acq;
	sumGpu += gpu;
	if (capNs + ticsNs + inputNs + drawNs > worst) worst = capNs + ticsNs + inputNs + drawNs;
	if (now - tStart >= 5000000000ull && frames > 0)
	{
		const double ms = 1e-6 / frames;
		printf("iOS: frames %.1f fps (FPS %d); per frame %.1f ms (worst %.1f): waiting for its turn %.1f, tics %.1f (%d of %d frames waited for a tic%s%s%s), "
			"input %.1f, drawing %.1f (waiting for the screen %.1f, for the GPU %.1f)\n",
			frames * 1e9 / (now - tStart), IOSTouch_MaxFps(), (sumCap + sumTics + sumInput + sumDraw) * ms, worst * 1e-6, sumCap * ms, sumTics * ms, waits, frames,
			(reasons & 2) ? ": cl_capfps" : "", (reasons & 4) ? ": paused" : "", (reasons & 8) ? ": no interpolation" : "",
			sumInput * ms, sumDraw * ms, sumAcq * ms, sumGpu * ms);
		// the screen's side: how evenly the frames were really shown (the
		// times the screen showed them; "uneven": off the frame period by
		// half a refresh or more, seen as judder; "late": not ready in time)
		const IOSPacerStats st = IOSPacer_TakeStats(iosPacer);
		if (st.shown > 0)
			printf("iOS: shown every %.2f ms (min %.2f, max %.2f, spread %.2f): %d uneven, %d late of %d; refresh %.2f ms, off plan %.2f ms\n",
				st.meanMs, st.minMs, st.maxMs, st.sdMs, st.uneven, st.late, st.shown, iosPacer.refresh * 1e-6, st.errMs);
		else
			printf("iOS: shown: no presentation times (no VK_GOOGLE_display_timing)\n");
		// the look's timing, when there was a drag (IOSTouch.m)
		char look[512];
		if (IOSTouch_LookStats(look, sizeof(look))) printf("%s\n", look);
		fflush(stdout);
		tStart = now;
		frames = waits = reasons = 0;
		sumCap = sumTics = sumInput = sumDraw = sumAcq = sumGpu = worst = 0;
	}
}

// The frames' pace (ios/IOSPacer.h). At the tray's FPS 60 a frame starts
// every 1/60 s and asks the screen to show it a frame period later, on a
// refresh the starts are steered onto; at 120 the frames go at vsync's pace.
// Called at the top of the game loop, before the frame's time is read for
// the interpolation and before the touches come in, so both follow the
// steady pace (build 14 slept after reading them: frames moved unevenly).
// Returns the ns slept, for the log.
extern "C" uint64_t IOS_FrameCap(void)
{
	const int fps = IOSTouch_MaxFps();
	const uint64_t period = (fps > 0 && fps < 120) ? 1000000000ull / (uint64_t)fps : 0;
	const uint64_t wait = IOSPacer_Wake(iosPacer, IOS_Ns(), period);
	if (wait > 0)
	{
		// The wait serves the main run loop (the game runs on the main
		// thread), so UIKit hands the touches over as they come; asleep, they
		// waited for the next frame's pump (build 23's log: during some
		// stretches half the frames of a drag had no new touch). The last
		// moment is slept, for an exact start.
		const uint64_t until = IOS_Ns() + wait;
		for (;;)
		{
			const uint64_t now = IOS_Ns();
			if (now + 1500000ull >= until) break;
			if (CFRunLoopRunInMode(kCFRunLoopDefaultMode, (double)(until - now - 1000000ull) * 1e-9, true) == kCFRunLoopRunFinished) break;
		}
		const uint64_t now = IOS_Ns();
		if (now < until)
		{
			const uint64_t rest = until - now;
			struct timespec ts = { (time_t)(rest / 1000000000ull), (long)(rest % 1000000000ull) };
			nanosleep(&ts, nullptr);
		}
	}
	return wait;
}

// (vk_framebuffer.cpp, at each present, with VK_GOOGLE_display_timing:) the
// screen's refresh, the presents shown so far and when, and the time this one
// asks to be shown at (0: as soon as it can) with its id
extern "C" void IOS_PresentRefresh(uint64_t refreshNs)
{
	if (refreshNs != iosPacer.refresh)
	{
		printf("iOS: the screen refreshes every %.2f ms (VK_GOOGLE_display_timing): frames shown on time\n", refreshNs * 1e-6);
		fflush(stdout);
	}
	iosPacer.refresh = refreshNs;
}

extern "C" void IOS_PresentShown(uint32_t id, uint64_t actualNs)
{
	IOSPacer_Shown(iosPacer, id, actualNs);
}

extern "C" uint64_t IOS_PresentPlan(uint32_t* pId)
{
	return IOSPacer_Present(iosPacer, pId);
}

// The frame's hand-over to the screen, done here instead of SDL's swap so
// that failures show in the log: SDL's colour renderbuffer, its size and
// attachment, and what presentRenderbuffer says (logged for the first frames
// and on every change of outcome).
extern "C" void IOS_Present(SDL_Window* window, int frame)
{
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_UIKIT) { SDL_GL_SwapWindow(window); return; }
	const GLuint fb = info.info.uikit.framebuffer, rb = info.info.uikit.colorbuffer;
	static int lastOk = -1;
	const bool report = frame < 1;
	if (report)
	{
		GLint boundFb = 0, boundRb = 0, att = 0, attType = 0, w = 0, h = 0;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &boundFb);
		glGetIntegerv(GL_RENDERBUFFER_BINDING, &boundRb);
		glBindFramebuffer(GL_FRAMEBUFFER, fb);
		glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &attType);
		glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &att);
		glBindRenderbuffer(GL_RENDERBUFFER, rb);
		glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &w);
		glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &h);
		printf("Present %d: bound fb %d rb %d; SDL fb %u rb %u (is rb %d, %dx%d); fb colour attachment type 0x%x name %d; GL error before 0x%x\n",
			frame, boundFb, boundRb, fb, rb, glIsRenderbuffer(rb), w, h, attType, att, glGetError());
		glBindFramebuffer(GL_FRAMEBUFFER, boundFb);
	}
	while (glGetError() != GL_NO_ERROR) {}
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	const uint64_t tPresent = IOS_Ns();
	const BOOL ok = [[EAGLContext currentContext] presentRenderbuffer:GL_RENDERBUFFER];
	iosPresentNs += IOS_Ns() - tPresent;
	const GLenum err = glGetError();
	if (report || (int)ok != lastOk)
	{
		printf("Present %d: %s, GL error 0x%x\n", frame, ok ? "ok" : "FAILED", err);
		fflush(stdout);
	}
	lastOk = ok;
}

// At start-up: the app's Documents folder (Files > On My iPhone > DOOM) and
// what is in it, into the log
__attribute__((constructor(102))) static void IOS_LogDocuments()
{
	NSString* docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
	NSArray* items = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:docs error:nil];
	printf("Documents: %s, %d items\n", docs.UTF8String, (int)items.count);
	for (NSString* name in items) printf("  %s\n", name.UTF8String);
	fflush(stdout);
}
