#include "stdafx.hpp"
#include "game_detection.hpp"
#include "..\feature.hpp"
#include "visualizations\imgui\imgui_interface.hpp"

ConVar spt_con_notify_cvar("spt_con_notify", 
                           "0", 
                           0,
                           "Enables or disables console notifying (printing of console text at the top left of the screen, like with developer 1)");

ConVar* developer;

namespace patterns
{
	PATTERNS(Con_ColorPrint, 
             "BMS 0.9", 
             "55 8B EC 83 EC 08 80 3D ?? ?? ?? ?? 00 0F 85", 
             "5135", 
             "83 EC 08 80 3D ?? ?? ?? ?? 00 0F 85");
	PATTERNS(CConPanel__AddToNotify, 
             "BMS 0.9", 
             "55 8B EC 81 EC 30 08 00 00 A1 ?? ?? ?? ?? 33 C5 89 45 ?? 80 3D ?? ?? ?? ?? 00",
             "5135", 
             "81 EC 04 08 00 00 80 3D ?? ?? ?? ?? 00");
	PATTERNS(CConPanel__DrawNotify,
	         "BMS 0.9",
	         "55 8B EC 83 EC 3C 56 8B F1 C7 45 ?? 05 00 00 00",
	         "5135",
	         "83 EC 08 55 57 8B F9");
}

// Feature description
class ConNotifyFeature : public FeatureWrapper<ConNotifyFeature>
{
public:
protected:
    virtual bool ShouldLoadFeature() override;

    virtual void InitHooks() override;

    virtual void LoadFeature() override;

private:
    DECL_HOOK_CDECL(void, Con_ColorPrint, const Color& clr, char const* msg);
	DECL_HOOK_THISCALL(void, CConPanel__AddToNotify, void*, const Color& clr, char const* msg);
    DECL_HOOK_THISCALL(void, CConPanel__DrawNotify, void*);
};

static ConNotifyFeature spt_con_notify;

bool ConNotifyFeature::ShouldLoadFeature()
{
    if (utils::DoesGameLookLikeSteampipe()) 
    {
        return false;
    }
    else 
    {
        return true;
    }
}

void ConNotifyFeature::InitHooks()
{
    HOOK_FUNCTION(engine, Con_ColorPrint);
    HOOK_FUNCTION(engine, CConPanel__AddToNotify);
    HOOK_FUNCTION(engine, CConPanel__DrawNotify);

    developer = g_pCVar->FindVar("developer");
}

void ConNotifyFeature::LoadFeature()
{
    if (ORIG_CConPanel__AddToNotify == nullptr 
        || ORIG_CConPanel__DrawNotify == nullptr
	    || ORIG_Con_ColorPrint == nullptr 
        || developer == nullptr)
	    return;

    InitConcommandBase(spt_con_notify_cvar);
    SptImGuiGroup::QoL_ConNotify.RegisterUserCallback(
        []() { SptImGui::CvarCheckbox(spt_con_notify_cvar, "##checkbox"); });
}

// just turn on developer before entering these functions
#define IMPL(funcName, ...) \
{ \
    if (!spt_con_notify_cvar.GetBool()) \
    { \
        spt_con_notify.ORIG_##funcName(__VA_ARGS__);\
        return;\
	} \
 \
    auto oldDev = developer->GetFloat(); \
    developer->SetValue(true); \
 \
    spt_con_notify.ORIG_##funcName(__VA_ARGS__); \
 \
    developer->SetValue(oldDev); \
    return;\
}

IMPL_HOOK_CDECL(ConNotifyFeature, void, Con_ColorPrint, const Color& clr, char const* msg) 
{
    IMPL(Con_ColorPrint, clr, msg);
}

IMPL_HOOK_THISCALL(ConNotifyFeature, void, CConPanel__AddToNotify, void*, const Color& clr, char const* msg)
{
    IMPL(CConPanel__AddToNotify, thisptr, clr, msg);
}

IMPL_HOOK_THISCALL(ConNotifyFeature, void, CConPanel__DrawNotify, void*)
{
    IMPL(CConPanel__DrawNotify, thisptr);
}