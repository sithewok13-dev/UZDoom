// The game picker: see IOSPicker.h. (Objective-C++ with ARC; no engine
// headers.) From the owner's Raze port (ios/IOSPicker.mm), for DOOM.
//
// A card for each main game found, side by side, each in its own colours
// with an emblem, its name and subtitle, "LAST" on the one played last, and
// how many add-ons it has. A tap starts the game (or the add-on of it played
// last); held 0.5 s (a ring runs round the card, then a thump) its add-ons
// open: the main game and each add-on, to tap. An add-on is its main game
// with the add-on's files loaded (-file).

#import <UIKit/UIKit.h>
#include <stdio.h>
#include <string.h>
#include "IOSPicker.h"

// ---------------------------------------------------------------- the games

// The four cards (and "Other" for any other game UZDoom finds)
enum { FAM_DOOM, FAM_DOOM2, FAM_TNT, FAM_PLUTONIA, FAM_OTHER, FAM_COUNT };

typedef struct
{
	const char* title;	// the card's name
	const char* subtitle;	// under it
	const char* symbol;	// an SF Symbol, the card's emblem
	CGFloat top[3], bottom[3];	// the card's gradient
} Family;

static const Family families[FAM_COUNT] =
{
	{ "DOOM", "The Ultimate DOOM", "flame.fill", { 0.78, 0.12, 0.05 }, { 0.22, 0.03, 0.02 } },
	{ "DOOM II", "Hell on Earth", "globe.europe.africa.fill", { 0.62, 0.32, 0.08 }, { 0.18, 0.07, 0.02 } },
	{ "TNT", "Evilution", "burst.fill", { 0.18, 0.45, 0.20 }, { 0.04, 0.12, 0.05 } },
	{ "PLUTONIA", "The Plutonia Experiment", "atom", { 0.42, 0.20, 0.55 }, { 0.10, 0.04, 0.15 } },
	{ "OTHER", "More games", "square.stack.3d.up.fill", { 0.35, 0.35, 0.38 }, { 0.08, 0.08, 0.09 } },
};

// Which card a game UZDoom found goes on, by its name
static int FamilyOfName(NSString* name)
{
	NSString* n = name.lowercaseString;
	if ([n containsString:@"tnt"] || [n containsString:@"evilution"]) return FAM_TNT;
	if ([n containsString:@"plutonia"]) return FAM_PLUTONIA;
	if ([n containsString:@"freedoom"] || [n containsString:@"freedm"]) return FAM_OTHER;
	if ([n containsString:@"doom 2"] || [n containsString:@"doom ii"] || [n containsString:@"hell on earth"]) return FAM_DOOM2;
	if ([n containsString:@"doom"]) return FAM_DOOM;
	return FAM_OTHER;
}

// The add-ons, each loaded over its card's main game (the files in load order)
typedef struct
{
	int family;
	const char* name;
	const char* files[3];
} Addon;

static const Addon addons[] =
{
	{ FAM_DOOM, "SIGIL", { "sigil.wad" } },
	{ FAM_DOOM, "SIGIL II", { "sigil2.wad" } },
	{ FAM_DOOM2, "No Rest for the Living", { "nerve.wad" } },
	{ FAM_DOOM2, "The Master Levels", { "masterlevels.wad" } },
	{ FAM_DOOM2, "Legacy of Rust", { "id1-res.wad", "id1.wad" } },
};

@interface IOSPickEntry : NSObject
@property (nonatomic) int iwad;		// the game UZDoom found (its index)
@property (nonatomic) int family;
@property (nonatomic) BOOL addon;
@property (nonatomic, copy) NSString* label;	// in the add-ons panel
@property (nonatomic, copy) NSString* key;	// remembered as "played last"
@property (nonatomic, copy) NSArray<NSString*>* files;	// an add-on's, full paths
@end
@implementation IOSPickEntry
@end

static NSArray<IOSPickEntry*>* _entries = nil;
static NSString* _lastKey = nil;
static NSString* const kLastKey = @"IOSPickerLast";

// The Documents folder's files by their lower-case names
static NSDictionary<NSString*, NSString*>* DocumentFiles()
{
	NSString* docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
	NSMutableDictionary* d = [NSMutableDictionary dictionary];
	for (NSString* f in [[NSFileManager defaultManager] contentsOfDirectoryAtPath:docs error:nil])
		d[f.lowercaseString] = [docs stringByAppendingPathComponent:f];
	return d;
}

static void MakeEntries(const char* const* names, int ctNames)
{
	NSMutableArray<IOSPickEntry*>* a = [NSMutableArray array];
	int mainOf[FAM_COUNT];
	for (int f = 0; f < FAM_COUNT; f++) mainOf[f] = -1;
	for (int i = 0; i < ctNames; i++)
	{
		IOSPickEntry* e = [IOSPickEntry new];
		NSString* name = [NSString stringWithUTF8String:names[i] ? names[i] : "?"] ?: @"?";
		e.iwad = i;
		e.family = FamilyOfName(name);
		e.addon = NO;
		e.label = name;
		e.key = name;
		e.files = @[];
		[a addObject:e];
		// (an add-on goes over the re-release's copy if there is one)
		const int f = e.family;
		if (mainOf[f] < 0 || ([name.lowercaseString containsString:@"kex"] && ![@(names[mainOf[f]]).lowercaseString containsString:@"kex"]))
			mainOf[f] = i;
	}
	NSDictionary<NSString*, NSString*>* docs = DocumentFiles();
	for (size_t k = 0; k < sizeof(addons) / sizeof(addons[0]); k++)
	{
		const Addon* ad = &addons[k];
		if (mainOf[ad->family] < 0) continue;
		NSMutableArray<NSString*>* files = [NSMutableArray array];
		BOOL all = YES;
		for (int j = 0; j < 3 && ad->files[j]; j++)
		{
			NSString* path = docs[@(ad->files[j])];
			if (!path) { all = NO; break; }
			[files addObject:path];
		}
		if (!all) continue;
		IOSPickEntry* e = [IOSPickEntry new];
		e.iwad = mainOf[ad->family];
		e.family = ad->family;
		e.addon = YES;
		e.label = @(ad->name);
		e.key = [@"addon:" stringByAppendingString:@(ad->name)];
		e.files = files;
		[a addObject:e];
	}
	_entries = a;
}

// A card's games: its main games first, then the add-ons
static NSArray<IOSPickEntry*>* GamesOf(int fam)
{
	NSMutableArray<IOSPickEntry*>* a = [NSMutableArray array];
	for (int pass = 0; pass < 2; pass++)
		for (IOSPickEntry* e in _entries)
			if (e.family == fam && e.addon == (pass == 1)) [a addObject:e];
	return a;
}

static BOOL IsLast(IOSPickEntry* e) { return _lastKey && [e.key isEqualToString:_lastKey]; }

// What a tap on a card starts: the one of its games played last, else its
// first main game
static IOSPickEntry* TapTarget(int fam)
{
	NSArray<IOSPickEntry*>* g = GamesOf(fam);
	for (IOSPickEntry* e in g) if (IsLast(e)) return e;
	return g.count && !g[0].addon ? g[0] : nil;
}

static UIColor* RGB(const CGFloat c[3], CGFloat a = 1.0)
{
	return [UIColor colorWithRed:c[0] green:c[1] blue:c[2] alpha:a];
}

// ---------------------------------------------------------------- a card

#define HOLD_TIME 0.5
#define TAP_SLOP 12.0 // points a finger may move and still tap

@interface IOSGameCard : UIControl
@property (nonatomic) int family;
@property (nonatomic, copy) void (^onTap)(int family);
@property (nonatomic, copy) void (^onHold)(int family);
@end

@implementation IOSGameCard
{
	CAGradientLayer* gradient;
	CAShapeLayer* ring;
	NSTimer* holdTimer;
	CGPoint down;
	BOOL fired;	// held long enough: its games opened
	BOOL moved;	// slid: no tap
}

- (instancetype)initWithFamily:(int)fam
{
	if (!(self = [super initWithFrame:CGRectZero])) return nil;
	_family = fam;
	const Family* f = &families[fam];
	IOSPickEntry* target = TapTarget(fam);
	const int ctMore = (int)GamesOf(fam).count - 1;

	self.layer.cornerRadius = 18;
	self.layer.borderWidth = 1.5;
	self.layer.borderColor = [UIColor colorWithWhite:1.0 alpha:0.18].CGColor;
	self.clipsToBounds = YES;

	gradient = [CAGradientLayer layer];
	gradient.colors = @[ (id)RGB(f->top).CGColor, (id)RGB(f->bottom).CGColor ];
	gradient.startPoint = CGPointMake(0.2, 0.0);
	gradient.endPoint = CGPointMake(0.8, 1.0);
	[self.layer addSublayer:gradient];

	// the emblem, big and faint behind the name, and small and bright above it
	UIImageSymbolConfiguration* big = [UIImageSymbolConfiguration configurationWithPointSize:120 weight:UIImageSymbolWeightBold];
	UIImageView* emblem = [[UIImageView alloc] initWithImage:[UIImage systemImageNamed:@(f->symbol) withConfiguration:big]];
	emblem.tintColor = [UIColor colorWithWhite:1.0 alpha:0.10];
	emblem.contentMode = UIViewContentModeScaleAspectFit;
	emblem.translatesAutoresizingMaskIntoConstraints = NO;
	[self addSubview:emblem];
	UIImageSymbolConfiguration* small = [UIImageSymbolConfiguration configurationWithPointSize:22 weight:UIImageSymbolWeightSemibold];
	UIImageView* badge = [[UIImageView alloc] initWithImage:[UIImage systemImageNamed:@(f->symbol) withConfiguration:small]];
	badge.tintColor = [UIColor colorWithWhite:1.0 alpha:0.9];
	badge.translatesAutoresizingMaskIntoConstraints = NO;
	[self addSubview:badge];

	UILabel* name = [[UILabel alloc] init];
	name.text = @(f->title);
	name.textColor = UIColor.whiteColor;
	name.font = [UIFont systemFontOfSize:24 weight:UIFontWeightHeavy];
	name.numberOfLines = 2;
	name.adjustsFontSizeToFitWidth = YES;
	name.minimumScaleFactor = 0.55;
	name.layer.shadowColor = UIColor.blackColor.CGColor;
	name.layer.shadowOpacity = 0.6;
	name.layer.shadowRadius = 3;
	name.layer.shadowOffset = CGSizeMake(0, 1);

	// what a tap starts (an add-on played last: its name)
	UILabel* sub = [[UILabel alloc] init];
	sub.text = (target && target.addon) ? target.label : (fam == FAM_OTHER && target ? target.label : @(f->subtitle));
	sub.textColor = [UIColor colorWithWhite:1.0 alpha:0.8];
	sub.font = [UIFont systemFontOfSize:13 weight:UIFontWeightSemibold];
	sub.numberOfLines = 2;
	sub.adjustsFontSizeToFitWidth = YES;
	sub.minimumScaleFactor = 0.7;

	UILabel* foot = [[UILabel alloc] init];
	foot.text = ctMore <= 0 ? @"" : [NSString stringWithFormat:@"HOLD · %d MORE", ctMore];
	foot.textColor = [UIColor colorWithRed:1.0 green:0.82 blue:0.45 alpha:1.0];
	foot.font = [UIFont systemFontOfSize:12 weight:UIFontWeightBold];
	foot.adjustsFontSizeToFitWidth = YES;
	foot.minimumScaleFactor = 0.7;

	UIStackView* text = [[UIStackView alloc] initWithArrangedSubviews:@[ name, sub, [UIView new], foot ]];
	text.axis = UILayoutConstraintAxisVertical;
	text.spacing = 4;
	text.userInteractionEnabled = NO;
	text.translatesAutoresizingMaskIntoConstraints = NO;
	[self addSubview:text];

	NSMutableArray* cons = [NSMutableArray arrayWithArray:@[
		[badge.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:14],
		[badge.topAnchor constraintEqualToAnchor:self.topAnchor constant:14],
		[emblem.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:24],
		[emblem.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:20],
		[emblem.widthAnchor constraintEqualToAnchor:self.widthAnchor multiplier:0.85],
		[emblem.heightAnchor constraintEqualToAnchor:emblem.widthAnchor],
		[text.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:14],
		[text.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-12],
		[text.topAnchor constraintEqualToAnchor:badge.bottomAnchor constant:10],
		[text.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:-12],
	]];
	if (target && IsLast(target))
	{
		UILabel* last = [[UILabel alloc] init];
		last.text = @" LAST ";
		last.font = [UIFont systemFontOfSize:10 weight:UIFontWeightHeavy];
		last.textColor = UIColor.blackColor;
		last.backgroundColor = [UIColor colorWithRed:1.0 green:0.82 blue:0.35 alpha:1.0];
		last.layer.cornerRadius = 7;
		last.clipsToBounds = YES;
		last.translatesAutoresizingMaskIntoConstraints = NO;
		[self addSubview:last];
		[cons addObjectsFromArray:@[
			[last.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-10],
			[last.centerYAnchor constraintEqualToAnchor:badge.centerYAnchor],
			[last.heightAnchor constraintEqualToConstant:18],
		]];
	}
	[NSLayoutConstraint activateConstraints:cons];

	// the hold's ring round the card's edge
	ring = [CAShapeLayer layer];
	ring.fillColor = UIColor.clearColor.CGColor;
	ring.strokeColor = [UIColor colorWithRed:1.0 green:0.78 blue:0.25 alpha:1.0].CGColor;
	ring.lineWidth = 5;
	ring.lineCap = kCALineCapRound;
	ring.strokeEnd = 0;
	[self.layer addSublayer:ring];
	return self;
}

- (void)layoutSubviews
{
	[super layoutSubviews];
	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	gradient.frame = self.bounds;
	ring.frame = self.bounds;
	ring.path = [UIBezierPath bezierPathWithRoundedRect:CGRectInset(self.bounds, 2.5, 2.5) cornerRadius:16].CGPath;
	[CATransaction commit];
}

- (void)setHighlighted:(BOOL)highlighted
{
	[super setHighlighted:highlighted];
	[UIView animateWithDuration:0.12 animations:^{
		self.transform = (highlighted && !self->moved) ? CGAffineTransformMakeScale(0.97, 0.97) : CGAffineTransformIdentity;
	}];
}

- (void)stopRing
{
	[holdTimer invalidate];
	holdTimer = nil;
	[ring removeAllAnimations];
	ring.strokeEnd = 0;
}

- (BOOL)beginTrackingWithTouch:(UITouch*)touch withEvent:(UIEvent*)event
{
	fired = NO;
	moved = NO;
	down = [touch locationInView:self];
	CABasicAnimation* a = [CABasicAnimation animationWithKeyPath:@"strokeEnd"];
	a.fromValue = @0;
	a.toValue = @1;
	a.duration = HOLD_TIME;
	a.fillMode = kCAFillModeForwards;
	a.removedOnCompletion = NO;
	[ring addAnimation:a forKey:@"hold"];
	__weak IOSGameCard* weakSelf = self;
	holdTimer = [NSTimer scheduledTimerWithTimeInterval:HOLD_TIME repeats:NO block:^(NSTimer* t) {
		IOSGameCard* card = weakSelf;
		if (!card) return;
		card->holdTimer = nil;
		card->fired = YES;
		[card stopRing];
		[[[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleMedium] impactOccurred];
		if (card.onHold) card.onHold(card.family);
	}];
	return YES;
}

- (BOOL)continueTrackingWithTouch:(UITouch*)touch withEvent:(UIEvent*)event
{
	// slid: neither a tap nor a hold
	const CGPoint p = [touch locationInView:self];
	if (!moved && hypot(p.x - down.x, p.y - down.y) > TAP_SLOP)
	{
		moved = YES;
		[self stopRing];
		self.highlighted = NO;
	}
	return !moved;
}

- (void)endTrackingWithTouch:(UITouch*)touch withEvent:(UIEvent*)event
{
	const BOOL tap = !fired && !moved && holdTimer != nil;
	[self stopRing];
	if (tap && self.onTap) self.onTap(self.family);
}

- (void)cancelTrackingWithEvent:(UIEvent*)event
{
	moved = YES;
	[self stopRing];
}
@end

// ---------------------------------------------------------------- the picker

@interface IOSPickerController : UIViewController
@property (nonatomic, copy) void (^done)(IOSPickEntry* picked); // nil: none
@end

@implementation IOSPickerController
{
	UIView* gamesBack;
	BOOL finished;
}

- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskLandscape; }

- (void)finish:(IOSPickEntry*)picked
{
	if (finished || picked == nil) return;
	finished = YES;
	if (self.done) self.done(picked);
}

- (void)viewDidLoad
{
	[super viewDidLoad];
	CAGradientLayer* back = [CAGradientLayer layer];
	back.colors = @[ (id)[UIColor colorWithRed:0.12 green:0.05 blue:0.04 alpha:1].CGColor,
	                 (id)[UIColor colorWithRed:0.03 green:0.02 blue:0.02 alpha:1].CGColor ];
	back.frame = CGRectMake(0, 0, 4000, 4000); // (any size the screen turns to)
	[self.view.layer addSublayer:back];
	self.view.backgroundColor = UIColor.blackColor;

	// DOOM  Choose a game
	UILabel* logo = [[UILabel alloc] init];
	logo.attributedText = [[NSAttributedString alloc] initWithString:@"DOOM" attributes:@{
		NSFontAttributeName: [UIFont systemFontOfSize:30 weight:UIFontWeightBlack],
		NSForegroundColorAttributeName: [UIColor colorWithRed:0.90 green:0.15 blue:0.08 alpha:1.0],
		NSKernAttributeName: @4 }];
	UILabel* heading = [[UILabel alloc] init];
	heading.text = @"Choose a game";
	heading.textColor = [UIColor colorWithWhite:1.0 alpha:0.9];
	heading.font = [UIFont systemFontOfSize:20 weight:UIFontWeightSemibold];
	UIStackView* top = [[UIStackView alloc] initWithArrangedSubviews:@[ logo, heading, [UIView new] ]];
	top.alignment = UIStackViewAlignmentLastBaseline;
	top.spacing = 16;

	// the cards: one for each game here, side by side, all on the screen
	UIStackView* row = [[UIStackView alloc] init];
	row.axis = UILayoutConstraintAxisHorizontal;
	row.spacing = 12;
	row.distribution = UIStackViewDistributionFillEqually;
	__weak IOSPickerController* weakSelf = self;
	for (int fam = 0; fam < FAM_COUNT; fam++)
	{
		if (TapTarget(fam) == nil) continue;
		IOSGameCard* card = [[IOSGameCard alloc] initWithFamily:fam];
		card.onTap = ^(int f) { [weakSelf finish:TapTarget(f)]; };
		card.onHold = ^(int f) { [weakSelf showGames:f]; };
		[row addArrangedSubview:card];
	}

	UILabel* hint = [[UILabel alloc] init];
	hint.text = @"Tap a game to play  ·  Hold it for its add-ons";
	hint.textColor = [UIColor colorWithWhite:1.0 alpha:0.5];
	hint.font = [UIFont systemFontOfSize:13 weight:UIFontWeightMedium];
	hint.adjustsFontSizeToFitWidth = YES;
	hint.minimumScaleFactor = 0.7;

	UIStackView* page = [[UIStackView alloc] initWithArrangedSubviews:@[ top, row, hint ]];
	page.axis = UILayoutConstraintAxisVertical;
	page.spacing = 14;
	page.translatesAutoresizingMaskIntoConstraints = NO;
	[self.view addSubview:page];

	UILayoutGuide* safe = self.view.safeAreaLayoutGuide;
	[NSLayoutConstraint activateConstraints:@[
		[page.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:20],
		[page.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-20],
		[page.topAnchor constraintEqualToAnchor:safe.topAnchor constant:14],
		[page.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor constant:-10],
	]];
}

// A card's games over the picker: its main games, then its add-ons
- (void)showGames:(int)fam
{
	[gamesBack removeFromSuperview];
	gamesBack = [[UIView alloc] initWithFrame:self.view.bounds];
	gamesBack.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
	gamesBack.backgroundColor = [UIColor colorWithWhite:0.0 alpha:0.7];
	gamesBack.alpha = 0;
	UITapGestureRecognizer* away = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(backTapped:)];
	[gamesBack addGestureRecognizer:away];
	[self.view addSubview:gamesBack];

	const Family* f = &families[fam];
	UIView* panel = [[UIView alloc] init];
	panel.backgroundColor = [UIColor colorWithRed:0.09 green:0.08 blue:0.08 alpha:0.98];
	panel.layer.cornerRadius = 20;
	panel.layer.borderWidth = 1.5;
	panel.layer.borderColor = RGB(f->top, 0.9).CGColor;
	panel.tag = 7;
	panel.translatesAutoresizingMaskIntoConstraints = NO;
	[gamesBack addSubview:panel];

	UILabel* head = [[UILabel alloc] init];
	head.text = @(f->title);
	head.font = [UIFont systemFontOfSize:20 weight:UIFontWeightHeavy];
	head.textColor = UIColor.whiteColor;
	UIButton* close = [UIButton buttonWithType:UIButtonTypeClose];
	[close addTarget:self action:@selector(closeGames) forControlEvents:UIControlEventTouchUpInside];
	UIStackView* headRow = [[UIStackView alloc] initWithArrangedSubviews:@[ head, [UIView new], close ]];
	headRow.alignment = UIStackViewAlignmentCenter;

	UIStackView* list = [[UIStackView alloc] init];
	list.axis = UILayoutConstraintAxisVertical;
	list.spacing = 8;
	NSArray<IOSPickEntry*>* games = GamesOf(fam);
	BOOL addonsHeader = NO;
	for (IOSPickEntry* e in games)
	{
		if (e.addon && !addonsHeader)
		{
			addonsHeader = YES;
			UILabel* h = [[UILabel alloc] init];
			h.text = @"ADD-ONS";
			h.font = [UIFont systemFontOfSize:12 weight:UIFontWeightHeavy];
			h.textColor = [UIColor colorWithRed:1.0 green:0.82 blue:0.45 alpha:1.0];
			[list addArrangedSubview:h];
		}
		NSString* label = e.label;
		if (IsLast(e)) label = [label stringByAppendingString:@"  ·  last played"];
		UIButton* b = [UIButton buttonWithType:UIButtonTypeSystem];
		[b setTitle:label forState:UIControlStateNormal];
		b.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
		b.titleLabel.font = [UIFont systemFontOfSize:17 weight:UIFontWeightBold];
		b.titleLabel.adjustsFontSizeToFitWidth = YES;
		b.titleLabel.minimumScaleFactor = 0.7;
		b.tintColor = UIColor.whiteColor;
		b.backgroundColor = e.addon ? [UIColor colorWithWhite:1.0 alpha:0.08] : RGB(f->top, 0.4);
		b.layer.cornerRadius = 12;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
		b.contentEdgeInsets = UIEdgeInsetsMake(0, 16, 0, 16);
#pragma clang diagnostic pop
		b.tag = [_entries indexOfObject:e];
		[b.heightAnchor constraintEqualToConstant:44].active = YES;
		[b addTarget:self action:@selector(gameTapped:) forControlEvents:UIControlEventTouchUpInside];
		[list addArrangedSubview:b];
	}
	UIScrollView* scroll = [[UIScrollView alloc] init];
	scroll.translatesAutoresizingMaskIntoConstraints = NO;
	list.translatesAutoresizingMaskIntoConstraints = NO;
	[scroll addSubview:list];

	UIStackView* stack = [[UIStackView alloc] initWithArrangedSubviews:@[ headRow, scroll ]];
	stack.axis = UILayoutConstraintAxisVertical;
	stack.spacing = 12;
	stack.translatesAutoresizingMaskIntoConstraints = NO;
	[panel addSubview:stack];

	NSLayoutConstraint* tall = [scroll.heightAnchor constraintEqualToConstant:(games.count + (addonsHeader ? 1 : 0)) * 52.0];
	tall.priority = UILayoutPriorityDefaultHigh;
	[NSLayoutConstraint activateConstraints:@[
		[panel.centerXAnchor constraintEqualToAnchor:gamesBack.centerXAnchor],
		[panel.centerYAnchor constraintEqualToAnchor:gamesBack.centerYAnchor],
		[panel.widthAnchor constraintEqualToConstant:460],
		[panel.heightAnchor constraintLessThanOrEqualToAnchor:gamesBack.safeAreaLayoutGuide.heightAnchor constant:-24],
		[stack.leadingAnchor constraintEqualToAnchor:panel.leadingAnchor constant:18],
		[stack.trailingAnchor constraintEqualToAnchor:panel.trailingAnchor constant:-18],
		[stack.topAnchor constraintEqualToAnchor:panel.topAnchor constant:14],
		[stack.bottomAnchor constraintEqualToAnchor:panel.bottomAnchor constant:-18],
		tall,
		[list.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor],
		[list.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor],
		[list.leadingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.leadingAnchor],
		[list.trailingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.trailingAnchor],
		[list.widthAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor],
	]];
	[UIView animateWithDuration:0.18 animations:^{ self->gamesBack.alpha = 1; }];
}

// a tap off the panel closes it (one on the panel does nothing)
- (void)backTapped:(UITapGestureRecognizer*)g
{
	UIView* panel = [gamesBack viewWithTag:7];
	if (panel && CGRectContainsPoint(panel.bounds, [g locationInView:panel])) return;
	[self closeGames];
}

- (void)closeGames
{
	UIView* v = gamesBack;
	gamesBack = nil;
	[UIView animateWithDuration:0.15 animations:^{ v.alpha = 0; } completion:^(BOOL f) { [v removeFromSuperview]; }];
}

- (void)gameTapped:(UIButton*)b
{
	if (b.tag >= 0 && b.tag < (NSInteger)_entries.count) [self finish:_entries[b.tag]];
}
@end

// ---------------------------------------------------------------- at start

static UIWindowScene* Scene()
{
	for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
		if ([scene isKindOfClass:UIWindowScene.class]) return (UIWindowScene*)scene;
	return nil;
}

int IOSPicker_Run(const char* const* names, int ctNames, int iLast, char* pszFiles, int ctFilesSize)
{
	if (pszFiles && ctFilesSize > 0) pszFiles[0] = 0;
	if (ctNames <= 0) return IOSPICKER_UNAVAILABLE;
	MakeEntries(names, ctNames);
	_lastKey = [[NSUserDefaults standardUserDefaults] stringForKey:kLastKey];
	// (nothing remembered yet: the game UZDoom played last)
	if (_lastKey == nil && iLast >= 0 && iLast < ctNames) _lastKey = _entries[iLast].key;

	UIWindowScene* scene = Scene();
	UIWindow* window = scene ? [[UIWindow alloc] initWithWindowScene:scene] : [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
	IOSPickerController* menu = [IOSPickerController new];
	window.rootViewController = menu;
	window.windowLevel = UIWindowLevelNormal + 1;
	[window makeKeyAndVisible];
	if (window.hidden) return IOSPICKER_UNAVAILABLE;

	__block IOSPickEntry* picked = nil;
	menu.done = ^(IOSPickEntry* p) { picked = p; };
	while (picked == nil)
	{
		@autoreleasepool
		{
			[[NSRunLoop mainRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
		}
	}
	window.hidden = YES;
	window.rootViewController = nil;

	[[NSUserDefaults standardUserDefaults] setObject:picked.key forKey:kLastKey];
	if (pszFiles && ctFilesSize > 0)
	{
		NSString* files = [picked.files componentsJoinedByString:@"\n"];
		strlcpy(pszFiles, files.UTF8String ?: "", ctFilesSize);
	}
	printf("Game picker: %s (game %d of %d)%s%s\n", picked.label.UTF8String, picked.iwad, ctNames,
		picked.addon ? ", with " : "", picked.addon ? [picked.files componentsJoinedByString:@", "].UTF8String : "");
	fflush(stdout);
	return picked.iwad;
}
