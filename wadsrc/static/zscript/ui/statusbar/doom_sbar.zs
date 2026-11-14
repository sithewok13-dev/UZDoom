/*
** doom_sbar.zs
**
**
**
**---------------------------------------------------------------------------
**
** Copyright 1993-1996 id Software
** Copyright 1999-2016 Marisa Heit
** Copyright 2006-2016 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

class DoomStatusBar : BaseStatusBar
{
	HUDFont mHUDFont;
	HUDFont mIndexFont;
	HUDFont mAmountFont;
	InventoryBarState diparms;


	override void Init()
	{
		Super.Init();
		SetSize(32, 320, 200);

		// Create the font used for the fullscreen HUD
		Font fnt = "HUDFONT_DOOM";
		mHUDFont = HUDFont.Create(fnt, fnt.GetCharWidth("0"), Mono_CellLeft, 1, 1);
		fnt = "INDEXFONT_DOOM";
		mIndexFont = HUDFont.Create(fnt, fnt.GetCharWidth("0"), Mono_CellLeft);
		mAmountFont = HUDFont.Create("INDEXFONT");
		diparms = InventoryBarState.Create();
	}

	override void Draw (int state, double TicFrac)
	{
		Super.Draw (state, TicFrac);

		if (state == HUD_StatusBar)
		{
			BeginStatusBar();
			// Added (iOS touch): the HUD CLASSIC at the strength the tray's HUD slider gives
			bool iosFade = int(IOSHud(0)) == 2;
			if (iosFade) Alpha = IOSHud(7);
			DrawMainBar (TicFrac);
			if (iosFade) Alpha = 1.;
		}
		else if (state == HUD_Fullscreen)
		{
			// Added (iOS touch): the touch HUD's pills instead (MENU's tray's HUD: SPLIT, STRIP)
			int iosHud = int(IOSHud(0));
			if (iosHud == 0 || iosHud == 1)
			{
				DrawIOSHud(iosHud == 0, IOSHud(1) != 0);
				return;
			}
			BeginHUD();
			DrawFullScreenStuff ();
		}
	}

	//---------------------------------------------------------------------------
	//
	// Added (iOS touch): the touch HUD (ios/IOSTouch.h, IOSTOUCH_HUD_*): dark
	// translucent rounded pills at the bottom, of DOOM's own graphics. SPLIT:
	// bottom left [face] | medikit + health | armour + armour; bottom right
	// the ammo of the weapon in hand | the keys held. STRIP: all in one pill
	// in the middle. Sizes are the approved mockups' (in their pixels: a pill
	// 64 high, DOOM's digits twice their size), scaled to the pill's height
	// the touch controls give (BaseStatusBar.IOSHud); where a graphic is
	// missing it is left out. The numbers are the status bar's own, in its
	// own red, as CLASSIC shows them; health under 25 pulses brighter red.
	//
	//---------------------------------------------------------------------------

	enum EIOSPart
	{
		IOSP_FACE, IOSP_GAP, IOSP_MEDI, IOSP_HEALTH, IOSP_PCT, IOSP_ARMI, IOSP_ARMN, IOSP_AMMOI, IOSP_AMMON,
		IOSP_KEY0 = 10 // ... + 5
	}

	Shape2D mIOSFill, mIOSEdge;
	double mIOSU;           // screen pixels per mockup pixel
	double mIOSA;           // how strongly it shows (the tray's HUD slider)
	int mIOSHealth, mIOSArmor, mIOSAmmo, mIOSArmorCr, mIOSAmmoCr;
	bool mIOSBlue, mIOSLow;
	TextureID mIOSAmmoIcon;
	bool mIOSKeys[6];

	// A graphic's width at height h (mockup pixels): 0 if the game hasn't it
	double IOSIconW(TextureID tex, double h)
	{
		if (!tex.IsValid()) return 0;
		Vector2 sz = TexMan.GetScaledSize(tex);
		return sz.Y > 0 ? sz.X * h / sz.Y : 0;
	}

	TextureID IOSTex(String name)
	{
		return TexMan.CheckForTexture(name, TexMan.Type_Any);
	}

	void IOSDrawIcon(TextureID tex, double x, double ymid, double w, double h)
	{
		double u = mIOSU;
		Screen.DrawTexture(tex, false, x * u, (ymid - h / 2) * u, DTA_DestWidthF, w * u, DTA_DestHeightF, h * u,
			DTA_LeftOffset, 0, DTA_TopOffset, 0, DTA_Alpha, mIOSA);
	}

	// A part's width (mockup pixels), and drawn at x (its left) when draw
	double IOSPart(int part, double x, double ymid, bool draw)
	{
		Font fnt = mHUDFont.mFont;
		double u = mIOSU;
		if (part == IOSP_GAP)
		{
			if (draw) Screen.DrawThickLine((x + 11) * u, (ymid - 14) * u, (x + 11) * u, (ymid + 14) * u, 2 * u, Color(255, 255, 255, 255), int(60 * mIOSA));
			return 22;
		}
		if (part == IOSP_FACE)
		{
			// DOOM's own mugshot (the status bar's: looks toward damage, grins,
			// ouch, god), placed by its offsets as on the status bar, 50 high
			TextureID st = IOSTex("STFST01");
			TextureID face = GetMugShot(5);
			if (!face.IsValid()) return 0;
			TextureID ref = st.IsValid() ? st : face;
			Vector2 rs = TexMan.GetScaledSize(ref), ro = TexMan.GetScaledOffset(ref);
			if (rs.Y <= 0) return 0;
			double k = 50 / rs.Y;
			if (draw)
			{
				Vector2 fs = TexMan.GetScaledSize(face), fo = TexMan.GetScaledOffset(face);
				double fx = x + (ro.X - fo.X) * k, fy = ymid - 25 + (ro.Y - fo.Y) * k;
				Screen.DrawTexture(face, false, fx * u, fy * u, DTA_DestWidthF, fs.X * k * u, DTA_DestHeightF, fs.Y * k * u,
					DTA_LeftOffset, 0, DTA_TopOffset, 0, DTA_Alpha, mIOSA);
			}
			return rs.X * k;
		}
		if (part == IOSP_MEDI || part == IOSP_ARMI || part == IOSP_AMMOI || part >= IOSP_KEY0)
		{
			TextureID tex;
			double h = 30;
			if (part == IOSP_MEDI) tex = IOSTex("MEDIA0");
			else if (part == IOSP_ARMI) tex = IOSTex(mIOSBlue ? "ARM2A0" : "ARM1A0");
			else if (part == IOSP_AMMOI) { tex = mIOSAmmoIcon; h = 24; }
			else { tex = IOSTex(String.Format("STKEYS%d", part - IOSP_KEY0)); h = 24; }
			double w = IOSIconW(tex, h);
			if (part == IOSP_AMMOI && w > 48) { h = h * 48 / w; w = 48; } // (a wide one: the shells)
			if (draw && w > 0) IOSDrawIcon(tex, x, ymid, w, h);
			return w;
		}
		// DOOM's big status bar digits (and its %), twice their size
		String str;
		int cr = Font.CR_UNTRANSLATED;
		if (part == IOSP_HEALTH) str = String.Format("%d", mIOSHealth);
		else if (part == IOSP_PCT) str = "%";
		else if (part == IOSP_ARMN) { str = String.Format("%d%%", mIOSArmor); cr = mIOSArmorCr; }
		else { str = String.Format("%d", mIOSAmmo); cr = mIOSAmmoCr; }
		double w = fnt.StringWidth(str) * 2;
		if (draw)
		{
			double y = (ymid - 16 + 2) * u;
			Screen.DrawText(fnt, cr, x * u, y, str, DTA_ScaleX, 2 * u, DTA_ScaleY, 2 * u, DTA_Alpha, mIOSA);
			if ((part == IOSP_HEALTH || part == IOSP_PCT) && mIOSLow)
			{
				// low: pulses brighter red (the bright red added over it), twice a second
				double a = 0.5 + 0.5 * sin(MSTimeF() * 0.72);
				Screen.DrawText(fnt, Font.CR_RED, x * u, y, str, DTA_ScaleX, 2 * u, DTA_ScaleY, 2 * u, DTA_Alpha, a * 0.9 * mIOSA,
					DTA_LegacyRenderStyle, STYLE_Add);
			}
		}
		return w;
	}

	double IOSPillWidth(Array<int> parts)
	{
		double w = 44;
		int n = 0;
		for (int i = 0; i < parts.Size(); i++)
		{
			double pw = IOSPart(parts[i], 0, 0, false);
			if (pw <= 0) continue;
			w += pw;
			n++;
		}
		return w + 8 * max(n - 1, 0);
	}

	// A rounded box's outline as points, inset by d (screen pixels)
	void IOSRoundRect(Array<double> pts, double x0, double y0, double x1, double y1, double r, double d)
	{
		pts.Clear();
		x0 += d; y0 += d; x1 -= d; y1 -= d; r = max(r - d, 0.5);
		for (int c = 0; c < 4; c++)
		{
			double cx = (c == 0 || c == 3) ? x1 - r : x0 + r;
			double cy = (c < 2) ? y0 + r : y1 - r;
			for (int k = 0; k <= 8; k++)
			{
				double ang = 90.0 * c + 90.0 * k / 8; // (counter-clockwise from the right, up being -y)
				pts.Push(cx + r * cos(ang));
				pts.Push(cy - r * sin(ang));
			}
		}
	}

	// One pill: x0 its left, its bottom at y1 (mockup pixels)
	void IOSPill(Array<int> parts, double x0, double y1)
	{
		double u = mIOSU;
		double w = IOSPillWidth(parts), h = 64;
		double y0 = y1 - h;
		if (!mIOSFill) mIOSFill = new("Shape2D");
		if (!mIOSEdge) mIOSEdge = new("Shape2D");
		Array<double> outer, inner;
		IOSRoundRect(outer, x0 * u, y0 * u, (x0 + w) * u, y1 * u, 26 * u, 0);
		IOSRoundRect(inner, x0 * u, y0 * u, (x0 + w) * u, y1 * u, 26 * u, 2 * u);
		int n = outer.Size() / 2;
		// the inside: a fan; the edge: a band between the two outlines
		mIOSFill.Clear();
		// (a shape needs a texture coordinate for each point: unused by a fill)
		mIOSFill.PushVertex(((x0 + w / 2) * u, (y0 + h / 2) * u));
		mIOSFill.PushCoord((0, 0));
		for (int i = 0; i < n; i++)
		{
			mIOSFill.PushVertex((inner[i * 2], inner[i * 2 + 1]));
			mIOSFill.PushCoord((0, 0));
		}
		for (int i = 0; i < n; i++) mIOSFill.PushTriangle(0, 1 + i, 1 + (i + 1) % n);
		mIOSEdge.Clear();
		for (int i = 0; i < n; i++)
		{
			mIOSEdge.PushVertex((outer[i * 2], outer[i * 2 + 1]));
			mIOSEdge.PushVertex((inner[i * 2], inner[i * 2 + 1]));
			mIOSEdge.PushCoord((0, 0));
			mIOSEdge.PushCoord((0, 0));
		}
		for (int i = 0; i < n; i++)
		{
			int a = i * 2, b = ((i + 1) % n) * 2;
			mIOSEdge.PushTriangle(a, a + 1, b);
			mIOSEdge.PushTriangle(b, a + 1, b + 1);
		}
		Screen.DrawShapeFill(Color(255, 10, 6, 4), 135 / 255. * mIOSA, mIOSFill);
		Screen.DrawShapeFill(Color(255, 255, 255, 255), 55 / 255. * mIOSA, mIOSEdge);
		double x = x0 + 22, ymid = y0 + h / 2;
		bool first = true;
		for (int i = 0; i < parts.Size(); i++)
		{
			double pw = IOSPart(parts[i], 0, 0, false);
			if (pw <= 0) continue;
			IOSPart(parts[i], x, ymid, true);
			x += pw + 8;
		}
	}

	protected void DrawIOSHud(bool split, bool face)
	{
		if (CPlayer == null || CPlayer.mo == null) return;
		double pillH = IOSHud(2), bottom = IOSHud(3), left = IOSHud(4), right = IOSHud(5), half = IOSHud(6);
		if (pillH <= 0) return;
		mIOSA = clamp(IOSHud(7), 0., 1.);

		// what to show
		mIOSHealth = CPlayer.health;
		mIOSLow = mIOSHealth < 25;
		let armor = BasicArmor(CPlayer.mo.FindInventory("BasicArmor", true));
		mIOSArmor = (armor != null) ? armor.Amount : 0;
		mIOSBlue = armor != null && (armor.ArmorType == 'BlueArmor' || armor.SavePercent >= 0.5);
		// (the numbers in the status bar's own red, as CLASSIC shows them, the
		// owner's wish: the armour's kind shows in its icon)
		mIOSArmorCr = Font.CR_UNTRANSLATED;
		Inventory ammo1, ammo2;
		[ammo1, ammo2] = GetCurrentAmmo();
		mIOSAmmo = ammo1 != null ? ammo1.Amount : -1;
		mIOSAmmoCr = Font.CR_UNTRANSLATED;
		mIOSAmmoIcon.SetInvalid();
		if (ammo1 != null)
		{
			TextureID ic;
			bool applyScale;
			[ic, applyScale] = GetInventoryIcon(ammo1, 0);
			mIOSAmmoIcon = ic;
		}
		// the keys held, as the status bar's (STKEYS0..5: blue, yellow, red cards, then skulls)
		static const int lockOf[] = { 2, 3, 1, 5, 6, 4 };
		for (int i = 0; i < 6; i++) mIOSKeys[i] = CPlayer.mo.CheckKeys(lockOf[i], false, true);

		Array<int> you, gun;
		if (face) { you.Push(IOSP_FACE); you.Push(IOSP_GAP); }
		you.Push(IOSP_MEDI); you.Push(IOSP_HEALTH); you.Push(IOSP_PCT);
		you.Push(IOSP_GAP); you.Push(IOSP_ARMI); you.Push(IOSP_ARMN);
		if (mIOSAmmo >= 0) { gun.Push(IOSP_AMMOI); gun.Push(IOSP_AMMON); }
		bool anyKey = false;
		for (int i = 0; i < 6; i++) if (mIOSKeys[i]) anyKey = true;
		if (anyKey)
		{
			if (gun.Size() > 0) gun.Push(IOSP_GAP);
			for (int i = 0; i < 6; i++) if (mIOSKeys[i]) gun.Push(IOSP_KEY0 + i);
		}

		// sizes: the pill's height the touch controls give; smaller where a
		// pill wouldn't fit its room (STRIP: between the buttons; SPLIT: half
		// the width each)
		double u = pillH / 64;
		if (split)
		{
			mIOSU = u;
			double room = ((right - left) / 2 - 12 * u) / u;
			double need = max(IOSPillWidth(you), gun.Size() > 0 ? IOSPillWidth(gun) : 0);
			if (need > room) u *= room / need;
			mIOSU = u;
			IOSPill(you, left / u, bottom / u);
			if (gun.Size() > 0) IOSPill(gun, right / u - IOSPillWidth(gun), bottom / u);
		}
		else
		{
			Array<int> all;
			all.Copy(you);
			if (gun.Size() > 0) { all.Push(IOSP_GAP); all.Append(gun); }
			mIOSU = u;
			double need = IOSPillWidth(all), room = 2 * half / u;
			if (need > room) u *= room / need;
			mIOSU = u;
			IOSPill(all, (Screen.GetWidth() / 2) / u - IOSPillWidth(all) / 2, bottom / u);
		}
	}

	protected void DrawMainBar (double TicFrac)
	{
		DrawImage("STBAR", (0, 168), DI_ITEM_OFFSETS);
		DrawImage("STTPRCNT", (90, 171), DI_ITEM_OFFSETS);
		DrawImage("STTPRCNT", (221, 171), DI_ITEM_OFFSETS);

		Inventory a1 = GetCurrentAmmo();
		if (a1 != null) DrawString(mHUDFont, FormatNumber(a1.Amount, 3), (44, 171), DI_TEXT_ALIGN_RIGHT|DI_NOSHADOW);
		DrawString(mHUDFont, FormatNumber(CPlayer.health, 3), (90, 171), DI_TEXT_ALIGN_RIGHT|DI_NOSHADOW);
		DrawString(mHUDFont, FormatNumber(GetArmorAmount(), 3), (221, 171), DI_TEXT_ALIGN_RIGHT|DI_NOSHADOW);

		DrawBarKeys();
		DrawBarAmmo();

		if (deathmatch || teamplay)
		{
			DrawString(mHUDFont, FormatNumber(CPlayer.FragCount, 3), (138, 171), DI_TEXT_ALIGN_RIGHT);
		}
		else
		{
			DrawBarWeapons();
		}

		if (multiplayer)
		{
			DrawImage("STFBANY", (143, 168), DI_ITEM_OFFSETS|DI_TRANSLATABLE);
		}

		if (CPlayer.mo.InvSel != null && !Level.NoInventoryBar)
		{
			DrawInventoryIcon(CPlayer.mo.InvSel, (160, 198), DI_DIMDEPLETED);
			if (CPlayer.mo.InvSel.Amount > 1)
			{
				DrawString(mAmountFont, FormatNumber(CPlayer.mo.InvSel.Amount), (175, 198-mIndexFont.mFont.GetHeight()), DI_TEXT_ALIGN_RIGHT, Font.CR_GOLD);
			}
		}
		else
		{
			DrawTexture(GetMugShot(5), (143, 168), DI_ITEM_OFFSETS);
		}
		if (isInventoryBarVisible())
		{
			DrawInventoryBar(diparms, (48, 169), 7, DI_ITEM_LEFT_TOP);
		}

	}

	protected virtual void DrawBarKeys()
	{
		bool locks[6];
		String image;
		for(int i = 0; i < 6; i++) locks[i] = CPlayer.mo.CheckKeys(i + 1, false, true);
		// key 1
		if (locks[1] && locks[4]) image = "STKEYS6";
		else if (locks[1]) image = "STKEYS0";
		else if (locks[4]) image = "STKEYS3";
		DrawImage(image, (239, 171), DI_ITEM_OFFSETS);
		// key 2
		if (locks[2] && locks[5]) image = "STKEYS7";
		else if (locks[2]) image = "STKEYS1";
		else if (locks[5]) image = "STKEYS4";
		else image = "";
		DrawImage(image, (239, 181), DI_ITEM_OFFSETS);
		// key 3
		if (locks[0] && locks[3]) image = "STKEYS8";
		else if (locks[0]) image = "STKEYS2";
		else if (locks[3]) image = "STKEYS5";
		else image = "";
		DrawImage(image, (239, 191), DI_ITEM_OFFSETS);
	}

	protected virtual void DrawBarAmmo()
	{
		int amt1, maxamt;
		[amt1, maxamt] = GetAmount("Clip");
		DrawString(mIndexFont, FormatNumber(amt1, 3), (288, 173), DI_TEXT_ALIGN_RIGHT);
		DrawString(mIndexFont, FormatNumber(maxamt, 3), (314, 173), DI_TEXT_ALIGN_RIGHT);

		[amt1, maxamt] = GetAmount("Shell");
		DrawString(mIndexFont, FormatNumber(amt1, 3), (288, 179), DI_TEXT_ALIGN_RIGHT);
		DrawString(mIndexFont, FormatNumber(maxamt, 3), (314, 179), DI_TEXT_ALIGN_RIGHT);

		[amt1, maxamt] = GetAmount("RocketAmmo");
		DrawString(mIndexFont, FormatNumber(amt1, 3), (288, 185), DI_TEXT_ALIGN_RIGHT);
		DrawString(mIndexFont, FormatNumber(maxamt, 3), (314, 185), DI_TEXT_ALIGN_RIGHT);

		[amt1, maxamt] = GetAmount("Cell");
		DrawString(mIndexFont, FormatNumber(amt1, 3), (288, 191), DI_TEXT_ALIGN_RIGHT);
		DrawString(mIndexFont, FormatNumber(maxamt, 3), (314, 191), DI_TEXT_ALIGN_RIGHT);
	}

	protected virtual void DrawBarWeapons()
	{
		DrawImage("STARMS", (104, 168), DI_ITEM_OFFSETS);
		DrawImage(CPlayer.HasWeaponsInSlot(2)? "STYSNUM2" : "STGNUM2", (111, 172), DI_ITEM_OFFSETS);
		DrawImage(CPlayer.HasWeaponsInSlot(3)? "STYSNUM3" : "STGNUM3", (123, 172), DI_ITEM_OFFSETS);
		DrawImage(CPlayer.HasWeaponsInSlot(4)? "STYSNUM4" : "STGNUM4", (135, 172), DI_ITEM_OFFSETS);
		DrawImage(CPlayer.HasWeaponsInSlot(5)? "STYSNUM5" : "STGNUM5", (111, 182), DI_ITEM_OFFSETS);
		DrawImage(CPlayer.HasWeaponsInSlot(6)? "STYSNUM6" : "STGNUM6", (123, 182), DI_ITEM_OFFSETS);
		DrawImage(CPlayer.HasWeaponsInSlot(7)? "STYSNUM7" : "STGNUM7", (135, 182), DI_ITEM_OFFSETS);
	}

	protected void DrawFullScreenStuff ()
	{
		Vector2 iconbox = (40, 20);
		// Draw health
		let berserk = CPlayer.mo.FindInventory("PowerStrength");
		DrawImage(berserk? "PSTRA0" : "MEDIA0", (20, -2));
		DrawString(mHUDFont, FormatNumber(CPlayer.health, 3), (44, -20));

		let armor = CPlayer.mo.FindInventory("BasicArmor", true);
		if (armor != null && armor.Amount > 0)
		{
			DrawInventoryIcon(armor, (20, -22));
			DrawString(mHUDFont, FormatNumber(armor.Amount, 3), (44, -40));
		}
		Inventory ammotype1, ammotype2;
		[ammotype1, ammotype2] = GetCurrentAmmo();
		int invY = -20;
		if (ammotype1 != null)
		{
			DrawInventoryIcon(ammotype1, (-14, -4));
			DrawString(mHUDFont, FormatNumber(ammotype1.Amount, 3), (-30, -20), DI_TEXT_ALIGN_RIGHT);
			invY -= 20;
		}
		if (ammotype2 != null && ammotype2 != ammotype1)
		{
			DrawInventoryIcon(ammotype2, (-14, invY + 17));
			DrawString(mHUDFont, FormatNumber(ammotype2.Amount, 3), (-30, invY), DI_TEXT_ALIGN_RIGHT);
			invY -= 20;
		}
		if (!isInventoryBarVisible() && !Level.NoInventoryBar && CPlayer.mo.InvSel != null)
		{
			DrawInventoryIcon(CPlayer.mo.InvSel, (-14, invY + 17), DI_DIMDEPLETED);
			DrawString(mHUDFont, FormatNumber(CPlayer.mo.InvSel.Amount, 3), (-30, invY), DI_TEXT_ALIGN_RIGHT);
		}
		if (deathmatch)
		{
			DrawString(mHUDFont, FormatNumber(CPlayer.FragCount, 3), (-3, 1), DI_TEXT_ALIGN_RIGHT, Font.CR_GOLD);
		}
		else
		{
			DrawFullscreenKeys();
		}

		if (isInventoryBarVisible())
		{
			DrawInventoryBar(diparms, (0, 0), 7, DI_SCREEN_CENTER_BOTTOM, HX_SHADOW);
		}
	}

	protected virtual void DrawFullscreenKeys()
	{
		// Draw the keys. This does not use a special draw function like SBARINFO because the specifics will be different for each mod
		// so it's easier to copy or reimplement the following piece of code instead of trying to write a complicated all-encompassing solution.
		Vector2 keypos = (-10, 2);
		int rowc = 0;
		double roww = 0;
		for(let i = CPlayer.mo.Inv; i != null; i = i.Inv)
		{
			if (i is "Key" && i.Icon.IsValid())
			{
				DrawTexture(i.Icon, keypos, DI_SCREEN_RIGHT_TOP|DI_ITEM_LEFT_TOP);
				Vector2 size = TexMan.GetScaledSize(i.Icon);
				keypos.Y += size.Y + 2;
				roww = max(roww, size.X);
				if (++rowc == 3)
				{
					keypos.Y = 2;
					keypos.X -= roww + 2;
					roww = 0;
					rowc = 0;
				}
			}
		}
	}
}
