/* 2019年2月10日13:55:38 */
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/generated/protocol.h>
#include <engine/shared/config.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "pickup.h"

CPickup::CPickup(CGameWorld *pGameWorld, int Type, vec2 Pos, int SubType)
: CEntity(pGameWorld, ENTTYPE_PICKUP)
{
	m_Type = Type;
	m_Pos = Pos;
	m_SubType = SubType;
	m_Drop = 0;

	Reset();

	GameWorld()->InsertEntity(this);
}

void CPickup::Reset()
{
	if (g_pData->m_aPickups[m_Type].m_Spawndelay > 0)
		m_SpawnTick = Server()->Tick() + Server()->TickSpeed() * g_pData->m_aPickups[m_Type].m_Spawndelay;
	else
		m_SpawnTick = -1;
}

void CPickup::Tick()
{
	// wait for respawn
	if(m_SpawnTick > 0)
	{
		if(Server()->Tick() > m_SpawnTick)
		{
			// respawn
			m_SpawnTick = -1;

			if(m_Type == WEAPON_GRENADE || m_Type == WEAPON_SHOTGUN || m_Type == WEAPON_RIFLE)
				GameServer()->CreateSound(m_Pos, SOUND_WEAPON_SPAWN);
		}
		else
			return;
	}

	// Find other players
	for(CCharacter *p = (CCharacter*) GameWorld()->FindFirst(ENTTYPE_CHARACTER); p; p = (CCharacter *)p->TypeNext())
	{
		if(!p->GetPlayer()->IsBot() && distance(p->m_Pos, m_Pos) < 20) 
		{
			switch (m_Type)
			{
				case 0:
					if(m_SubType != 3 && m_SubType != 4 && m_SubType != 5)
					{
						GameServer()->CreateSound(m_Pos, SOUND_PICKUP_HEALTH);
						Picking(0);
					}
					break;

				case 1:
					//Picked = true;
					break;

				case 3:
					Picking(0);
					break;

				case 2:
					Picking(0);
					break;

				case 4:
					Picking(0);
					break;

				case 5:
					Picking(0);
					break;

				default:
					break;
			};

		}
	}
}

void CPickup::Picking(int Time)
{
	int RespawnTime = g_pData->m_aPickups[m_Type].m_Respawntime+Time;
	if(RespawnTime >= 0)
		m_SpawnTick = Server()->Tick() + Server()->TickSpeed() * RespawnTime;
}

void CPickup::StartFarm(int ClientID)
{
	CPlayer *pFarm = GameServer()->m_apPlayers[ClientID];
	if(!pFarm || !pFarm->GetCharacter() || m_SpawnTick != -1 || pFarm->IsBot())
		return;

	if(!m_SubType) // ########################### FARMING
	{
		int Dropable = 0;
		int Broke = 0;
		int Count = 0;
		int Temp = 0;
		const char* ItemName = "啥都没有";
		Temp += 20;
		if(Server()->GetItemCount(ClientID, DRAGONHOE))
		{
			Count = Server()->GetItemCount(ClientID, DRAGONHOE);
			Broke = 608*Server()->GetItemCount(ClientID, DRAGONHOE);
			Dropable = Server()->GetItemSettings(ClientID, DRAGONHOE);
			if(Dropable <= 0)
			{
				Server()->RemItem(ClientID, DRAGONHOE, Server()->GetItemCount(ClientID, DRAGONHOE), -1);
				GameServer()->SendChatTarget_Localization(ClientID, -1, _("~ 农具: {str:name} 已损毁, 不能用了"), "name", Server()->GetItemName(ClientID, DRAGONHOE), NULL);
			}
			Server()->SetItemSettingsCount(ClientID, DRAGONHOE, Dropable-1);
			ItemName = Server()->GetItemName(ClientID, DRAGONHOE);
			Temp += 5;
		}

		if(Server()->GetItemSettings(ClientID, TITLEFARMF))
			Temp *= 2;
		
		m_Drop += Temp;
		
		GameServer()->CreateSound(m_Pos, 20); 

		int LevelItem = 1+Server()->GetItemCount(ClientID, FARMLEVEL)/g_Config.m_SvFarmExp;
		int NeedExp = LevelItem*g_Config.m_SvFarmExp;
		int Exp = Server()->GetItemCount(ClientID, FARMLEVEL);

		float getlv = (m_Drop*100.0)/100;
		
		GameServer()->SendBroadcast_Localization(ClientID, 1000, 100, _("专长 - 种地: {int:lvl}级: {int:exp}/{int:expneed}经验\n工具: {str:name}x{int:count} ({int:brok}/{int:brok2})\n物品 : 采集进度: {str:got} / {int:gotp}%"), 
			"lvl", &LevelItem, "exp", &Exp, "expneed", &NeedExp, "name", ItemName, "count", &Count, "brok", &Dropable, "brok2", &Broke, "got", GameServer()->LevelString(100, (int)getlv, 10, ':', ' '), "gotp", &m_Drop, NULL);
		

		if(m_Drop >= 100)
		{
			if(random_prob(0.5f))
				Server()->SetMaterials(2, Server()->GetMaterials(2)+1);

			if(Server()->GetItemSettings(ClientID, TITLEFARMM))
				LevelItem *= 2;

			switch(random_int(0, 5))
			{
				case 0:	GameServer()->GiveItem(ClientID, TOMATE, LevelItem); break; 
				case 1: GameServer()->GiveItem(ClientID, POTATO, LevelItem); break;
				case 2: GameServer()->GiveItem(ClientID, CABBAGE, LevelItem); break;
				default: GameServer()->GiveItem(ClientID, CARROT, LevelItem); break;
			}
			if(Server()->GetItemCount(ClientID, FARMLEVEL) % g_Config.m_SvFarmExp == 0)
			{
				GameServer()->SendChatTarget_Localization(ClientID, -1, _("~ 种地等级提升~ 获得了农耕盲盒"), NULL);
				GameServer()->GiveItem(ClientID, FARMBOX, 1);
			}
			if(random_prob(0.01f)) // 1/100
				GameServer()->GiveItem(ClientID, FARMBOX, 1);

			GameServer()->GiveItem(ClientID, FARMLEVEL, 1);
		}
	}
	else if(m_SubType == 2) // ########################### MINER
	{
		int Dropable = 0;
		int Broke = 0;
		int Count = 0;
		int Temp = 0;
		const char* ItemName = "啥都没有";
		if(Server()->GetItemCount(ClientID, DRAGONPIX))
		{
			Count = Server()->GetItemCount(ClientID, DRAGONPIX);
			Broke = 638*Server()->GetItemCount(ClientID, DRAGONPIX);
			Dropable = Server()->GetItemSettings(ClientID, DRAGONPIX);
			if(Dropable <= 0)
			{
				Server()->RemItem(ClientID, DRAGONPIX, Server()->GetItemCount(ClientID, DRAGONPIX), -1);
				GameServer()->SendChatTarget_Localization(ClientID, -1, _("~ 矿具: {str:name} 已损毁, 不能用了"), "name", Server()->GetItemName(ClientID, DRAGONPIX), NULL);
			}
			Server()->SetItemSettingsCount(ClientID, DRAGONPIX, Dropable-1);
			ItemName = Server()->GetItemName(ClientID, DRAGONPIX);
			Temp += random_int(45,60);	
		}
		if(Server()->GetItemCount(ClientID, DIAMONDPIX))
		{
			Count = Server()->GetItemCount(ClientID, DIAMONDPIX);
			Broke = 499*Server()->GetItemCount(ClientID, DIAMONDPIX);
			Dropable = Server()->GetItemSettings(ClientID, DIAMONDPIX);
			if(Dropable <= 0)
			{
				Server()->RemItem(ClientID, DIAMONDPIX, Server()->GetItemCount(ClientID, DIAMONDPIX), -1);
				GameServer()->SendChatTarget_Localization(ClientID, -1, _("~ 矿具: {str:name} 已损毁, 不能用了"), "name", Server()->GetItemName(ClientID, DIAMONDPIX), NULL);
			}
			Server()->SetItemSettingsCount(ClientID, DIAMONDPIX, Dropable-1);
			ItemName = Server()->GetItemName(ClientID, DIAMONDPIX);
			Temp += 35;	
		}
		else if(Server()->GetItemCount(ClientID, GOLDPIX))
		{
			Count = Server()->GetItemCount(ClientID, GOLDPIX);
			Broke = 291*Server()->GetItemCount(ClientID, GOLDPIX);
			Dropable = Server()->GetItemSettings(ClientID, GOLDPIX);
			if(Dropable <= 0)
			{
				Server()->RemItem(ClientID, GOLDPIX, Server()->GetItemCount(ClientID, GOLDPIX), -1);
				GameServer()->SendChatTarget_Localization(ClientID, -1, _("~ 矿具: {str:name} 已损毁, 不能用了"), "name", Server()->GetItemName(ClientID, GOLDPIX), NULL);
			}
			Server()->SetItemSettingsCount(ClientID, GOLDPIX, Dropable-1);
			ItemName = Server()->GetItemName(ClientID, GOLDPIX);
			Temp += 20;	
		}
		else if(Server()->GetItemCount(ClientID, IRONPIX))
		{
			Count = Server()->GetItemCount(ClientID, IRONPIX);
			Broke = 162*Server()->GetItemCount(ClientID, IRONPIX);
			Dropable = Server()->GetItemSettings(ClientID, IRONPIX);
			if(Dropable <= 0)
			{
				Server()->RemItem(ClientID, IRONPIX, Server()->GetItemCount(ClientID, IRONPIX), -1);
				GameServer()->SendChatTarget_Localization(ClientID, -1, _("~ 矿具: {str:name} 已损毁, 不能用了"), "name", Server()->GetItemName(ClientID, IRONPIX), NULL);
			}
			Server()->SetItemSettingsCount(ClientID, IRONPIX, Dropable-1);
			ItemName = Server()->GetItemName(ClientID, IRONPIX);
			Temp += 15;	
		}
		else if(Server()->GetItemCount(ClientID, COOPERPIX))
		{
			Count = Server()->GetItemCount(ClientID, COOPERPIX);
			Broke = 88*Server()->GetItemCount(ClientID, COOPERPIX);
			Dropable = Server()->GetItemSettings(ClientID, COOPERPIX);
			if(Dropable <= 0)
			{
				Server()->RemItem(ClientID, COOPERPIX, Server()->GetItemCount(ClientID, COOPERPIX), -1);
				GameServer()->SendChatTarget_Localization(ClientID, -1, _("~ 矿具: {str:name} 已损毁, 不能用了"), "name", Server()->GetItemName(ClientID, COOPERPIX), NULL);
			}
			Server()->SetItemSettingsCount(ClientID, COOPERPIX, Dropable-1);
			ItemName = Server()->GetItemName(ClientID, COOPERPIX);
			Temp += 10;	
		}
		else
		{
			Temp += 5;
		}
		if(Server()->GetItemSettings(ClientID, TITLEWORKF))
			Temp *= 2;

		m_Drop += Temp;

		GameServer()->CreateSound(m_Pos, 20); 

		int LevelItem = 1+Server()->GetItemCount(ClientID, MINEREXP)/g_Config.m_SvMinerExp;
		int ExpNeed = LevelItem*g_Config.m_SvMinerExp;
		int Exp = Server()->GetItemCount(ClientID, MINEREXP);
		
		float getlv = (m_Drop*100.0)/100;
		GameServer()->SendBroadcast_Localization(ClientID, 1000, 100, _("专长 - 采掘: {int:lvl}级 : {int:exp}/{int:expneed}经验\n工具: {str:name}x{int:count} ({int:brok}/{int:brok2})\n挖掘进度: {str:got} / {int:gotp}%"), 
			"lvl", &LevelItem, "exp", &Exp, "expneed", &ExpNeed, "brok", &Dropable, "brok2", &Broke, "name", ItemName, "count", &Count, "got", GameServer()->LevelString(100, (int)getlv, 10, ':', ' '), "gotp", &m_Drop, NULL);


		if(m_Drop >= 100)
		{
			int ItemDrop = 3+LevelItem/g_Config.m_SvMinerUpgrade;
			if(ItemDrop > 11)
				ItemDrop = 11;
			if(Server()->GetItemSettings(ClientID, TITLEWORKM))
			{
				LevelItem *= 2;
			}

			switch(random_int(0, ItemDrop))
			{
				case 3: GameServer()->GiveItem(ClientID, IRONORE, 1+LevelItem/30); break; 
				case 4: GameServer()->GiveItem(ClientID, GOLDORE, 1+LevelItem/45); break; 
				case 5: GameServer()->GiveItem(ClientID, DIAMONDORE, 1+LevelItem/60); break; 
				case 7: GameServer()->GiveItem(ClientID, DRAGONORE, 1+LevelItem/600); break; 
				default: GameServer()->GiveItem(ClientID, COOPERORE, 1+LevelItem/15); break;
			}
			GameServer()->GiveItem(ClientID, MINEREXP, 1);
			
			float StanProb = (Server()->GetItemCount(ClientID, MINECORE) + 1) * 0.001f;
			if(random_prob(min(0.1f, StanProb)))
			{	
				GameServer()->GiveItem(ClientID, STANNUM, 1);
				if(Server()->GetItemCount(ClientID, MINECORE))
				{
					GameServer()->GiveItem(ClientID, COOPERORE, 1);
					GameServer()->GiveItem(ClientID, IRONORE, 1);
					GameServer()->GiveItem(ClientID, GOLDORE, 1);
					GameServer()->GiveItem(ClientID, DIAMONDORE, 1);
					GameServer()->GiveItem(ClientID, DRAGONORE, 1);
					GameServer()->SendChatTarget_Localization(ClientID, -1, _("[{str:minecore}] 矿物额外奖励"), "minecore", Server()->GetItemName(ClientID, MINECORE), NULL);
				}
				
			}

			// 加经验
			int exp = 10 + LevelItem;
			GameServer()->m_apPlayers[ClientID]->AccData()->m_Exp += exp;
			GameServer()->SendChatTarget_Localization(ClientID, -1, _("[Player] 经验+{int:exp} +{int:bonus}点专长经验"), "exp", &exp, "bonus", &LevelItem, NULL);
		}
	}
	else if(m_SubType == 3) // ########################### WOOOD
	{
		int Dropable = 0;
		int Broke = 0;
		int Count = 0;
		int Temp = 0;

		const char* ItemName = "啥都没有";
		if(Server()->GetItemCount(ClientID, DRAGONAXE))
		{
			Count = Server()->GetItemCount(ClientID, DRAGONAXE);
			Broke = 608*Server()->GetItemCount(ClientID, DRAGONAXE);
			Dropable = Server()->GetItemSettings(ClientID, DRAGONAXE);
			if(Dropable <= 0)
			{
				Server()->RemItem(ClientID, DRAGONAXE, Server()->GetItemCount(ClientID, DRAGONAXE), -1);
				GameServer()->SendChatTarget_Localization(ClientID, -1, _("~ 工具: {str:name} 已损毁, 不能用了"), "name", Server()->GetItemName(ClientID, DRAGONAXE), NULL);
			}
			Server()->SetItemSettingsCount(ClientID, DRAGONAXE, Dropable-1);
			ItemName = Server()->GetItemName(ClientID, DRAGONAXE);
			Temp += 35;	
		}
		else
		{
			Temp += 10+random_int(0, 25);
		}

		if(Server()->GetItemSettings(ClientID, TITLEGLF))
			Temp *= 2;
		Temp += min(65, int(Server()->GetItemCount(ClientID, WOODCORE)) * 5);
		
		m_Drop += Temp;

		GameServer()->CreateSound(m_Pos, 20); 

		float getlv = (m_Drop*100.0)/100;
		GameServer()->SendBroadcast_Localization(ClientID, 1000, 100, _("专长 - 伐木工: (光头强不会升级)\n工具: {str:name}x{int:count} ({int:brok}/{int:brok2})\n砍伐进度: {str:got} / {int:gotp}%"), 
			"name", ItemName, "count", &Count, "brok", &Dropable, "brok2", &Broke, "got", GameServer()->LevelString(100, (int)getlv, 10, ':', ' '), "gotp", &m_Drop, NULL);


		if(m_Drop >= 100)
		{
			Temp = 1;
			if(Server()->GetItemSettings(ClientID, TITLEGLF))
				Temp += 2;
			GameServer()->GiveItem(ClientID, WOOD, Temp);
		
			// 加经验
			GameServer()->m_apPlayers[ClientID]->AccData()->m_Exp += 10;
			GameServer()->SendChatTarget_Localization(ClientID, -1, _("[Player] 经验+10 +并不存在的专长经验"), NULL);
		}
	}

	else if(m_SubType == 4) // ########################### LOADER
	{
		MaterFarm(ClientID, 1);
	}

	else if(m_SubType == 5) // ########################### LOADER FARM
	{
		MaterFarm(ClientID, 2);
	}

	if(m_Drop >= 100)
	{
		m_Drop = 0;
		GameServer()->CreateSoundGlobal(7, ClientID);
		if(m_SubType != 4)
			Picking(30);
	}
}
//TODO
void CPickup::MaterFarm(int ClientID, int MaterialID)
{
	if(Server()->GetMaterials(MaterialID) < 25)
		return	GameServer()->SendBroadcast_Localization(ClientID, 1000, 100, _("这里没有足够的材料. 至少需要25个"), NULL); 
	
	if(Server()->GetItemCount(ClientID, MATERIAL) > 50000)
		return	GameServer()->SendBroadcast_Localization(ClientID, 1000, 100, _("物品栏内最多塞50000个附魔材料. 在物品栏对装备附魔，或者卖给商店吧!"), NULL); 

	m_Drop += 25;
	GameServer()->CreateSound(m_Pos, 20); 

	int LevelItem = 1+Server()->GetItemCount(ClientID, LOADEREXP)/g_Config.m_SvMaterExp;
	int NeedExp = LevelItem*g_Config.m_SvMaterExp;
	int Exp = Server()->GetItemCount(ClientID, LOADEREXP);
	int Bonus = LevelItem*3 ;

	float getlv = (m_Drop*100.0)/100;
	GameServer()->SendBroadcast_Localization(ClientID, 1000, 100, _("专长 - 萃取: {int:lvl}级 : {int:exp}/{int:expneed}经验\n奖励: 25+{int:bonus} : 萃取进度: {str:got} / {int:gotp}%"), 
		"lvl", &LevelItem, "exp", &Exp, "expneed", &NeedExp, "bonus", &Bonus, "got", GameServer()->LevelString(100, (int)getlv, 10, ':', ' '), "gotp", &m_Drop, NULL);

	
	if(m_Drop >= 100)
	{
		Server()->SetMaterials(MaterialID, Server()->GetMaterials(MaterialID)-25);
		GameServer()->SendBroadcast_Localization(ClientID, 1000, 100, _("在装备栏对装备附魔或者卖给商店吧."), NULL);
		GameServer()->GiveItem(ClientID, MATERIAL, 25+LevelItem*3);
		GameServer()->GiveItem(ClientID, LOADEREXP, 10);

		// 加经验
		GameServer()->m_apPlayers[ClientID]->AccData()->m_Exp += 20+LevelItem;
		GameServer()->SendChatTarget_Localization(ClientID, -1, _("[Player] 经验+20 +{int:bonus}点专长经验"), "bonus", &LevelItem, NULL);

		Picking(5);
	}
}

void CPickup::TickPaused()
{
	if(m_SpawnTick != -1)
		++m_SpawnTick;
}

void CPickup::Snap(int SnappingClient)
{
	if(m_SpawnTick != -1 || NetworkClipped(SnappingClient))
		return;

	if(m_SubType != 1)
	{
		CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_ID, sizeof(CNetObj_Pickup)));
		if(!pP)
			return;

		pP->m_X = (int)m_Pos.x;
		pP->m_Y = (int)m_Pos.y;
		pP->m_Type = m_Type;
	}
	else
	{
		CNetObj_Projectile *pObj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_ID, sizeof(CNetObj_Projectile)));
		if(pObj)
		{
			pObj->m_X = (int)m_Pos.x;
			pObj->m_Y = (int)m_Pos.y;
			pObj->m_VelX = 0;
			pObj->m_VelY = 0;
			pObj->m_StartTick = Server()->Tick();
			pObj->m_Type = WEAPON_HAMMER;
		}		
	}
}
