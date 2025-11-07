#include "stdafx.hpp"
#include "..\feature.hpp"

#include "hud.hpp"
#include "visualizations/imgui/imgui_interface.hpp"

#include "const.h"

struct CEntInfo
{
	void* m_pEntity;
	int m_SerialNumber;
	CEntInfo* m_pPrev;
	CEntInfo* m_pNext;
};

struct CBaseEntityList_guts
{
	struct CEntInfoList
	{
		CEntInfo* m_pHead;
		CEntInfo* m_pTail;
	};

	void** vfptr;
	CEntInfo m_EntPtrArray[NUM_ENT_ENTRIES];
	CEntInfoList m_activeList;
	CEntInfoList m_freeNonNetworkableList;

	int GetEntInfoIndex(const CEntInfo* pEntInfo) const
	{
		return (int)(pEntInfo - m_EntPtrArray);
	}
};

ConVar spt_hud_next_slot("spt_hud_next_slot", "0", FCVAR_CHEAT, "Turns on next non-networkable entity slot hud.\n");

// server gEntList
class EntityList : public FeatureWrapper<EntityList>
{
public:
protected:
	virtual bool ShouldLoadFeature() override;

	virtual void InitHooks() override;

	virtual void LoadFeature() override;

	virtual void UnloadFeature() override;

private:
	uintptr_t ORIG_CAI_BaseNPC__ClearAllSchedules = 0;
	CBaseEntityList_guts* entitylist = nullptr;
};

static EntityList spt_entitylist;

namespace patterns
{
	PATTERNS(CAI_BaseNPC__ClearAllSchedules,
	         "5135",
	         "56 6A 00 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 8B F0 85 F6 74 ?? 68 ?? ?? ?? ?? 8B CE",
	         "7122284",
	         "57 6A 00 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 8B F8 85 FF 74 ?? 56 68 ?? ?? ?? ?? 8B CF");
}

bool EntityList::ShouldLoadFeature()
{
	return true;
}

void EntityList::InitHooks()
{
	FIND_PATTERN(server, CAI_BaseNPC__ClearAllSchedules);
}

void EntityList::LoadFeature()
{
	if (!ORIG_CAI_BaseNPC__ClearAllSchedules)
		return;

	int off_entitylist = 4;

	entitylist = *reinterpret_cast<CBaseEntityList_guts**>(ORIG_CAI_BaseNPC__ClearAllSchedules + off_entitylist);
	if (!entitylist)
		return;

#ifdef SPT_HUD_ENABLED
	bool hudEnabled = AddHudCallback(
	    "next_slot",
	    [this](std::string)
	    {
		    CEntInfo* pSlot = entitylist->m_freeNonNetworkableList.m_pHead;
		    if (pSlot)
			    spt_hud_feat.DrawTopHudElement(L"next slot: %d", entitylist->GetEntInfoIndex(pSlot));
		    else
			    spt_hud_feat.DrawTopHudElement(L"no free slots");
	    },
	    spt_hud_next_slot);

	if (hudEnabled)
		SptImGui::RegisterHudCvarCheckbox(spt_hud_next_slot);
#endif
}

void EntityList::UnloadFeature() {}
