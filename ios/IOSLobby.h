// LAN games on the phone: the lobby while waiting for the players, instead
// of UZDoom's ZWidget NetStartWindow (no ZWidget windows on iOS). The same
// calls as NetStartWindow's (i_net.cpp uses these under DOOM_IOS); a box
// over the screen: what is happening, who is here, Start now (the host)
// and Cancel. In ios/i_ios.mm. (From the owner's Raze port's waiting box.)
#pragma once

namespace IOSLobby
{
	void NetInit(const char* message, bool host);
	void NetMessage(const char* message);
	void NetConnect(int client, const char* name, unsigned flags, int status);
	void NetUpdate(int client, int status);
	void NetDisconnect(int client);
	void NetProgress(int cur, int limit);
	void NetDone();
	void NetClose();
	bool ShouldStartNet();
	int GetNetKickClient();
	int GetNetBanClient();
	bool NetLoop(bool (*loopCallback)(void*), void* data);
}
