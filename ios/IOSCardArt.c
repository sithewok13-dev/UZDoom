// The game picker's card pictures: see IOSCardArt.h. Ported from the
// owner's approved mockups (Python, Nov 2026): each monster's own recipe.
//
//   SOFT (the Cyberdemon): the sprite blown up blocky, blurred, its
//     brightness a curve into cream on dark.
//   SMOOTH (the Cacodemon, the Revenant): the colours blown up smoothly
//     (Lanczos), the outline from the blocky shape blurred and cut; the
//     brightness stretched between its 2nd/3rd and 99th percentiles, a
//     gamma, then sharpened.
//   SCALE2X (the Spider): Scale2x three times (8x, its edges kept crisp),
//     blurred a little, stretched and sharpened as SMOOTH.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "IOSCardArt.h"

#define PAD 4	// transparent room round the sprite (the blurs need it)

static const float BG[3] = { 30, 13, 16 };	// dark (the picture's shadows)
static const float CR[3] = { 244, 230, 204 };	// cream (its lights)

enum { SOFT, SMOOTH, SCALE2X };

typedef struct
{
	const char* sprite;
	int recipe;
	int k;			// how many times bigger (SOFT, SMOOTH)
	float gamma, lo, hi;	// SMOOTH, SCALE2X: the stretch's percentiles and gamma
	float keep;		// the top part of the sprite kept (the Revenant: to the knees)
	float fade;		// the bottom part faded out (of the picture's height)
	float height, down;	// IOSCardArt_Layout
} Recipe;

static const Recipe recipes[IOSCARD_COUNT] =
{
	{ "SPIDC1F1", SCALE2X, 8, 1.10f, 2, 99, 1.00f, 0.00f, 212, 26 },
	{ "SKELJ1", SMOOTH, 10, 1.25f, 3, 99, 0.68f, 0.28f, 156, 0 },
	{ "CYBRA1", SOFT, 10, 0, 0, 0, 1.00f, 0.00f, 165, 0 },
	{ "HEADA1", SMOOTH, 10, 1.10f, 2, 99, 1.00f, 0.00f, 156, -8 },
};

void IOSCardArt_Layout(int iCard, float* pfHeight, float* pfDown)
{
	if (iCard < 0 || iCard >= IOSCARD_COUNT) { *pfHeight = 0; *pfDown = 0; return; }
	*pfHeight = recipes[iCard].height;
	*pfDown = recipes[iCard].down;
}

// ---------------------------------------------------------------- the WAD

typedef struct { unsigned char* data; long size; } Lump;

static unsigned rd32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }
static int rd16s(const unsigned char* p) { return (short)(p[0] | (p[1] << 8)); }

// The last lump of that name (as the game would use), read whole
static int ReadLump(FILE* f, const char* name, Lump* out)
{
	unsigned char hdr[12];
	if (fseek(f, 0, SEEK_SET) || fread(hdr, 1, 12, f) != 12) return 0;
	if (memcmp(hdr + 1, "WAD", 3) || (hdr[0] != 'I' && hdr[0] != 'P')) return 0;
	const unsigned n = rd32(hdr + 4), dir = rd32(hdr + 8);
	if (n > 200000) return 0;
	unsigned char* d = (unsigned char*)malloc((size_t)n * 16);
	if (!d) return 0;
	if (fseek(f, dir, SEEK_SET) || fread(d, 16, n, f) != n) { free(d); return 0; }
	long pos = -1, size = 0;
	for (unsigned i = 0; i < n; i++)
	{
		char nm[9] = { 0 };
		memcpy(nm, d + i * 16 + 8, 8);
		if (!strncmp(nm, name, 8)) { pos = rd32(d + i * 16); size = rd32(d + i * 16 + 4); }
	}
	free(d);
	if (pos < 0 || size <= 0 || size > 4 << 20) return 0;
	out->data = (unsigned char*)malloc(size);
	out->size = size;
	if (!out->data || fseek(f, pos, SEEK_SET) || fread(out->data, 1, size, f) != (size_t)size)
	{
		free(out->data);
		out->data = NULL;
		return 0;
	}
	return 1;
}

// A Doom picture, as RGBA (transparent: 0,0,0,0), with PAD round it
static unsigned char* DecodePicture(const Lump* pic, const unsigned char* pal, int* pw, int* ph)
{
	if (pic->size < 8) return NULL;
	const int w = rd16s(pic->data), h = rd16s(pic->data + 2);
	if (w <= 0 || h <= 0 || w > 1024 || h > 1024 || pic->size < 8 + 4 * w) return NULL;
	const int W = w + 2 * PAD, H = h + 2 * PAD;
	unsigned char* out = (unsigned char*)calloc((size_t)W * H, 4);
	if (!out) return NULL;
	for (int x = 0; x < w; x++)
	{
		long c = rd32(pic->data + 8 + 4 * x);
		while (c >= 0 && c + 3 < pic->size && pic->data[c] != 255)
		{
			const int top = pic->data[c], len = pic->data[c + 1];
			for (int j = 0; j < len && c + 3 + j < pic->size; j++)
			{
				const int y = top + j;
				if (y >= h) break;
				unsigned char* o = out + ((size_t)(y + PAD) * W + x + PAD) * 4;
				const unsigned char* p = pal + pic->data[c + 3 + j] * 3;
				o[0] = p[0]; o[1] = p[1]; o[2] = p[2]; o[3] = 255;
			}
			c += len + 4;
		}
	}
	*pw = W;
	*ph = H;
	return out;
}

// ---------------------------------------------------------------- images

// float planes, 4 channels interleaved (r g b a), 0-255
typedef struct { int w, h; float* p; } Img;

static Img NewImg(int w, int h)
{
	Img m = { w, h, (float*)calloc((size_t)w * h * 4, sizeof(float)) };
	return m;
}

static Img FromRGBA(const unsigned char* src, int w, int h)
{
	Img m = NewImg(w, h);
	if (m.p) for (size_t i = 0; i < (size_t)w * h * 4; i++) m.p[i] = src[i];
	return m;
}

static Img Nearest(const Img* s, int k)
{
	Img m = NewImg(s->w * k, s->h * k);
	if (!m.p) return m;
	for (int y = 0; y < m.h; y++)
		for (int x = 0; x < m.w; x++)
			memcpy(m.p + ((size_t)y * m.w + x) * 4, s->p + ((size_t)(y / k) * s->w + x / k) * 4, 4 * sizeof(float));
	return m;
}

// Separable Gaussian blur, sigma in pixels, edges extended
static void Blur(Img* m, float sigma)
{
	if (!m->p || sigma <= 0) return;
	const int r = (int)ceilf(sigma * 3);
	float* kern = (float*)malloc((2 * r + 1) * sizeof(float));
	float sum = 0;
	for (int i = -r; i <= r; i++) sum += kern[i + r] = expf(-(float)(i * i) / (2 * sigma * sigma));
	for (int i = 0; i <= 2 * r; i++) kern[i] /= sum;
	const int n = m->w > m->h ? m->w : m->h;
	float* line = (float*)malloc((size_t)n * 4 * sizeof(float));
	for (int pass = 0; pass < 2; pass++)
	{
		const int len = pass ? m->h : m->w, count = pass ? m->w : m->h;
		for (int c = 0; c < count; c++)
		{
			for (int i = 0; i < len; i++)
			{
				float acc[4] = { 0, 0, 0, 0 };
				for (int t = -r; t <= r; t++)
				{
					int j = i + t;
					j = j < 0 ? 0 : j >= len ? len - 1 : j;
					const float* px = pass ? m->p + ((size_t)j * m->w + c) * 4 : m->p + ((size_t)c * m->w + j) * 4;
					const float kw = kern[t + r];
					acc[0] += px[0] * kw; acc[1] += px[1] * kw; acc[2] += px[2] * kw; acc[3] += px[3] * kw;
				}
				memcpy(line + i * 4, acc, sizeof(acc));
			}
			for (int i = 0; i < len; i++)
				memcpy(pass ? m->p + ((size_t)i * m->w + c) * 4 : m->p + ((size_t)c * m->w + i) * 4, line + i * 4, 4 * sizeof(float));
		}
	}
	free(line);
	free(kern);
}

static float Lanczos3(float x)
{
	if (x == 0) return 1;
	if (x <= -3 || x >= 3) return 0;
	const float px = (float)M_PI * x;
	return 3 * sinf(px) * sinf(px / 3) / (px * px);
}

// Lanczos-3 enlargement by k (each axis in turn)
static Img Lanczos(const Img* s, int k)
{
	Img t = NewImg(s->w * k, s->h);
	Img m = NewImg(s->w * k, s->h * k);
	if (!t.p || !m.p) { free(t.p); return m; }
	for (int pass = 0; pass < 2; pass++)
	{
		const Img* src = pass ? &t : s;
		Img* dst = pass ? &m : &t;
		const int outLen = pass ? dst->h : dst->w, inLen = pass ? src->h : src->w, count = pass ? dst->w : dst->h;
		for (int o = 0; o < outLen; o++)
		{
			const float c = (o + 0.5f) / k - 0.5f;
			const int i0 = (int)floorf(c) - 2;
			float wts[6], wsum = 0;
			for (int t2 = 0; t2 < 6; t2++) wsum += wts[t2] = Lanczos3(c - (i0 + t2));
			for (int line = 0; line < count; line++)
			{
				float acc[4] = { 0, 0, 0, 0 };
				for (int t2 = 0; t2 < 6; t2++)
				{
					int i = i0 + t2;
					i = i < 0 ? 0 : i >= inLen ? inLen - 1 : i;
					const float* px = pass ? src->p + ((size_t)i * src->w + line) * 4 : src->p + ((size_t)line * src->w + i) * 4;
					const float wt = wts[t2] / wsum;
					acc[0] += px[0] * wt; acc[1] += px[1] * wt; acc[2] += px[2] * wt; acc[3] += px[3] * wt;
				}
				float* d = pass ? dst->p + ((size_t)o * dst->w + line) * 4 : dst->p + ((size_t)line * dst->w + o) * 4;
				for (int ch = 0; ch < 4; ch++) d[ch] = acc[ch] < 0 ? 0 : acc[ch] > 255 ? 255 : acc[ch];
			}
		}
	}
	free(t.p);
	return m;
}

// Scale2x (EPX) on exact colours
static unsigned char* Scale2x(const unsigned char* a, int w, int h)
{
	unsigned char* o = (unsigned char*)malloc((size_t)w * h * 16);
	if (!o) return NULL;
#define PX(x, y) (a + ((size_t)((y) < 0 ? 0 : (y) >= h ? h - 1 : (y)) * w + ((x) < 0 ? 0 : (x) >= w ? w - 1 : (x))) * 4)
#define EQ(p, q) (!memcmp((p), (q), 4))
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
		{
			const unsigned char *E = PX(x, y), *B = PX(x, y - 1), *H = PX(x, y + 1), *D = PX(x - 1, y), *F = PX(x + 1, y);
			const int c = !EQ(B, H) && !EQ(D, F);
			const unsigned char* e0 = c && EQ(D, B) ? D : E;
			const unsigned char* e1 = c && EQ(B, F) ? F : E;
			const unsigned char* e2 = c && EQ(D, H) ? D : E;
			const unsigned char* e3 = c && EQ(H, F) ? F : E;
			const size_t W2 = (size_t)w * 2;
			memcpy(o + ((2 * y) * W2 + 2 * x) * 4, e0, 4);
			memcpy(o + ((2 * y) * W2 + 2 * x + 1) * 4, e1, 4);
			memcpy(o + ((2 * y + 1) * W2 + 2 * x) * 4, e2, 4);
			memcpy(o + ((2 * y + 1) * W2 + 2 * x + 1) * 4, e3, 4);
		}
#undef PX
#undef EQ
	return o;
}

// The value at the given percentile of v where mask (0-1 values, 4096 bins)
static float Percentile(const float* v, const unsigned char* mask, size_t n, float pct)
{
	static unsigned hist[4096];
	memset(hist, 0, sizeof(hist));
	size_t total = 0;
	for (size_t i = 0; i < n; i++)
		if (mask[i])
		{
			int b = (int)(v[i] * 4095.0f);
			hist[b < 0 ? 0 : b > 4095 ? 4095 : b]++;
			total++;
		}
	if (!total) return 0;
	const size_t want = (size_t)(pct / 100.0f * (total - 1));
	size_t run = 0;
	for (int b = 0; b < 4096; b++)
	{
		run += hist[b];
		if (run > want) return b / 4095.0f;
	}
	return 1;
}

// ---------------------------------------------------------------- the recipes

static int Make(const Recipe* rc, const unsigned char* sprite, int sw, int sh, IOSCardPicture* pic)
{
	// (the Revenant: to the knees; the sprite's own rows, then PAD again)
	int keepH = sh;
	if (rc->keep < 1)
	{
		keepH = PAD + (int)((sh - 2 * PAD) * rc->keep);
		unsigned char* cut = (unsigned char*)calloc((size_t)sw * (keepH + PAD), 4);
		memcpy(cut, sprite, (size_t)sw * keepH * 4);
		sprite = cut;
		keepH += PAD;
	}
	Img col = { 0 }, edge = { 0 };	// colour (premultiplied) and the outline's alpha (in col.p[3] for SOFT/SCALE2X)
	if (rc->recipe == SOFT)
	{
		Img s = FromRGBA(sprite, sw, keepH);
		col = Nearest(&s, rc->k);
		free(s.p);
		Blur(&col, rc->k * 0.45f);
	}
	else if (rc->recipe == SMOOTH)
	{
		Img s = FromRGBA(sprite, sw, keepH);
		for (size_t i = 0; i < (size_t)sw * keepH; i++)
			for (int c = 0; c < 3; c++) s.p[i * 4 + c] *= s.p[i * 4 + 3] / 255.0f;
		col = Lanczos(&s, rc->k);
		Blur(&col, rc->k * 0.2f);
		edge = Nearest(&s, rc->k);
		free(s.p);
		Blur(&edge, rc->k * 0.5f);
	}
	else
	{
		int w = sw, h = keepH;
		unsigned char* a = (unsigned char*)malloc((size_t)w * h * 4);
		memcpy(a, sprite, (size_t)w * h * 4);
		for (int pass = 0; pass < 3; pass++)
		{
			unsigned char* b = Scale2x(a, w, h);
			free(a);
			a = b;
			w *= 2;
			h *= 2;
		}
		col = FromRGBA(a, w, h);
		free(a);
		Blur(&col, 1.2f);
	}
	if (rc->keep < 1) free((void*)sprite);
	if (!col.p) { free(edge.p); return 0; }

	const int W = col.w, H = col.h;
	const size_t n = (size_t)W * H;
	float* l = (float*)malloc(n * sizeof(float));
	float* al = (float*)malloc(n * sizeof(float));
	unsigned char* mask = (unsigned char*)malloc(n);
	for (size_t i = 0; i < n; i++)
	{
		const float* p = col.p + i * 4;
		const float a = p[3] / 255.0f, ad = a > 1e-3f ? a : 1e-3f;
		const float r = p[0] / ad, g = p[1] / ad, b = p[2] / ad;
		const float lum = (0.3f * r + 0.55f * g + 0.15f * b) / 255.0f;
		if (rc->recipe == SOFT)
		{
			float v = (lum - 0.08f) / 0.6f;
			v = v < 0 ? 0 : v > 1 ? 1 : v;
			l[i] = powf(v, 0.75f);
			float e = (a - 0.35f) * 4;
			al[i] = e < 0 ? 0 : e > 1 ? 1 : e;
		}
		else
		{
			const float mx = (r > g ? (r > b ? r : b) : (g > b ? g : b)) / 255.0f;
			l[i] = 0.5f * lum + 0.5f * mx;
			const float ea = rc->recipe == SMOOTH ? edge.p[i * 4 + 3] / 255.0f : a;
			mask[i] = ea > 0.6f;
			float e = rc->recipe == SMOOTH ? (ea - 0.45f) * 6 : (ea - 0.4f) * 5;
			al[i] = e < 0 ? 0 : e > 1 ? 1 : e;
		}
	}
	if (rc->recipe != SOFT)
	{
		const float p0 = Percentile(l, mask, n, rc->lo), p1 = Percentile(l, mask, n, rc->hi);
		const float span = p1 - p0 > 1e-3f ? p1 - p0 : 1e-3f;
		for (size_t i = 0; i < n; i++)
		{
			float v = (l[i] - p0) / span;
			v = v < 0 ? 0 : v > 1 ? 1 : v;
			l[i] = powf(v, rc->gamma);
		}
	}
	free(mask);
	free(edge.p);
	free(col.p);

	// the colour, cream on dark, then sharpened (not SOFT)
	Img out = NewImg(W, H);
	for (size_t i = 0; i < n; i++)
		for (int c = 0; c < 3; c++) out.p[i * 4 + c] = BG[c] + (CR[c] - BG[c]) * l[i];
	if (rc->recipe != SOFT)
	{
		Img bl = NewImg(W, H);
		memcpy(bl.p, out.p, n * 4 * sizeof(float));
		const float radius = rc->recipe == SMOOTH ? rc->k * 0.35f : 2.0f, amount = rc->recipe == SMOOTH ? 0.7f : 0.6f;
		Blur(&bl, radius);
		for (size_t i = 0; i < n * 4; i++)
		{
			const float d = out.p[i] - bl.p[i];
			if (fabsf(d) > 2) out.p[i] += d * amount;
		}
		free(bl.p);
	}
	free(l);

	// cut to what shows
	int x0 = W, y0 = H, x1 = -1, y1 = -1;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			if (al[(size_t)y * W + x] > 0)
			{
				if (x < x0) x0 = x;
				if (x > x1) x1 = x;
				if (y < y0) y0 = y;
				if (y > y1) y1 = y;
			}
	if (x1 < 0) { free(al); free(out.p); return 0; }
	pic->w = x1 - x0 + 1;
	pic->h = y1 - y0 + 1;
	pic->rgba = (unsigned char*)malloc((size_t)pic->w * pic->h * 4);
	for (int y = 0; y < pic->h; y++)
	{
		// (the bottom faded out: the Revenant)
		float fade = 1;
		if (rc->fade > 0)
		{
			fade = (pic->h - y) / (pic->h * rc->fade);
			fade = fade > 1 ? 1 : fade;
		}
		for (int x = 0; x < pic->w; x++)
		{
			const size_t si = (size_t)(y + y0) * W + x + x0;
			const float a = al[si] * fade;
			unsigned char* d = pic->rgba + ((size_t)y * pic->w + x) * 4;
			for (int c = 0; c < 3; c++)
			{
				float v = out.p[si * 4 + c];
				v = v < 0 ? 0 : v > 255 ? 255 : v;
				d[c] = (unsigned char)(v * a + 0.5f);
			}
			d[3] = (unsigned char)(a * 255 + 0.5f);
		}
	}
	free(al);
	free(out.p);
	return 1;
}

int IOSCardArt_Make(const char* pszWad, int iCard, IOSCardPicture* pPic)
{
	memset(pPic, 0, sizeof(*pPic));
	if (!pszWad || iCard < 0 || iCard >= IOSCARD_COUNT) return 0;
	FILE* f = fopen(pszWad, "rb");
	if (!f) return 0;
	Lump pal = { 0 }, spr = { 0 };
	int ok = ReadLump(f, "PLAYPAL", &pal) && pal.size >= 768 && ReadLump(f, recipes[iCard].sprite, &spr);
	fclose(f);
	if (ok)
	{
		int w = 0, h = 0;
		unsigned char* sprite = DecodePicture(&spr, pal.data, &w, &h);
		ok = sprite && Make(&recipes[iCard], sprite, w, h, pPic);
		free(sprite);
	}
	free(pal.data);
	free(spr.data);
	return ok;
}

void IOSCardArt_Free(IOSCardPicture* pPic)
{
	free(pPic->rgba);
	pPic->rgba = NULL;
}
