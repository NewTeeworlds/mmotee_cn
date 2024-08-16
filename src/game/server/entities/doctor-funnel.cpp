// Copyright (c) ST-Chara 2024 - 2024
#include <game/server/gamecontext.h>
#include <engine/shared/config.h>
#include <base/math.h>
#include <base/vmath.h>
#include <game/server/player.h>
#include "biologist-laser.h"
#include "doctor-funnel.h"

#include "electro.h"

CDoctorFunnel::CDoctorFunnel(CGameWorld *pGameWorld, vec2 Pos, int Owner)
    : CEntity(pGameWorld, ENTTYPE_FUNNEL), m_Owner(Owner), m_LockTarget(false), m_TargetCID(-1)
{
    for (int i = 0; i < NUM_LASER; i++)
        m_LaserIDs[i] = Server()->SnapNewID();
    m_ConnectID = Server()->SnapNewID();

    m_FunnelState = CDoctorFunnel::STATE_FOLLOW;
	m_PowerBattery = g_Config.m_InfDoctorMaxPowerBattery;

    GameWorld()->InsertEntity(this);
}

CDoctorFunnel::~CDoctorFunnel()
{
    for (int i = 0; i < NUM_LASER; i++)
        Server()->SnapFreeID(m_LaserIDs[i]);
    Server()->SnapFreeID(m_ConnectID);
}

void CDoctorFunnel::ConsumePower(int Power)
{
    if (!GameServer()->GetPlayerChar(m_Owner))
        return;

    if (m_PowerBattery <= 0)
        Reset();
    else
        m_PowerBattery -= Power;
}

void CDoctorFunnel::FunnelMove()
{
    m_Pos += (m_TargetPos - m_Pos) / 24.f;
}

void CDoctorFunnel::ResetLock()
{
    m_LockTarget = false;
    m_ChangeTargetNeed = 0;
}

vec2 CDoctorFunnel::GetOwnerPos()
{
    return GameServer()->GetPlayerChar(m_Owner)->m_Pos;
}

vec2 CDoctorFunnel::GetTargetPos()
{
    if (!m_LockTarget)
    {
        std::vector<int> Targets;
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            CPlayer *pPlayer = GameServer()->m_apPlayers[i];
            if(!pPlayer)
                continue;
            
            if(!pPlayer->GetCharacter())
                continue;
            
            if(!pPlayer->IsBot())
                continue;

            if (distance(pPlayer->GetCharacter()->m_Pos, GameServer()->GetPlayerChar(m_Owner)->m_Pos) < 2000)
                Targets.push_back(pPlayer->GetCID());
        }

        float Distance = 999999999;
        int ID = -1;
        for (size_t i = 0; i < Targets.size(); i++)
        {
            if (!GameServer()->GetPlayerChar(Targets[i]))
                continue;

            if (distance(GameServer()->GetPlayerChar(Targets[i])->m_Pos, GameServer()->GetPlayerChar(m_Owner)->m_Pos) < Distance)
            {
                Distance = distance(GameServer()->GetPlayerChar(Targets[i])->m_Pos, GameServer()->GetPlayerChar(m_Owner)->m_Pos);
                ID = Targets[i];
            }
        }

        if (Targets.size() > 0)
        {
            m_LockTarget = true;
            m_TargetCID = ID;
        }
    }

    if (m_TargetCID >= 0 && GameServer()->GetPlayerChar(m_TargetCID))
        return GameServer()->GetPlayerChar(m_TargetCID)->m_Pos;

    return vec2(GetOwnerPos().x, GetOwnerPos().y - 128.f);
}

void CDoctorFunnel::Tick()
{
    if (!GameServer()->GetPlayerChar(m_Owner))
        return Reset();

    if (Server()->Tick() % 50 == 0)
        m_ChangeTargetNeed--;

    switch (m_FunnelState)
    {
    case STATE_FOLLOW:
    {
        ResetLock();
        m_TargetPos = vec2(GetOwnerPos().x, GetOwnerPos().y - 128.f);
        for (CCharacter *pChr = (CCharacter *)GameWorld()->FindFirst(ENTTYPE_CHARACTER); pChr; pChr = (CCharacter *)pChr->TypeNext())
        {
            if (!pChr->GetPlayer()->IsBot())
                continue;

            float Len = distance(pChr->m_Pos, m_Pos);
            if (Len < 500 && Server()->Tick() % 10 == 0)
            {
                vec2 Direction = normalize(pChr->m_Pos - m_Pos);
                vec2 Start = m_Pos;
                float Reach = 400;
                float a = GetAngle(Direction);
				vec2 To = m_Pos + vec2(cosf(a), sinf(a))*Reach;
                
                Start += Direction*50;
				
                GameServer()->Collision()->IntersectLine(Start, To, 0x0, &To);
				GameServer()->Collision()->IntersectLine(Start, To, 0x0, &To);

                int A = distance(Start, To) / 100;
					
				if (A > 4)
					A = 4;
				
				if (A < 2)
					A = 2;

                new CElectro(GameWorld(), Start, To, Direction, A);
                GameServer()->CreateSound(m_Pos, SOUND_RIFLE_FIRE);

                To = pChr->m_Pos;
				pChr->ElectroShock();
				pChr->TakeDamage(Direction, g_Config.m_InfDoctorFunnelDamage, GetOwner(), WEAPON_RIFLE, TAKEDAMAGEMODE_INFECTION);
            }
        }
        FunnelMove();
        ConsumePower(1);
        break;
    }
    case STATE_FIND:
    {
        m_TargetPos = vec2(GetTargetPos().x, GetTargetPos().y);
        FunnelMove();
        
        if (!GameServer()->GetPlayerChar(m_TargetCID) || m_ChangeTargetNeed > 10 || (GameServer()->GetPlayerChar(m_TargetCID) && distance(GameServer()->GetPlayerChar(m_TargetCID)->m_Pos, GameServer()->GetPlayerChar(m_Owner)->m_Pos) > 2000))
            ResetLock();

        if (GameServer()->GetPlayerChar(m_TargetCID) && distance(m_TargetPos, m_Pos) < 150.f && Server()->Tick() % 10 == 0)
            GameServer()->GetPlayerChar(m_TargetCID)->TakeDamage(vec2(0, 0), g_Config.m_InfDoctorFunnelDamage, m_Owner, WEAPON_HAMMER, TAKEDAMAGEMODE_NOINFECTION);

        ConsumePower(2);
        break;
    }
    default:
        break;
    }
}

void CDoctorFunnel::Snap(int SnappingClient)
{
    if (NetworkClipped(SnappingClient) || !GameServer()->GetPlayerChar(m_Owner))
        return;

    {
        vec2 SnapPos[NUM_LASER] = {vec2(-48, 0), vec2(-24, -14), vec2(0, -28),
                                   vec2(24, -14), vec2(48, 0),
                                   vec2(24, 14), vec2(0, 28), vec2(-24, 14),
                                   vec2(-48, 0)};

        for (int i = 0; i < NUM_LASER; i++)
        {
            CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_LaserIDs[i], sizeof(CNetObj_Laser)));
            if (!pObj)
                return;

            pObj->m_FromX = SnapPos[i].x + m_Pos.x;
            pObj->m_FromY = SnapPos[i].y + m_Pos.y;
            pObj->m_X = SnapPos[(i >= NUM_LASER - 1) ? 0 : i + 1].x + m_Pos.x;
            pObj->m_Y = SnapPos[(i >= NUM_LASER - 1) ? 0 : i + 1].y + m_Pos.y;
            pObj->m_StartTick = Server()->Tick();
        }

        {
            CNetObj_Projectile *pProj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_ID, sizeof(CNetObj_Projectile)));
            if (!pProj)
                return;

            pProj->m_X = m_Pos.x;
            pProj->m_Y = m_Pos.y;
            pProj->m_VelX = 0;
            pProj->m_VelY = 0;
            pProj->m_Type = WEAPON_HAMMER;
            pProj->m_StartTick = Server()->Tick();
        }

        if (m_FunnelState == STATE_FIND)
        {
            if (distance(m_TargetPos, m_Pos) < 150.f)
            {
                CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_ConnectID, sizeof(CNetObj_Laser)));
                if (!pObj)
                    return;

                pObj->m_FromX = m_Pos.x;
                pObj->m_FromY = m_Pos.y;
                pObj->m_X = m_TargetPos.x;
                pObj->m_Y = m_TargetPos.y;
                pObj->m_StartTick = Server()->Tick();
            }
        }
    }
}

void CDoctorFunnel::Reset()
{
    GameServer()->m_World.DestroyEntity(this);
}