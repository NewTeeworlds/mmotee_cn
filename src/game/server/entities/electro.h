/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_ELECTRO_H
#define GAME_SERVER_ENTITIES_ELECTRO_H

#include <game/server/entity.h>

class CElectro : public CEntity
{
public:
	CElectro(CGameWorld *pGameWorld, vec2 Start, vec2 End, vec2 Offset, int Left);

	virtual void Reset();
	virtual void Tick();
	virtual void TickPaused();
	virtual void Snap(int SnappingClient);

protected:

private:
	vec2 m_End;
	bool m_Render;
	int m_EvalTick;
};

#endif
