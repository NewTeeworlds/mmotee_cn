// Copyright (c) ST-Chara 2024 - 2024
#pragma once

#include <game/server/entity.h>

class CDoctorFunnel : public CEntity
{
public:
    enum
    {
        NUM_LASER = 9,

        STATE_FOLLOW = 0,
        STATE_FIND,
        NUM_STATE,
    };

public:
    CDoctorFunnel(CGameWorld *pGameWorld, vec2 Pos, int Owner);
    virtual ~CDoctorFunnel();

    virtual void Snap(int SnappingClient);
    virtual void Reset();
    virtual void Tick();

    vec2 GetOwnerPos();
    vec2 GetTargetPos();

    void ConsumePower(int Power);
    void FunnelMove();
    void ResetLock();

    int GetOwner() { return m_Owner; }
    int m_FunnelState;
	int m_PowerBattery;
private:
    int m_Owner;
    int m_Angle;
    int m_LaserIDs[NUM_LASER];
    bool m_LockTarget;
    int m_TargetCID;
    int m_ConnectID;
    int m_ChangeTargetNeed;
    vec2 m_TargetPos;
};