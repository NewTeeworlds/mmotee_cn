/* 2019年2月10日13:36:17 */
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <new>
#include <iostream>
#include <engine/shared/config.h>
#include "player.h"
#include <engine/shared/network.h>
#include <game/server/entities/bots/monster.h>
#include <game/server/entities/bots/npcs.h>
#include <game/server/entities/bots/npcsw.h>
#include <game/server/entities/bots/kwah.h>
#include <game/server/entities/bots/boomer.h>
#include <game/server/entities/bots/bossslime.h>
#include <game/server/entities/bots/bosspig.h>
#include <game/server/entities/bots/bossguard.h>
#include <game/server/entities/bots/farmer.h>

MACRO_ALLOC_POOL_ID_IMPL(CPlayer, MAX_CLIENTS * ENGINE_MAX_MAPS + MAX_CLIENTS)

IServer *CPlayer::Server() const { return m_pGameServer->Server(); }

CPlayer::CPlayer(CGameContext *pGameServer, int ClientID, int Team)
{
	m_pGameServer = pGameServer;
	m_RespawnTick = Server()->Tick();
	m_DieTick = Server()->Tick();
	m_ScoreStartTick = Server()->Tick();

	m_pCharacter = 0;
	m_ClientID = ClientID;
	m_Team = GameServer()->m_pController->ClampTeam(Team);
	m_SpectatorID = SPEC_FREEVIEW;
	m_LastActionTick = Server()->Tick();
	m_SpecTick = Server()->Tick();

	m_Bot = (ClientID >= g_Config.m_SvMaxClients - MAX_BOTS);
	m_BotType = m_BotSubType = m_SelectItem = m_SelectArmor = -1;

	m_Authed = IServer::AUTHED_NO;
	int *pIdMap = Server()->GetIdMap(m_ClientID);
	for (int i = 1; i < VANILLA_MAX_CLIENTS; i++)
	{
		pIdMap[i] = -1;
	}
	pIdMap[0] = m_ClientID;

	m_MapMenu = 0;
	m_MapMenuItem = -1;
	m_MapMenuTick = -1;

	m_PrevTuningParams = *pGameServer->Tuning();
	m_NextTuningParams = m_PrevTuningParams;

	m_MoneyAdd = m_ExperienceAdd = m_aInviteClanID = m_Mana = 0;
	m_Search = m_BigBot = m_InArea = m_IsInGame = m_InBossed = false;

	m_pChatCmd = new CCmd(this, m_pGameServer);
	SetLanguage("en");

	InitSnap();
}

CPlayer::~CPlayer()
{
	delete m_pChatCmd;
	m_pChatCmd = 0;

	delete m_pCharacter;
	m_pCharacter = 0;
}

bool CPlayer::GetShop()
{
	if (m_pCharacter && m_pCharacter->m_InShop)
		return true;

	return false;
}

bool CPlayer::GetWork()
{
	if (m_pCharacter && m_pCharacter->m_InWork)
		return true;

	return false;
}

bool CPlayer::GetBoss()
{
	if (m_pCharacter && m_pCharacter->m_InBoss)
		return true;

	return false;
}

/*
Rand Craft Box 概率：
95% 中的 50% 为 Body Boomer，50% 为 Foot Kwah
5% 中，耳环蓝图、戒指蓝图、武器蓝图、Rare Slime Dirt各占 25%
*/
void CPlayer::RandomBoxTick()
{
	if (m_OpenBox && m_OpenBoxType == RANDOMCRAFTITEM)
	{
		int getitem = 0;
		if (m_OpenBox % 30 == 0)
		{
			if (random_prob(0.95f))
			{
				getitem = random_prob(0.5f) ? FOOTKWAH : HEADBOOMER;
			}
			else
			{
				switch (random_int(0, 3))
				{
				case 0:
					getitem = FORMULAFORRING;
					break;
				case 1:
					getitem = FORMULAWEAPON;
					break;
				case 2:
					getitem = RARESLIMEDIRT;
					break;
				default:
					getitem = FORMULAEARRINGS;
					break;
				}
			}
			if (m_pCharacter)
				GameServer()->CreateLolText(m_pCharacter, false, vec2(0, -75), vec2(0, -1), 10, Server()->GetItemName_en(getitem));

			if (m_OpenBox == 30)
			{
				m_OpenBox = 0;
				m_OpenBoxType = 0;

				if (m_pCharacter)
					GameServer()->CreateDeath(m_pCharacter->m_Pos, m_ClientID);

				GameServer()->GiveItem(m_ClientID, getitem, m_OpenBoxAmount);
				GameServer()->SendChatTarget_World(-1, CHATCATEGORY_DEFAULT, _("{str:name} 使用了物品:{str:used} x{int:num} 而且获得了 {str:get} x{int:num2}"),
														  "name", Server()->ClientName(m_ClientID), "used", Server()->GetItemName(m_ClientID, RANDOMCRAFTITEM, false), "num", &m_OpenBoxAmount, "get", Server()->GetItemName(m_ClientID, getitem, false), "num2", &m_OpenBoxAmount, NULL);
				m_OpenBoxAmount = 0;
			}
		}
	}
	/*
	Event Box 概率：
	79/80 概率为钱袋
	1/80 概率为 Rare Event Hammer
	*/
	if (m_OpenBox && m_OpenBoxType == EVENTBOX)
	{
		int getitem = 0;
		if (m_OpenBox % 30 == 0)
		{
			getitem = random_prob(0.995f) ? MONEYBAG : RAREEVENTHAMMER;

			if (m_pCharacter)
				GameServer()->CreateLolText(m_pCharacter, false, vec2(0, -75), vec2(0, -1), 10, Server()->GetItemName_en(getitem));

			if (m_OpenBox == 30)
			{
				m_OpenBox = 0;
				m_OpenBoxType = 0;

				if (m_pCharacter)
					GameServer()->CreateDeath(m_pCharacter->m_Pos, m_ClientID);

				GameServer()->GiveItem(m_ClientID, getitem, m_OpenBoxAmount);
				GameServer()->SendChatTarget_World(-1, CHATCATEGORY_DEFAULT, _("{str:name} 使用了物品:{str:used} x{int:num} 而且获得了 {str:get} x{int:num2}"),
														  "name", Server()->ClientName(m_ClientID), "used", Server()->GetItemName(m_ClientID, EVENTBOX, false), "num", &m_OpenBoxAmount, "get", Server()->GetItemName(m_ClientID, getitem, false), "num2", &m_OpenBoxAmount, NULL);
				m_OpenBoxAmount = 0;
			}
		}
	}
	/*
	Farming Box 概率：
	92% 中，5 个种地经验值，2 个钱袋，5 个 Event Box 各占三分之一
	剩下 8% 中，Jump Impuls 占 50%,Rare Freeazer 和 Rare Slime Dirt 各占 25%
	*/
	if (m_OpenBox && m_OpenBoxType == FARMBOX)
	{
		int getitem = 0;
		if (m_OpenBox % 30 == 0)
		{
			int Get = 1;
			if (random_prob(0.92f))
			{
				switch (random_int(0, 2))
				{
				case 0:
					getitem = FARMLEVEL;
					Get = 5 * m_OpenBoxAmount;
					break;
				case 1:
					getitem = MONEYBAG;
					Get = 2 * m_OpenBoxAmount;
					break;
				default:
					getitem = EVENTBOX;
					Get = 5 * m_OpenBoxAmount;
					break;
				}
			}
			else
			{
				switch (random_int(0, 3))
				{
				case 1:
					getitem = JUMPIMPULS;
					break;
				case 2:
					getitem = RARESLIMEDIRT;
					break;
				default:
					getitem = FREEAZER;
					break;
				}
				Get = m_OpenBoxAmount;
			}
			if (m_pCharacter)
				GameServer()->CreateLolText(m_pCharacter, false, vec2(0, -75), vec2(0, -1), 10, Server()->GetItemName_en(getitem));

			if (m_OpenBox == 30)
			{
				m_OpenBox = 0;
				m_OpenBoxType = 0;

				if (m_pCharacter)
					GameServer()->CreateDeath(m_pCharacter->m_Pos, m_ClientID);

				GameServer()->GiveItem(m_ClientID, getitem, Get);
				GameServer()->SendChatTarget_World(-1, CHATCATEGORY_DEFAULT, _("{str:name} 使用了物品:{str:used} x{int:num} 而且获得了 {str:get} x{int:num2}"),
														  "name", Server()->ClientName(m_ClientID), "used", Server()->GetItemName(m_ClientID, FARMBOX, false), "num", &m_OpenBoxAmount, "get", Server()->GetItemName(m_ClientID, getitem, false), "num2", &Get, NULL);
				m_OpenBoxAmount = 0;
			}
		}
	}
	if (m_OpenBox)
		m_OpenBox--;
}

void CPlayer::BasicAuthedTick()
{
	char aBuf[64];
	if (Server()->GetItemSettings(GetCID(), SSHOWCLAN))
	{
		str_format(aBuf, sizeof(aBuf), "%s", Server()->ClientClan(m_ClientID));
		str_copy(m_aTitle, aBuf, sizeof(m_aTitle));
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), "%s", TitleGot());
		str_copy(m_aTitle, aBuf, sizeof(m_aTitle));
	}

	if (Server()->GetItemCount(m_ClientID, PIGPORNO) > 1000 && !Server()->GetItemCount(m_ClientID, PIGPIG))
	{
		GameServer()->SendMail(m_ClientID, 3, PIGPIG, 1);
		GameServer()->SendChat(m_ClientID, m_Team, "系统已收回100个猪肉");
		Server()->RemItem(m_ClientID, PIGPORNO, 100, -1);
	}
	if (AccData()->m_Money >= 10000)
	{
		AccData()->m_Gold += AccData()->m_Money / 10000;
		int Got = AccData()->m_Money / 10000;

		AccData()->m_Money -= Got * 10000;
	}
	bool upgraded = false;
	unsigned long int needexp = AccData()->m_Level * GetNeedForUp();
	while (AccData()->m_Exp >= needexp)
	{

		upgraded = true;
		AccData()->m_Exp -= AccData()->m_Level * GetNeedForUp();
		AccData()->m_Level++;
		AccUpgrade()->m_SkillPoint += 1;
		AccUpgrade()->m_Upgrade += 2;
		needexp = AccData()->m_Level * GetNeedForUp();
		int GetBag = Server()->GetItemCount(m_ClientID, AMULETCLEEVER) ? 20 : 1;
		GameServer()->GiveItem(m_ClientID, MONEYBAG, GetBag);
		if (AccData()->m_Level % 10 == 0)
			GameServer()->SendMail(m_ClientID, 8, RANDOMCRAFTITEM, 3);
		if (AccData()->m_Level == 2)
			GameServer()->SendChatTarget_World(m_ClientID, CHATCATEGORY_DEFAULT, _("你现在可以去做任务了."), NULL);
	}
	if (upgraded)
	{
		GameServer()->SendChatTarget_World(m_ClientID, CHATCATEGORY_DEFAULT, _("[Level UP] 恭喜你你升级了! 你获得了技能点和升级点."), NULL);
		if (m_pCharacter)
		{
			GameServer()->CreateLolText(m_pCharacter, false, vec2(0, -75), vec2(0, -1), 50, "Level ++");
			GameServer()->CreateDeath(m_pCharacter->m_Pos, m_ClientID);
		}
		GameServer()->CreateSoundGlobal(SOUND_CTF_CAPTURE, m_ClientID);

		GameServer()->UpdateUpgrades(m_ClientID);
		GameServer()->UpdateStats(m_ClientID);
	}
}

void CPlayer::Tick()
{
#ifdef CONF_DEBUG
	if (!g_Config.m_DbgDummies || m_ClientID < MAX_CLIENTS - g_Config.m_DbgDummies)
#endif
		if (!Server()->ClientIngame(m_ClientID))
			return;

	if (Server()->IsClientLogged(m_ClientID) && AccData()->m_Level == -1)
	{
		AccData()->m_Level = 1;
		if (!Server()->GetSecurity(m_ClientID))
			GameServer()->SendChatTarget_Localization(m_ClientID, CHATCATEGORY_DEFAULT, _("你的账户有风险, 请设置安全设置(security)"), NULL);
	}

	if (!IsBot())
	{
		// Мана сучка ебал вас геи ебанные в рт вы шлюхи - 这句是作者在抱怨吧
		if (m_Mana < GetNeedMana())
		{
			if (!m_ManaTick)
			{
				m_Mana++;
				m_ManaTick = 10;
				GameServer()->SendBroadcast_LStat(m_ClientID, 2, 50, -1);
			}
			else
				m_ManaTick--;
		}

		// Снимаем ангру
		if (m_AngryWroth && Server()->Tick() % (1 * Server()->TickSpeed() * 20) == 0)
		{
			if (m_AngryWroth < 20)
				m_AngryWroth = 0;
			else
				m_AngryWroth -= 20;
		}

		// ОПЫТ КНИГИ ДОБАВКА НУЖНО ОПТИМИЗИРОВАТЬ
		// 经验书追加需要优化
		if (m_MoneyAdd)
		{
			m_MoneyAdd--;
			if (Server()->Tick() % (1 * Server()->TickSpeed() * 120) == 0 && m_MoneyAdd > 1500)
			{
				int Time = m_MoneyAdd / Server()->TickSpeed() / 60;
				GameServer()->SendChatTarget_Localization(m_ClientID, CHATCATEGORY_DEFAULT, _("物品:{str:name} 效果即将在 {int:ends} 分钟后失效."), "name", Server()->GetItemName(m_ClientID, BOOKMONEYMIN), "ends", &Time, NULL);
			}
			if (m_MoneyAdd == 1)
				GameServer()->SendChatTarget_Localization(m_ClientID, CHATCATEGORY_DEFAULT, _("物品:{str:name} 效果失效"), "name", Server()->GetItemName(m_ClientID, BOOKMONEYMIN), NULL);
		}
		if (m_ExperienceAdd)
		{
			m_ExperienceAdd--;
			if (Server()->Tick() % (1 * Server()->TickSpeed() * 120) == 0 && m_ExperienceAdd > 1500)
			{
				int Time = m_ExperienceAdd / Server()->TickSpeed() / 60;
				GameServer()->SendChatTarget_Localization(m_ClientID, CHATCATEGORY_DEFAULT, _("物品:{str:name} 即将在 {int:ends} 分钟后失效(ending)."), "name", Server()->GetItemName(m_ClientID, BOOKEXPMIN), "ends", &Time, NULL);
			}
			if (m_ExperienceAdd == 1)
				GameServer()->SendChatTarget_Localization(m_ClientID, CHATCATEGORY_DEFAULT, _("物品:{str:name} 失效(ended)了"), "name", Server()->GetItemName(m_ClientID, BOOKEXPMIN), NULL);
		}

		// Уровни и все такое повышение
		if (Server()->IsClientLogged(m_ClientID) && GetTeam() != TEAM_SPECTATORS)
		{
			if (g_Config.m_SvEventSchool)
			{
				if (Server()->Tick() % (1 * Server()->TickSpeed() * 1800) == 0)
				{
					int Type;
					switch (random_int(0, 8))
					{
					case 1:
						Type = COOPERPIX;
						break;
					case 2:
						Type = WOOD;
						break;
					case 3:
						Type = DRAGONORE;
						break;
					case 4:
						Type = COOPERORE;
						break;
					case 5:
						Type = IRONORE;
						break;
					case 6:
						Type = GOLDORE;
						break;
					case 7:
						Type = DIAMONDORE;
						break;
					default:
						Type = EVENTCUSTOMSOUL;
					}

					GameServer()->SendMail(m_ClientID, 9, Type, 1);
					GameServer()->SendChatTarget_World(m_ClientID, CHATCATEGORY_DEFAULT, _("在线奖励:{str:name} 获得了 {str:item}."), "name", Server()->ClientName(m_ClientID), "item", Server()->GetItemName(m_ClientID, Type), NULL);
				}
			}
			if (Server()->Tick() % (1 * Server()->TickSpeed() * 120) == 0 && g_Config.m_SvLoginControl)
			{
				Server()->UpdateOnline(m_ClientID);
			}
			BasicAuthedTick();
			RandomBoxTick();
		}

		// Агресия и тюрьма
		// 通缉和监狱
		if (!m_Search && AccData()->m_Rel >= 1000)
		{
			m_Search = true;
			GameServer()->SendChatTarget_World(-1, CHATCATEGORY_HEALER, _("玩家 {str:name} 被通缉了!"), "name", Server()->ClientName(m_ClientID), NULL);
		}
		if (m_JailTick && AccData()->m_Jail)
		{
			int Time = m_JailTick / Server()->TickSpeed();
			if (!AccData()->m_IsJailed)
			{
				GameServer()->SendBroadcast_Localization(m_ClientID, 100, 100, _("你进了监狱, 刑期:{sec:siska}."), "siska", &Time, NULL);
			}
			else
			{
				GameServer()->SendBroadcast_Localization(m_ClientID, 100, 100, _("你因得罪皇上（管理员）被打入大牢, 刑期:{sec:siska}."), "siska", &Time, NULL);
			}
			m_JailTick--;
			if (!m_JailTick)
			{
				m_JailTick = 0;
				AccData()->m_Jail = false;
				AccData()->m_IsJailed = false;
				AccData()->m_JailLength = 0;

				if (m_pCharacter)
					m_pCharacter->Die(m_ClientID, WEAPON_WORLD);

				GameServer()->UpdateStats(m_ClientID);
				GameServer()->SendChatTarget_World(-1, CHATCATEGORY_HEALER, _("玩家 {str:name}, 出狱了"), "name", Server()->ClientName(m_ClientID), NULL);
			}
		}
		if (GetTeam() != TEAM_SPECTATORS && AccData()->m_Rel > 0 && Server()->Tick() % (1 * Server()->TickSpeed() * 60) == 0)
		{
			AccData()->m_Rel -= 100;
			if (AccData()->m_Rel < 0)
				AccData()->m_Rel = 0;

			if (AccData()->m_Rel == 0 && m_Search)
			{
				m_Search = false;
				GameServer()->SendChatTarget_World(-1, CHATCATEGORY_HEALER, _("玩家 {str:name} 被取消通缉了"), "name", Server()->ClientName(m_ClientID), NULL);
			}
			GameServer()->SendBroadcast_Localization(m_ClientID, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE, _("交际愤怒值 -100. 你的交际愤怒值:{int:rel}"), "rel", &AccData()->m_Rel, NULL);
			GameServer()->UpdateStats(m_ClientID);
		}

		// вывод текста АРЕНА
		if (m_InArea)
		{
			if (GameServer()->m_AreaStartTick)
			{
				int Time = GameServer()->m_AreaStartTick / Server()->TickSpeed();
				GameServer()->SendBroadcast_Localization(m_ClientID, 101, 100, _("热身时间.{int:siska}秒后开始."), "siska", &Time, NULL);

				if (GameServer()->m_AreaStartTick == 100)
					GameServer()->SendBroadcast_Localization(m_ClientID, 105, 100, _("开战吧!!!"), NULL);
			}
			else if (GameServer()->m_AreaEndGame)
			{
				int Time = GameServer()->m_AreaEndGame / Server()->TickSpeed();
				int couns = GameServer()->GetAreaCount();
				GameServer()->SendBroadcast_Localization(m_ClientID, 102, 100, _("{int:siska}秒后结束战斗.{int:num}存活."), "siska", &Time, "num", &couns, NULL);
			}
		}

		// вывод текста по поводу ожидания времени босса
		if (m_InBossed)
		{
			if (GameServer()->m_WinWaitBoss)
			{
				int Time = GameServer()->m_WinWaitBoss / Server()->TickSpeed();
				GameServer()->SendBroadcast_Localization(m_ClientID, 101, 100, _("等待玩家在 {int:siska} 秒捡起物品."), "siska", &Time, NULL);
			}
			else if (GameServer()->m_BossStartTick > 10 * Server()->TickSpeed())
			{
				int Time = GameServer()->m_BossStartTick / Server()->TickSpeed();
				GameServer()->SendBroadcast_Localization(m_ClientID, 101, 100, _("等待玩家加入Boss战 {sec:siska}. Boss:{str:name}"), "siska", &Time, "name", GameServer()->GetBossName(GameServer()->m_BossType), NULL);
			}
			else if (Server()->Tick() % (1 * Server()->TickSpeed()) == 0 && GameServer()->m_BossStartTick > 100)
				GameServer()->SendGuide(m_ClientID, GameServer()->m_BossType);
			else if (GameServer()->m_BossStart)
				GameServer()->SendBroadcast_LBossed(m_ClientID, 250, 100);
		}

		// таймер синхронизации
		if (m_LoginSync)
		{
			m_LoginSync--;
			if (!m_LoginSync)
			{
				if (Server()->IsClientLogged(m_ClientID))
				{
					if (Server()->GetClanID(m_ClientID) > 0)
						Server()->UpdClanCount(Server()->GetClanID(m_ClientID));

					GameServer()->ResetVotes(m_ClientID, AUTH);
				}
			}
		}
		if (!Server()->IsClientLogged(m_ClientID))
			m_Team = -1;

		if (m_MapMenu > 0)
			m_MapMenuTick++;
	}

	// do latency stuff
	{
		IServer::CClientInfo Info;
		if (Server()->GetClientInfo(m_ClientID, &Info))
		{
			m_Latency.m_Accum += Info.m_Latency;
			m_Latency.m_AccumMax = max(m_Latency.m_AccumMax, Info.m_Latency);
			m_Latency.m_AccumMin = min(m_Latency.m_AccumMin, Info.m_Latency);
		}
		// each second
		if (Server()->Tick() % Server()->TickSpeed() == 0)
		{
			m_Latency.m_Avg = m_Latency.m_Accum / Server()->TickSpeed();
			m_Latency.m_Max = m_Latency.m_AccumMax;
			m_Latency.m_Min = m_Latency.m_AccumMin;
			m_Latency.m_Accum = 0;
			m_Latency.m_AccumMin = 1000;
			m_Latency.m_AccumMax = 0;
		}
	}

	if (!GameServer()->m_World.m_Paused)
	{
		if (!m_pCharacter && m_Team == TEAM_SPECTATORS && m_SpectatorID == SPEC_FREEVIEW)
			m_ViewPos -= vec2(clamp(m_ViewPos.x - m_LatestActivity.m_TargetX, -500.0f, 500.0f), clamp(m_ViewPos.y - m_LatestActivity.m_TargetY, -400.0f, 400.0f));

		if (!m_pCharacter && m_DieTick + Server()->TickSpeed() * 3 <= Server()->Tick())
			m_Spawning = true;

		if (m_pCharacter)
		{
			if (m_pCharacter->IsAlive())
			{
				m_ViewPos = m_pCharacter->m_Pos;
			}
			else
			{
				m_pCharacter->Destroy();
				delete m_pCharacter;
				m_pCharacter = 0;
			}
		}
		else if (m_Spawning && m_RespawnTick <= Server()->Tick())
			TryRespawn();
	}
	else
	{
		++m_RespawnTick;
		++m_DieTick;
		++m_ScoreStartTick;
		++m_LastActionTick;
	}
	HandleTuningParams();
}

int CPlayer::GetNeedForUp()
{
	// 玩家升级所需经验基数赋值
	if (AccData()->m_Level < 100)
		return 400;
	else if (AccData()->m_Level < 200)
		return 20000;
	else if (AccData()->m_Level < 300)
		return 70000;
	else if (AccData()->m_Level < 400)
		return 100000;
	else if (AccData()->m_Level < 500)
		return 120000;
	else if (AccData()->m_Level < 600)
		return 140000;
	else if (AccData()->m_Level < 700)
		return 180000;
	else if (AccData()->m_Level < 1000)
		return 210000;
	else if (AccData()->m_Level < 1100)
		return 250000;
	else if (AccData()->m_Level < 1200)
		return 500000;
	else
		return 400000;
}

int CPlayer::GetNeedForUpgClan(Clan Type)
{
	int Get = Server()->GetClan(Type, Server()->GetClanID(m_ClientID));
	if (Type != Clan::Level)
	{
		return 100 + Get * 500;
	}
	else
	{
		return 1000000 + Get * 1000000;
	}
}

void CPlayer::PostTick()
{
	// update latency value
	if (m_PlayerFlags & PLAYERFLAG_SCOREBOARD)
	{
		for (int i = 0; i < MAX_PLAYERS; ++i)
		{
			if (GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
				m_aActLatency[i] = GameServer()->m_apPlayers[i]->m_Latency.m_Min;
		}
	}

	// update view pos for spectators
	if (m_Team == TEAM_SPECTATORS && m_SpectatorID != SPEC_FREEVIEW && GameServer()->m_apPlayers[m_SpectatorID])
		m_ViewPos = GameServer()->m_apPlayers[m_SpectatorID]->m_ViewPos;
}

void CPlayer::HandleTuningParams()
{
	if (!(m_PrevTuningParams == m_NextTuningParams))
	{
		if (m_IsReady)
		{
			CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
			int *pParams = (int *)&m_NextTuningParams;
			for (unsigned i = 0; i < sizeof(m_NextTuningParams) / sizeof(int); i++)
				Msg.AddInt(pParams[i]);
			Server()->SendMsg(&Msg, MSGFLAG_VITAL, GetCID(), -1);
		}
		m_PrevTuningParams = m_NextTuningParams;
	}
	m_NextTuningParams = *GameServer()->Tuning();
}

void CPlayer::MoneyAdd(int Size, bool ClanBonus, bool MoneyDouble)
{
	if (IsBot())
		return;

	int GetMoney = Size;
	if (ClanBonus && Server()->GetClanID(m_ClientID))
		GetMoney = (Size + Server()->GetClan(Clan::MoneyAdd, Server()->GetClanID(m_ClientID)) * 100);

	if (MoneyDouble)
	{
		if (Server()->GetItemSettings(m_ClientID, X2MONEYEXPVIP))
			GetMoney = (int)GetMoney * (Server()->GetItemCount(m_ClientID, X2MONEYEXPVIP) * 2);
		else if (MoneyDouble && (m_MoneyAdd))
			GetMoney = (int)(GetMoney * 2);
	}

	if (Size >= 10000)
	{
		AccData()->m_Gold += Size / 10000;
		int Got = (int)(Size / 10000);

		AccData()->m_Money -= Got * 10000;
	}

	GameServer()->SendBroadcast_LStat(m_ClientID, BROADCAST_PRIORITY_GAMEANNOUNCE, 100, INADDMONEY, GetMoney);
	AccData()->m_Money += GetMoney;
	if (random_prob(0.125f))
		GameServer()->UpdateStats(m_ClientID);

	GameServer()->ResetVotes(m_ClientID, AUTH);
	return;
}

void CPlayer::ExpAdd(unsigned long int Size, bool Bonus)
{
	if (IsBot())
		return;

	unsigned long int GetExp = Size, Get = 0;
	if (Bonus && Server()->GetClanID(m_ClientID))
	{
		Get = Size * 50;
		Server()->InitClanID(Server()->GetClanID(m_ClientID), PLUS, "Exp", Get, true);
		GetExp = Size + Server()->GetClan(Clan::ExpAdd, Server()->GetClanID(m_ClientID));
	}

	if (Bonus && m_ExperienceAdd)
		GetExp = GetExp * 2;
	if (Server()->GetItemSettings(m_ClientID, X2MONEYEXPVIP))
		GetExp = GetExp * ((Server()->GetItemCount(m_ClientID, X2MONEYEXPVIP)) + 1);

	if (Server()->GetClanID(m_ClientID) &&
		Server()->GetClan(Clan::Exp, Server()->GetClanID(m_ClientID)) >= Server()->GetClan(Clan::Level, Server()->GetClanID(m_ClientID)) * GetNeedForUpgClan(Clan::Level))
	{
		GameServer()->SendChatClan(Server()->GetClanID(m_ClientID), "[公会] 您所在的公会升级了!");

		int warpminus = Server()->GetClan(Clan::Level, Server()->GetClanID(m_ClientID)) * GetNeedForUpgClan(Clan::Level);
		Server()->InitClanID(Server()->GetClanID(m_ClientID), MINUS, "Exp", warpminus, true);
		Server()->InitClanID(Server()->GetClanID(m_ClientID), PLUS, "Level", 1, true);
	}

	GameServer()->SendBroadcast_LStat(m_ClientID, BROADCAST_PRIORITY_GAMEANNOUNCE, 100, Server()->GetClanID(m_ClientID) > 0 ? INADDCEXP : INADDEXP, GetExp, Get);
	AccData()->m_Exp += GetExp;
	if (random_prob(0.125f))
		GameServer()->UpdateStats(m_ClientID);

	return;
}

void CPlayer::Snap(int SnappingClient)
{
#ifdef CONF_DEBUG
	if (!g_Config.m_DbgDummies || m_ClientID < MAX_CLIENTS - g_Config.m_DbgDummies)
#endif
		if (!Server()->ClientIngame(m_ClientID))
			return;

	int id = m_ClientID;
	if (SnappingClient > -1 && !Server()->Translate(id, SnappingClient))
		return;

	CNetObj_ClientInfo *pClientInfo = static_cast<CNetObj_ClientInfo *>(Server()->SnapNewItem(NETOBJTYPE_CLIENTINFO, id, sizeof(CNetObj_ClientInfo)));
	if (!pClientInfo)
		return;

	if (Server()->IsClientLogged(m_ClientID) && GetTeam() != TEAM_SPECTATORS)
	{
		char pSendName[32];
		str_format(pSendName, sizeof(pSendName), "%s", Server()->ClientName(m_ClientID));
		StrToInts(&pClientInfo->m_Name0, 4, pSendName);

		if (IsBot() && m_pCharacter)
		{

			float getlv = ((m_Health * 100.0) / m_HealthStart) - 1;
			switch (GetBotType())
			{
			default:
				str_format(pSendName, sizeof(pSendName), "%d:%s[%d\%]", AccData()->m_Level, Server()->ClientName(m_ClientID), (int)getlv);
				break;
			case BOT_GUARD:
			case BOT_BOSSSLIME:
			case BOT_BOSSVAMPIRE:
			case BOT_BOSSPIGKING:
			case BOT_BOSSGUARD:
				str_format(pSendName, sizeof(pSendName), "%s[%d\%]", Server()->ClientName(m_ClientID), (int)getlv);
				break;
			case BOT_NPCW:
				str_format(pSendName, sizeof(pSendName), "%s", Server()->ClientName(m_ClientID));
				break;
			}
			StrToInts(&pClientInfo->m_Name0, 4, pSendName);
		}
	}
	else
		StrToInts(&pClientInfo->m_Name0, 4, Server()->ClientName(m_ClientID));

	char aClan[32];
	str_copy(aClan, Server()->ClientClan(m_ClientID), sizeof(aClan));

	if(Server()->GetClientChangeMap(GetCID()))
		str_copy(aClan, "换图ing", sizeof(aClan));
	else if(Server()->IsClientLogged(GetCID()))
		str_copy(aClan, m_aTitle, sizeof(aClan));

	StrToInts(&pClientInfo->m_Clan0, 3, aClan);

	pClientInfo->m_Country = Server()->ClientCountry(m_ClientID);

	StrToInts(&pClientInfo->m_Skin0, 6, m_TeeInfos.m_aSkinName);
	pClientInfo->m_UseCustomColor = m_TeeInfos.m_UseCustomColor;
	pClientInfo->m_ColorBody = m_TeeInfos.m_ColorBody;
	pClientInfo->m_ColorFeet = m_TeeInfos.m_ColorFeet;

	CNetObj_PlayerInfo *pPlayerInfo = static_cast<CNetObj_PlayerInfo *>(Server()->SnapNewItem(NETOBJTYPE_PLAYERINFO, id, sizeof(CNetObj_PlayerInfo)));
	if (!pPlayerInfo)
		return;
	if (!IsBot())
	{
		pPlayerInfo->m_Latency = SnappingClient == -1 ? m_Latency.m_Min : GameServer()->m_apPlayers[SnappingClient]->m_aActLatency[m_ClientID];
	}
	else
	{
		pPlayerInfo->m_Latency = 0;
	}
	pPlayerInfo->m_Local = 0;
	pPlayerInfo->m_ClientID = id;

	if (IsBoss() || !IsBot())
		pPlayerInfo->m_Score = AccData()->m_Level;
	else
		pPlayerInfo->m_Score = 0;

	if (!IsBot())
		pPlayerInfo->m_Team = m_Team;
	else
		pPlayerInfo->m_Team = 10;

	if (m_ClientID == SnappingClient)
		pPlayerInfo->m_Local = 1;

	if (m_ClientID == SnappingClient && m_Team == TEAM_SPECTATORS)
	{
		CNetObj_SpectatorInfo *pSpectatorInfo = static_cast<CNetObj_SpectatorInfo *>(Server()->SnapNewItem(NETOBJTYPE_SPECTATORINFO, m_ClientID, sizeof(CNetObj_SpectatorInfo)));
		if (!pSpectatorInfo)
			return;

		pSpectatorInfo->m_SpectatorID = m_SpectatorID;
		pSpectatorInfo->m_X = m_ViewPos.x;
		pSpectatorInfo->m_Y = m_ViewPos.y;
	}
}

void CPlayer::FakeSnap(int SnappingClient)
{
	IServer::CClientInfo info;
	Server()->GetClientInfo(SnappingClient, &info);
	if (info.m_CustClt)
		return;

	int id = VANILLA_MAX_CLIENTS - 1;

	CNetObj_ClientInfo *pClientInfo = static_cast<CNetObj_ClientInfo *>(Server()->SnapNewItem(NETOBJTYPE_CLIENTINFO, id, sizeof(CNetObj_ClientInfo)));

	if (!pClientInfo)
		return;

	StrToInts(&pClientInfo->m_Name0, 4, " ");
	StrToInts(&pClientInfo->m_Clan0, 3, "");
	StrToInts(&pClientInfo->m_Skin0, 6, m_TeeInfos.m_aSkinName);
}

void CPlayer::OnDisconnect(int Type, const char *pReason)
{
	KillCharacter();
}

void CPlayer::OnPredictedInput(CNetObj_PlayerInput *NewInput)
{
	// skip the input if chat is active
	if ((m_PlayerFlags & PLAYERFLAG_CHATTING) && (NewInput->m_PlayerFlags & PLAYERFLAG_CHATTING))
		return;

	if (m_pCharacter)
		m_pCharacter->OnPredictedInput(NewInput);
}

void CPlayer::OnDirectInput(CNetObj_PlayerInput *NewInput)
{
	if (NewInput->m_PlayerFlags & PLAYERFLAG_CHATTING)
	{
		// skip the input if chat is active
		if (m_PlayerFlags & PLAYERFLAG_CHATTING)
			return;

		// reset input
		if (m_pCharacter)
			m_pCharacter->ResetInput();

		m_PlayerFlags = NewInput->m_PlayerFlags;
		return;
	}

	m_PlayerFlags = NewInput->m_PlayerFlags;

	if (m_pCharacter)
		m_pCharacter->OnDirectInput(NewInput);

	if (!m_pCharacter && m_Team != TEAM_SPECTATORS && (NewInput->m_Fire & 1))
		m_Spawning = true;

	// check for activity
	if (NewInput->m_Direction || m_LatestActivity.m_TargetX != NewInput->m_TargetX ||
		m_LatestActivity.m_TargetY != NewInput->m_TargetY || NewInput->m_Jump ||
		NewInput->m_Fire & 1 || NewInput->m_Hook)
	{
		m_LatestActivity.m_TargetX = NewInput->m_TargetX;
		m_LatestActivity.m_TargetY = NewInput->m_TargetY;
		m_LastActionTick = Server()->Tick();
	}
}

CCharacter *CPlayer::GetCharacter()
{
	if (m_pCharacter && m_pCharacter->IsAlive())
		return m_pCharacter;
	return 0;
}

void CPlayer::KillCharacter(int Weapon)
{
	if(m_pCharacter)
	{
		m_pCharacter->Die(m_ClientID, Weapon);

		delete m_pCharacter;
		m_pCharacter = nullptr;
	}
}

void CPlayer::Respawn()
{
	if (m_Team != TEAM_SPECTATORS)
		m_Spawning = true;
}

void CPlayer::SetTeam(int Team, bool DoChatMsg)
{
	// clamp the team
	Team = GameServer()->m_pController->ClampTeam(Team);
	if (DoChatMsg)
		GameServer()->SendChatTarget_Localization(-1, -1, _("{str:PlayerName} joined the RPG Azataz"), "PlayerName", Server()->ClientName(m_ClientID), NULL);

	KillCharacter();

	m_Team = Team;
	m_LastActionTick = Server()->Tick();
	m_SpectatorID = SPEC_FREEVIEW;

	// we got to wait 0.5 secs before respawning
	m_RespawnTick = Server()->Tick() + Server()->TickSpeed() / 2;
	GameServer()->m_pController->OnPlayerInfoChange(GameServer()->m_apPlayers[m_ClientID]);

	if (Team == TEAM_SPECTATORS)
	{
		// update spectator modes
		for (int i = 0; i < MAX_PLAYERS; ++i)
		{
			if (GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->m_SpectatorID == m_ClientID)
				GameServer()->m_apPlayers[i]->m_SpectatorID = SPEC_FREEVIEW;
		}
	}
}

void CPlayer::TryRespawn()
{
	vec2 SpawnPos;
	if (!GameServer()->m_pController->PreSpawn(this, &SpawnPos))
		return;

	m_Spawning = false;

	const int AllocMemoryCell = m_ClientID + GameServer()->GetMapID() * MAX_CLIENTS;

	if (IsBot())
	{
		// жирный бот рандом
		if (random_prob(0.1f))
			m_BigBot = true;
		else
			m_BigBot = false;

		GameServer()->UpdateBotInfo(m_ClientID);
		switch (m_BotType)
		{
		case BOT_L1MONSTER:
			m_pCharacter = new (AllocMemoryCell) CMonster(&GameServer()->m_World);

			if (GameServer()->m_CityStart == 1)
			{
				AccData()->m_Level = m_BigBot ? 280 + random_int(0, 3) : 250;
				AccUpgrade()->m_Health = 100 + AccData()->m_Level * 20;
				AccUpgrade()->m_Damage = AccData()->m_Level + 50;
			}
			else
			{
				AccData()->m_Level = m_BigBot ? 10 + random_int(0, 3) : 5;
				AccUpgrade()->m_Health = m_BigBot ? AccData()->m_Level : 0;
				if (m_BigBot)
				{
					Server()->SetMaxAmmo(m_ClientID, INFWEAPON_GUN, 10);
					Server()->SetAmmoRegenTime(m_ClientID, INFWEAPON_GUN, 100);
					Server()->SetFireDelay(m_ClientID, INFWEAPON_GUN, 800);
				}
			}
			break;
		case BOT_L2MONSTER:
			m_pCharacter = new (AllocMemoryCell) CKwah(&GameServer()->m_World);

			if (GameServer()->m_CityStart == 1)
			{
				AccData()->m_Level = m_BigBot ? 370 + random_int(0, 3) : 350 + random_int(0, 3);
				AccUpgrade()->m_Health = 100 + AccData()->m_Level * 2;
				AccUpgrade()->m_Damage = AccData()->m_Level + 50;
			}
			else
			{
				AccData()->m_Level = m_BigBot ? 140 : 125 + random_int(0, 3);
				AccUpgrade()->m_Health = 100 + AccData()->m_Level;
				AccUpgrade()->m_Damage = AccData()->m_Level / 2;
			}
			break;
		case BOT_L3MONSTER:
			m_pCharacter = new (AllocMemoryCell) CBoomer(&GameServer()->m_World);

			if (GameServer()->m_CityStart == 1)
			{
				AccData()->m_Level = m_BigBot ? 510 + random_int(0, 3) : 490 + random_int(0, 15);
				AccUpgrade()->m_Health = 100 + (int)(AccData()->m_Level * 2);
				AccUpgrade()->m_Damage = (int)(AccData()->m_Level + 50);
			}
			else
			{
				AccData()->m_Level = m_BigBot ? 250 + random_int(0, 3) : 200 + random_int(0, 3);
				AccUpgrade()->m_Health = 100 + AccData()->m_Level;
				AccUpgrade()->m_Damage = AccData()->m_Level;
			}
			break;
		case BOT_BOSSSLIME:
			m_pCharacter = new (AllocMemoryCell) CBossSlime(&GameServer()->m_World);
			AccData()->m_Level = 1000 + random_int(0, 3);

			m_BigBot = true;

			AccUpgrade()->m_Health = (int)(AccData()->m_Level / 3);
			AccUpgrade()->m_Damage = 100;
			break;
		case BOT_BOSSVAMPIRE:
			m_pCharacter = new (AllocMemoryCell) CBossSlime(&GameServer()->m_World);
			AccData()->m_Level = 1500 + random_int(0, 3);

			m_BigBot = true;

			AccUpgrade()->m_Health = (int)(AccData()->m_Level / 3);
			AccUpgrade()->m_Damage = (int)(AccData()->m_Level * 2);
			break;
		case BOT_BOSSPIGKING:
			m_pCharacter = new (AllocMemoryCell) CBossPig(&GameServer()->m_World);
			AccData()->m_Level = 300 + random_int(3, 13);
			m_BigBot = true;
			AccUpgrade()->m_Health = (int)(AccData()->m_Level * 2);
			AccUpgrade()->m_Damage = (int)(AccData()->m_Level * 5);
			break;
		case BOT_BOSSGUARD:
			m_pCharacter = new (AllocMemoryCell) CBossGuard(&GameServer()->m_World);
			AccData()->m_Level = 2000 + random_int(0, 100);
			AccUpgrade()->m_Damage = (int)(AccData()->m_Level * 20);
			m_BigBot = true;
			break;
		case BOT_GUARD:
			m_pCharacter = new (AllocMemoryCell) CNpcSold(&GameServer()->m_World);
			AccData()->m_Level = 1000 + random_int(0, 10);
			AccUpgrade()->m_Damage = (int)(AccData()->m_Level * 5);
			AccUpgrade()->m_Health = (int)(AccData()->m_Level * 50);
			m_BigBot = true;
			break;
		case BOT_NPCW:
			m_pCharacter = new (AllocMemoryCell) CNpcWSold(&GameServer()->m_World);
			AccData()->m_Level = 3;
			AccUpgrade()->m_Damage = (int)(AccData()->m_Level * 5);
			AccUpgrade()->m_Health = (int)(AccData()->m_Level * 2);
			m_BigBot = true;
			break;
		case BOT_FARMER:
			m_pCharacter = new (AllocMemoryCell) CNpcFarmer(&GameServer()->m_World);
			AccData()->m_Level = 3;
			AccUpgrade()->m_Damage = (int)(AccData()->m_Level * 5);
			AccUpgrade()->m_Health = (int)(AccData()->m_Level * 2);
			m_BigBot = true;
			break;
		case BOT_BOSSCLEANER:
			m_pCharacter = new (AllocMemoryCell) CBossSlime(&GameServer()->m_World);
			AccData()->m_Level = 1000 + random_int(0, 3);

			m_BigBot = true;

			AccUpgrade()->m_Health = (int)(AccData()->m_Level / 3);
			AccUpgrade()->m_Damage = 100;
			if (GameServer()->m_CityStart == 1)
				AccUpgrade()->m_Damage = 400;
			break;
		default: 
			dbg_msg("sys", "Invalid value %d in %s:%d", m_BotType, __FILE__, __LINE__); 
			break;
		}
		Server()->SetMaxAmmo(m_ClientID, INFWEAPON_HAMMER, -1);
		Server()->SetAmmoRegenTime(m_ClientID, INFWEAPON_HAMMER, 0);
		Server()->SetFireDelay(m_ClientID, INFWEAPON_HAMMER, 1);
	}
	else
		m_pCharacter = new (AllocMemoryCell) CCharacter(&GameServer()->m_World);

	m_pCharacter->Spawn(this, SpawnPos);
	if (GetClass() != PLAYERCLASS_NONE)
		GameServer()->CreatePlayerSpawn(SpawnPos);
}

int CPlayer::GetClass()
{
	return AccData()->m_Class;
}

void CPlayer::SetClassSkin(int newClass, int State)
{
	switch (newClass)
	{
	case PLAYERCLASS_ASSASINS:
		m_TeeInfos.m_UseCustomColor = 0;
		str_copy(m_TeeInfos.m_aSkinName, "bluekitty", sizeof(m_TeeInfos.m_aSkinName));
		break;
	case PLAYERCLASS_BERSERK:
		m_TeeInfos.m_UseCustomColor = 0;
		str_copy(m_TeeInfos.m_aSkinName, "coala", sizeof(m_TeeInfos.m_aSkinName));
		break;
	case PLAYERCLASS_HEALER:
		m_TeeInfos.m_UseCustomColor = 0;
		str_copy(m_TeeInfos.m_aSkinName, "redstripe", sizeof(m_TeeInfos.m_aSkinName));
		break;
	default:
		m_TeeInfos.m_UseCustomColor = 0;
		str_copy(m_TeeInfos.m_aSkinName, "default", sizeof(m_TeeInfos.m_aSkinName));
	}
}

void CPlayer::SetClass(int newClass)
{
	if (AccData()->m_Class == newClass)
		return;

	AccData()->m_Class = newClass;
	SetClassSkin(newClass);

	if (m_pCharacter)
		m_pCharacter->SetClass(newClass);
}

bool CPlayer::IsKownClass(int c)
{
	return m_aKnownClass[c];
}

const char *CPlayer::GetLanguage()
{
	return m_aLanguage;
}

const char *CPlayer::GetClassName()
{
	if (AccData()->m_Class == PLAYERCLASS_ASSASINS)
		return "Assasin";
	else if (AccData()->m_Class == PLAYERCLASS_BERSERK)
		return "Berserk";
	else if (AccData()->m_Class == PLAYERCLASS_HEALER)
		return "Healer";
	else
		return "You bitch";
}

void CPlayer::SetLanguage(const char *pLanguage)
{
	str_copy(m_aLanguage, pLanguage, sizeof(m_aLanguage));
}
void CPlayer::OpenMapMenu(int Menu)
{
	m_MapMenu = Menu;
	m_MapMenuTick = 0;
}

void CPlayer::CloseMapMenu()
{
	m_MapMenu = 0;
	m_MapMenuTick = -1;
}

bool CPlayer::MapMenuClickable()
{
	return (m_MapMenu > 0 && (m_MapMenuTick > Server()->TickSpeed() / 2));
}

void CPlayer::ResetUpgrade(int ClientID)
{
	if (Server()->IsClientLogged(m_ClientID))
	{
		int Back = AccUpgrade()->m_Speed * 50 + AccUpgrade()->m_Health * 20 + AccUpgrade()->m_Damage * 50 + AccUpgrade()->m_HPRegen * 20 + AccUpgrade()->m_Mana * 10 + AccUpgrade()->m_AmmoRegen * 50 + AccUpgrade()->m_Ammo * 100 + AccUpgrade()->m_Spray * 100;
		AccUpgrade()->m_Speed = AccUpgrade()->m_Health = AccUpgrade()->m_Damage = AccUpgrade()->m_HPRegen = AccUpgrade()->m_Mana = 0;
		AccUpgrade()->m_AmmoRegen = AccUpgrade()->m_Ammo = AccUpgrade()->m_Spray = 0;

		AccUpgrade()->m_Upgrade += Back;
		GameServer()->UpdateUpgrades(ClientID);
	}
}

void CPlayer::ResetSkill(int ClientID)
{
	if (Server()->IsClientLogged(m_ClientID))
	{
		int Back = AccUpgrade()->m_HammerRange * 15 + AccUpgrade()->m_Pasive2 * 15;
		AccUpgrade()->m_Pasive2 = AccUpgrade()->m_HammerRange = 0;
		AccUpgrade()->m_SkillPoint += Back;
		GameServer()->UpdateUpgrades(ClientID);
	}
}

const char *CPlayer::TitleGot()
{
	if (Server()->GetItemSettings(m_ClientID, X2MONEYEXPVIP))
	{
		int i = Server()->GetItemCount(m_ClientID, X2MONEYEXPVIP);
		switch (i)
		{
		case 1:
			return "青铜VIP1";
		case 2:
			return "白银VIP2";
		case 3:
			return "黄金VIP3";
		case 4:
			return "巨石VIP4";
		case 5:
			return "大佬VIP5";
		case 6:
			return "巨佬VIP6";
		case 7:
			return "超佬VIP7";
		case 8:
			return "神级VIP8";
		case 9:
			return "VIP180";
		default:
			return "#人上人#";
		}
	}
	else if (Server()->GetItemSettings(m_ClientID, TITLEQUESTS))
		return "1阶段";
	else if (Server()->GetItemSettings(m_ClientID, BOSSDIE))
		return "Boss克星";
	else if (Server()->GetItemSettings(m_ClientID, PIGPIG))
		return "猪猪";
	else if (Server()->GetItemSettings(m_ClientID, BIGCRAFT))
		return "合成师";
	else if (Server()->GetItemSettings(m_ClientID, TITLESUMMER))
		return "日光浴";
	else if (Server()->GetItemSettings(m_ClientID, TITLEENCHANT))
		return "魔法师";
	else if(Server()->GetItemSettings(m_ClientID, TITLEDNTCRIT))
		return "GG爆";
	else if(Server()->GetItemSettings(m_ClientID, TITLEDNTHP))
		return "TANK!";
	else if(Server()->GetItemSettings(m_ClientID, TITLETEEFUN))
		return "TeeFun";
	else if(Server()->GetItemSettings(m_ClientID, TITLEWORKF))
		return "工爷";
	else if(Server()->GetItemSettings(m_ClientID, TITLEWORKM))
		return "工奶";
	else if(Server()->GetItemSettings(m_ClientID, TITLEFARMF))
		return "农爷";
	else if(Server()->GetItemSettings(m_ClientID, TITLEFARMM))
		return "农奶";
	else if(Server()->GetItemSettings(m_ClientID, TITLEHANDCRAFT))
		return "手工业";
	else if(Server()->GetItemSettings(m_ClientID, TITLEPPP))
		return "GS合营";
	else if(Server()->GetItemSettings(m_ClientID, TITLECR))
		return "文革";
	else if(Server()->GetItemSettings(m_ClientID, TITLEGLF))
		return "大跃进";
	else if(Server()->GetItemSettings(m_ClientID, TITLEPC))
		return "公社";
	//else if(Server()->GetItemSettings(m_ClientID, TITLEMOON))
	//	return "~〇~ Moon ~〇~";	
	else 
		return "新玩家";
}

bool CPlayer::IsBoss()
{
	switch (GetBotType())
	{
	case BOT_BOSSSLIME:
	case BOT_BOSSVAMPIRE:
	case BOT_BOSSPIGKING:
	case BOT_BOSSGUARD:
		return true;
	
	default:
		return false;
	}
}

bool CPlayer::GetSnap(int EntityID)
{
	if(EntityID >= NUM_ENTTYPES || EntityID < 0)
		return false;
	
	return m_aShouldSnap[EntityID];
}

void CPlayer::SetSnap(int EntityID, bool Snap)
{
	if(EntityID >= NUM_ENTTYPES || EntityID < 0)
		return;
	
	m_aShouldSnap[EntityID] = Snap;
}

void CPlayer::InitSnap()
{
	for (int i = 0; i < NUM_ENTTYPES; i++)
		SetSnap(i, false);

	SetSnap(ENTTYPE_CHARACTER, true);
}

void CPlayer::UpdateSnap()
{
	for (int i = 0; i < NUM_ENTTYPES; i++)
			SetSnap(i, true);

	switch (Server()->GetItemSettings(GetCID(), SANTIPING))
	{
	case 3:
		for (int i = 0; i < NUM_ENTTYPES; i++)
		{
			if (i == ENTTYPE_CHARACTER)
				continue;
			SetSnap(i, false);
		}
		break;
	case 2:
	{
		SetSnap(ENTTYPE_PICKUP, false);
		SetSnap(ENTTYPE_GROWINGEXPLOSION, false);
		SetSnap(ENTTYPE_BONUS, false);
		SetSnap(ENTTYPE_BIOLOGIST_MINE, false);	
		[[fallthrough]];
	}
	case 1:
		SetSnap(ENTTYPE_FLYINGPOINT, false);
		SetSnap(ENTTYPE_SNAP_FULLPICKUP, false);
		SetSnap(ENTTYPE_SNAP_FULLPROJECT, false);
		SetSnap(ENTTYPE_FEYA, false);
		SetSnap(ENTTYPE_BUFFS, false);
		SetSnap(ENTTYPE_DRAW, false);
		SetSnap(ENTTYPE_HEALTHHEALER, false);
		SetSnap(ENTTYPE_SWORD, false);
		break;
		
	default:
		break;
	}
}