/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "info.h"

CInfo::CInfo(CGameWorld *pGameWorld, int Type, int ID, vec2 Pos, int MapID)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_DRAW, MapID)
{
	m_Type = Type; // 0 = Clan info, 1 = Material info
	m_Pos = Pos;
	m_InfoID = ID;

	GameWorld()->InsertEntity(this);
}

void CInfo::Tick()
{

	if(Server()->Tick() % (1 * Server()->TickSpeed() * 5) == 0)
	{
		if (m_Type)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "M %d", Server()->GetMaterials(m_InfoID));
			GameServer()->CreateLolText(this, true, vec2(0,0), vec2 (0, 0), 250, aBuf, GetMapID());
		}
		else
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "House %s %d", Server()->GetClanName(Server()->GetTopHouse(m_InfoID)), (m_InfoID + 1));
			GameServer()->CreateLolText(this, true, vec2(0,0), vec2 (0, 0), 500, aBuf, GetMapID());
		}
	}
}

void CInfo::TickPaused()
{

}

void CInfo::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;
}
