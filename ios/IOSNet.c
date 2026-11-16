/*
** IOSNet.c: LAN games on the phone's Wi-Fi (see IOSNet.h)
*/

#include "IOSNet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/select.h>


static double NowSeconds(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec + tv.tv_usec * 1e-6;
}

// The Wi-Fi's address and mask (host order). en0 is the phone's Wi-Fi;
// without it, the first IPv4 that is up and not loopback.
static int WifiAddress(uint32_t* pAddr, uint32_t* pMask)
{
	struct ifaddrs* list = NULL;
	if (getifaddrs(&list) != 0) return 0;
	int found = 0;
	for (int pass = 0; pass < 2 && !found; pass++)
	{
		for (struct ifaddrs* ifa = list; ifa; ifa = ifa->ifa_next)
		{
			if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
			if (!(ifa->ifa_flags & IFF_UP) || (ifa->ifa_flags & IFF_LOOPBACK)) continue;
			if (pass == 0 && strcmp(ifa->ifa_name, "en0") != 0) continue;
			*pAddr = ntohl(((struct sockaddr_in*)ifa->ifa_addr)->sin_addr.s_addr);
			*pMask = ifa->ifa_netmask ? ntohl(((struct sockaddr_in*)ifa->ifa_netmask)->sin_addr.s_addr) : 0xffffff00u;
			found = 1;
			break;
		}
	}
	freeifaddrs(list);
	return found;
}

int IOSNet_WifiAddress(char* strAddress, int ctAddress)
{
	uint32_t addr, mask;
	if (!WifiAddress(&addr, &mask)) return 0;
	struct in_addr in;
	in.s_addr = htonl(addr);
	snprintf(strAddress, ctAddress, "%s", inet_ntoa(in));
	return 1;
}

static int OpenSocket(void)
{
	int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s < 0) return -1;
	fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
	struct sockaddr_in local;
	memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_port = 0;	// any port: the host answers to where it came from
	local.sin_addr.s_addr = htonl(INADDR_ANY);
	bind(s, (struct sockaddr*)&local, sizeof(local));
	return s;
}

static void Ask(int s, uint32_t addr)
{
	static const char ask[4] = { IOSNET_ASK[0], IOSNET_ASK[1], IOSNET_ASK[2], IOSNET_ASK[3] };
	struct sockaddr_in to;
	memset(&to, 0, sizeof(to));
	to.sin_family = AF_INET;
	to.sin_port = htons(IOSNET_PORT);
	to.sin_addr.s_addr = htonl(addr);
	sendto(s, (const char*)ask, sizeof(ask), 0, (struct sockaddr*)&to, sizeof(to));
}

// "UZD1|game|mode|present|wanted"
static int ParseReply(const char* text, IOSNetHost* pHost)
{
	char copy[200];
	snprintf(copy, sizeof(copy), "%s", text);
	char* field[5];
	int n = 0;
	char* p = copy;
	while (n < 5)
	{
		field[n++] = p;
		char* bar = strchr(p, '|');
		if (!bar) break;
		*bar = 0;
		p = bar + 1;
	}
	if (n < 5 || strcmp(field[0], "UZD1") != 0) return 0;
	snprintf(pHost->strGame, sizeof(pHost->strGame), "%s", field[1]);
	snprintf(pHost->strMode, sizeof(pHost->strMode), "%s", field[2]);
	pHost->ctPresent = atoi(field[3]);
	pHost->ctWanted = atoi(field[4]);
	return 1;
}

int IOSNet_Scan(IOSNetHost* aHosts, int ctMax, double fSeconds)
{
	int s = OpenSocket();
	if (s < 0) return 0;

	// every address of the /24 (a bigger Wi-Fi: the /24 around this phone),
	// and this machine (a second instance on a computer)
	uint32_t addr = 0, mask = 0;
	if (WifiAddress(&addr, &mask))
	{
		const uint32_t net = addr & 0xffffff00u;
		for (uint32_t i = 1; i < 255; i++)
			if ((net | i) != addr) Ask(s, net | i);
	}
	Ask(s, 0x7f000001u);	// 127.0.0.1

	int count = 0;
	const double tEnd = NowSeconds() + fSeconds;
	double tAgain = NowSeconds() + fSeconds * 0.4;
	for (;;)
	{
		const double left = tEnd - NowSeconds();
		if (left <= 0) break;
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(s, &rfds);
		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = (int)((left < 0.1 ? left : 0.1) * 1e6);
		if (select(s + 1, &rfds, NULL, NULL, &tv) > 0)
		{
			unsigned char buf[256];
			struct sockaddr_in from;
			socklen_t fromlen = sizeof(from);
			const int len = (int)recvfrom(s, (char*)buf, sizeof(buf) - 1, 0, (struct sockaddr*)&from, &fromlen);
			if (len > 4 && !memcmp(buf, IOSNET_ANSWER, 4))
			{
				buf[len] = 0;
				IOSNetHost host;
				memset(&host, 0, sizeof(host));
				snprintf(host.strAddress, sizeof(host.strAddress), "%s", inet_ntoa(from.sin_addr));
				// (a host on this machine answers both on its Wi-Fi address and
				// on 127.0.0.1: once is enough)
				int seen = 0;
				for (int i = 0; i < count; i++)
					if (!strcmp(aHosts[i].strAddress, host.strAddress)) seen = 1;
				if (!seen && count < ctMax && ParseReply((const char*)buf + 4, &host))
					aHosts[count++] = host;
			}
		}
		// (once more for the addresses whose first packet was lost)
		if (tAgain > 0 && NowSeconds() >= tAgain)
		{
			tAgain = 0;
			if (addr)
			{
				const uint32_t net = addr & 0xffffff00u;
				for (uint32_t i = 1; i < 255; i++)
					if ((net | i) != addr) Ask(s, net | i);
			}
		}
	}
	close(s);

	// (a host on this machine, found both ways: keep the Wi-Fi one)
	if (count > 1 && addr)
	{
		for (int i = 0; i < count; i++)
		{
			if (strcmp(aHosts[i].strAddress, "127.0.0.1") != 0) continue;
			char mine[64];
			struct in_addr in;
			in.s_addr = htonl(addr);
			snprintf(mine, sizeof(mine), "%s", inet_ntoa(in));
			for (int j = 0; j < count; j++)
			{
				if (j != i && !strcmp(aHosts[j].strAddress, mine))
				{
					aHosts[i] = aHosts[--count];
					break;
				}
			}
		}
	}
	return count;
}

void IOSNet_TouchNetwork(void)
{
	uint32_t addr, mask;
	if (!WifiAddress(&addr, &mask)) return;
	int s = OpenSocket();
	if (s < 0) return;
	// the Wi-Fi's first address (usually the router): only a search packet,
	// which nothing but a UZDoom host answers
	Ask(s, (addr & 0xffffff00u) | 1);
	close(s);
}
