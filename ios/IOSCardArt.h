// The game picker's card pictures (IOSPicker.mm): a monster from the
// player's own game file, in cream on dark, made at run time (nothing of the
// games is in the app). Plain C, so the Linux rig can make them too.
//
// The owner's picks: DOOM the Lost Soul, hurt (SKULE1), DOOM II the
// Revenant firing (SKELJ1, from the knees up, fading out at the bottom),
// TNT the Cyberdemon (CYBRA1), Plutonia the Cacodemon (HEADA1).

#ifndef IOSCARDART_H
#define IOSCARDART_H

#ifdef __cplusplus
extern "C" {
#endif

// The picker's cards (IOSPicker.mm's families)
enum { IOSCARD_DOOM, IOSCARD_DOOM2, IOSCARD_TNT, IOSCARD_PLUTONIA, IOSCARD_COUNT };

typedef struct
{
	int w, h;
	unsigned char* rgba;	// w*h*4, premultiplied by alpha (malloc'd)
} IOSCardPicture;

// Reads the card's monster from the game's WAD file and makes its picture.
// 1: made (free it with IOSCardArt_Free); 0: no such sprite or file.
int IOSCardArt_Make(const char* pszWad, int iCard, IOSCardPicture* pPic);
void IOSCardArt_Free(IOSCardPicture* pPic);

// Where the picture goes on its card, the owner's layout, in points for a
// card 268 pt tall (scale it with the card): its height, and how far it sits
// below the middle of the picture's room (negative: above) and right of
// the card's middle
void IOSCardArt_Layout(int iCard, float* pfHeight, float* pfDown, float* pfRight);

#ifdef __cplusplus
}
#endif

#endif
