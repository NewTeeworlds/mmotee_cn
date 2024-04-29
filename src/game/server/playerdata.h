// Prepare for Multi-World
#pragma once

struct SAccData
{
    int m_Level;
    unsigned long int m_Exp;
    int m_Class = 0;
    int m_Money;
    unsigned long int m_Gold;
    int m_Donate;
    unsigned long int m_ClanAdded;
    int m_Quest;
    int m_Kill;
    int m_WinArea;
    int m_Rel;
    bool m_Jail;
    bool m_IsJailed;          // 是否被送进监狱
    int m_JailLength;         // 手动设置监禁时长
    int m_SummerHealingTimes; // Skill Summer Healing 合成保底
};

struct SAccUpgrade
{
    int m_HammerRange;
    int m_Pasive2;
    int m_Speed;
    int m_Health;
    int m_Damage;
    int m_HPRegen;
    int m_AmmoRegen;
    int m_Ammo;
    int m_Spray;
    int m_Mana;
    int m_SkillPoint;
    int m_Upgrade;
};