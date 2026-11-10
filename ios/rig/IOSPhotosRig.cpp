/* The CAMERA's photos on the Linux rig (ios/IOSPhotos.mm is the iOS one):
   PNGs into $IOSCAM_DIR (or ./ioscam), named as on iOS; the messages on
   stdout ("rig: camera ...") and on the overlay's message line. No touch
   buttons here: a "hud+buttons" photo is the HUD one. Saved at once. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <vector>

#include <miniz.h>
#include "IOSCamera.h"
#include "IOSCapture.h"
#include "IOSTouch.h"

static int batch = 0, numSaved = 0, numFailed = 0, shot = 0;

static void Report()
{
	if(batch || (!numSaved && !numFailed))
		return;
	char msg[128];
	if(numFailed)
		snprintf(msg, sizeof(msg), "%d photos saved, %d couldn't be saved", numSaved, numFailed);
	else
		snprintf(msg, sizeof(msg), numSaved == 1 ? "1 photo saved" : "%d photos saved", numSaved);
	IOSCamera_Message(msg);
	numSaved = numFailed = 0;
}

extern "C" void IOSPhotos_SetBatch(int bOn)
{
	batch = bOn != 0;
	if(bOn)
		shot = 0;
	Report();
}

static void Chunk(FILE *f, const char *type, const unsigned char *data, unsigned long len)
{
	const unsigned char l[4] = { (unsigned char)(len >> 24), (unsigned char)(len >> 16), (unsigned char)(len >> 8), (unsigned char)len };
	fwrite(l, 1, 4, f);
	fwrite(type, 1, 4, f);
	if(len)
		fwrite(data, 1, len, f);
	uLong c = crc32(0, (const Bytef *)type, 4);
	if(len)
		c = crc32(c, data, len);
	const unsigned char cb[4] = { (unsigned char)(c >> 24), (unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c };
	fwrite(cb, 1, 4, f);
}

static bool WritePng(const unsigned char *p, int w, int h, const char *name)
{
	const char *dir = getenv("IOSCAM_DIR");
	if(!dir || !*dir)
		dir = "ioscam";
	mkdir(dir, 0755);
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	unsigned char *rgb = (unsigned char *)malloc((size_t)w * h * 3);
	if(!rgb)
		return false;
	for(size_t i = 0, n = (size_t)w * h;i < n;i++)
		memcpy(rgb + i * 3, p + i * 4, 3);
	// (a plain RGB PNG)
	std::vector<unsigned char> raw((size_t)(w * 3 + 1) * h);
	for(int y = 0;y < h;y++)
	{
		raw[(size_t)y * (w * 3 + 1)] = 0;
		memcpy(&raw[(size_t)y * (w * 3 + 1) + 1], rgb + (size_t)y * w * 3, (size_t)w * 3);
	}
	uLongf zlen = compressBound(raw.size());
	std::vector<unsigned char> z(zlen);
	bool ok = compress2(&z[0], &zlen, &raw[0], raw.size(), 6) == Z_OK;
	FILE *f = ok ? fopen(path, "wb") : NULL;
	if(f)
	{
		static const unsigned char sig[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
		fwrite(sig, 1, 8, f);
		unsigned char ihdr[13] = { (unsigned char)(w >> 24), (unsigned char)(w >> 16), (unsigned char)(w >> 8), (unsigned char)w,
			(unsigned char)(h >> 24), (unsigned char)(h >> 16), (unsigned char)(h >> 8), (unsigned char)h, 8, 2, 0, 0, 0 };
		Chunk(f, "IHDR", ihdr, 13);
		Chunk(f, "IDAT", &z[0], zlen);
		Chunk(f, "IEND", NULL, 0);
		ok = fclose(f) == 0;
	}
	else
		ok = false;
	free(rgb);
	printf("rig: camera saved %s (%dx%d)%s\n", path, w, h, ok ? "" : " FAILED");
	fflush(stdout);
	return ok;
}

extern "C" void IOSPhotos_SaveCapture(unsigned char *pClean, int cleanW, int cleanH, unsigned char *pHud, int hudW, int hudH,
                                      int kinds, int mode)
{
	const long bytes = (long)cleanW * cleanH * 4 * (pClean ? 1 : 0) + (long)hudW * hudH * 4 * (pHud ? ((kinds & IOSCAPTURE_HUDBTNS) ? 2 : 1) : 0);
	char base[96], name[160];
	time_t t = time(NULL);
	strftime(base, sizeof(base), "DOOM %Y-%m-%d %H.%M.%S", localtime(&t));
	static int same = 0;
	static char last[96];
	if(!strcmp(last, base))
		same++;
	else
	{
		strcpy(last, base);
		same = 1;
	}
	if(same > 1)
		snprintf(base + strlen(base), sizeof(base) - strlen(base), "-%d", same);
	const bool numbered = mode == IOSCAPTURE_MODE_BURST || (mode == IOSCAPTURE_MODE_SELFIE && batch);
	if(numbered)
		snprintf(base + strlen(base), sizeof(base) - strlen(base), mode == IOSCAPTURE_MODE_BURST ? " burst %02d" : " selfie %d", ++shot);
	struct { unsigned char *p; int w, h, bit; const char *suffix; } a[3] = {
		{ pClean, cleanW, cleanH, IOSCAPTURE_CLEAN, "clean" }, { pHud, hudW, hudH, IOSCAPTURE_HUD, "hud" },
		{ pHud, hudW, hudH, IOSCAPTURE_HUDBTNS, "hud+buttons" } };
	for(int i = 0;i < 3;i++)
	{
		if(!(kinds & a[i].bit))
			continue;
		if(!a[i].p)
		{
			numFailed++;
			continue;
		}
		if(numbered)
			snprintf(name, sizeof(name), "%s.png", base);
		else
			snprintf(name, sizeof(name), "%s %s.png", base, mode == IOSCAPTURE_MODE_SELFIE ? "selfie" : a[i].suffix);
		if(WritePng(a[i].p, a[i].w, a[i].h, name))
			numSaved++;
		else
			numFailed++;
	}
	free(pClean);
	free(pHud);
	IOSCapture_Done(bytes, 0);
	Report();
}

extern "C" void IOSCamera_Message(const char *str)
{
	printf("rig: camera message: %s\n", str);
	fflush(stdout);
	IOSTouch_ShowMessage(str, 3.0);
}

extern "C" void IOSCamera_ShowOptions(void)
{
	printf("rig: camera options: kinds %d (1 clean, 2 HUD, 4 HUD and buttons), selfies %d, freeze %d\n", IOSCamera_GetKinds(),
		IOSCamera_GetSelfieShots(), IOSCamera_GetFreezeOn());
	fflush(stdout);
}

extern "C" void IOSPhotos_Init(void)
{
}

extern "C" void IOSPhotos_FreezeShown(int bOn)
{
	printf("rig: camera Action Freeze %s\n", bOn ? "on" : "off");
	fflush(stdout);
}
