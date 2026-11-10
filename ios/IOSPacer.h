/*
** IOSPacer.h: the frames' pace at the tray's FPS 60 (ios/i_ios.mm), as
** plain C++ so the Linux rig's tests can drive it with a pretend screen.
**
** A frame starts every 1/60 s (the game's clock and its interpolation are
** read then), and asks the screen (VK_GOOGLE_display_timing, MoltenVK's
** presentAtTime) to show it one frame period after it started: the first
** screen refresh after "start + period - refresh / 2". Build 14 had two
** pacers that didn't know of each other (a sleep every 1/60 s, and vsync at
** the screen's own refresh), so a frame finished near a refresh was shown
** at one refresh or the next (8 or 25 ms apart on a 120 Hz screen). Here
** the frames' starts are steered onto the screen's refreshes: each shown
** frame's real time (from the past presentation timings) says how far off
** its refresh the plan was, and the next starts move a little that way, so
** the times asked for stay half a refresh clear of a refresh either side.
** Times are nanoseconds on the system's uptime clock (CACurrentMediaTime's).
*/

#pragma once
#include <stdint.h>
#include <math.h>

struct IOSPacer
{
	enum { HISTORY = 64 };
	uint64_t next = 0;        // the next frame's planned start (0: from now)
	uint64_t period = 0;      // its frame period (0: not pacing)
	uint64_t refresh = 0;     // the screen's refresh (0: unknown; then no times asked)
	uint64_t start = 0;       // this frame's planned start
	int fresh = 0;            // a frame started that hasn't been handed to the screen
	uint32_t id = 0;          // the last present's id
	uint64_t starts[HISTORY] = {}; // each present's planned start, by id (0: none asked)
	double steer = 0;         // ns the next starts still move by
	// for the log
	uint64_t lastActual = 0;
	uint32_t lastActualId = 0;
	int shown = 0, uneven = 0, late = 0;
	double sumDt = 0, sumDt2 = 0, minDt = 0, maxDt = 0, sumErr = 0;
	int errs = 0;
};

// The frame's start: how long to sleep (ns) before it starts, at now with a
// frame period of periodNs (0: no pacing, the frames go at vsync's pace)
static inline uint64_t IOSPacer_Wake(IOSPacer& p, uint64_t now, uint64_t periodNs)
{
	if (periodNs != p.period) { p.period = periodNs; p.next = 0; p.steer = 0; }
	p.fresh = 1;
	if (periodNs == 0) { p.start = now; return 0; }
	// (far behind, after a pause or a load: from now on)
	if (p.next == 0 || now > p.next + periodNs) p.next = now;
	p.start = p.next;
	// steered at most a sixteenth of a refresh a frame, so the pace never jumps
	double s = p.steer;
	const double most = p.refresh ? p.refresh / 16.0 : 0.0;
	if (s > most) s = most;
	if (s < -most) s = -most;
	p.steer -= s;
	p.next += periodNs + (int64_t)s;
	return now < p.start ? p.start - now : 0;
}

// The frame is handed to the screen: its id (for its timing later) and the
// time it asks to be shown at (0: as soon as it can)
static inline uint64_t IOSPacer_Present(IOSPacer& p, uint32_t* pId)
{
	const uint32_t id = ++p.id;
	*pId = id;
	uint64_t desired = 0;
	// (only a frame's first present: a screen wipe hands over several)
	if (p.fresh && p.period && p.refresh)
	{
		p.starts[id % IOSPacer::HISTORY] = p.start;
		desired = p.start + p.period - p.refresh / 2;
	}
	else p.starts[id % IOSPacer::HISTORY] = 0;
	p.fresh = 0;
	return desired;
}

// A present was shown at actual (ns): the frames' starts are steered onto
// the screen's refreshes, and the log's figures kept
static inline void IOSPacer_Shown(IOSPacer& p, uint32_t id, uint64_t actual)
{
	if (actual == 0) return;
	if (p.lastActual && id == p.lastActualId + 1 && actual > p.lastActual)
	{
		const double dt = (double)(actual - p.lastActual);
		if (p.shown == 0 || dt < p.minDt) p.minDt = dt;
		if (p.shown == 0 || dt > p.maxDt) p.maxDt = dt;
		p.sumDt += dt;
		p.sumDt2 += dt * dt;
		p.shown++;
		const double want = p.period ? (double)p.period : (p.refresh ? (double)p.refresh : 0.0);
		if (want > 0 && p.refresh && fabs(dt - want) > p.refresh / 2.0) p.uneven++;
	}
	p.lastActual = actual;
	p.lastActualId = id;

	const uint64_t start = p.starts[id % IOSPacer::HISTORY];
	if (!start || p.id - id >= IOSPacer::HISTORY || !p.period || !p.refresh) return;
	p.starts[id % IOSPacer::HISTORY] = 0;
	// how far its refresh was from the plan (start + period), within half a
	// refresh; a whole refresh or more after it: late (not ready in time)
	const double V = (double)p.refresh;
	double e = (double)(int64_t)(actual - (start + p.period));
	const double k = floor(e / V + 0.5);
	if (k >= 1.0) p.late++;
	e -= k * V;
	p.sumErr += fabs(e);
	p.errs++;
	p.steer += e * 0.1;
}

// The log's figures, from the last call on
struct IOSPacerStats
{
	int shown, uneven, late;
	double meanMs, minMs, maxMs, sdMs, errMs;
};

static inline IOSPacerStats IOSPacer_TakeStats(IOSPacer& p)
{
	IOSPacerStats s = {};
	s.shown = p.shown;
	s.uneven = p.uneven;
	s.late = p.late;
	if (p.shown > 0)
	{
		const double m = p.sumDt / p.shown;
		const double var = p.sumDt2 / p.shown - m * m;
		s.meanMs = m * 1e-6;
		s.minMs = p.minDt * 1e-6;
		s.maxMs = p.maxDt * 1e-6;
		s.sdMs = (var > 0 ? sqrt(var) : 0.0) * 1e-6;
	}
	s.errMs = p.errs ? p.sumErr / p.errs * 1e-6 : 0.0;
	p.shown = p.uneven = p.late = p.errs = 0;
	p.sumDt = p.sumDt2 = p.minDt = p.maxDt = p.sumErr = 0;
	return s;
}
