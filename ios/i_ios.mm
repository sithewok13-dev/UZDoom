/*
** i_ios.mm: the iOS pieces the SDL backend expects from the Mac files
** (from the owner's Raze port, ios/i_ios.mm, without its LAN games)
**
** - I_PickIWad_Cocoa: the game picker (ios/IOSPicker.mm), a plain list if
**   that can't be shown
** - Mac_I_FatalError: an alert with the error before the app ends
** - the log in Files (doom-log.txt), MoltenVK, the game window's scene
**
** The picker and the alert run before or outside the game loop, so they
** show UIKit and spin the main run loop until they are answered.
*/

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

// The game picker (i_system.cpp's I_PickIWad on iOS): the picker's cards,
// a plain list if they can't be shown. An add-on's files are loaded with
// the game (-file). false: none picked (the app ends).
bool I_PickIWad_IOS(FStartupSelectionInfo& info)
{
	const TArray<WadStuff>& wads = *info.Wads;
	const int numwads = (int)wads.Size();
	TArray<const char*> names;
	for (int i = 0; i < numwads; i++) names.Push(wads[i].Name.GetChars());
	static char files[4096];
	int chosen = IOSPicker_Run(names.Data(), numwads, info.DefaultIWAD, files, sizeof(files));
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
#define GLES_SILENCE_DEPRECATION 1
#include <OpenGLES/ES3/gl.h>
#import <OpenGLES/EAGL.h>
#include <vector>
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
	for (UIWindow* win in UIApplication.sharedApplication.windows)
	{
		printf("  window %s: %s%s%s level %.0f, %.0fx%.0f, root %s\n", class_getName(win.class),
			win.isKeyWindow ? "key, " : "", win.hidden ? "hidden" : "shown", win.windowScene ? ", in a scene" : ", NO scene", win.windowLevel,
			win.bounds.size.width, win.bounds.size.height,
			win.rootViewController ? class_getName(win.rootViewController.class) : "none");
	}
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

static uint64_t IOS_Ns()
{
	return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}
static uint64_t iosPresentNs = 0;

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
