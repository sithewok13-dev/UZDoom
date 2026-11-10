// The CAMERA's iOS side (from the owner's Raze port's iosPhotos.mm, itself
// after the Jedi Knight and Serious Sam ports' photos):
// - the photos: the touch buttons drawn onto the status-bar shot, then saved
//   to Photos (add-only access, asked the first time), or to the app's Files
//   folder (Screenshots) if that access is off or saving fails;
// - the flash, the shutter sound and a tap as each is taken;
// - CAMERA's options (the tray's CAMERA held): PHOTO kinds, SELFIES, FREEZE,
//   remembered in NSUserDefaults under the other ports' keys;
// - Action Freeze's controls: one finger turns the camera round, two move it
//   and pinch to zoom; the shutter takes a photo of the view, X goes back.
// Messages go on the overlay's message line. On the main thread, which is
// the game's. Built with ARC (src/CMakeLists.txt).

#import <UIKit/UIKit.h>
#import <Photos/Photos.h>
#import <ImageIO/ImageIO.h>
#import <QuartzCore/QuartzCore.h>
#import <AudioToolbox/AudioToolbox.h>
#include <mach/mach.h>

#include "IOSCamera.h"
#include "IOSCapture.h"
#include "IOSTouch.h"

extern "C" int IOSGame_LastPictureRect(int r[6]);

// Remembered settings (the same keys as the other ports'): what a tap takes
// (IOSCAPTURE_* bits; never set or none: CLEAN), photos per selfie (1, 3, 5;
// never set: 3), whether FREEZE is on (never set: it is); and the FPS
// readout's (the overlay's), which adds how the photos went to the message
#define IOSPHOTOS_KINDS_KEY @"iosTouchCameraKinds"
#define IOSPHOTOS_SELFIES_KEY @"iosTouchSelfieShots"
#define IOSPHOTOS_FREEZE_KEY @"iosTouchFreeze"
#define IOSPHOTOS_FPS_KEY @"iosTouchShowFps"
#define IOSPHOTOS_SHUTTER_SOUND 1108
#define IOSPHOTOS_LIGHT_SOUND 1104
#define IOSPHOTOS_MESSAGE_TIME 3.0
// Action Freeze: degrees turned per point dragged
#define IOSPHOTOS_FREEZE_ORBIT 0.3

static UIWindow *iosPhotos_Window(void)
{
	for (UIScene *scene in UIApplication.sharedApplication.connectedScenes)
	{
		if (![scene isKindOfClass:UIWindowScene.class]) continue;
		for (UIWindow *w in ((UIWindowScene *)scene).windows)
			if (w.isKeyWindow) return w;
	}
	return UIApplication.sharedApplication.windows.firstObject;
}

// ------------------------------------------------------------ settings

static void iosPhotos_LoadSettings(void)
{
	static BOOL loaded = NO;
	if (loaded) return;
	loaded = YES;
	NSUserDefaults *d = NSUserDefaults.standardUserDefaults;
	if ([d objectForKey:IOSPHOTOS_KINDS_KEY]) IOSCamera_SetKinds((int)[d integerForKey:IOSPHOTOS_KINDS_KEY]);
	if ([d objectForKey:IOSPHOTOS_SELFIES_KEY]) IOSCamera_SetSelfieShots((int)[d integerForKey:IOSPHOTOS_SELFIES_KEY]);
	if ([d objectForKey:IOSPHOTOS_FREEZE_KEY]) IOSCamera_SetFreezeOn([d boolForKey:IOSPHOTOS_FREEZE_KEY] ? 1 : 0);
}

static void iosPhotos_SaveSettings(void)
{
	NSUserDefaults *d = NSUserDefaults.standardUserDefaults;
	[d setInteger:IOSCamera_GetKinds() forKey:IOSPHOTOS_KINDS_KEY];
	[d setInteger:IOSCamera_GetSelfieShots() forKey:IOSPHOTOS_SELFIES_KEY];
	[d setBool:(IOSCamera_GetFreezeOn() ? YES : NO) forKey:IOSPHOTOS_FREEZE_KEY];
}

extern "C" void IOSPhotos_Init(void)
{
	iosPhotos_LoadSettings();
}

// ------------------------------------------------------------ the views

// The camera's own views, left out of a "HUD and buttons" photo
@interface IOSCamView : UIView
@end
@implementation IOSCamView
@end

static IOSCamView *_flash = nil;

// The camera's views go on top of everything in the window
static void iosPhotos_AddToWindow(UIView *v)
{
	UIWindow *w = iosPhotos_Window();
	if (!w) return;
	if (v.superview != w) [w addSubview:v];
	[w bringSubviewToFront:v];
}

static void iosPhotos_Flash(int mode)
{
	UIWindow *w = iosPhotos_Window();
	if (!w) return;
	if (!_flash)
	{
		_flash = [[IOSCamView alloc] initWithFrame:w.bounds];
		_flash.backgroundColor = UIColor.whiteColor;
		_flash.userInteractionEnabled = NO;
		_flash.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
	}
	_flash.frame = w.bounds;
	iosPhotos_AddToWindow(_flash);
	// lighter for each photo of a burst, and a selfie's after its first
	const int bLight = mode == IOSCAPTURE_MODE_BURST || (mode == IOSCAPTURE_MODE_SELFIE && IOSCamera_NumTaken() > 0);
	[_flash.layer removeAllAnimations];
	_flash.alpha = bLight ? 0.35 : 0.85;
	[UIView animateWithDuration:(bLight ? 0.12 : 0.3) delay:0 options:UIViewAnimationOptionCurveEaseOut
					 animations:^{ _flash.alpha = 0.0; } completion:nil];
	AudioServicesPlaySystemSound(bLight ? IOSPHOTOS_LIGHT_SOUND : IOSPHOTOS_SHUTTER_SOUND);
	static UIImpactFeedbackGenerator *tap = nil;
	if (!tap) tap = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
	[tap impactOccurred];
}

// A line for the player: the overlay's message line
extern "C" void IOSCamera_Message(const char *str)
{
	if (str) IOSTouch_ShowMessage(str, IOSPHOTOS_MESSAGE_TIME);
}

// ------------------------------------------------------------ saving
// (as the Serious Sam port's IOSPhotos.m)

static dispatch_queue_t iosPhotos_Queue(void)
{
	static dispatch_queue_t q = nil;
	static dispatch_once_t once;
	dispatch_once(&once, ^{
		q = dispatch_queue_create("doom.photos", dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INITIATED, 0));
	});
	return q;
}

static double iosPhotos_FootprintMB(void)
{
	task_vm_info_data_t info;
	mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
	if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) != KERN_SUCCESS) return -1.0;
	return (double)info.phys_footprint / (1024.0 * 1024.0);
}
static double _peakFootprintMB = 0;

static void iosPhotos_FreeBytes(void *info, const void *data, size_t size)
{
	free((void *)data);
}

// A CGImage over w x h RGBA bytes (top row first, opaque); it frees them
static CGImageRef iosPhotos_MakeImage(unsigned char *p, int w, int h)
{
	CGDataProviderRef prov = CGDataProviderCreateWithData(NULL, p, (size_t)w * h * 4, iosPhotos_FreeBytes);
	if (!prov)
	{
		free(p);
		return NULL;
	}
	CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
	CGImageRef img = CGImageCreate(w, h, 8, 32, (size_t)w * 4, cs, kCGBitmapByteOrderDefault | kCGImageAlphaNoneSkipLast, prov, NULL,
								   false, kCGRenderingIntentDefault);
	CGColorSpaceRelease(cs);
	CGDataProviderRelease(prov);
	return img;
}

static NSData *iosPhotos_Png(CGImageRef img)
{
	NSMutableData *data = [NSMutableData data];
	CGImageDestinationRef dst = CGImageDestinationCreateWithData((__bridge CFMutableDataRef)data, (__bridge CFStringRef) @"public.png", 1, NULL);
	if (!dst) return nil;
	CGImageDestinationAddImage(dst, img, NULL);
	BOOL bOk = CGImageDestinationFinalize(dst);
	CFRelease(dst);
	return bOk ? data : nil;
}

// "DOOM 2026-11-07 14.05.09", "-2", "-3"... for more in the same second
static NSString *iosPhotos_BaseName(NSDate *when)
{
	NSDateFormatter *f = [[NSDateFormatter alloc] init];
	f.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
	f.dateFormat = @"yyyy-MM-dd HH.mm.ss";
	NSString *app = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleDisplayName"];
	if (![app isKindOfClass:[NSString class]] || !app.length) app = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleName"];
	if (![app isKindOfClass:[NSString class]] || !app.length) app = @"DOOM";
	NSString *base = [NSString stringWithFormat:@"%@ %@", app, [f stringFromDate:when]];
	static NSString *lastBase = nil;
	static int sameSecond = 0;
	if (lastBase && [lastBase isEqualToString:base]) sameSecond++;
	else
	{
		lastBase = base;
		sameSecond = 1;
	}
	return sameSecond > 1 ? [NSString stringWithFormat:@"%@-%d", base, sameSecond] : base;
}

// One message once nothing more is being saved and no burst is going
static int _numPending = 0;
static int _bBatch = 0;
static int _numToPhotos = 0, _numToFiles = 0, _numFailed = 0;
static int _bPhotosOff = 0;
static int _burstShot = 0;

static void iosPhotos_Report(void)
{
	char msg[160];
	int n = 0;
	if (_numPending > 0 || _bBatch) return;
	msg[0] = 0;
	if (_numToPhotos) n += snprintf(msg + n, sizeof(msg) - n, _numToPhotos == 1 ? "1 photo saved" : "%d photos saved", _numToPhotos);
	if (_numToFiles && n < (int)sizeof(msg))
		n += snprintf(msg + n, sizeof(msg) - n, "%s%d saved to Files (%s)", n ? ", " : "", _numToFiles,
					  _bPhotosOff ? "Photos access is off" : "Photos didn't take them");
	if (_numFailed && n < (int)sizeof(msg)) n += snprintf(msg + n, sizeof(msg) - n, "%s%d couldn't be saved", n ? ", " : "", _numFailed);
	if (!msg[0]) return;
	IOSCaptureStats st;
	IOSCapture_TakeStats(&st);
	if ([NSUserDefaults.standardUserDefaults boolForKey:IOSPHOTOS_FPS_KEY] && n < (int)sizeof(msg))
	{
		n += snprintf(msg + n, sizeof(msg) - n, " [read %.0f ms, save %.2f s, held %.0f MB, app %.0f MB", st.readSeconds * 1000.0, st.saveSeconds,
					  (double)st.peakBytes / (1024.0 * 1024.0), _peakFootprintMB);
		if (st.freezeSeconds > 0 && n < (int)sizeof(msg)) n += snprintf(msg + n, sizeof(msg) - n, ", freeze %.2f s", st.freezeSeconds);
		if (n < (int)sizeof(msg)) snprintf(msg + n, sizeof(msg) - n, "]");
	}
	_peakFootprintMB = 0;
	IOSCamera_Message(msg);
	_numToPhotos = _numToFiles = _numFailed = 0;
	_bPhotosOff = 0;
}

static void iosPhotos_Finish(int toPhotos, int toFiles, int failed, int bPhotosOff, long bytes, double t0)
{
	const double mb = iosPhotos_FootprintMB();
	dispatch_async(dispatch_get_main_queue(), ^{
		if (mb > _peakFootprintMB) _peakFootprintMB = mb;
		_numToPhotos += toPhotos;
		_numToFiles += toFiles;
		_numFailed += failed;
		if (bPhotosOff) _bPhotosOff = 1;
		if (_numPending > 0) _numPending--;
		IOSCapture_Done(bytes, CACurrentMediaTime() - t0);
		iosPhotos_Report();
	});
}

extern "C" void IOSPhotos_SetBatch(int bOn)
{
	_bBatch = bOn != 0;
	if (bOn) _burstShot = 0;
	iosPhotos_Report();
}

static int iosPhotos_SaveToFiles(NSArray<NSData *> *aData, NSArray<NSString *> *aName)
{
	NSString *docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
	if (!docs) return 0;
	NSString *dir = [docs stringByAppendingPathComponent:@"Screenshots"];
	[[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
	int n = 0;
	for (NSUInteger i = 0; i < aData.count; i++)
		if ([aData[i] writeToFile:[dir stringByAppendingPathComponent:aName[i]] atomically:YES]) n++;
	return n;
}

static void iosPhotos_Save(NSArray<NSData *> *aData, NSArray<NSString *> *aName, NSDate *when, int numBad, long bytes, double t0)
{
	const int n = (int)aData.count;
	PHAuthorizationStatus st = [PHPhotoLibrary authorizationStatusForAccessLevel:PHAccessLevelAddOnly];
	if (st == PHAuthorizationStatusNotDetermined)
	{
		// the first photo: the system asks, then it is saved as answered
		[PHPhotoLibrary requestAuthorizationForAccessLevel:PHAccessLevelAddOnly handler:^(PHAuthorizationStatus) {
			dispatch_async(iosPhotos_Queue(), ^{ iosPhotos_Save(aData, aName, when, numBad, bytes, t0); });
		}];
		return;
	}
	if (st != PHAuthorizationStatusAuthorized && st != PHAuthorizationStatusLimited)
	{
		int k = iosPhotos_SaveToFiles(aData, aName);
		iosPhotos_Finish(0, k, n - k + numBad, 1, bytes, t0);
		return;
	}
	[[PHPhotoLibrary sharedPhotoLibrary] performChanges:^{
		for (NSUInteger i = 0; i < aData.count; i++)
		{
			PHAssetCreationRequest *req = [PHAssetCreationRequest creationRequestForAsset];
			PHAssetResourceCreationOptions *opt = [[PHAssetResourceCreationOptions alloc] init];
			opt.originalFilename = aName[i];
			[req addResourceWithType:PHAssetResourceTypePhoto data:aData[i] options:opt];
			req.creationDate = when;
		}
	} completionHandler:^(BOOL bOk, NSError *err) {
		if (bOk) iosPhotos_Finish(n, 0, numBad, 0, bytes, t0);
		else
		{
			int k = iosPhotos_SaveToFiles(aData, aName);
			iosPhotos_Finish(0, k, n - k + numBad, 0, bytes, t0);
		}
	}];
}

static BOOL iosPhotos_IsGL(UIView *v)
{
	static Class metal = NSClassFromString(@"CAMetalLayer");
	return [v.layer isKindOfClass:[CAEAGLLayer class]] || (metal && [v.layer isKindOfClass:metal]);
}

static BOOL iosPhotos_HoldsGL(UIView *v)
{
	if (iosPhotos_IsGL(v)) return YES;
	for (UIView *s in v.subviews)
		if (iosPhotos_HoldsGL(s)) return YES;
	return NO;
}

// The window's views as the screen shows them over the game, without the
// game's own (GL) view and the camera's views: the touch buttons and
// anything else drawn over the game
static void iosPhotos_DrawViews(UIView *v, UIView *root, CGContextRef c)
{
	if (v.hidden || v.alpha < 0.01 || [v isKindOfClass:[IOSCamView class]]) return;
	if (iosPhotos_HoldsGL(v))
	{
		for (UIView *s in v.subviews) iosPhotos_DrawViews(s, root, c);
		return;
	}
	const CGRect r = [v convertRect:v.bounds toView:root];
	CGContextSaveGState(c);
	CGContextTranslateCTM(c, r.origin.x, r.origin.y);
	[v.layer renderInContext:c];
	CGContextRestoreGState(c);
}

extern "C" void IOSPhotos_SaveCapture(unsigned char *pClean, int cleanW, int cleanH, unsigned char *pHud, int w, int h, int kinds,
									  int mode)
{
	const int bBurst = mode == IOSCAPTURE_MODE_BURST;
	const long bytes = (long)cleanW * cleanH * 4 * (pClean ? 1 : 0) + (long)w * h * 4 * (pHud ? ((kinds & IOSCAPTURE_HUDBTNS) ? 2 : 1) : 0);
	const double t0 = CACurrentMediaTime();
	@autoreleasepool
	{
		_numPending++;
		CGImageRef clean = pClean ? iosPhotos_MakeImage(pClean, cleanW, cleanH) : NULL;
		CGImageRef hud = pHud ? iosPhotos_MakeImage(pHud, w, h) : NULL;
		CGImageRef hudBtns = NULL;
		UIWindow *win = iosPhotos_Window();
		int pic[6];
		if (hud && (kinds & IOSCAPTURE_HUDBTNS) && win && win.bounds.size.width > 0 && IOSGame_LastPictureRect(pic) && pic[2] > 0 && pic[3] > 0)
		{
			// the touch buttons over the game's picture, as the player sees
			// them (UIKit views aren't in the game's frame): the picture's
			// place in the window, in points
			const CGFloat k = win.bounds.size.width / pic[4];
			const CGRect r = CGRectMake(pic[0] * k, pic[1] * k, pic[2] * k, pic[3] * k);
			UIGraphicsImageRendererFormat *fmt = [UIGraphicsImageRendererFormat preferredFormat];
			fmt.scale = 1.0;
			fmt.opaque = YES;
			fmt.preferredRange = UIGraphicsImageRendererFormatRangeStandard;
			UIGraphicsImageRenderer *rr = [[UIGraphicsImageRenderer alloc] initWithSize:CGSizeMake(w, h) format:fmt];
			UIImage *out = [rr imageWithActions:^(UIGraphicsImageRendererContext *ctx) {
				CGContextRef c = ctx.CGContext;
				[[UIImage imageWithCGImage:hud] drawInRect:CGRectMake(0, 0, w, h)];
				CGContextSaveGState(c);
				CGContextScaleCTM(c, w / r.size.width, h / r.size.height);
				CGContextTranslateCTM(c, -r.origin.x, -r.origin.y);
				for (UIView *v in win.subviews) iosPhotos_DrawViews(v, win, c);
				CGContextRestoreGState(c);
			}];
			hudBtns = CGImageRetain(out.CGImage);
		}

		CGImageRef aImg[3] = { NULL, NULL, NULL };
		int n = 0, numMissing = 0;
		if (kinds & IOSCAPTURE_CLEAN) { if (clean) aImg[0] = CGImageRetain(clean); else numMissing++; }
		if (kinds & IOSCAPTURE_HUD) { if (hud) aImg[1] = CGImageRetain(hud); else numMissing++; }
		if (kinds & IOSCAPTURE_HUDBTNS) { if (hudBtns) aImg[2] = CGImageRetain(hudBtns); else numMissing++; }
		CGImageRelease(clean);
		CGImageRelease(hud);
		CGImageRelease(hudBtns);
		for (int i = 0; i < 3; i++) n += aImg[i] != NULL;
		if (!n)
		{
			iosPhotos_Finish(0, 0, numMissing ? numMissing : 1, 0, bytes, t0);
			return;
		}

		// the flash, the shutter and a tap, now the frame is taken
		iosPhotos_Flash(mode);

		NSDate *when = [NSDate date];
		NSString *base = iosPhotos_BaseName(when);
		if (bBurst) base = [base stringByAppendingFormat:@" burst %02d", ++_burstShot];
		else if (mode == IOSCAPTURE_MODE_SELFIE && _bBatch) base = [base stringByAppendingFormat:@" selfie %d", ++_burstShot];
		CGImageRef img0 = aImg[0], img1 = aImg[1], img2 = aImg[2];
		const BOOL bNumbered = bBurst || (mode == IOSCAPTURE_MODE_SELFIE && _bBatch);
		dispatch_async(iosPhotos_Queue(), ^{
			NSMutableArray<NSData *> *aData = [NSMutableArray array];
			NSMutableArray<NSString *> *aName = [NSMutableArray array];
			int numBad = numMissing;
			CGImageRef aJob[3] = { img0, img1, img2 };
			NSString *aJobSuffix[3] = { @"clean", @"hud", @"hud+buttons" };
			for (int i = 0; i < 3; i++)
			{
				if (!aJob[i]) continue;
				@autoreleasepool
				{
					NSData *png = iosPhotos_Png(aJob[i]);
					if (png)
					{
						[aData addObject:png];
						[aName addObject:bNumbered ? [base stringByAppendingString:@".png"]
												   : [NSString stringWithFormat:@"%@ %@.png", base,
																				mode == IOSCAPTURE_MODE_SELFIE ? @"selfie" : aJobSuffix[i]]];
					}
					else numBad++;
				}
				CGImageRelease(aJob[i]);
			}
			if (!aData.count)
			{
				iosPhotos_Finish(0, 0, numBad, 0, bytes, t0);
				return;
			}
			iosPhotos_Save(aData, aName, when, numBad, bytes, t0);
		});
	}
}

// ------------------------------------------------------------ options

static NSString *const kRowNames[] = { @"CLEAN", @"HUD", @"HUD +\nBUTTONS", @"1", @"3", @"5", @"FREEZE" };
enum { OPT_CLEAN, OPT_HUD, OPT_HUDBTNS, OPT_SHOTS1, OPT_SHOTS3, OPT_SHOTS5, OPT_FREEZE, OPT_NUM };

@interface IOSCamOptions : IOSCamView
@end

@implementation IOSCamOptions
{
	UIButton *aBtn[OPT_NUM];
	UIView *panel;
}

- (instancetype)initWithFrame:(CGRect)frame
{
	self = [super initWithFrame:frame];
	self.backgroundColor = [UIColor colorWithWhite:0 alpha:0.25];
	panel = [[UIView alloc] init];
	panel.backgroundColor = [UIColor colorWithWhite:0.08 alpha:0.92];
	panel.layer.cornerRadius = 14;
	[self addSubview:panel];
	NSString *aHead[3] = { @"PHOTO", @"SELFIES", @"ACTION FREEZE" };
	for (int i = 0; i < 3; i++)
	{
		UILabel *l = [[UILabel alloc] init];
		l.text = aHead[i];
		l.tag = 100 + i;
		l.font = [UIFont boldSystemFontOfSize:12];
		l.textColor = [UIColor colorWithWhite:1 alpha:0.7];
		[panel addSubview:l];
	}
	for (int i = 0; i < OPT_NUM; i++)
	{
		UIButton *b = [UIButton buttonWithType:UIButtonTypeCustom];
		[b setTitle:kRowNames[i] forState:UIControlStateNormal];
		b.titleLabel.font = [UIFont boldSystemFontOfSize:13];
		b.titleLabel.numberOfLines = 2;
		b.titleLabel.textAlignment = NSTextAlignmentCenter;
		b.layer.cornerRadius = 8;
		b.layer.borderWidth = 1.5;
		b.tag = i;
		[b addTarget:self action:@selector(tapped:) forControlEvents:UIControlEventTouchUpInside];
		aBtn[i] = b;
		[panel addSubview:b];
	}
	UIButton *x = [UIButton buttonWithType:UIButtonTypeCustom];
	[x setTitle:@"✕" forState:UIControlStateNormal];
	x.titleLabel.font = [UIFont boldSystemFontOfSize:18];
	x.tag = 99;
	[x addTarget:self action:@selector(close) forControlEvents:UIControlEventTouchUpInside];
	[panel addSubview:x];
	[self refresh];
	return self;
}

- (void)layoutSubviews
{
	[super layoutSubviews];
	const CGFloat bw = 74, bh = 40, gap = 8, pad = 14, head = 18;
	const CGFloat w = pad * 2 + bw * 3 + gap * 2;
	const CGFloat h = pad + (head + bh + gap) * 3 + pad;
	panel.bounds = CGRectMake(0, 0, w, h);
	panel.center = CGPointMake(self.bounds.size.width / 2, self.bounds.size.height / 2);
	for (int row = 0; row < 3; row++)
	{
		const CGFloat y = pad + row * (head + bh + gap);
		[panel viewWithTag:100 + row].frame = CGRectMake(pad, y, w - 2 * pad, head);
	}
	for (int i = 0; i < OPT_NUM; i++)
	{
		const int row = i < 3 ? 0 : i < 6 ? 1 : 2, col = i < 6 ? i % 3 : 0;
		const CGFloat y = pad + row * (head + bh + gap) + head;
		aBtn[i].frame = CGRectMake(pad + col * (bw + gap), y, i == OPT_FREEZE ? bw * 3 + gap * 2 : bw, bh);
	}
	[panel viewWithTag:99].frame = CGRectMake(w - 40, 2, 38, 34);
}

- (BOOL)isOn:(int)i
{
	switch (i)
	{
	case OPT_CLEAN: return (IOSCamera_GetKinds() & IOSCAPTURE_CLEAN) != 0;
	case OPT_HUD: return (IOSCamera_GetKinds() & IOSCAPTURE_HUD) != 0;
	case OPT_HUDBTNS: return (IOSCamera_GetKinds() & IOSCAPTURE_HUDBTNS) != 0;
	case OPT_SHOTS1: return IOSCamera_GetSelfieShots() == 1;
	case OPT_SHOTS3: return IOSCamera_GetSelfieShots() == 3;
	case OPT_SHOTS5: return IOSCamera_GetSelfieShots() == 5;
	default: return IOSCamera_GetFreezeOn() != 0;
	}
}

- (void)refresh
{
	UIColor *gold = [UIColor colorWithRed:1.0 green:0.8 blue:0.3 alpha:1];
	for (int i = 0; i < OPT_NUM; i++)
	{
		const BOOL on = [self isOn:i];
		aBtn[i].backgroundColor = on ? [gold colorWithAlphaComponent:0.3] : [UIColor colorWithWhite:1 alpha:0.06];
		aBtn[i].layer.borderColor = (on ? gold : [UIColor colorWithWhite:1 alpha:0.25]).CGColor;
		[aBtn[i] setTitleColor:(on ? UIColor.whiteColor : [UIColor colorWithWhite:1 alpha:0.5]) forState:UIControlStateNormal];
	}
	[aBtn[OPT_FREEZE] setTitle:(IOSCamera_GetFreezeOn() ? @"ON (slide down on CAMERA)" : @"OFF") forState:UIControlStateNormal];
}

- (void)tapped:(UIButton *)b
{
	const int i = (int)b.tag;
	static const int aBit[3] = { IOSCAPTURE_CLEAN, IOSCAPTURE_HUD, IOSCAPTURE_HUDBTNS };
	if (i <= OPT_HUDBTNS)
	{
		const int k = IOSCamera_GetKinds() ^ aBit[i];
		if (k) IOSCamera_SetKinds(k); // (at least one kind stays ticked)
	}
	else if (i <= OPT_SHOTS5) IOSCamera_SetSelfieShots(i == OPT_SHOTS1 ? 1 : i == OPT_SHOTS3 ? 3 : 5);
	else IOSCamera_SetFreezeOn(!IOSCamera_GetFreezeOn());
	iosPhotos_SaveSettings();
	[[UISelectionFeedbackGenerator new] selectionChanged];
	[self refresh];
}

- (void)close
{
	[self removeFromSuperview];
}

// a touch off the panel closes it
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
	CGPoint p = [touches.anyObject locationInView:self];
	if (!CGRectContainsPoint(panel.frame, p)) [self close];
}
@end

static IOSCamOptions *_options = nil;

extern "C" void IOSCamera_ShowOptions(void)
{
	iosPhotos_LoadSettings();
	UIWindow *w = iosPhotos_Window();
	if (!w) return;
	if (!_options) _options = [[IOSCamOptions alloc] initWithFrame:w.bounds];
	_options.frame = w.bounds;
	[_options refresh];
	iosPhotos_AddToWindow(_options);
}

// ------------------------------------------------------------ Action Freeze

@interface IOSCamFreeze : IOSCamView
@end

@implementation IOSCamFreeze
{
	UIButton *close, *shutter;
	UILabel *zoomLabel; // "2.5×" while the lens is zoomed in
	CGPoint lastOne, lastMid;
	CGFloat lastSpan;
	int lastCount;
}

- (instancetype)initWithFrame:(CGRect)frame
{
	self = [super initWithFrame:frame];
	self.multipleTouchEnabled = YES;
	self.backgroundColor = UIColor.clearColor;
	close = [UIButton buttonWithType:UIButtonTypeCustom];
	[close setTitle:@"✕" forState:UIControlStateNormal];
	close.titleLabel.font = [UIFont boldSystemFontOfSize:20];
	close.backgroundColor = [UIColor colorWithWhite:0 alpha:0.5];
	close.layer.cornerRadius = 22;
	close.layer.borderWidth = 1.5;
	close.layer.borderColor = [UIColor colorWithWhite:1 alpha:0.6].CGColor;
	[close addTarget:self action:@selector(end) forControlEvents:UIControlEventTouchUpInside];
	[self addSubview:close];
	shutter = [UIButton buttonWithType:UIButtonTypeCustom];
	shutter.backgroundColor = [UIColor colorWithWhite:1 alpha:0.85];
	shutter.layer.cornerRadius = 30;
	shutter.layer.borderWidth = 4;
	shutter.layer.borderColor = [UIColor colorWithWhite:0 alpha:0.35].CGColor;
	[shutter addTarget:self action:@selector(shoot) forControlEvents:UIControlEventTouchUpInside];
	[self addSubview:shutter];
	zoomLabel = [[UILabel alloc] init];
	zoomLabel.textAlignment = NSTextAlignmentCenter;
	zoomLabel.textColor = [UIColor colorWithRed:1.0 green:0.85 blue:0.35 alpha:1.0];
	zoomLabel.font = [UIFont monospacedDigitSystemFontOfSize:15 weight:UIFontWeightBold];
	zoomLabel.backgroundColor = [UIColor colorWithWhite:0 alpha:0.45];
	zoomLabel.layer.cornerRadius = 13;
	zoomLabel.clipsToBounds = YES;
	zoomLabel.userInteractionEnabled = NO;
	zoomLabel.hidden = YES;
	[self addSubview:zoomLabel];
	return self;
}

- (void)layoutSubviews
{
	[super layoutSubviews];
	const UIEdgeInsets in = self.safeAreaInsets;
	close.frame = CGRectMake(self.bounds.size.width - in.right - 16 - 44, in.top + 12, 44, 44);
	shutter.frame = CGRectMake(self.bounds.size.width - in.right - 24 - 60, self.bounds.size.height / 2 - 30, 60, 60);
	zoomLabel.frame = CGRectMake(CGRectGetMidX(shutter.frame) - 32, CGRectGetMaxY(shutter.frame) + 12, 64, 26);
}

- (void)end
{
	IOSCamera_FreezeEnd();
}

- (void)shoot
{
	IOSCamera_Photo();
}

// The touches now: one turns, two move and pinch
- (void)take:(UIEvent *)event moved:(BOOL)moved
{
	NSMutableArray<UITouch *> *a = [NSMutableArray array];
	for (UITouch *t in [event touchesForView:self])
		if (t.phase != UITouchPhaseEnded && t.phase != UITouchPhaseCancelled) [a addObject:t];
	const int count = (int)a.count;
	if (count == 1)
	{
		CGPoint p = [a[0] locationInView:self];
		if (moved && lastCount == 1)
			IOSCamera_FreezeOrbit((float)(-(p.x - lastOne.x) * IOSPHOTOS_FREEZE_ORBIT), (float)((p.y - lastOne.y) * IOSPHOTOS_FREEZE_ORBIT));
		lastOne = p;
	}
	else if (count >= 2)
	{
		CGPoint p0 = [a[0] locationInView:self], p1 = [a[1] locationInView:self];
		CGPoint mid = CGPointMake((p0.x + p1.x) / 2, (p0.y + p1.y) / 2);
		CGFloat span = hypot(p1.x - p0.x, p1.y - p0.y);
		if (moved && lastCount >= 2)
		{
			const CGFloat hgt = fmax(self.bounds.size.height, 1);
			IOSCamera_FreezePan((float)((mid.x - lastMid.x) / hgt), (float)((mid.y - lastMid.y) / hgt));
			if (lastSpan > 10 && span > 10)
			{
				IOSCamera_FreezeZoom((float)(span / lastSpan));
				const float z = IOSCamera_FreezeZoomNow();
				zoomLabel.hidden = z < 1.05f;
				zoomLabel.text = [NSString stringWithFormat:@"%.1f×", z];
			}
		}
		lastMid = mid;
		lastSpan = span;
	}
	lastCount = count;
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { [self take:event moved:NO]; }
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { [self take:event moved:YES]; }
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { [self take:event moved:NO]; }
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { [self take:event moved:NO]; }
@end

static IOSCamFreeze *_freeze = nil;

extern "C" void IOSPhotos_FreezeShown(int bOn)
{
	UIWindow *w = iosPhotos_Window();
	if (!bOn || !w)
	{
		[_freeze removeFromSuperview];
		return;
	}
	if (!_freeze) _freeze = [[IOSCamFreeze alloc] initWithFrame:w.bounds];
	_freeze.frame = w.bounds;
	iosPhotos_AddToWindow(_freeze);
}
