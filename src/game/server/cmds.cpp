#include <stdio.h>
#include <string.h>

#include <engine/shared/config.h>
#include <engine/server.h>
#include <game/version.h>
#include "cmds.h"

#include "playerdata.h"

/*
聊天指令一览： ()为可选项，<>为必填项
1.注册与登录
/login (用户名) <密码> 登录
/register <用户名> <密码> 注册
/password <密码> <重复密码> 修改密码
2.公会指令
/newclan <公会名称> 创建公会
/invite <玩家名称> 邀请玩家进入公会 (需要公会所有者权限)
3.其他常用指令
/cmdlist 显示命令列表 (不全)
/lang (语言ID) 设置语言 (留空显示可用语言列表)
4.管理员指令
/sd <声音ID> 设置声音(?)
/giveitem <玩家ID> <物品ID> <物品数量> (物品等级) 给某人物品
/remitem <玩家ID> <物品ID> <物品数量> 拿走某人物品
/sendmail <玩家ID> <物品ID> <物品数量> 通过邮件向某人发送物品
/givedonate <玩家ID> <黄金数量> 给某人点券,购买捐赠物品
/jail <玩家ID> <时长(秒)> 将某人关进监狱
/unjail <玩家ID> 将某人放出监狱
/chpw <用户名> <密码> 修改某人密码
*/
CCmd::CCmd(CPlayer *pPlayer, CGameContext *pGameServer)
{
	m_pPlayer = pPlayer;
	m_pGameServer = pGameServer;
}

void CCmd::ChatCmd(CNetMsg_Cl_Say *Msg)
{
	int ClientID = m_pPlayer->GetCID() >= 0 ? m_pPlayer->GetCID() : -1;
	if (!strncmp(Msg->m_pMessage, "/login", 6))
	{
		LastChat();
		if (GameServer()->Server()->IsClientLogged(ClientID))
		{
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("你已登录"), NULL);
		}
		char Username[256], Password[256];
		if (GameServer()->Server()->GetSecurity(ClientID))
		{
			if (sscanf(Msg->m_pMessage, "/login %s %s", Username, Password) != 2)
			{
				GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("这个账户启用了安全设置,需要用户名与密码!"), NULL);
				GameServer()->SendChatTarget(m_pPlayer->GetCID(), "登录方法: /login <用户名> <密码>");
				return;
			}
			GameServer()->Server()->Login(ClientID, Username, Password);
		}
		else
		{
			if (sscanf(Msg->m_pMessage, "/login %s", Password) != 1)
			{
				GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("这个账户未启用安全设置,只需要密码"), NULL);
				GameServer()->SendChatTarget(m_pPlayer->GetCID(), "登录方法: /login <密码>");
				return;
			}
			GameServer()->Server()->Login(ClientID, Password, Password);
		}
		return;
	}
	else if (!strncmp(Msg->m_pMessage, "/register", 9))
	{
		LastChat();
		if (GameServer()->m_CityStart > 0)
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("请在 1-250 服务器上注册"), NULL);

		char Username[256], Password[256];
		if (sscanf(Msg->m_pMessage, "/register %s %s", Username, Password) != 2)
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("使用方法: /register <用户名> <密码>"), NULL);
		if (str_length(Username) > 15 || str_length(Username) < 2 || str_length(Password) > 15 || str_length(Password) < 2)
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("用户名 / 密码必须包含 2-15 个字符"), NULL);

		GameServer()->Server()->Register(ClientID, Username, Password, "Lol");
		return;
	}

	else if (!strncmp(Msg->m_pMessage, "/newclan", 8))
	{
		if (GameServer()->Server()->GetClanID(ClientID) > 0 || str_comp_nocase(GameServer()->Server()->ClientClan(ClientID), "NOPE") != 0)
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("# 你已经加入一个公会了!"), NULL);

		if (GameServer()->Server()->GetItemCount(ClientID, CLANTICKET))
		{
			char Reformat[256];
			if (sscanf(Msg->m_pMessage, "/newclan %s", Reformat) != 1)
				return GameServer()->SendChatTarget(m_pPlayer->GetCID(), "使用方法: /newclan <公会名称>");

			remove_spaces(Reformat);
			if (str_length(Reformat) > MAX_CLAN_LENGTH || str_length(Reformat) < 1)
				return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("公会名称长度必须在 1~12 个字符之间"), NULL);
			GameServer()->Server()->NewClan(ClientID, Reformat);
			m_pPlayer->m_LoginSync = 150;
			return;
		}
		else
		{
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("你没有公会票,请前往商店购买"), NULL);
		}
	}

	else if (!strncmp(Msg->m_pMessage, "/invite", 7))
	{
		if (!GameServer()->Server()->GetLeader(ClientID, GameServer()->Server()->GetClanID(ClientID)) && !GameServer()->Server()->GetAdmin(ClientID, GameServer()->Server()->GetClanID(ClientID)))
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("# 你不是公会会长或者管理员!"), NULL);

		if (GameServer()->Server()->GetClan(Clan::MemberNum, GameServer()->Server()->GetClanID(ClientID)) >= GameServer()->Server()->GetClan(Clan::MaxMemberNum, GameServer()->Server()->GetClanID(ClientID)))
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("公会人数已达到上限"), NULL);

		if (GameServer()->Server()->GetClanID(ClientID) > 0)
		{
			bool Found = false;
			char NameInv[256];
			if (sscanf(Msg->m_pMessage, "/invite %s", NameInv) != 1)
				return GameServer()->SendChatTarget(m_pPlayer->GetCID(), "用法: /invite <玩家昵称>");

			for (int i = 0; i < MAX_PLAYERS; ++i)
			{
				if (GameServer()->m_apPlayers[i])
				{
					if (str_comp_nocase(NameInv, GameServer()->Server()->ClientName(i)) == 0 && GameServer()->Server()->GetClanID(i) <= 0)
					{
						Found = true;
						GameServer()->m_apPlayers[i]->m_aInviteClanID = GameServer()->Server()->GetClanID(ClientID);

						CNetMsg_Sv_VoteSet Msg;
						Msg.m_Timeout = 10;
						Msg.m_pReason = "";
						Msg.m_pDescription = 0;

						Msg.m_pDescription = GameServer()->Server()->Localization()->Localize(m_pPlayer->GetLanguage(), _("是否加入公会?"));
						GameServer()->Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i, -1);

						GameServer()->m_aInviteTick[i] = 10 * GameServer()->Server()->TickSpeed();
						GameServer()->SendBroadcast_Localization(i, BROADCAST_PRIORITY_INTERFACE, 600, _("玩家 {str:name} 邀请你加入 {str:cname} 公会!"), "name", GameServer()->Server()->ClientName(ClientID), "cname", GameServer()->Server()->ClientClan(ClientID), NULL);
					}
				}
			}
			if (!Found)
				GameServer()->SendBroadcast_Localization(ClientID, BROADCAST_PRIORITY_INTERFACE, 150, _("玩家未找到,或者玩家已在公会!"), NULL);
			else
				GameServer()->SendBroadcast_Localization(ClientID, BROADCAST_PRIORITY_INTERFACE, 150, _("玩家已找到,请求已发送."), NULL);
		}
		return;
	}

	else if (!strncmp(Msg->m_pMessage, "/cmdlist", 8))
	{
		GameServer()->SendChatTarget(m_pPlayer->GetCID(), "? ---- 命令列表");
		GameServer()->SendChatTarget(m_pPlayer->GetCID(), "/invite <玩家昵称>, /cmdlist, /lang <语言>");
		GameServer()->SendChatTarget(m_pPlayer->GetCID(), "/login (用户名) <密码>, /register <用户名> <密码>");
		GameServer()->SendChatTarget(m_pPlayer->GetCID(), "/newclan <公会名称>, /password <密码> <重复密码>");
		return;
	}

	else if (!strncmp(Msg->m_pMessage, "/game", 5))
	{
		int Type;
		if ((sscanf(Msg->m_pMessage, "/game %d", &Type)) != 1)
		{
			GameServer()->SendChatTarget(ClientID, "发起小游戏. 命令方法: /game <类型> ");
			GameServer()->SendChatTarget(ClientID, "类型1为激光瞬杀,2为激光献祭");
			return;
		}
		if (Type > 2 || Type < 1)
		{
			GameServer()->SendChatTarget(ClientID, "发起小游戏. 命令方法: /game <类型> ");
			GameServer()->SendChatTarget(ClientID, "类型1为激光瞬杀,2为激光献祭");
			return;
		}
		GameServer()->StartArea(60, Type, ClientID);
		return;
	}

	// 密码修改
	else if (!strncmp(Msg->m_pMessage, "/password", 9))
	{
		LastChat();
		if (!GameServer()->Server()->IsClientLogged(ClientID))
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("# 请先登录"), NULL);
		char Password[256];

		char RepeatPassword[256];
		if (sscanf(Msg->m_pMessage, "/password %s %s", Password, RepeatPassword) != 2)
		{
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("修改密码: /password <密码> <重复密码>"), NULL);
		}

		if (str_comp(Password, RepeatPassword) != 0)
		{
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("两次密码不一致"), NULL);
		}

		if (str_length(Password) > 15 || str_length(Password) < 2)
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("密码必须包含 2~15 个字符"), NULL);

		// GameServer()->Server()->Register(ClientID, Username, Password, "Lol");
		GameServer()->Server()->ChangePassword(ClientID, Password);
		return;
	}
	else if (!strncmp(Msg->m_pMessage, "/lang", 5))
	{
		GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, ("Sorry, currently we have not finished translation yet. \nFor more info, see https://github.com/NewTeeworlds/mmotee_cn ."), NULL);
		return;
	}
	// 管理员指令
	else if (!strncmp(Msg->m_pMessage, "/giveitem", 9) && GameServer()->Server()->IsAuthed(ClientID))
	{
		LastChat();
		int id = 0, itemid = 0, citem = 0, enchant = 0;
		if ((sscanf(Msg->m_pMessage, "/giveitem %d %d %d %d", &id, &itemid, &citem, &enchant)) < 3)
			return GameServer()->SendChatTarget(ClientID, "命令方法: /giveitem <玩家id> <物品id> <物品数量> (附魔等级)");
		if (id >= MAX_PLAYERS)
		{
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("输入的 ID 无效."), NULL);
		}
		if (GameServer()->m_apPlayers[id] && GameServer()->Server()->IsClientLogged(id) && itemid > 0 && itemid < 500 && citem > 0)
			GameServer()->GiveItem(id, itemid, citem, enchant);
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "!警告! 管理员%s给了游戏名为%s的玩家物品%sx%d个", GameServer()->Server()->ClientName(ClientID), GameServer()->Server()->ClientName(id), GameServer()->Server()->GetItemName(ClientID, itemid), citem);
		GameServer()->Server()->LogWarning(aBuf);
		return;
	}

	else if (!strncmp(Msg->m_pMessage, "/remitem", 8) && GameServer()->Server()->IsAuthed(ClientID))
	{
		LastChat();
		int id = 0, itemid = 0, citem = 0;
		if ((sscanf(Msg->m_pMessage, "/remitem %d %d %d", &id, &itemid, &citem)) != 3)
			return GameServer()->SendChatTarget(ClientID, "命令方法: /giveitem <玩家id> <物品id> <物品数量>");
		if (id >= MAX_PLAYERS)
		{
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("输入的 ID 无效."), NULL);
		}
		if (GameServer()->m_apPlayers[id] && GameServer()->Server()->IsClientLogged(id) && itemid > 0 && itemid < 500 && citem > 0)
			GameServer()->RemItem(id, itemid, citem);

		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "!警告! 管理员%s删除了游戏名为%s的玩家物品%sx%d个", GameServer()->Server()->ClientName(ClientID), GameServer()->Server()->ClientName(id), GameServer()->Server()->GetItemName(ClientID, itemid), citem);
		GameServer()->Server()->LogWarning(aBuf);
		return;
	}

	else if (!strncmp(Msg->m_pMessage, "/sendmailall", 12) && GameServer()->Server()->IsAuthed(ClientID))
	{
		LastChat();
		int itemid = 0, citem = 0;
		if ((sscanf(Msg->m_pMessage, "/sendmailall %d %d", &itemid, &citem)) != 2)
			return GameServer()->SendChatTarget(ClientID, "命令方法: /sendmailall <物品id> <物品数量>");

		if (0 < itemid < MAX_ITEM && citem > 0){
			for(int id = 0; id < MAX_PLAYERS; id++){
				if(GameServer()->m_apPlayers[id] && GameServer()->Server()->IsClientLogged(id))
					GameServer()->SendMail(id, 12, itemid, citem);
			}
		}
		
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "!警告! 管理员%s邮给全体玩家物品%sx%d个", GameServer()->Server()->ClientName(ClientID), GameServer()->Server()->GetItemName(ClientID, itemid), citem);
		GameServer()->Server()->LogWarning(aBuf);
		return;
	}

	else if (!strncmp(Msg->m_pMessage, "/sendmail", 9) && GameServer()->Server()->IsAuthed(ClientID))
	{
		LastChat();
		int id = 0, itemid = 0, citem = 0;
		if ((sscanf(Msg->m_pMessage, "/sendmail %d %d %d", &id, &itemid, &citem)) != 3)
			return GameServer()->SendChatTarget(ClientID, "命令方法: /sendmail <玩家id> <物品id> <物品数量>");
		if (id >= MAX_PLAYERS)
		{
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("输入的 ID 无效."), NULL);
		}
		if (GameServer()->m_apPlayers[id] && GameServer()->Server()->IsClientLogged(id) && itemid > 0 && itemid < MAX_ITEM && citem > 0)
			GameServer()->SendMail(id, 12, itemid, citem);
		
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "!警告! 管理员%s邮给游戏名为%s的玩家物品%sx%d个", GameServer()->Server()->ClientName(ClientID), GameServer()->Server()->ClientName(id), GameServer()->Server()->GetItemName(ClientID, itemid), citem);
		GameServer()->Server()->LogWarning(aBuf);
		return;
	}

	else if (!strncmp(Msg->m_pMessage, "/givedonate_acc", 14) && GameServer()->Server()->IsAuthed(ClientID))
	{
		LastChat();
		char Username[64];
		int Donate = 0;
		if ((sscanf(Msg->m_pMessage, "/givedonate_acc %s %d", Username, &Donate)) != 2)
			return GameServer()->SendChatTarget(ClientID, "命令方法: /givedonate_acc <用户名> <点券>");
		
		GameServer()->Server()->GiveDonate(Username, Donate, ClientID);
		return;
	}

	else if (!strncmp(Msg->m_pMessage, "/givedonate", 10) && GameServer()->Server()->IsAuthed(ClientID))
	{
		LastChat();
		int id = 0, citem = 0;
		if ((sscanf(Msg->m_pMessage, "/givedonate %d %d", &id, &citem)) != 2)
			return GameServer()->SendChatTarget(ClientID, "命令方法: /givedonate <玩家id> <点券>");
		if (id >= MAX_PLAYERS)
		{
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("输入的 ID 无效."), NULL);
		}
		if (GameServer()->m_apPlayers[id] && GameServer()->Server()->IsClientLogged(id))
		{
			GameServer()->SendChatTarget(ClientID, "点券已发出.");
			GameServer()->SendChatTarget(id, "你的点券数增加了.");
			GameServer()->m_apPlayers[id]->AccData()->m_Donate += citem;

			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "!警告! 管理员%s给了游戏名为%s的玩家%d点券", GameServer()->Server()->ClientName(ClientID), GameServer()->Server()->ClientName(id), citem);
			GameServer()->Server()->LogWarning(aBuf);
		}
		return;
	}

	else if (!strncmp(Msg->m_pMessage, "/sd", 3) && GameServer()->Server()->IsAuthed(ClientID))
	{
		LastChat();
		int size = 0;
		if ((sscanf(Msg->m_pMessage, "/sd %d", &size)) != 1)
			return GameServer()->SendChatTarget(ClientID, "命令方法: /sd <音效id>");

		int soundid = clamp(size, 0, 40);
		if (GameServer()->GetPlayerChar(ClientID))
			GameServer()->CreateSound(m_pPlayer->GetCharacter()->m_Pos, soundid);
		return;
	}
	else if (!strncmp(Msg->m_pMessage, "/jail", 5) && GameServer()->Server()->IsAuthed(ClientID))
	{
		LastChat();
		int id = 0, JailLength = 0;
		if ((sscanf(Msg->m_pMessage, "/jail %d %d", &id, &JailLength)) != 2)
		{
			return GameServer()->SendChatTarget(ClientID, "使用: /jail <玩家id> <入狱时长>");
		}
		if (id >= MAX_PLAYERS)
		{
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("输入的 ID 无效."), NULL);
		}
		GameServer()->m_apPlayers[id]->AccData()->m_IsJailed = true;
		GameServer()->m_apPlayers[id]->AccData()->m_Jail = true;
		GameServer()->m_apPlayers[id]->AccData()->m_Rel = 0;
		GameServer()->m_apPlayers[id]->AccData()->m_JailLength = JailLength;
		if (GameServer()->m_apPlayers[id]->GetCharacter())
			GameServer()->m_apPlayers[id]->GetCharacter()->Die(id, WEAPON_WORLD);
		GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, ("成功将 {str:name} 关进监狱"), "name", GameServer()->Server()->ClientName(id), NULL);
	}
	else if (!strncmp(Msg->m_pMessage, "/unjail", 7) && GameServer()->Server()->IsAuthed(ClientID))
	{
		LastChat();
		int id = 0;
		if ((sscanf(Msg->m_pMessage, "/unjail %d", &id)) != 1)
		{
			return GameServer()->SendChatTarget(ClientID, "使用: /unjail <玩家id>");
		}
		if (id >= MAX_PLAYERS)
		{
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("输入的 ID 无效."), NULL);
		}
		GameServer()->m_apPlayers[id]->AccData()->m_IsJailed = false;
		GameServer()->m_apPlayers[id]->AccData()->m_Jail = false;
		GameServer()->m_apPlayers[id]->AccData()->m_Rel = 0;
		GameServer()->m_apPlayers[id]->m_JailTick = 0;
		GameServer()->m_apPlayers[id]->AccData()->m_JailLength = 0;
		if (GameServer()->m_apPlayers[id]->GetCharacter())
			GameServer()->m_apPlayers[id]->GetCharacter()->Die(id, WEAPON_WORLD);
		return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, ("成功将 {str:name} 放出监狱"), "name", GameServer()->Server()->ClientName(id), NULL);
	}
	else if (!strncmp(Msg->m_pMessage, "/ban", 4) && GameServer()->Server()->IsAuthed(ClientID) && g_Config.m_SvLoginControl)
	{
		LastChat();
		int ClientID_Ban;
		char Reason[256];
		if (sscanf(Msg->m_pMessage, "/ban %d %s", &ClientID_Ban, Reason) != 2)
		{
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("封禁用户(管理员): /ban <客户端ID> <原因> "), NULL);
		}
		if (ClientID_Ban >= MAX_PLAYERS)
		{
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("输入的 ID 无效."), NULL);
		}
		GameServer()->Server()->Ban_DB(ClientID, ClientID_Ban, Reason);

		return;
	}
	else if (!strncmp(Msg->m_pMessage, "/unban", 6) && GameServer()->Server()->IsAuthed(ClientID) && g_Config.m_SvLoginControl)
	{
		LastChat();
		char Nick[512];
		if (sscanf(Msg->m_pMessage, "/unban %s", Nick) != 1)
		{
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("解封用户(管理员): /unban <昵称>"), NULL);
		}

		GameServer()->Server()->Unban_DB(ClientID, Nick);
		return;
	}
	// 密码修改(管理员专用)
	else if (!strncmp(Msg->m_pMessage, "/chpw", 5) && GameServer()->Server()->IsAuthed(ClientID))
	{
		LastChat();
		char Nick[256], Password[256];
		if (sscanf(Msg->m_pMessage, "/chpw %s %s", Nick, Password) != 2)
		{
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("修改密码(管理员): /chpw <昵称> <密码> "), NULL);
		}

		if (str_length(Password) > 15 || str_length(Password) < 2)
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("密码必须包含 2~15 个字符"), NULL);

		GameServer()->Server()->ChangePassword_Admin(ClientID, Nick, Password);
		return;
	}
	else if (!strncmp(Msg->m_pMessage, "/offline", 8) && GameServer()->Server()->IsAuthed(ClientID) && g_Config.m_SvLoginControl)
	{
		LastChat();
		char Nick[512];
		if (sscanf(Msg->m_pMessage, "/offline %s", Nick) != 1)
		{
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("设置用户状态为下线(管理员): /offline <昵称>"), NULL);
		}

		GameServer()->Server()->SetOffline(ClientID, Nick);
		return;
	}
	else if (!strncmp(Msg->m_pMessage, "/goto", 5))
	{
		return; // baby mmotee wait me for one year plz...
		LastChat();
		int MapID;
		if (sscanf(Msg->m_pMessage, "/goto %d", &MapID) != 1)
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("没有这个编号的地图"), NULL);

		GameServer()->Server()->ChangeClientMap(ClientID, MapID);
		return;
	}
	else if (!strncmp(Msg->m_pMessage, "/setclass", 9))
	{
		LastChat();
		int CID, Class;
		if (sscanf(Msg->m_pMessage, "/setclass %d %d", &CID, &Class) != 2)
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("使用方法：/setclass <启动器ID> <职业ID>"), NULL);

		if(!GameServer()->m_apPlayers[CID])
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("无效的启动器ID"), NULL);

		GameServer()->m_apPlayers[CID]->SetClass(Class);
		GameServer()->UpdateStats(CID);
		return;
	}
	else if (!strncmp(Msg->m_pMessage, "/givepoint", 10))
	{
		LastChat();
		int CID, Point;
		if (sscanf(Msg->m_pMessage, "/givepoint %d %d", &CID, &Point) != 2)
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("使用方法：/givepoint <启动器ID> <点数>"), NULL);

		if(!GameServer()->m_apPlayers[CID])
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("无效的启动器ID"), NULL);

		GameServer()->m_apPlayers[CID]->GiveUpPoint(Point);;
		GameServer()->UpdateStats(CID);
		return;
	}
	else if (!strncmp(Msg->m_pMessage, "/giveskill", 10))
	{
		LastChat();
		int CID, Point;
		if (sscanf(Msg->m_pMessage, "/giveskill %d %d", &CID, &Point) != 2)
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("使用方法：/giveskill <启动器ID> <点数>"), NULL);

		if(!GameServer()->m_apPlayers[CID])
			return GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("无效的启动器ID"), NULL);

		GameServer()->Server()->GetAccUpgrade(CID)->m_SkillPoint += Point;
		GameServer()->UpdateStats(CID);
		return;
	}
	if (!strncmp(Msg->m_pMessage, "/", 1))
	{
		LastChat();
		GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("# 未知命令 {str:cmd} !"), "cmd", Msg->m_pMessage, NULL);
		return;
	}
}

void CCmd::LastChat()
{
	m_pPlayer->m_LastChat = GameServer()->Server()->Tick();
}
