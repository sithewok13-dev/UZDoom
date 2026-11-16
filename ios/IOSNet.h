/*
** IOSNet.h: LAN games on the phone's Wi-Fi (plain C, also built in the
** Linux rig)
**
** - IOSNet_WifiAddress: the phone's own address on the Wi-Fi (shown while
**   hosting, so the others can type it)
** - IOSNet_Scan: the hosts waiting for players: every address of the Wi-Fi's
**   /24 is asked on the game's port (UZDoom's i_net.cpp answers while it
**   waits for players: IOSNET_ASK, then IOSNET_ANSWER and its text), no
**   broadcasts
** (From the owner's Raze port, ios/IOSNet.c.)
*/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define IOSNET_PORT 5029	// UZDoom's (IPPORT_USERRESERVED + 29)
#define IOSNET_ASK "UZD?"	// the search's packet
#define IOSNET_ANSWER "UZD!"	// ...a host's answer: then "UZD1|game|mode|present|wanted"
#define IOSNET_MAXHOSTS 32

typedef struct
{
	char strAddress[64];	// "192.168.1.20"
	char strGame[96];		// the host's game, as its picker names it
	char strMode[48];		// "Co-op", "Deathmatch", ...
	int ctPresent, ctWanted;	// players there, players the host waits for
} IOSNetHost;

// The Wi-Fi's IPv4 address ("en0" on the phone; elsewhere the first that
// isn't loopback) as text. Returns 0 when there is none.
int IOSNet_WifiAddress(char* strAddress, int ctAddress);

// Asks every address of the Wi-Fi's /24 (and this machine itself) for
// hosts, waits up to fSeconds for answers. Returns how many were found.
int IOSNet_Scan(IOSNetHost* aHosts, int ctMax, double fSeconds);

// Sends one harmless packet on the Wi-Fi, so iOS asks for the Local Network
// permission before the game needs it (a host only listens, which doesn't)
void IOSNet_TouchNetwork(void);

#ifdef __cplusplus
}
#endif
