#include "stdafx.hpp"

#include "hud.hpp"

#if defined(SPT_HUD_ENABLED) && !defined(SPT_HUD_TEXTONLY)

#include "..\feature.hpp"
#include "convar.hpp"
#include "interfaces.hpp"
#include "playerio.hpp"
#include "signals.hpp"
#include "game_detection.hpp"
#include "visualizations\imgui\imgui_interface.hpp"

ConVar y_spt_hud_edgebug("y_spt_hud_edgebug",
                         "0",
                         0,
                         "Draws text in the middle of the screen when you get an edgeboog");
ConVar y_spt_hud_edgebug_sec("y_spt_hud_edgebug_sec", "1", 0, "Duration of the boog text in seconds");

// Boog detection
class BoogFeature : public FeatureWrapper<BoogFeature>
{
public:
	void BoogTick(bool simulating);
	bool ShouldDrawBoog();
	void DrawBoog();
	int ticksLeftToDrawBoog = 0;
	bool previousTickFalling = false;
	vgui::HFont boogFont = 0;

protected:
	virtual bool ShouldLoadFeature() override;

	virtual void InitHooks() override;

	virtual void LoadFeature() override;

	virtual void UnloadFeature() override;
};

static BoogFeature spt_boog;

void BoogFeature::BoogTick(bool simulating)
{
	if (!simulating)
		return;

	if (ticksLeftToDrawBoog > 0)
	{
		ticksLeftToDrawBoog -= 1;
	}

	bool ground = spt_playerio.m_fFlags.GetValue() & FL_ONGROUND;
	Vector vel = spt_playerio.m_vecAbsVelocity.GetValue();
	const float boog_vel = -4.5f;

	if (previousTickFalling)
	{
		if (!ground && vel.z == boog_vel)
		{
			ticksLeftToDrawBoog = (y_spt_hud_edgebug_sec.GetFloat()) / 0.015f;
		}
	}

	previousTickFalling = vel.z < boog_vel && !ground;
}

bool BoogFeature::ShouldDrawBoog()
{
	bool rval = ticksLeftToDrawBoog > 0 && y_spt_hud_edgebug.GetBool();

	if (rval)
	{
		return true;
	}
	else
	{
		return false;
	}
}

void BoogFeature::DrawBoog()
{
	const auto& screen = spt_hud_feat.screen;

	if (boogFont == 0)
	{
		boogFont = interfaces::surface->CreateFont();
		if (boogFont == 0)
			return;
		interfaces::surface->SetFontGlyphSet(boogFont,
		                                     "Trebuchet MS",
		                                     96,
		                                     0,
		                                     0,
		                                     0,
		                                     vgui::ISurface::EFontFlags::FONTFLAG_ANTIALIAS);
	}

	interfaces::surface->DrawSetTextFont(boogFont);
	interfaces::surface->DrawSetTextColor(Color(255, 255, 255, 255));

	int tall = 0, len = 0;
	interfaces::surface->GetTextSize(boogFont, L"boog", len, tall);

	int x = screen.width / 2 - len / 2;
	int y = screen.height / 2 + 100;

	if (tall + y > screen.height)
	{
		y = screen.height - tall;
	}

	interfaces::surface->DrawSetTextPos(x, y);
	interfaces::surface->DrawPrintText(L"boog");
}

bool BoogFeature::ShouldLoadFeature()
{
	return interfaces::surface && spt_hud_feat.ShouldLoadFeature() && spt_playerio.ShouldLoadFeature();
}

void BoogFeature::InitHooks() {}

static void SV_FrameWrapper(bool finalTick)
{
	spt_boog.BoogTick(true);
}

void BoogFeature::LoadFeature()
{
	bool boogHooked = false;

	// Prefer SV_Frame, also works in demos
	if (SV_FrameSignal.Works)
	{
		boogHooked = true;
		SV_FrameSignal.Connect(SV_FrameWrapper);
	}
	// Fall back on Tick signal, only works outside of demos
	else if (TickSignal.Works)
	{
		boogHooked = true;
		TickSignal.Connect(this, &BoogFeature::BoogTick);
	}

	if (boogHooked)
	{
		bool result = spt_hud_feat.AddHudDefaultGroup(HudCallback(std::bind(&BoogFeature::DrawBoog, this),
		                                                          std::bind(&BoogFeature::ShouldDrawBoog, this),
		                                                          false));
		if (result)
		{
			InitConcommandBase(y_spt_hud_edgebug);
			InitConcommandBase(y_spt_hud_edgebug_sec);

			SptImGui::RegisterHudCvarCallback(
			    y_spt_hud_edgebug,
			    [](ConVar& var)
			    {
				    SptImGui::CvarCheckbox(y_spt_hud_edgebug, "##checkbox");
				    SptImGui::CvarDouble(y_spt_hud_edgebug_sec,
				                         "edgebug display time",
				                         "enter time in seconds");
			    },
			    true);
		}
	}
}

void BoogFeature::UnloadFeature() {}

#endif
