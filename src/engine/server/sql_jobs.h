#include "server.h"

#include <engine/shared/config.h>
#include "sql_server.h"
#include "sql_job.h"

#include <teeuniverses/components/localization.h>

class CGameServerCmd_AddLocalizeVote_Language : public CServer::CGameServerCmd
{
private:
	int m_ClientID;
	char m_aType[64];
	char m_aText[128];
	
public:
	CGameServerCmd_AddLocalizeVote_Language(int ClientID, const char* pType, const char* pText, ...)
	{
		m_ClientID = ClientID;
		str_copy(m_aText, pText, sizeof(m_aText));
		str_copy(m_aType, pType, sizeof(m_aType));
	}

	virtual void Execute(IGameServer* pGameServer)
	{
		pGameServer->AddVote_Localization(m_ClientID, m_aType, m_aText);
	}
};

class CGameServerCmd_SendChatMOTD : public CServer::CGameServerCmd
{
private:
	int m_ClientID;
	char m_aText[512];
	
public:
	CGameServerCmd_SendChatMOTD(int ClientID, const char* pText)
	{
		m_ClientID = ClientID;
		str_copy(m_aText, pText, sizeof(m_aText));
	}

	virtual void Execute(IGameServer* pGameServer)
	{
		pGameServer->SendMOTD(m_ClientID, m_aText);
	}
};

class CGameServerCmd_SendChatTarget : public CServer::CGameServerCmd
{
private:
	int m_ClientID;
	char m_aText[128];
	
public:
	CGameServerCmd_SendChatTarget(int ClientID, const char* pText)
	{
		m_ClientID = ClientID;
		str_copy(m_aText, pText, sizeof(m_aText));
	}

	virtual void Execute(IGameServer* pGameServer)
	{
		pGameServer->SendChatTarget(m_ClientID, m_aText);
	}
};

class CGameServerCmd_SendChatTarget_Language : public CServer::CGameServerCmd
{
private:
	int m_ClientID;
	int m_ChatCategory;
	char m_aText[128];
	
public:
	CGameServerCmd_SendChatTarget_Language(int ClientID, int ChatCategory, const char* pText, ...)
	{
		m_ClientID = ClientID;
		m_ChatCategory = ChatCategory;
		str_copy(m_aText, pText, sizeof(m_aText));
	}

	virtual void Execute(IGameServer* pGameServer)
	{
		pGameServer->SendChatTarget_Localization(m_ClientID, m_ChatCategory, m_aText, NULL);
	}
};

class CGameServerCmd_UseItem : public CServer::CGameServerCmd
{
private:
	int m_ClientID;
	int m_ItemID;
	int m_Count;
	int m_Type;
	
public:
	CGameServerCmd_UseItem(int ClientID, int ItemID, int Count, int Type)
	{
		m_ClientID = ClientID;
		m_ItemID = ItemID;
		m_Count = Count;
		m_Type = Type;
	}
	
	virtual void Execute(IGameServer* pGameServer)
	{
		pGameServer->UseItem(m_ClientID, m_ItemID, m_Count, m_Type);
	}
};

// Инициализация Матерьялов
class CSqlJob_Server_InitMaterialID : public CSqlJob
{
private:
	CServer* m_pServer;
public:
	explicit CSqlJob_Server_InitMaterialID(CServer* pServer)
	{
		m_pServer = pServer;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		try
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "SELECT * FROM %s_Materials", pSqlServer->GetPrefix());
			pSqlServer->executeSqlQuery(aBuf);
			while(pSqlServer->GetResults()->next())
			{
				int IDMAT = (int)pSqlServer->GetResults()->getInt("ID");
				int Count = (int)pSqlServer->GetResults()->getInt("Materials");
				m_pServer->m_Materials[IDMAT-1] = Count;
			}
		}
		catch (sql::SQLException const &e)
		{
			return false;
		}
		return true;
	}
};

// Выдача предмета
class CSqlJob_Server_SaveMaterial : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ID;
public:
	CSqlJob_Server_SaveMaterial(CServer* pServer, int ID)
	{
		m_pServer = pServer;
		m_ID = ID;
	}
	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[256];
		try
		{
			str_format(aBuf, sizeof(aBuf), 
				"UPDATE %s_Materials "
				"SET Materials = '%d' "
				"WHERE ID = '%d';"
				, pSqlServer->GetPrefix()
				, m_pServer->m_Materials[m_ID], m_ID+1);
			pSqlServer->executeSql(aBuf);
		}
		catch (sql::SQLException const &e)
		{
			dbg_msg("sql", "Can't Save Material (MySQL Error: %s)", e.what());
			return false;
		}
		return true;
	}
};

// Инициализация Почты по ID
class CSqlJob_Server_InitMailID : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ClientID;

public:
	CSqlJob_Server_InitMailID(CServer* pServer, int ClientID)
	{
		m_pServer = pServer;
		m_ClientID = ClientID;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[256];
		try
		{
			int iscope = 0;
			str_format(aBuf, sizeof(aBuf), 
				"SELECT * FROM %s_Mail "
				"WHERE IDOwner = '%d' LIMIT 20;"
				, pSqlServer->GetPrefix()
				, m_pServer->m_aClients[m_ClientID].m_UserID);
			pSqlServer->executeSqlQuery(aBuf);
			while(pSqlServer->GetResults()->next())
			{
				int IDMAIL = (int)pSqlServer->GetResults()->getInt("ID");
				int ItemID = (int)pSqlServer->GetResults()->getInt("ItemID");
				int ItemNum = (int)pSqlServer->GetResults()->getInt("ItemCount");
				int MailType = (int)pSqlServer->GetResults()->getInt("MailType");
				m_pServer->m_aClients[m_ClientID].m_MailID[iscope] = IDMAIL;
				m_pServer->SetRewardMail(m_ClientID, iscope, ItemID, ItemNum);

				char Text[64];
				//str_format(Text, sizeof(Text), "%s", pSqlServer->GetResults()->getString("TextMail").c_str());
				switch (MailType)
				{
				case 1:
					str_format(Text, sizeof(Text), "%s", "你获得了奖品，真幸运!");
					break;
				case 2:
					str_format(Text, sizeof(Text), "%s", "Hello, 这是来自任务系统的奖励!");
					break;
				case 3:
					str_format(Text, sizeof(Text), "%s", "Hello, 你解锁了一个新的称号!");
					break;
				case 4:
					str_format(Text, sizeof(Text), "%s", "恭喜你成功升到满级,这是你的奖励!");
					break;
				case 5:
					str_format(Text, sizeof(Text), "%s", "Hello, 你解锁了一项新技能!");
					break;
				case 6:
					str_format(Text, sizeof(Text), "%s", "您赞助了我们服务器，这是你的奖励!");
					break;
				case 7:
					str_format(Text, sizeof(Text), "%s", "Hello, 合成物品成功!");
					break;
				case 8:
					str_format(Text, sizeof(Text), "%s", "每升十级，你就会获得奖品!");
					break;
				case 9:
					str_format(Text, sizeof(Text), "%s", "在线奖励!");
					break;
				case 10:
					str_format(Text, sizeof(Text), "%s", "Hello, 这是你的注册奖励!");
					break;
				case 11:
					str_format(Text, sizeof(Text), "%s", "你现在获得了Soul automatic, 能够使用自定义皮肤了!");
					break;
				case 12:
					str_format(Text, sizeof(Text), "%s", "Hello, 这是来自管理员的物品!");
					break;
				default:
					str_format(Text, sizeof(Text), "%s", "没有内容");
					break;
				}
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_AddLocalizeVote_Language(m_ClientID, "null", _(Text));
				m_pServer->AddGameServerCmd(pCmd);

				char aProtocol[16];
				str_format(aProtocol, sizeof(aProtocol), "reward%d", iscope);
				str_format(Text, sizeof(Text), "领取 %s : %d 并删除邮件", m_pServer->GetItemName(m_ClientID, ItemID, false), ItemNum);	

				pCmd = new CGameServerCmd_AddLocalizeVote_Language(m_ClientID, aProtocol, _(Text));
				m_pServer->AddGameServerCmd(pCmd);

				pCmd = new CGameServerCmd_AddLocalizeVote_Language(m_ClientID, "null", _("------------"));
				m_pServer->AddGameServerCmd(pCmd);

				iscope++;
			}
		}
		catch (sql::SQLException const &e)
		{
			return false;
		}
		return true;
	}
};

// Удаление почты
class CSqlJob_Server_RemMail : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_IDMail;
	int m_ClientID;
	
public:
	CSqlJob_Server_RemMail(CServer* pServer,int ClientID, int IDMail)
	{
		m_pServer = pServer;
		m_IDMail = IDMail; 
		m_ClientID = ClientID;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[256];
		if(m_pServer->m_aClients[m_ClientID].m_ItemReward[m_IDMail] > 0 && m_pServer->m_aClients[m_ClientID].m_ItemNumReward[m_IDMail] > 0)
		{
			try
			{
				int it_id = m_pServer->m_aClients[m_ClientID].m_ItemReward[m_IDMail], it_count = m_pServer->m_aClients[m_ClientID].m_ItemNumReward[m_IDMail];
				m_pServer->SetRewardMail(m_ClientID, m_IDMail, -1, -1);
				m_pServer->GameServer()->GiveItem(m_ClientID, it_id, it_count);
				str_format(aBuf, sizeof(aBuf),
						   "DELETE FROM %s_Mail "
						   "WHERE ID = '%d' LIMIT 1;",
						   pSqlServer->GetPrefix(),
						   m_pServer->m_aClients[m_ClientID].m_MailID[m_IDMail]);
				pSqlServer->executeSql(aBuf);
			}
			catch (sql::SQLException const &e)
			{
				return false;
			}
			return true;
		}
		else
		{
			return true;
		}
	}
};

// 批量领取邮件(在线奖励)
class CSqlJob_Server_RemMail_OnlineBonus : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_IDOwner;
	int m_ClientID;
	
public:
	CSqlJob_Server_RemMail_OnlineBonus(CServer* pServer, int ClientID)
	{
		m_pServer = pServer;
		m_ClientID = ClientID;
		m_IDOwner = m_pServer->m_aClients[m_ClientID].m_UserID;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[256];	
		if(m_pServer->m_aClients[m_ClientID].m_LogInstance != GetInstance())
			return true;
		struct _Items
		{
			int ItemID = 0;
			int ItemCount = 0;
		};
		_Items Items[8];
		Items[0].ItemID = COOPERPIX;
		Items[1].ItemID = WOOD;
		Items[2].ItemID = DRAGONORE;
		Items[3].ItemID = COOPERORE;
		Items[4].ItemID = IRONORE;
		Items[5].ItemID = GOLDORE;
		Items[6].ItemID = DIAMONDORE;
		Items[7].ItemID = EVENTCUSTOMSOUL;
		for(int i = 0;i < 8;i++)
		{
			try
			{
				str_format(aBuf, sizeof(aBuf), 
					"SELECT COUNT(*) FROM %s_Mail " 
					"WHERE IDOwner = '%d' AND ItemID = '%d';"
					, pSqlServer->GetPrefix()
					, m_IDOwner, Items[i].ItemID);	
				pSqlServer->executeSqlQuery(aBuf);
				if(pSqlServer->GetResults()->next())
				{
					Items[i].ItemCount = (int)pSqlServer->GetResults()->getInt("COUNT(*)");
				}
			}
			catch (sql::SQLException const &e)
			{
				return false;
			}
		}

		str_format(aBuf, sizeof(aBuf), 
			"DELETE FROM %s_Mail " 
			"WHERE IDOwner = '%d' AND MailType = '9';"
			, pSqlServer->GetPrefix()
			, m_IDOwner);	
			pSqlServer->executeSql(aBuf);
		m_pServer->InitMailID(m_ClientID);
		/*if(Items[0].ItemCount > 0)
		{
			m_pServer->GameServer()->GiveItem(m_ClientID, Items[0].ItemID, Items[0].ItemCount);
			//m_pServer->GameServer()->SendChatTarget_Localization(m_ClientID, CHATCATEGORY_DEFAULT, _("你获得了 {str:items}x{int:counts}"), "items", m_pServer->GetItemName(m_ClientID, Items[0].ItemID), "counts", &Items[0].ItemCount, NULL);
		}*/
		for(int i = 0;i < 8;i++)
		{
			if(Items[i].ItemCount > 0)
			{
				m_pServer->GameServer()->GiveItem(m_ClientID, Items[i].ItemID, Items[i].ItemCount);
				//m_pServer->GameServer()->SendChatTarget_Localization(m_ClientID, CHATCATEGORY_DEFAULT, _("你获得了 {str:items}x{int:counts}"), "items", m_pServer->GetItemName(m_ClientID, Items[i].ItemID), "counts", &Items[i].ItemCount, NULL);
			}
		}
		return true;
	}

	virtual void CleanInstanceRef()
	{
		m_pServer->m_aClients[m_ClientID].m_LogInstance = -1;
	}
};

/*
class CSqlJob_Server_GetMailCount : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ClientID;
	int m_MailCount;
	
public:
	CSqlJob_Server_GetMailCount(CServer* pServer, int ClientID)
	{
		m_pServer = pServer;
		m_ClientID = ClientID; 
		m_MailCount = 0;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[256];			
		try
		{
			str_format(aBuf, sizeof(aBuf), 
				"SELECT count(*) FROM %s_Mail " 
				"WHERE IDOwner = '%d' ;"
				, pSqlServer->GetPrefix()
				, m_ClientID);	
			pSqlServer->executeSqlQuery(aBuf);
			if(pSqlServer->GetResults()->next())
			{
				m_MailCount = (int)pSqlServer->GetResults()->getInt("count(*)");
			}
		}
		catch (sql::SQLException const &e)
		{
			return -1;
		}
		return m_MailCount;
	}
};
int CServer::GetMailCount(int ClientID)
{
	CSqlJob* pJob = new CSqlJob_Server_GetMailCount(this, ClientID);
	pJob->Start();
	return ;
}
*/
// Выдача предмета
// 发邮件
class CSqlJob_Server_SendMail : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_AuthedID;
	int m_ItemID;
	int m_ItemNum;
	int m_MailType;
	
public:
	CSqlJob_Server_SendMail(CServer* pServer, int AuthedID, int MailType, int ItemID, int ItemNum)
	{
		m_pServer = pServer;
		m_AuthedID = AuthedID;
		m_ItemID = ItemID;
		m_ItemNum = ItemNum;
		m_MailType = MailType;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[256];
		try
		{
			str_format(aBuf, sizeof(aBuf), 
				"INSERT INTO %s_Mail "
				"(IDOwner, MailType, ItemID, ItemCount) "
				"VALUES ('%d', '%d', '%d', '%d');"
				, pSqlServer->GetPrefix()
				, m_AuthedID, m_MailType, m_ItemID, m_ItemNum);	
			pSqlServer->executeSql(aBuf);
		}
		catch (sql::SQLException const &e)
		{
			return false;
		}
		return true;
	}
};

// Инициализация Инвентаря по ID
// 通过 ID 初始化库存
class CSqlJob_Server_InitInvID : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ClientID, m_ItemID;

public:
	CSqlJob_Server_InitInvID(CServer* pServer, int ClientID, int ItemID)
	{
		m_pServer = pServer;
		m_ClientID = ClientID;
		m_ItemID = ItemID;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		try
		{
			if(m_ItemID == -1 && m_ClientID == -1)
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "SELECT * FROM %s_uItemList;", pSqlServer->GetPrefix());
				pSqlServer->executeSqlQuery(aBuf);
				while(pSqlServer->GetResults()->next())
				{
					for(int i = 0; i < MAX_PLAYERS; ++i)
					{
						int ItemID = (int)pSqlServer->GetResults()->getInt("il_id");
						m_pServer->m_stInv[i][ItemID].i_id = ItemID;
						m_pServer->m_stInv[i][ItemID].i_type = (int)pSqlServer->GetResults()->getInt("item_type");
						str_copy(m_pServer->m_stInv[i][ItemID].i_name, pSqlServer->GetResults()->getString("item_name").c_str(), sizeof(m_pServer->m_stInv[i][ItemID].i_name));
						str_copy(m_pServer->m_stInv[i][ItemID].i_desc, pSqlServer->GetResults()->getString("item_desc").c_str(), sizeof(m_pServer->m_stInv[i][ItemID].i_desc));
					}
				}
				str_format(aBuf, sizeof(aBuf), "SELECT * FROM %s_uItemList_en;", pSqlServer->GetPrefix());
				pSqlServer->executeSqlQuery(aBuf);
				while(pSqlServer->GetResults()->next())
				{
					int ItemID = (int)pSqlServer->GetResults()->getInt("il_id");
					m_pServer->ItemName_en[ItemID].i_id = ItemID;
					m_pServer->ItemName_en[ItemID].i_type = (int)pSqlServer->GetResults()->getInt("item_type");
					str_copy(m_pServer->ItemName_en[ItemID].i_name, pSqlServer->GetResults()->getString("item_name").c_str(), sizeof(m_pServer->ItemName_en[ItemID].i_name));
					str_copy(m_pServer->ItemName_en[ItemID].i_desc, pSqlServer->GetResults()->getString("item_desc").c_str(), sizeof(m_pServer->ItemName_en[ItemID].i_desc));
				}
			}
			else
			{
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "SELECT item_count FROM %s_uItems WHERE item_owner = '%d' AND il_id = '%d';", pSqlServer->GetPrefix(), m_pServer->m_aClients[m_ClientID].m_UserID, m_ItemID);
				pSqlServer->executeSqlQuery(aBuf);

				if(pSqlServer->GetResults()->next())
					m_pServer->m_stInv[m_ClientID][m_ItemID].i_count = (int)pSqlServer->GetResults()->getInt("item_count");
			}
		}
		catch (sql::SQLException const &e)
		{
			return false;
		}
		return true;
	}
};

// Выдача предмета
class CSqlJob_Server_GetItem : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ItemID;
	int m_ClientID;
	int m_Count;
	int m_Settings;
	int m_Enchant;

public:
	CSqlJob_Server_GetItem(CServer* pServer, int ItemID, int ClientID, int Count, int Settings, int Enchant)
	{
		m_pServer = pServer;
		m_ItemID = ItemID;
		m_ClientID = ClientID;
		m_Count = Count;
		m_Settings = Settings;
		m_Enchant = Enchant;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[256];
		try
		{
			if(m_pServer->m_stInv[m_ClientID][m_ItemID].i_count > 0)
			{
				str_format(aBuf, sizeof(aBuf), 
					"UPDATE %s_uItems "
					"SET item_count = item_count + '%d', item_settings = item_settings + '%d' "
					"WHERE item_owner = '%d' AND il_id = '%d';"
					, pSqlServer->GetPrefix()
					, m_Count, m_Settings, m_pServer->m_aClients[m_ClientID].m_UserID, m_ItemID);
				pSqlServer->executeSql(aBuf);
				
				m_pServer->m_stInv[m_ClientID][m_ItemID].i_count += m_Count;
				m_pServer->m_stInv[m_ClientID][m_ItemID].i_settings += m_Settings;
				return true;
			}
			str_format(aBuf, sizeof(aBuf), 
				"INSERT INTO %s_uItems "
				"(il_id, item_owner, item_count, item_type, item_settings, item_enchant) "
				"VALUES ('%d', '%d', '%d', '%d', '%d', '%d');"
				, pSqlServer->GetPrefix()
				, m_ItemID, m_pServer->m_aClients[m_ClientID].m_UserID, m_Count, m_pServer->m_stInv[m_ClientID][m_ItemID].i_type, m_Settings, m_Enchant);	
			pSqlServer->executeSql(aBuf);

			m_pServer->m_stInv[m_ClientID][m_ItemID].i_settings = m_Settings;
			m_pServer->m_stInv[m_ClientID][m_ItemID].i_count = m_Count;
			m_pServer->m_aClients[m_ClientID].m_ItemCount[m_pServer->m_stInv[m_ClientID][m_ItemID].i_type]++;
		}
		catch (sql::SQLException const &e)
		{
			// Scheme - 
			// if(item) -> Add item if succeses -> Got resoult delete item drop -> 
			//
			//
			//
			//
			return false;
		}
		return true;
	}
};

// Забратие предмета
class CSqlJob_Server_RemItems : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ItemID;
	int m_ClientID;
	int m_Count;
	int m_Type;
	
public:
	CSqlJob_Server_RemItems(CServer* pServer, int ItemID, int ClientID, int Count, int Type)
	{
		m_pServer = pServer;
		m_ItemID = ItemID; 
		m_ClientID = ClientID;
		m_Count = Count;
		m_Type = Type;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[256];			
		try
		{
			str_format(aBuf, sizeof(aBuf), 
				"SELECT item_count FROM %s_uItems "
				"WHERE item_owner = '%d' AND il_id = '%d';",
				pSqlServer->GetPrefix(),
				m_pServer->m_aClients[m_ClientID].m_UserID, m_ItemID);
			pSqlServer->executeSqlQuery(aBuf);

			if(pSqlServer->GetResults()->next())
			{
				int Count = (int)pSqlServer->GetResults()->getInt("item_count");
				m_pServer->m_stInv[m_ClientID][m_ItemID].i_count = Count;
			
				if(Count > m_Count)
				{
					str_format(aBuf, sizeof(aBuf), 
						"UPDATE %s_uItems "
						"SET item_count = item_count - '%d' "
						"WHERE item_owner = '%d' AND il_id = '%d';"
						, pSqlServer->GetPrefix()
						, m_Count, m_pServer->m_aClients[m_ClientID].m_UserID, m_ItemID);
					pSqlServer->executeSql(aBuf);	
					m_pServer->m_stInv[m_ClientID][m_ItemID].i_count -= m_Count;
				}
				else
				{
					str_format(aBuf, sizeof(aBuf), 
						"DELETE FROM %s_uItems " 
						"WHERE item_owner = '%d' AND il_id = '%d';"
						, pSqlServer->GetPrefix()
						, m_pServer->m_aClients[m_ClientID].m_UserID, m_ItemID);	
					pSqlServer->executeSql(aBuf);			
					m_pServer->m_stInv[m_ClientID][m_ItemID].i_count = 0;
					m_pServer->m_stInv[m_ClientID][m_ItemID].i_settings = 0;
					m_pServer->m_aClients[m_ClientID].m_ItemCount[m_pServer->m_stInv[m_ClientID][m_ItemID].i_type]--;
				}

				if(m_Count > Count)
					m_Count = Count;

				CServer::CGameServerCmd* pCmd = new CGameServerCmd_UseItem(m_ClientID, m_ItemID, m_Count, m_Type);
				m_pServer->AddGameServerCmd(pCmd);
			}
			return true;
		}
		catch (sql::SQLException const &e)
		{
			return false;
		}
		return true;
	}
};

// Обновление настроек предмета
class CSqlJob_Server_UpdateItemSettings : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ItemID;
	int m_ClientID;
	
public:
	CSqlJob_Server_UpdateItemSettings(CServer* pServer, int ItemID, int ClientID)
	{
		m_pServer = pServer;
		m_ItemID = ItemID;
		m_ClientID = ClientID;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[128];
		try
		{
			if(m_pServer->GetItemCount(m_ClientID, m_ItemID) > 0)
			{
				str_format(aBuf, sizeof(aBuf), 
					"UPDATE %s_uItems "
					"SET item_settings = '%d', item_enchant = '%d' "
					"WHERE item_owner = '%d' AND il_id = '%d';"
					, pSqlServer->GetPrefix()
					, m_pServer->m_stInv[m_ClientID][m_ItemID].i_settings, m_pServer->m_stInv[m_ClientID][m_ItemID].i_enchant, m_pServer->m_aClients[m_ClientID].m_UserID, m_ItemID);
				pSqlServer->executeSql(aBuf);
			}
			return true;
		}
		catch (sql::SQLException const &e)
		{
			return false;
		}
		return true;
	}
};

// Лист предметов
class CSqlJob_Server_ListInventory : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ClientID;
	int m_Type;
	bool bGetCount;

public:
	CSqlJob_Server_ListInventory(CServer* pServer, int ClientID, int Type, bool GetCount)
	{
		m_pServer = pServer;
		m_ClientID = ClientID;
		m_Type = Type;
		bGetCount = GetCount;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[256];
		if(m_pServer->m_aClients[m_ClientID].m_LogInstance != GetInstance())
			return true;
	
		try
		{
			if(bGetCount)
			{
				str_format(aBuf, sizeof(aBuf), 
					"SELECT il_id, item_type FROM %s_uItems "
					"WHERE item_owner = '%d' AND item_type != '10, 12, 15, 16, 17';",
					pSqlServer->GetPrefix(),
					m_pServer->m_aClients[m_ClientID].m_UserID);
				pSqlServer->executeSqlQuery(aBuf);

				for(int i = 0; i < 16; i++)
					m_pServer->m_aClients[m_ClientID].m_ItemCount[i] = 0;

				while(pSqlServer->GetResults()->next())
				{
					int ItemType = (int)pSqlServer->GetResults()->getInt("item_type");
					m_pServer->m_aClients[m_ClientID].m_ItemCount[ItemType]++;
				}
				return true;			
			}

			str_format(aBuf, sizeof(aBuf), 
				"SELECT il_id, item_count FROM %s_uItems "
				"WHERE item_owner = '%d' AND item_type = '%d';",
				pSqlServer->GetPrefix(),
				m_pServer->m_aClients[m_ClientID].m_UserID, m_Type);
			pSqlServer->executeSqlQuery(aBuf);
			
			bool found = false;
			while(pSqlServer->GetResults()->next())
			{
				// Псевдо Инициализация по сути нихуя нагрузки нет все норм
				int ItemID = (int)pSqlServer->GetResults()->getInt("il_id");
				int ItemCount = (int)pSqlServer->GetResults()->getInt("item_count");
				m_pServer->m_stInv[m_ClientID][ItemID].i_count = ItemCount;

				char iName[64], iUsed[8];
				if(m_Type == 15 || m_Type == 16 || m_Type == 17) 
				{
					str_format(iUsed, sizeof(iUsed), "it%d", ItemID);
					str_format(iName, sizeof(iName), "➳ Lvl%d %s +%d", 
						m_pServer->GetItemPrice(m_ClientID, ItemID, 0), m_pServer->GetItemName(m_ClientID, ItemID), m_pServer->GetItemEnchant(m_ClientID, ItemID), ItemCount);

					CServer::CGameServerCmd* pCmd = new CGameServerCmd_AddLocalizeVote_Language(m_ClientID, iUsed, _(iName));
					m_pServer->AddGameServerCmd(pCmd);

					const char* Data = m_pServer->GetItemSettings(m_ClientID, ItemID) ? "☑" : "☐";
					str_format(iUsed, sizeof(iUsed), "set%d", ItemID);
					if(m_Type == 17)
					{
						str_format(iName, sizeof(iName), "➳ %s %s (伤害 +%d)", 
							Data,  m_pServer->GetItemName(m_ClientID, ItemID), m_pServer->GetBonusEnchant(m_ClientID, ItemID, m_Type));				
					}
					else
					{
						str_format(iName, sizeof(iName), "➳ %s %s (生命值 +%d 护盾 +%d)", 
							Data,  m_pServer->GetItemName(m_ClientID, ItemID), m_pServer->GetBonusEnchant(m_ClientID, ItemID, m_Type), 
							m_pServer->GetBonusEnchant(m_ClientID, ItemID, m_Type));				
					}

					pCmd = new CGameServerCmd_AddLocalizeVote_Language(m_ClientID, iUsed, _(iName));
					m_pServer->AddGameServerCmd(pCmd);
				}
				else
				{
					str_format(iUsed, sizeof(iUsed), "it%d", ItemID);
					str_format(iName, sizeof(iName), "➳ Lvl%d %s : X%d", 
						m_pServer->GetItemPrice(m_ClientID, ItemID, 0), m_pServer->GetItemName(m_ClientID, ItemID), ItemCount);
				
					CServer::CGameServerCmd* pCmd = new CGameServerCmd_AddLocalizeVote_Language(m_ClientID, iUsed, _(iName));
					m_pServer->AddGameServerCmd(pCmd);
				}
				found = true;
			}
			if(!found)
			{
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_AddLocalizeVote_Language(m_ClientID, "null", _("这个栏目是空的"));
				m_pServer->AddGameServerCmd(pCmd);
			}
		}
		catch (sql::SQLException const &e)
		{		
			return false;
		}
		return true;
	}
	
	virtual void CleanInstanceRef()
	{
		m_pServer->m_aClients[m_ClientID].m_LogInstance = -1;
	}
};

// Инициализация кланов
class CSqlJob_Server_InitClan : public CSqlJob
{
private:
	CServer* m_pServer;
	
public:
	CSqlJob_Server_InitClan(CServer* pServer)
	{
		m_pServer = pServer;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		try
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "SELECT * FROM %s_Clans", pSqlServer->GetPrefix());
			pSqlServer->executeSqlQuery(aBuf);

			int Num = 0;
			while(pSqlServer->GetResults()->next())
			{
				int ClanID = (int)pSqlServer->GetResults()->getInt("ClanID");
				m_pServer->m_stClan[ClanID].ID = ClanID;
				m_pServer->m_stClan[ClanID].Level = (int)pSqlServer->GetResults()->getInt("Level");
				m_pServer->m_stClan[ClanID].Exp = (int)pSqlServer->GetResults()->getInt("Exp");
				m_pServer->m_stClan[ClanID].Money = (int)pSqlServer->GetResults()->getInt("Money");
				m_pServer->m_stClan[ClanID].MaxMemberNum = (int)pSqlServer->GetResults()->getInt("MaxNum");
				m_pServer->m_stClan[ClanID].Relevance = (int)pSqlServer->GetResults()->getInt("Relevance");
				m_pServer->m_stClan[ClanID].ExpAdd = (int)pSqlServer->GetResults()->getInt("ExpAdd");
				m_pServer->m_stClan[ClanID].MoneyAdd = (int)pSqlServer->GetResults()->getInt("MoneyAdd");
				m_pServer->m_stClan[ClanID].IsSpawnInHouse = (int)pSqlServer->GetResults()->getInt("SpawnHouse");
				m_pServer->m_stClan[ClanID].ChairLevel = (int)pSqlServer->GetResults()->getInt("ChairHouse");
				str_copy(m_pServer->m_stClan[ClanID].Name, pSqlServer->GetResults()->getString("Clanname").c_str(), sizeof(m_pServer->m_stClan[ClanID].Name));
				str_copy(m_pServer->m_stClan[ClanID].Creator, pSqlServer->GetResults()->getString("LeaderName").c_str(), sizeof(m_pServer->m_stClan[ClanID].Creator));
				str_copy(m_pServer->m_stClan[ClanID].Admin, pSqlServer->GetResults()->getString("AdminName").c_str(), sizeof(m_pServer->m_stClan[ClanID].Admin));
				
				
				m_pServer->UpdClanCount(ClanID);
				Num++;
			}
			
			dbg_msg("mmotee", "############################################");
			dbg_msg("mmotee", "################ 加载了 %d 个公会", Num);
			dbg_msg("mmotee", "############################################");
		}
		catch (sql::SQLException const &e)
		{
			dbg_msg("mmotee", "Fail in initialize clans");
			return false;
		}
		
		return true;
	}
};

// Инициализация клана по ID
class CSqlJob_Server_InitClanID : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ClanID;
	bool m_Need;
	int m_Price;
	bool m_Save;
	CSqlString<64> m_sType;

public:
	
	CSqlJob_Server_InitClanID(CServer* pServer, int ClanID, Sign Need, const char* SubType, int Price, bool Save)
	{
		m_pServer = pServer;
		m_ClanID = ClanID;
		m_Need = Need;
		m_sType = CSqlString<64>(SubType);
		m_Price = Price;
		m_Save = Save;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[256];
		try
		{
			if(str_comp(m_sType.ClrStr(), "Leader") == 0)
			{
				m_sType = CSqlString<64>(m_pServer->m_stClan[m_ClanID].Creator);
				str_format(aBuf, sizeof(aBuf), 
					"UPDATE %s_Clans SET LeaderName = '%s' WHERE ClanID = '%d';",
					pSqlServer->GetPrefix(),
					m_sType.ClrStr(), m_ClanID);
				pSqlServer->executeSqlQuery(aBuf);
				return true;
			}

			if(str_comp(m_sType.ClrStr(), "Admin") == 0)
			{
				m_sType = CSqlString<64>(m_pServer->m_stClan[m_ClanID].Admin);
				str_format(aBuf, sizeof(aBuf), 
					"UPDATE %s_Clans SET AdminName = '%s' WHERE ClanID = '%d';",
					pSqlServer->GetPrefix(),
					m_sType.ClrStr(), m_ClanID);
				pSqlServer->executeSqlQuery(aBuf);
				return true;
			}

			if(str_comp(m_sType.ClrStr(), "Init") == 0)
			{
				str_format(aBuf, sizeof(aBuf), "SELECT * FROM %s_Clans WHERE ClanID = '%d';", pSqlServer->GetPrefix(), m_ClanID);
				pSqlServer->executeSqlQuery(aBuf);
				if(pSqlServer->GetResults()->next())
				{
					m_pServer->m_stClan[m_ClanID].ChairLevel = (int)pSqlServer->GetResults()->getInt("ChairHouse"); 
					m_pServer->m_stClan[m_ClanID].IsSpawnInHouse = (int)pSqlServer->GetResults()->getInt("SpawnHouse");
					m_pServer->m_stClan[m_ClanID].MoneyAdd = (int)pSqlServer->GetResults()->getInt("MoneyAdd");
					m_pServer->m_stClan[m_ClanID].ExpAdd = (int)pSqlServer->GetResults()->getInt("ExpAdd");
					m_pServer->m_stClan[m_ClanID].Relevance = (int)pSqlServer->GetResults()->getInt("Relevance");
					m_pServer->m_stClan[m_ClanID].MaxMemberNum = (int)pSqlServer->GetResults()->getInt("MaxNum");
					m_pServer->m_stClan[m_ClanID].Level = (int)pSqlServer->GetResults()->getInt("Level");
					m_pServer->m_stClan[m_ClanID].Exp = (int)pSqlServer->GetResults()->getInt("Exp");
					m_pServer->m_stClan[m_ClanID].Money = (unsigned long)pSqlServer->GetResults()->getUInt64("Money");
					str_copy(m_pServer->m_stClan[m_ClanID].Creator, pSqlServer->GetResults()->getString("LeaderName").c_str(), sizeof(m_pServer->m_stClan[m_ClanID].Creator));
					str_copy(m_pServer->m_stClan[m_ClanID].Admin, pSqlServer->GetResults()->getString("AdminName").c_str(), sizeof(m_pServer->m_stClan[m_ClanID].Admin));
				}
				return true;
			}

			str_format(aBuf, sizeof(aBuf), "SELECT %s FROM %s_Clans WHERE ClanID = '%d';", m_sType.ClrStr(), pSqlServer->GetPrefix(), m_ClanID);
			pSqlServer->executeSqlQuery(aBuf);
			if(pSqlServer->GetResults()->next())
			{
				int VarGot = -1;
				if(str_comp(m_sType.ClrStr(), "Money") == 0)
				{
					m_pServer->m_stClan[m_ClanID].Money = (int)pSqlServer->GetResults()->getInt("Money");
					if(m_Need == PLUS) m_pServer->m_stClan[m_ClanID].Money += m_Price;
					else m_pServer->m_stClan[m_ClanID].Money -= m_Price;

					VarGot = m_pServer->m_stClan[m_ClanID].Money;
				}
				else if(str_comp(m_sType.ClrStr(), "Exp") == 0)
				{
					m_pServer->m_stClan[m_ClanID].Exp = (int)pSqlServer->GetResults()->getInt("Exp");
					if(m_Need == PLUS) m_pServer->m_stClan[m_ClanID].Exp += m_Price;
					else m_pServer->m_stClan[m_ClanID].Exp -= m_Price;

					VarGot = m_pServer->m_stClan[m_ClanID].Exp;
				}
				else if(str_comp(m_sType.ClrStr(), "Level") == 0)
				{
					m_pServer->m_stClan[m_ClanID].Level = (int)pSqlServer->GetResults()->getInt("Level");
					if(m_Need == PLUS) m_pServer->m_stClan[m_ClanID].Level += m_Price;
					else m_pServer->m_stClan[m_ClanID].Level -= m_Price;

					VarGot = m_pServer->m_stClan[m_ClanID].Level;
				}
				else if(str_comp(m_sType.ClrStr(), "MaxNum") == 0)
				{
					m_pServer->m_stClan[m_ClanID].MaxMemberNum = (int)pSqlServer->GetResults()->getInt("MaxNum");
					if(m_Need == PLUS) m_pServer->m_stClan[m_ClanID].MaxMemberNum += m_Price;
					else m_pServer->m_stClan[m_ClanID].MaxMemberNum -= m_Price;

					VarGot = m_pServer->m_stClan[m_ClanID].MaxMemberNum;
				}
				else if(str_comp(m_sType.ClrStr(), "Relevance") == 0)
				{
					m_pServer->m_stClan[m_ClanID].Relevance = (int)pSqlServer->GetResults()->getInt("Relevance");
					if(m_Need == PLUS) m_pServer->m_stClan[m_ClanID].Relevance += m_Price;
					else m_pServer->m_stClan[m_ClanID].Relevance -= m_Price;

					VarGot = m_pServer->m_stClan[m_ClanID].Relevance;
				}
				else if(str_comp(m_sType.ClrStr(), "ExpAdd") == 0)
				{
					m_pServer->m_stClan[m_ClanID].ExpAdd = (int)pSqlServer->GetResults()->getInt("ExpAdd");
					if(m_Need == PLUS) m_pServer->m_stClan[m_ClanID].ExpAdd += m_Price;
					else m_pServer->m_stClan[m_ClanID].ExpAdd -= m_Price;

					VarGot = m_pServer->m_stClan[m_ClanID].ExpAdd;
				}
				else if(str_comp(m_sType.ClrStr(), "MoneyAdd") == 0)
				{
					m_pServer->m_stClan[m_ClanID].MoneyAdd = (int)pSqlServer->GetResults()->getInt("MoneyAdd");
					if(m_Need == PLUS) m_pServer->m_stClan[m_ClanID].MoneyAdd += m_Price;
					else m_pServer->m_stClan[m_ClanID].MoneyAdd -= m_Price;

					VarGot = m_pServer->m_stClan[m_ClanID].MoneyAdd;
				}		
				else if(str_comp(m_sType.ClrStr(), "SpawnHouse") == 0)
				{
					m_pServer->m_stClan[m_ClanID].IsSpawnInHouse = (int)pSqlServer->GetResults()->getInt("SpawnHouse");
					if(m_Need == PLUS) m_pServer->m_stClan[m_ClanID].IsSpawnInHouse += m_Price;
					else m_pServer->m_stClan[m_ClanID].IsSpawnInHouse -= m_Price;

					VarGot = m_pServer->m_stClan[m_ClanID].IsSpawnInHouse;
				}	
				else if(str_comp(m_sType.ClrStr(), "ChairHouse") == 0)
				{
					m_pServer->m_stClan[m_ClanID].ChairLevel = (int)pSqlServer->GetResults()->getInt("ChairHouse");
					if(m_Need == PLUS) m_pServer->m_stClan[m_ClanID].ChairLevel += m_Price;
					else m_pServer->m_stClan[m_ClanID].ChairLevel -= m_Price;

					VarGot = m_pServer->m_stClan[m_ClanID].ChairLevel;
				}	
				if(m_Save && VarGot > -1)
				{
					str_format(aBuf, sizeof(aBuf), 
						"UPDATE %s_Clans SET %s = '%d' WHERE ClanID = '%d';",
						pSqlServer->GetPrefix(),
						m_sType.ClrStr(), VarGot, m_ClanID);
					pSqlServer->executeSqlQuery(aBuf);
				}
				m_pServer->UpdClanCount(m_ClanID);
			}	
			return true;
		}
		catch (sql::SQLException const &e)
		{
			return false;
		}
		return true;
	}
};

// Выход с клана
class CSqlJob_Server_ExitClanOff : public CSqlJob
{
private:
	CServer* m_pServer;
	CSqlString<64> m_sName;
	int m_ClientID;
public:
	CSqlJob_Server_ExitClanOff(CServer* pServer, int ClientID, const char* pName)
	{
		m_pServer = pServer;
		m_sName = CSqlString<64>(pName);
		m_ClientID = ClientID;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[512];		
		try
		{
			int ClanID = 0;
			str_format(aBuf, sizeof(aBuf), 
				"UPDATE %s_Users SET ClanID = %d, ClanAdded = %d WHERE Nick = '%s';", pSqlServer->GetPrefix(), ClanID, ClanID, m_sName.ClrStr());
			pSqlServer->executeSqlQuery(aBuf);	
		}
		catch (sql::SQLException const &e)
		{
			return false;
		}
		return true;
	}
};


/////////////////////////////////////////// ИНИЦИАЛИЗАЦИЯ СТАНДАРТНЫХ ПАРАМЕТРОВ
class CSqlJob_Server_InitClient : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ClientID;
	
public:
	CSqlJob_Server_InitClient(CServer* pServer, int ClientID)
	{
		m_pServer = pServer;
		m_ClientID = ClientID;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[512];
		// Сразу инициализировать
		try
		{
			str_format(aBuf, sizeof(aBuf), 
				"SELECT * FROM %s_Users "
				"WHERE UserId = %d;"
				, pSqlServer->GetPrefix()
				, m_pServer->m_aClients[m_ClientID].m_UserID);
			pSqlServer->executeSqlQuery(aBuf);

			if(pSqlServer->GetResults()->next())
			{
				m_pServer->m_aClients[m_ClientID].AccData.m_Level = pSqlServer->GetResults()->getInt("Level");
				m_pServer->m_aClients[m_ClientID].AccData.m_Exp = pSqlServer->GetResults()->getInt("Exp");
				m_pServer->m_aClients[m_ClientID].AccData.m_Money = pSqlServer->GetResults()->getInt("Money");
				m_pServer->m_aClients[m_ClientID].AccData.m_Gold = pSqlServer->GetResults()->getInt("Gold");
				m_pServer->m_aClients[m_ClientID].AccData.m_Donate = pSqlServer->GetResults()->getInt("Donate");
				m_pServer->m_aClients[m_ClientID].AccData.m_Jail = pSqlServer->GetResults()->getInt("Jail");
				m_pServer->m_aClients[m_ClientID].AccData.m_Rel = pSqlServer->GetResults()->getInt("Rel");
				m_pServer->m_aClients[m_ClientID].AccData.m_Class = pSqlServer->GetResults()->getInt("Class");
				m_pServer->m_aClients[m_ClientID].AccData.m_ClanID = pSqlServer->GetResults()->getInt("ClanID");
				m_pServer->m_aClients[m_ClientID].AccData.m_Quest = pSqlServer->GetResults()->getInt("Quest");
				m_pServer->m_aClients[m_ClientID].AccData.m_Kill = pSqlServer->GetResults()->getInt("Killing");
				m_pServer->m_aClients[m_ClientID].AccData.m_WinArea = pSqlServer->GetResults()->getInt("WinArea");
				m_pServer->m_aClients[m_ClientID].AccData.m_IsJailed = pSqlServer->GetResults()->getInt("IsJailed");
				m_pServer->m_aClients[m_ClientID].AccData.m_JailLength = pSqlServer->GetResults()->getInt("JailLength");
				m_pServer->m_aClients[m_ClientID].AccData.m_SummerHealingTimes = pSqlServer->GetResults()->getInt("SummerHealingTimes");
				m_pServer->m_aClients[m_ClientID].AccData.m_ClanAdded = m_pServer->m_aClients[m_ClientID].AccData.m_ClanID > 0 ? pSqlServer->GetResults()->getInt("ClanAdded") : 0;
	
				str_copy(m_pServer->m_aClients[m_ClientID].m_aUsername, pSqlServer->GetResults()->getString("Nick").c_str(), sizeof(m_pServer->m_aClients[m_ClientID].m_aUsername));
				if(m_pServer->m_aClients[m_ClientID].AccData.m_Level <= 0 || m_pServer->m_aClients[m_ClientID].AccData.m_Class == -1 ) 
				{
					CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, "登录时出现错误,请报告管理员");
					m_pServer->AddGameServerCmd(pCmd);
					dbg_msg("user", "玩家ID %d 的数据初始化出现问题", m_pServer->m_aClients[m_ClientID].m_UserID);	
					return false;
				}
				dbg_msg("user", "玩家ID %d 的数据初始化成功", m_pServer->m_aClients[m_ClientID].m_UserID);
			}
			else
			{
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("登录时出现错误,请报告管理员"));
				m_pServer->AddGameServerCmd(pCmd);
				dbg_msg("sql", "玩家 %s 数据初始化失败", m_pServer->m_aClients[m_ClientID].m_aName);
			
			return false;
			}			
		}
		catch (sql::SQLException const &e)
		{
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("登录时出现错误,请报告管理员"));
			m_pServer->AddGameServerCmd(pCmd);
			dbg_msg("sql", "用户数据初始化失败 (MySQL 错误: %s)", e.what());
			
			return false;
		}
		
		try
		{
			str_format(aBuf, sizeof(aBuf), 
				"SELECT il_id, item_count, item_settings, item_enchant FROM %s_uItems WHERE item_owner = %d;", pSqlServer->GetPrefix(), m_pServer->m_aClients[m_ClientID].m_UserID);
			pSqlServer->executeSqlQuery(aBuf);

			while(pSqlServer->GetResults()->next())
			{
				int IDitem = pSqlServer->GetResults()->getInt("il_id");
				int ItemSettings = pSqlServer->GetResults()->getInt("item_settings");
				int ItemEnchant = pSqlServer->GetResults()->getInt("item_enchant");
				m_pServer->m_stInv[m_ClientID][IDitem].i_count = pSqlServer->GetResults()->getInt("item_count");
				m_pServer->m_stInv[m_ClientID][IDitem].i_settings = ItemSettings;			
				m_pServer->m_stInv[m_ClientID][IDitem].i_enchant = ItemEnchant;
			}					
		}
		catch (sql::SQLException const &e)
		{
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, "登录时出现错误,请报告管理员");
			m_pServer->AddGameServerCmd(pCmd);
			dbg_msg("sql", "用户数据初始化失败 (MySQL 错误: %s)", e.what());
			
			return false;
		}

		try
		{
			str_format(aBuf, sizeof(aBuf), 
				"SELECT * FROM %s_uClass WHERE UserID = %d;"
				, pSqlServer->GetPrefix()
				, m_pServer->m_aClients[m_ClientID].m_UserID);
			pSqlServer->executeSqlQuery(aBuf);

			if(pSqlServer->GetResults()->next())
			{				
				m_pServer->m_aClients[m_ClientID].AccUpgrade.m_Upgrade = pSqlServer->GetResults()->getInt("Upgrade");
				m_pServer->m_aClients[m_ClientID].AccUpgrade.m_SkillPoint = pSqlServer->GetResults()->getInt("SkillPoint");
				m_pServer->m_aClients[m_ClientID].AccUpgrade.m_Damage = pSqlServer->GetResults()->getInt("Damage");
				m_pServer->m_aClients[m_ClientID].AccUpgrade.m_Speed = pSqlServer->GetResults()->getInt("Speed");
				m_pServer->m_aClients[m_ClientID].AccUpgrade.m_Health = pSqlServer->GetResults()->getInt("Health");
				m_pServer->m_aClients[m_ClientID].AccUpgrade.m_HPRegen = pSqlServer->GetResults()->getInt("HPRegen");
				m_pServer->m_aClients[m_ClientID].AccUpgrade.m_AmmoRegen = pSqlServer->GetResults()->getInt("AmmoRegen");
				m_pServer->m_aClients[m_ClientID].AccUpgrade.m_Ammo = pSqlServer->GetResults()->getInt("Ammo");
				m_pServer->m_aClients[m_ClientID].AccUpgrade.m_Spray = pSqlServer->GetResults()->getInt("Spray");
				m_pServer->m_aClients[m_ClientID].AccUpgrade.m_Mana = pSqlServer->GetResults()->getInt("Mana");
				m_pServer->m_aClients[m_ClientID].AccUpgrade.m_HammerRange = pSqlServer->GetResults()->getInt("HammerRange");
				m_pServer->m_aClients[m_ClientID].AccUpgrade.m_Pasive2 = pSqlServer->GetResults()->getInt("Pasive2");

				CServer::CGameServerCmd *pCmd1 = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("登录成功.按下esc界面中的“开始游戏”进入."));
				m_pServer->AddGameServerCmd(pCmd1);
			}					
		}
		catch (sql::SQLException const &e)
		{
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, "登录时出现错误,请报告管理员");
			m_pServer->AddGameServerCmd(pCmd);
			dbg_msg("sql", "用户数据初始化失败 (MySQL 错误: %s)", e.what());
			return false;
		}
		return true;
	}
};

class CSqlJob_Server_UpdateStat	 : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ClientID;
	int m_Type;
	int m_UserID;
	
public:
	CSqlJob_Server_UpdateStat(CServer* pServer, int ClientID, int UserID, int Type)
	{
		m_pServer = pServer;
		m_ClientID = ClientID;
		m_UserID = UserID;
		m_Type = Type;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[1024];
		try
		{
			if(m_Type == 0)
			{
				if(m_pServer->m_aClients[m_ClientID].AccData.m_Level <= 0) return false;
				str_format(aBuf, sizeof(aBuf), 
					"UPDATE %s_Users "
					"SET Level = '%d', "
					"Exp = '%d', "
					"Class = '%d', "
					"Money = '%d', "
					"Gold = '%d', "
					"Donate = '%d', "
					"Rel = '%d', "
					"Jail = '%d', "
					"Quest = '%d', "
					"Killing = '%d', "
					"WinArea = '%d', "
					"Seccurity = '%d', "
					"ClanAdded = '%d', "
					"IsJailed = '%d', "
					"JailLength = '%d', "
					"SummerHealingTimes = '%d'"
					"WHERE UserId = '%d';"
					, pSqlServer->GetPrefix(), 
					m_pServer->m_aClients[m_ClientID].AccData.m_Level, 
					m_pServer->m_aClients[m_ClientID].AccData.m_Exp, 
					m_pServer->m_aClients[m_ClientID].AccData.m_Class, 
					m_pServer->m_aClients[m_ClientID].AccData.m_Money, 
					m_pServer->m_aClients[m_ClientID].AccData.m_Gold, 
					m_pServer->m_aClients[m_ClientID].AccData.m_Donate, 
					m_pServer->m_aClients[m_ClientID].AccData.m_Rel, 
					m_pServer->m_aClients[m_ClientID].AccData.m_Jail, 
					m_pServer->m_aClients[m_ClientID].AccData.m_Quest, 
					m_pServer->m_aClients[m_ClientID].AccData.m_Kill,  
					m_pServer->m_aClients[m_ClientID].AccData.m_WinArea, 
					m_pServer->m_aClients[m_ClientID].AccData.m_Security, 
					m_pServer->m_aClients[m_ClientID].AccData.m_ClanAdded, 
					m_pServer->m_aClients[m_ClientID].AccData.m_IsJailed, 
					m_pServer->m_aClients[m_ClientID].AccData.m_JailLength,
					m_pServer->m_aClients[m_ClientID].AccData.m_SummerHealingTimes,  
					m_pServer->m_aClients[m_ClientID].m_UserID);
				//dbg_msg("sql",aBuf);
				pSqlServer->executeSqlQuery(aBuf);
			}
			else if(m_Type == 1)
			{
				str_format(aBuf, sizeof(aBuf), 
					"UPDATE %s_uClass "
					"SET Upgrade = '%d', "
					"SkillPoint = '%d', "
					"Speed = '%d', "
					"Health = '%d', "
					"Damage = '%d', "
					"HPRegen = '%d', "
					"AmmoRegen = '%d', "
					"Ammo = '%d', "
					"Spray = '%d', "
					"Mana = '%d', "
					"HammerRange = '%d', "
					"Pasive2 = '%d' "
					"WHERE UserID = '%d';"
					, pSqlServer->GetPrefix(), m_pServer->m_aClients[m_ClientID].AccUpgrade.m_Upgrade, m_pServer->m_aClients[m_ClientID].AccUpgrade.m_SkillPoint, m_pServer->m_aClients[m_ClientID].AccUpgrade.m_Speed, m_pServer->m_aClients[m_ClientID].AccUpgrade.m_Health, m_pServer->m_aClients[m_ClientID].AccUpgrade.m_Damage,
					m_pServer->m_aClients[m_ClientID].AccUpgrade.m_HPRegen, m_pServer->m_aClients[m_ClientID].AccUpgrade.m_AmmoRegen, m_pServer->m_aClients[m_ClientID].AccUpgrade.m_Ammo, m_pServer->m_aClients[m_ClientID].AccUpgrade.m_Spray, m_pServer->m_aClients[m_ClientID].AccUpgrade.m_Mana, 
					m_pServer->m_aClients[m_ClientID].AccUpgrade.m_HammerRange, m_pServer->m_aClients[m_ClientID].AccUpgrade.m_Pasive2, m_pServer->m_aClients[m_ClientID].m_UserID);
				
				pSqlServer->executeSqlQuery(aBuf);
			}
			else if(m_Type == 3)
			{
				int ClanID = m_pServer->m_aClients[m_ClientID].AccData.m_ClanID;
				str_format(aBuf, sizeof(aBuf), 
					"UPDATE %s_Users SET ClanID = '%d' WHERE UserId = '%d';"
					, pSqlServer->GetPrefix()
					, ClanID, m_pServer->m_aClients[m_ClientID].m_UserID);
				pSqlServer->executeSqlQuery(aBuf);	
				
				if(ClanID > 0)
					m_pServer->UpdClanCount(ClanID);
			}
		}
		catch (sql::SQLException const &e)
		{

			if(str_length(e.what()) > 0)
			{
				if(m_Type == 0)
				{
					dbg_msg("sql", "个人信息更新失败 (MySQL Error: %s)", e.what());
				}
				else if(m_Type == 1)
				{
					dbg_msg("sql", "玩家升级点信息更新失败 (MySQL Error: %s)", e.what());	
				}
				else if(m_Type == 3)
				{
					dbg_msg("sql", "玩家公会信息更新失败 (MySQL Error: %s)", e.what());	
				}
			}
			
			return false;
		}
		return true;
	}
};    

class CSqlJob_Server_Login : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ClientID;
	CSqlString<64> m_sName;
	CSqlString<64> m_sNick;
	CSqlString<64> m_sPasswordHash;
	
public:
	CSqlJob_Server_Login(CServer* pServer, int ClientID, const char* pName, const char* pPasswordHash)
	{
		m_pServer = pServer;
		m_ClientID = ClientID;
		m_sName = CSqlString<64>(pName);
		m_sNick = CSqlString<64>(m_pServer->ClientName(m_ClientID));
		m_sPasswordHash = CSqlString<64>(pPasswordHash);
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		// Проверка регистра
		if(m_pServer->m_aClients[m_ClientID].m_LogInstance != GetInstance())
			return true;

		char aBuf[512];
		try
		{	
			if(m_pServer->m_aClients[m_ClientID].AccData.m_Security)
				str_format(aBuf, sizeof(aBuf), "SELECT UserId, Username, Nick, PasswordHash FROM %s_Users "
					"WHERE Username = '%s' AND PasswordHash = '%s' AND Nick = '%s';", pSqlServer->GetPrefix(), m_sName.ClrStr(), m_sPasswordHash.ClrStr(), m_sNick.ClrStr());
			else
				str_format(aBuf, sizeof(aBuf), "SELECT * FROM %s_Users WHERE PasswordHash = '%s' AND Nick = '%s';"
					, pSqlServer->GetPrefix(), m_sPasswordHash.ClrStr(), m_sNick.ClrStr());	

			pSqlServer->executeSqlQuery(aBuf);
			
			if(pSqlServer->GetResults()->next())
			{
				for(int i = 0; i < MAX_PLAYERS; ++i)
				{
					if((int)pSqlServer->GetResults()->getInt("UserId") == m_pServer->m_aClients[i].m_UserID)
					{
						m_pServer->m_aClients[m_ClientID].m_UserID = -1;
						return false;
					}
				}
				m_pServer->m_aClients[m_ClientID].m_UserID = (int)pSqlServer->GetResults()->getInt("UserId");
				m_pServer->InitClientDB(m_ClientID);
			}
			else
			{
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("用户名或密码错误,或是您的账户不属于该昵称."));
				m_pServer->AddGameServerCmd(pCmd);
			}
		}
		catch (sql::SQLException const &e)
		{
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, "登录时出现错误,请报告管理员");
			m_pServer->AddGameServerCmd(pCmd);
			dbg_msg("sql", "Can't check username/password (MySQL Error: %s)", e.what());
			
			return false;
		}
		return true;
	}
	virtual void CleanInstanceRef()
	{
		m_pServer->m_aClients[m_ClientID].m_LogInstance = -1;
	}
};

// Инициализация первого клиента
class CSqlJob_Server_FirstInit : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ClientID;
	CSqlString<64> m_sNick;
	
public:
	CSqlJob_Server_FirstInit(CServer* pServer, int ClientID)
	{
		m_pServer = pServer;
		m_ClientID = ClientID;
		m_sNick = CSqlString<64>(m_pServer->ClientName(m_ClientID));
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[512];
		try
		{
			str_format(aBuf, sizeof(aBuf), 
				"SELECT Nick, Seccurity FROM %s_Users "
				"WHERE Nick = '%s';"
				, pSqlServer->GetPrefix()
				, m_sNick.ClrStr());
			pSqlServer->executeSqlQuery(aBuf);
			
			if(pSqlServer->GetResults()->next())
				m_pServer->m_aClients[m_ClientID].AccData.m_Security = (int)pSqlServer->GetResults()->getInt("Seccurity");
			else
			{
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(-1, CHATCATEGORY_DEFAULT, _("欢迎新玩家!"));
				m_pServer->AddGameServerCmd(pCmd);
			}
		}
		catch (sql::SQLException const &e)
		{
			dbg_msg("sql", "Can't check newplayer security (MySQL Error: %s)", e.what());
			return false;			
		}
		return true;
	}
};

// Регистрация аккаунта
class CSqlJob_Server_Register : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ClientID;
	CSqlString<64> m_sName;
	CSqlString<64> m_sNick;
	CSqlString<64> m_sPasswordHash;
	CSqlString<64> m_sEmail;
	
public:
	CSqlJob_Server_Register(CServer* pServer, int ClientID, const char* pName, const char* pPasswordHash, const char* pEmail)
	{
		m_pServer = pServer;
		m_ClientID = ClientID;
		m_sName = CSqlString<64>(pName);
		m_sNick = CSqlString<64>(m_pServer->ClientName(m_ClientID));
		m_sPasswordHash = CSqlString<64>(pPasswordHash);
		if(pEmail)
			m_sEmail = CSqlString<64>(pEmail);
		else
			m_sEmail = CSqlString<64>("");
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		// Проверка регистра
		if(m_pServer->m_aClients[m_ClientID].m_LogInstance != GetInstance())
			return true;
		
		char aBuf[512];
		char aAddrStr[64];
		net_addr_str(m_pServer->m_NetServer.ClientAddr(m_ClientID), aAddrStr, sizeof(aAddrStr), false);

		try
		{
			//检查数据库中的名称或昵称
			str_format(aBuf, sizeof(aBuf), 
				"SELECT UserId FROM %s_Users WHERE Username = '%s' OR Nick = '%s';"
				, pSqlServer->GetPrefix()
				, m_sName.ClrStr(), m_sNick.ClrStr());
			pSqlServer->executeSqlQuery(aBuf);

			if(pSqlServer->GetResults()->next())
			{
				dbg_msg("mmotee", "用户名/昵称 %s 已被占用",m_sNick.ClrStr());
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("这个用户名/昵称已被占用"));
				m_pServer->AddGameServerCmd(pCmd);
				return true;
			}
		}
		catch (sql::SQLException const &e)
		{
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("在注册账号时发生了错误"));
			m_pServer->AddGameServerCmd(pCmd);
			dbg_msg("sql", "Can't check username existance (MySQL Error: %s)", e.what());
			
			return false;
		}
		
		//Создаем сам аккаунт
		try
		{	
			str_format(aBuf, sizeof(aBuf), 
				"INSERT INTO %s_Users "
				"(Username, Nick, PasswordHash, Email, RegisterIp) "
				"VALUES ('%s', '%s', '%s', '%s', '%s');"
				, pSqlServer->GetPrefix()
				, m_sName.ClrStr(), m_sNick.ClrStr(), m_sPasswordHash.ClrStr(), m_sEmail.ClrStr(), aAddrStr);
			pSqlServer->executeSql(aBuf);
		}
		catch (sql::SQLException const &e)
		{
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("在注册账号时发生了错误"));
			m_pServer->AddGameServerCmd(pCmd);
			dbg_msg("sql", "Can't create new user (MySQL Error: %s)", e.what());
			
			return false;
		}
		
		// Получаем инфу пользователя
		try
		{	
			str_format(aBuf, sizeof(aBuf), 
				"SELECT UserId FROM %s_Users WHERE Username = '%s' AND PasswordHash = '%s';"
				, pSqlServer->GetPrefix()
				, m_sName.ClrStr(), m_sPasswordHash.ClrStr());
			pSqlServer->executeSqlQuery(aBuf);

			if(pSqlServer->GetResults()->next())
			{
				int UsedID = (int)pSqlServer->GetResults()->getInt("UserId");
				str_format(aBuf, sizeof(aBuf), 
					"INSERT INTO %s_uClass (UserID, Username) VALUES ('%d', '%s');"
					, pSqlServer->GetPrefix()
					, UsedID, m_sName.ClrStr());
				pSqlServer->executeSql(aBuf);

				str_copy(m_pServer->m_aClients[m_ClientID].AccData.m_Clan, "NOPE", sizeof(m_pServer->m_aClients[m_ClientID].AccData.m_Clan));
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("账户注册成功.请使用/login <密码> 登录."));
				m_pServer->AddGameServerCmd(pCmd);
				return true;
			}
			else
			{
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("在注册账号时发生了错误"));
				m_pServer->AddGameServerCmd(pCmd);
				return false;
			}
		}
		catch (sql::SQLException const &e)
		{
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("在注册账号时发生了错误"));
			m_pServer->AddGameServerCmd(pCmd);
			dbg_msg("sql", "Can't get the ID of the new user (MySQL Error: %s)", e.what());
			
			return false;
		}
		return true;
	}
	virtual void CleanInstanceRef()
	{
		m_pServer->m_aClients[m_ClientID].m_LogInstance = -1;
	}
};

class CSqlJob_Server_ChangePassword_Admin : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ClientID;
	CSqlString<64> m_sNick;
	CSqlString<64> m_sPasswordHash;
	
public:
	CSqlJob_Server_ChangePassword_Admin(CServer* pServer,int ClientID, const char* pNick, const char* pPasswordHash)
	{
		m_ClientID = ClientID;
		m_pServer = pServer;
		m_sNick = pNick;
		m_sPasswordHash = CSqlString<64>(pPasswordHash);
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[512];
		
		
		// 更新密码 Hash
		try
		{
			str_format(aBuf, sizeof(aBuf),
					   "SELECT * FROM %s_Users WHERE Nick = '%s';"
					   , pSqlServer->GetPrefix(), m_sNick.ClrStr());
			pSqlServer->executeSqlQuery(aBuf);
			if (!pSqlServer->GetResults()->next())
			{
				CServer::CGameServerCmd *pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("找不到用户"));
				m_pServer->AddGameServerCmd(pCmd);
				return false;
			}
		}
		catch (sql::SQLException const &e)
		{
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("更改密码失败"));
			m_pServer->AddGameServerCmd(pCmd);
			dbg_msg("sql","管理员更改玩家 %s 的密码时发生了错误 %s", m_sNick.ClrStr(), e.what());
			return false;
		}
		str_format(aBuf, sizeof(aBuf),
				   "UPDATE %s_Users SET PasswordHash = '%s' WHERE Nick = '%s';"
				   , pSqlServer->GetPrefix(), m_sPasswordHash.ClrStr(), m_sNick.ClrStr());
		pSqlServer->executeSql(aBuf);
		CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("成功更改密码"));
			m_pServer->AddGameServerCmd(pCmd);
		dbg_msg("sql","玩家 %s 的密码被更改", m_sNick.ClrStr());
		return true;
	}
	
};

class CSqlJob_Server_ChangePassword : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ClientID;
	CSqlString<64> m_sPasswordHash;
	
public:
	CSqlJob_Server_ChangePassword(CServer* pServer, int ClientID, const char* pPasswordHash)
	{
		m_pServer = pServer;
		m_ClientID = ClientID;
		m_sPasswordHash = CSqlString<64>(pPasswordHash);
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		
		char aBuf[512];
		
		
		// 更新密码 Hash
		try
		{	
			//dbg_msg("test","1");
			str_format(aBuf, sizeof(aBuf), 
				"UPDATE %s_Users SET PasswordHash = '%s' WHERE UserId = '%d';"
				, pSqlServer->GetPrefix()
				, m_sPasswordHash.ClrStr(), m_pServer->m_aClients[m_ClientID].m_UserID);
			//dbg_msg("test","1.5");
			pSqlServer->executeSql(aBuf);	
			//dbg_msg("test","2");
		}
		catch (sql::SQLException const &e)
		{
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("更改密码失败"));
			m_pServer->AddGameServerCmd(pCmd);		
			dbg_msg("sql","玩家 %s 更改密码时发生了错误 %s", m_pServer->m_aClients[m_ClientID].m_aName, e.what());
			return true;
		}
		//dbg_msg("test","3");
		CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("成功更改密码"));
		m_pServer->AddGameServerCmd(pCmd);
		dbg_msg("sql","玩家 %s 更改了密码", m_pServer->m_aClients[m_ClientID].m_aName);
		return true;
	}
	
};

// Тоp 10
class CSqlJob_Server_ShowTop10 : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ClientID;
	int m_Type;
	CSqlString<64> m_sType;
	
public:
	CSqlJob_Server_ShowTop10(CServer* pServer, int ClientID, const char* Type, int TypeGet)
	{
		m_pServer = pServer;
		m_ClientID = ClientID;
		m_sType = CSqlString<64>(Type);
		m_Type = TypeGet;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[256];
		
		try
		{
			int SortTop = m_Type == 2 ? 5 : 10;
			str_format(aBuf, sizeof(aBuf), 
				"SELECT %s, Nick FROM %s_Users ORDER BY %s DESC LIMIT %d;",
				m_sType.ClrStr(), pSqlServer->GetPrefix(), m_sType.ClrStr(), SortTop);
			pSqlServer->executeSqlQuery(aBuf);

			int Rank = 0;
			while(pSqlServer->GetResults()->next())
			{
				Rank++;

				int Level = (int)pSqlServer->GetResults()->getInt(m_sType.ClrStr());
				dynamic_string Buffer;
				m_pServer->Localization()->Format(Buffer, m_pServer->GetClientLanguage(m_ClientID), _("Rank {int:Rank} - {str:Name} {int:Count}"), 
					"Rank", &Rank, "Name", pSqlServer->GetResults()->getString("Nick").c_str(), "Count", &Level);
			
				if(m_Type == 1)
				{
					CServer::CGameServerCmd* pCmd = new CGameServerCmd_AddLocalizeVote_Language(m_ClientID, "null", _(Buffer.buffer()));
					m_pServer->AddGameServerCmd(pCmd);
				}
				else
				{	
					CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(-1, CHATCATEGORY_DEFAULT, _(Buffer.buffer()));
					m_pServer->AddGameServerCmd(pCmd);	
				}
				Buffer.clear();
			}
		}
		catch (sql::SQLException const &e)
		{
			dbg_msg("sql", "Can't get top10 (MySQL Error: %s)", e.what());
			return false;
		}
		
		return true;
	}
};

// Топ 10 Кланов
class CSqlJob_Server_ShowTop10Clans : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ClientID;
	int m_Type;
	CSqlString<64> m_sType;
	
public:
	CSqlJob_Server_ShowTop10Clans(CServer* pServer, int ClientID, const char* Type, int TypeGet)
	{
		m_pServer = pServer;
		m_ClientID = ClientID;
		m_sType = CSqlString<64>(Type);
		m_Type = TypeGet;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[256];
		try
		{
			int SortTop = m_Type == 2 ? 5 : 10;		
			str_format(aBuf, sizeof(aBuf), 
				"SELECT %s, Clanname, LeaderName FROM %s_Clans ORDER BY %s DESC LIMIT %d;",
				m_sType.ClrStr(), pSqlServer->GetPrefix(), m_sType.ClrStr(), SortTop);
			pSqlServer->executeSqlQuery(aBuf);

			int Rank = 0;
			while(pSqlServer->GetResults()->next())
			{
				Rank++;
				int Level = (int)pSqlServer->GetResults()->getInt(m_sType.ClrStr());

				dynamic_string Buffer;
				m_pServer->Localization()->Format(Buffer, m_pServer->GetClientLanguage(m_ClientID), _("第{int:Rank}名 - {str:Name} {int:Count} 会长 {str:Leader}"), 
					"Rank", &Rank, "Name", pSqlServer->GetResults()->getString("Clanname").c_str(), "Count", &Level, "Leader", pSqlServer->GetResults()->getString("LeaderName").c_str());
			
				if(m_Type == 1)
				{
					CServer::CGameServerCmd* pCmd = new CGameServerCmd_AddLocalizeVote_Language(m_ClientID, "null", _(Buffer.buffer()));
					m_pServer->AddGameServerCmd(pCmd);
				}
				else
				{	
					CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(-1, CHATCATEGORY_DEFAULT, _(Buffer.buffer()));
					m_pServer->AddGameServerCmd(pCmd);	
				}
				Buffer.clear();
			}
		}
		catch (sql::SQLException const &e)
		{
			dbg_msg("sql", "Can't get top10 clans (MySQL Error: %s)", e.what());
			return false;
		}
		return true;
	}
};

// ********************************************** OZOZOZ ДОМА СОСАТЬ ЛЕЖАТЬ НАХУЙ!!! ——抱怨
// Получить дома кланов
// 获得公会房屋
class CSqlJob_Server_GetTopClanHouse : public CSqlJob
{
private:
	CServer* m_pServer;
	
public:
	CSqlJob_Server_GetTopClanHouse(CServer* pServer)
	{
		m_pServer = pServer;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		try
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "SELECT ClanID, Money FROM %s_Clans ORDER BY Money DESC LIMIT 3;", pSqlServer->GetPrefix());
			pSqlServer->executeSqlQuery(aBuf);
			int House = 0;
			while(pSqlServer->GetResults()->next())
			{
				int ClanID = (int)pSqlServer->GetResults()->getInt("ClanID");
				m_pServer->m_HouseClanID[House] = ClanID;
			
				char aBuf[128];
				if(m_pServer->m_HouseClanID[House] != m_pServer->m_HouseOldClanID[House])
				{
					str_format(aBuf, sizeof(aBuf), "[房屋#%d] 公会 %s 得到房屋,公会 %s 失去了它!", House,  m_pServer->GetClanName(ClanID), m_pServer->GetClanName(m_pServer->m_HouseOldClanID[House]));
				
					CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(-1, CHATCATEGORY_DEFAULT, _(aBuf));
					m_pServer->AddGameServerCmd(pCmd);
				}
				else
				{
					str_format(aBuf, sizeof(aBuf), "[房屋#%d] 公会 %s 守住了房屋!", House,  m_pServer->GetClanName(ClanID));
				
					CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(-1, CHATCATEGORY_DEFAULT, _(aBuf));
					m_pServer->AddGameServerCmd(pCmd);
				}
				m_pServer->m_HouseOldClanID[House] = ClanID;
				House++;
			} 
		}
		catch (sql::SQLException const &e)
		{
			return false;
		}
		return true;
	}
};

class CSqlJob_Server_SyncOnline : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ClientID;
	CSqlString<64> m_sNick;
	
public:
	CSqlJob_Server_SyncOnline(CServer* pServer, int ClientID)
	{
		m_pServer = pServer;
		m_ClientID = ClientID;
		m_sNick = CSqlString<64>(m_pServer->ClientName(m_ClientID));
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		// Проверка регистра
		//if(m_pServer->m_aClients[m_ClientID].m_LogInstance != GetInstance())
		//	return true;
		//dbg_msg("test","5");
		while(m_pServer->m_aClients[m_ClientID].m_LogInstance != GetInstance())
		{
			sleep(1);
		}
		char aBuf[512];
		char aAddrStr[64];
		net_addr_str(m_pServer->m_NetServer.ClientAddr(m_ClientID), aAddrStr, sizeof(aAddrStr), false);
		//dbg_msg("test","6");
		try
		{
			//检查数据库中的 IP
			str_format(aBuf, sizeof(aBuf), 
				"SELECT * FROM %s_UserStatus WHERE IP = '%s' ORDER BY ID DESC LIMIT 1;"
				,pSqlServer->GetPrefix()
				,aAddrStr);
			pSqlServer->executeSqlQuery(aBuf);
			//dbg_msg("test","7 %s", aBuf);
			if(pSqlServer->GetResults()->next())
			{
				int IsOnline;
				int IsBanned;
				char banreason[512];
				IsOnline = (int)pSqlServer->GetResults()->getInt("online");
				IsBanned = (int)pSqlServer->GetResults()->getInt("ban");
				str_copy(banreason, pSqlServer->GetResults()->getString("banreason").c_str(), sizeof(banreason));
				//dbg_msg("test","8 %d %d %s", IsOnline, IsBanned, banreason);
				if(IsOnline)
				{
					m_pServer->Kick(m_ClientID, "禁止重复登录");
					return true;
				}
				else if(IsBanned)
				{
					char buf[512];
					str_format(buf, sizeof(buf), "你被封禁了,原因: %s", banreason);
					m_pServer->Kick(m_ClientID, buf);
					return true;
				} 
				else if(IsOnline == -1) // 允许同 IP 双开
				{
					m_pServer->m_aClients[m_ClientID].m_UserStatusID = -1;
					return true;
				}
			}
			//检查数据库中的名称或昵称
			str_format(aBuf, sizeof(aBuf), 
				"SELECT * FROM %s_UserStatus WHERE Nick = '%s' ORDER BY ID DESC LIMIT 1;"
				, pSqlServer->GetPrefix()
				, m_sNick.ClrStr());
			pSqlServer->executeSqlQuery(aBuf);
			//dbg_msg("test","7 %s", aBuf);
			if(pSqlServer->GetResults()->next())
			{
				int IsOnline;
				int IsBanned;
				char banreason[512];
				IsOnline = (int)pSqlServer->GetResults()->getInt("online");
				IsBanned = (int)pSqlServer->GetResults()->getInt("ban");
				str_copy(banreason, pSqlServer->GetResults()->getString("banreason").c_str(), sizeof(banreason));
				//dbg_msg("test","8 %d %d %s", IsOnline, IsBanned, banreason);
				if(IsOnline)
				{
					m_pServer->Kick(m_ClientID, "禁止重复登录");
					return true;
				}
				else if(IsBanned)
				{
					char buf[512];
					str_format(buf, sizeof(buf), "你被封禁了,原因: %s", banreason);
					m_pServer->Kick(m_ClientID, buf);
					return true;
				}
			}

			str_format(aBuf, sizeof(aBuf),
				"INSERT INTO %s_UserStatus (IP, Nick, online, serverid) VALUES ('%s', '%s', '1','%d');",
				 pSqlServer->GetPrefix(),
				 aAddrStr, m_sNick.ClrStr(), g_Config.m_ServerID);
			
			pSqlServer->executeSql(aBuf);

			str_format(aBuf, sizeof(aBuf), 
				"SELECT ID FROM %s_UserStatus WHERE IP = '%s' ORDER BY ID DESC LIMIT 1;"
				, pSqlServer->GetPrefix()
				, aAddrStr);
			pSqlServer->executeSqlQuery(aBuf);
			if(pSqlServer->GetResults()->next())
			{
				m_pServer->m_aClients[m_ClientID].m_UserStatusID = (int)pSqlServer->GetResults()->getInt("ID");
				//dbg_msg("uid","%d",m_pServer->m_aClients[m_ClientID].m_UserID);
			}
				dbg_msg("user","玩家 %s 上线了", m_sNick.ClrStr());
		}
		catch (sql::SQLException const &e)
		{
			dbg_msg("sql", "在检查登录状态时发生了错误 (MySQL 错误: %s)", e.what());
			
			return false;
		}
		
		return true;
	}
	virtual void CleanInstanceRef()
	{
		m_pServer->m_aClients[m_ClientID].m_LogInstance = -1;
	}
};

class CSqlJob_Server_SyncOffline : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ClientID;
	int m_UserStatusID;
	CSqlString<64> m_sNick;
	
public:
	CSqlJob_Server_SyncOffline(CServer* pServer, int ClientID)
	{
		m_pServer = pServer;
		m_ClientID = ClientID;
		m_sNick = CSqlString<64>(m_pServer->ClientName(m_ClientID));
		m_UserStatusID = m_pServer->m_aClients[m_ClientID].m_UserStatusID;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[512];
		if(m_UserStatusID >= 0)
		{
			try
			{
				////检查数据库中的 IP
				str_format(aBuf, sizeof(aBuf), 
					"SELECT * FROM %s_UserStatus WHERE ID = '%d';"
					, pSqlServer->GetPrefix()
					, m_UserStatusID);
				pSqlServer->executeSqlQuery(aBuf);
				if(pSqlServer->GetResults()->next())
				{
					str_format(aBuf, sizeof(aBuf), 
						"UPDATE %s_UserStatus SET online = '0' WHERE ID = '%d';"
						, pSqlServer->GetPrefix()
						, m_UserStatusID);
					pSqlServer->executeSql(aBuf);
				}
				dbg_msg("user","玩家 %s 下线了", m_sNick.ClrStr());
				return true;
			}
			catch (sql::SQLException const &e)
			{
				dbg_msg("sql", "在检查登录状态时发生了错误 (MySQL 错误: %s)", e.what());

				return false;
			}
		}
		return true;
	}
	
};

class CSqlJob_Server_Ban_DB : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ClientID;
	int m_UserStatusID;
	int m_ClientID_Ban;
	CSqlString<64> m_sNick;	
	CSqlString<64> m_Reason;

public:
	CSqlJob_Server_Ban_DB(CServer* pServer, int ClientID, int ClientID_Ban, const char* Reason)
	{
		m_pServer = pServer;
		m_ClientID = ClientID;
		m_sNick = CSqlString<64>(m_pServer->ClientName(m_ClientID_Ban));
		m_ClientID_Ban = ClientID_Ban;
		m_UserStatusID = m_pServer->m_aClients[m_ClientID_Ban].m_UserStatusID;
		m_Reason = CSqlString<64>(Reason);
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[512];
		if(m_UserStatusID >= 0)
		{
			try
			{
				//检查数据库中的 IP
				str_format(aBuf, sizeof(aBuf), 
					"SELECT * FROM %s_UserStatus WHERE ID = '%d';"
					, pSqlServer->GetPrefix()
					, m_UserStatusID);
				pSqlServer->executeSqlQuery(aBuf);
				//dbg_msg("test","1 %s",aBuf);
				if(pSqlServer->GetResults()->next())
				{
					str_format(aBuf, sizeof(aBuf), 
						"UPDATE %s_UserStatus SET ban = '1', banreason = '%s' WHERE ID = '%d';"
						, pSqlServer->GetPrefix()
						, m_Reason.ClrStr(), m_UserStatusID);
					pSqlServer->executeSql(aBuf);
					//dbg_msg("test","2 %s", aBuf);
				}
				m_pServer->Kick(m_ClientID_Ban, m_Reason.ClrStr());
				dbg_msg("user","玩家 %s 被封禁了", m_sNick.ClrStr());
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("封禁成功."));
				m_pServer->AddGameServerCmd(pCmd);	
				return true;
			}
			catch (sql::SQLException const &e)
			{
				dbg_msg("sql", "在写入 ban 数据时发生了错误 (MySQL 错误: %s)", e.what());
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("封禁失败."));
				m_pServer->AddGameServerCmd(pCmd);
				return false;
			}
		}
		return true;
	}
	
};

class CSqlJob_Server_Unban_DB : public CSqlJob
{
private:
	CServer* m_pServer;
	CSqlString<64> m_sNick;	
	int m_ClientID;
public:
	CSqlJob_Server_Unban_DB(CServer* pServer, int ClientID, const char* Nick)
	{
		m_pServer = pServer;
		m_sNick = CSqlString<64>(Nick);
		m_ClientID = ClientID;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[512];
		try
		{
			//检查数据库中的 IP
			str_format(aBuf, sizeof(aBuf), 
				"SELECT ID FROM %s_UserStatus WHERE Nick = '%s' AND ban = '1' ORDER BY ID DESC LIMIT 1;"
				, pSqlServer->GetPrefix()
				, m_sNick.ClrStr());
			pSqlServer->executeSqlQuery(aBuf);
			//dbg_msg("test","1 %s",aBuf);
			if(pSqlServer->GetResults()->next())
			{
				int m_UserStatusID;
				m_UserStatusID = (int)pSqlServer->GetResults()->getInt("ID");
				str_format(aBuf, sizeof(aBuf), 
					"UPDATE %s_UserStatus SET ban = '0', banreason = '' WHERE ID = '%d';"
					, pSqlServer->GetPrefix()
					, m_UserStatusID);
				pSqlServer->executeSql(aBuf);
				//dbg_msg("test","2 %s", aBuf);
			
			dbg_msg("user","玩家 %s 被解封了", m_sNick.ClrStr());
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("解封成功."));
			m_pServer->AddGameServerCmd(pCmd);	
			return true;
			}
			else
			{
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("未找到该用户或该用户未被封禁."));
				m_pServer->AddGameServerCmd(pCmd);
				return true;
			}
		}
		catch (sql::SQLException const &e)
		{
			dbg_msg("sql", "在写入 ban 数据时发生了错误 (MySQL 错误: %s)", e.what());
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("解封失败."));
			m_pServer->AddGameServerCmd(pCmd);	
			return false;
		}
		return true;
	}
	
};

class CSqlJob_Server_SetOffline : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ClientID;
	int m_UserStatusID;
	CSqlString<64> m_sNick;
	
public:
	CSqlJob_Server_SetOffline(CServer* pServer, int ClientID, const char* pNick)
	{
		m_pServer = pServer;
		m_ClientID = ClientID;
		m_sNick = CSqlString<64>(pNick);
		//m_UserStatusID = m_pServer->m_aClients[m_ClientID].m_UserStatusID;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[512];
		//char aAddrStr[64];
		//net_addr_str(m_pServer->m_NetServer.ClientAddr(m_ClientID), aAddrStr, sizeof(aAddrStr), false);
		//dbg_msg("ID","%d",m_UserID);
		//if(m_UserStatusID >= 0)
		//{
			try
			{
				str_format(aBuf, sizeof(aBuf), 
					"SELECT * FROM %s_UserStatus WHERE Nick = '%s' ORDER BY ID DESC LIMIT 1;"
					, pSqlServer->GetPrefix()
					, m_sNick.ClrStr());
				pSqlServer->executeSqlQuery(aBuf);
				//dbg_msg("test","1 %s",aBuf);
				if(pSqlServer->GetResults()->next())
				{
					m_UserStatusID = (int)pSqlServer->GetResults()->getInt("ID");
					str_format(aBuf, sizeof(aBuf), 
						"UPDATE %s_UserStatus SET online = '0' WHERE ID = '%d';"
						, pSqlServer->GetPrefix()
						, m_UserStatusID);
					pSqlServer->executeSql(aBuf);
					//dbg_msg("test","2 %s", aBuf);
				}
				else
				{
					char aText[600];
					str_format(aText, sizeof(aText), "找不到玩家 %s.", m_sNick.ClrStr());
					CServer::CGameServerCmd *pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, aText);
					m_pServer->AddGameServerCmd(pCmd);
					return true;
				}
				dbg_msg("user","玩家 %s 被设置为下线了", m_sNick.ClrStr());
				char aText[600];
				str_format(aText, sizeof(aText), "成功将玩家 %s 设置为下线.", m_sNick.ClrStr());
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, aText);
				m_pServer->AddGameServerCmd(pCmd);
				return true;
			}
			catch (sql::SQLException const &e)
			{
				dbg_msg("sql", "在检查登录状态时发生了错误 (MySQL 错误: %s)", e.what());
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("操作失败."));
				m_pServer->AddGameServerCmd(pCmd);
				return false;
			}
		//}
		return true;
	}
	
};

class CSqlJob_Server_UpdateOnline : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ClientID;
	int m_UserStatusID;
	CSqlString<64> m_sNick;
	
public:
	CSqlJob_Server_UpdateOnline(CServer* pServer, int ClientID)
	{
		m_pServer = pServer;
		m_ClientID = ClientID;
		m_sNick = CSqlString<64>(m_pServer->ClientName(m_ClientID));
		m_UserStatusID = m_pServer->m_aClients[m_ClientID].m_UserStatusID;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[512];
		//dbg_msg("ID","%d",m_UserID);
		if(m_UserStatusID >= 0)
		{
			try
			{
				str_format(aBuf, sizeof(aBuf), 
					"SELECT * FROM %s_UserStatus WHERE ID = '%d';"
					, pSqlServer->GetPrefix()
					, m_UserStatusID);
				pSqlServer->executeSqlQuery(aBuf);
				//dbg_msg("test","1 %s",aBuf);
				if(pSqlServer->GetResults()->next())
				{
					str_format(aBuf, sizeof(aBuf), 
						"UPDATE %s_UserStatus SET lastupdate = now() WHERE ID = '%d';"
						, pSqlServer->GetPrefix()
						, m_UserStatusID);
					pSqlServer->executeSql(aBuf);
					//dbg_msg("test","2 %s", aBuf);
				}
				//dbg_msg("user","玩家 %s 的状态更新了", m_sNick.ClrStr());
				return true;
			}
			catch (sql::SQLException const &e)
			{
				if(str_length(e.what()) > 0)
					dbg_msg("sql", "在更新玩家状态时发生了错误 (MySQL 错误: %s)", e.what());
				return false;
			}
		}
		return true;
	}
	
};

class CSqlJob_Server_UpdateOffline : public CSqlJob
{
private:
	CServer* m_pServer;
	
public:
	CSqlJob_Server_UpdateOffline(CServer* pServer)
	{
		m_pServer = pServer;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[512];
		char Nick[64];
		try
		{
			str_format(aBuf, sizeof(aBuf), "SELECT * FROM %s_UserStatus WHERE online = '1' and timestampdiff(second, lastupdate, now()) > 360;", pSqlServer->GetPrefix());
			pSqlServer->executeSqlQuery(aBuf);
				//dbg_msg("test","1 %s",aBuf);
			while(pSqlServer->GetResults()->next())
			{
				str_copy(Nick, pSqlServer->GetResults()->getString("Nick").c_str(), sizeof(Nick));
				//dbg_msg("test","2 %s", aBuf);
				dbg_msg("user","玩家 %s 超时了,被设置为下线", Nick);
			}
			str_format(aBuf, sizeof(aBuf), "UPDATE %s_UserStatus SET online = '0' WHERE online = '1' and timestampdiff(second, lastupdate, now()) > 360;", pSqlServer->GetPrefix());
			pSqlServer->executeSqlQuery(aBuf);
			return true;
		}
		catch (sql::SQLException const &e)
		{
			if(str_length(e.what()) > 0)
				dbg_msg("sql", "在更新玩家状态时发生了错误 (MySQL 错误: %s)", e.what());
			return false;
		}
	}
	
};

class CSqlJob_Server_InitAuction : public CSqlJob
{
private:
	CServer* m_pServer;

public:
	CSqlJob_Server_InitAuction(CServer* pServer)
	{
		m_pServer = pServer;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		try
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "SELECT * FROM %s_Auction;", pSqlServer->GetPrefix());
			pSqlServer->executeSqlQuery(aBuf);
			while(pSqlServer->GetResults()->next())
			{
				int ID = pSqlServer->GetResults()->getInt("ID");
				m_pServer->m_stAuction[ID].ID = ID;
				m_pServer->m_stAuction[ID].ItemID = pSqlServer->GetResults()->getInt("ItemID");
				m_pServer->m_stAuction[ID].ItemValue = pSqlServer->GetResults()->getInt("ItemValue");
				m_pServer->m_stAuction[ID].Price = pSqlServer->GetResults()->getInt("Price");
				m_pServer->m_stAuction[ID].Enchant = pSqlServer->GetResults()->getInt("Enchant");
				m_pServer->m_stAuction[ID].UserID = pSqlServer->GetResults()->getInt("UserID");
			}
		}
		catch (sql::SQLException const &e)
		{
			return false;
		}
		return true;
	}
};

class CSqlJob_Server_LogWarning : public CSqlJob
{
private:
	CServer* m_pServer;
	char m_aWarning[256];

public:
	CSqlJob_Server_LogWarning(CServer* pServer, const char Warning[256])
	{
		m_pServer = pServer;
		str_copy(m_aWarning, Warning, sizeof(m_aWarning));
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		try
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "INSERT INTO `%s_WarningLog` VALUES ('%s');", pSqlServer->GetPrefix(), m_aWarning);
			pSqlServer->executeSqlQuery(aBuf);
			if(pSqlServer->GetResults()->next())
				return true;
			return false;
		}
		catch (sql::SQLException const &e)
		{
			return false;
		}
		return true;
	}
};

class CSqlJob_Server_GiveDonate : public CSqlJob
{
private:
	CServer* m_pServer;
	char m_aUsername[64];
	int m_Donate;
	int m_WhoDid;

public:
	CSqlJob_Server_GiveDonate(CServer* pServer, const char aUsername[64], int Donate, int Admin)
	{
		m_pServer = pServer;
		str_copy(m_aUsername, aUsername, sizeof(m_aUsername));
		m_Donate = Donate;
		m_WhoDid = Admin;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[128];
		try
		{
			str_format(aBuf, sizeof(aBuf), "SELECT * FROM %s_Users WHERE Username = '%s';", pSqlServer->GetPrefix(), m_aUsername);
			pSqlServer->executeSqlQuery(aBuf);
			if(pSqlServer->GetResults()->next())
			{
				str_format(aBuf, sizeof(aBuf), "UPDATE %s_Users SET Donate = Donate + %d WHERE Username = '%s';", pSqlServer->GetPrefix(), m_Donate, m_aUsername);
				pSqlServer->executeSql(aBuf);
				str_format(aBuf, sizeof(aBuf), "管理员%s给了用户名为%s的玩家%d点卷", m_pServer->ClientName(m_WhoDid), m_aUsername, m_Donate);
				dbg_msg("Donate", aBuf);
				m_pServer->LogWarning(aBuf);
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_WhoDid, CHATCATEGORY_DEFAULT, _("点卷已送达."));
				m_pServer->AddGameServerCmd(pCmd);
				return true;
			}
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_WhoDid, CHATCATEGORY_DEFAULT, _("未找到该玩家"));
			m_pServer->AddGameServerCmd(pCmd);
			return false;
		}
		catch (sql::SQLException const &e)
		{
			if(str_length(e.what()) > 0)
				dbg_msg("sql", "在更新玩家状态时发生了错误 (MySQL 错误: %s)", e.what());
			dbg_msg("sql", "在更新玩家状态时发生了错误 (MySQL 错误: %s)", e.what());
			return false;
		}
		return true;
	}
};

//#####################################################################
// Создание клана
class CSqlJob_Server_Newclan : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ClientID;
	CSqlString<64> m_sName;
	CSqlString<64> m_sNick;
	
public:
	CSqlJob_Server_Newclan(CServer* pServer, int ClientID, const char* pName)
	{
		m_pServer = pServer;
		m_ClientID = ClientID;
		m_sName = CSqlString<64>(pName);
		m_sNick = CSqlString<64>(m_pServer->ClientName(m_ClientID));
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[512];
		// Проверка регистра
		if(m_pServer->m_aClients[m_ClientID].m_LogInstance != GetInstance()) return true;
		try
		{
			str_format(aBuf, sizeof(aBuf), "SELECT ClanID FROM %s_Clans WHERE Clanname COLLATE UTF8_GENERAL_CI = '%s';", 
			pSqlServer->GetPrefix(), m_sName.ClrStr());
			pSqlServer->executeSqlQuery(aBuf);

			if(pSqlServer->GetResults()->next())
			{
				dbg_msg("clan", "公会名称 %s 已被使用", m_sName);
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("这个名称已被使用"));
				m_pServer->AddGameServerCmd(pCmd);
				
				return true;
			}
			else
			{
				str_format(aBuf, sizeof(aBuf), 
					"INSERT INTO %s_Clans (Clanname, LeaderName, LeaderID, Money, Exp) VALUES ('%s', '%s', '%d', '0', '0');"
					, pSqlServer->GetPrefix(), m_sName.ClrStr(), m_sNick.ClrStr(), m_pServer->m_aClients[m_ClientID].m_UserID);
				pSqlServer->executeSql(aBuf);
				//dbg_msg("test","1");
				str_format(aBuf, sizeof(aBuf), 
					"SELECT * FROM %s_Clans WHERE Clanname COLLATE UTF8_GENERAL_CI = '%s';"
					, pSqlServer->GetPrefix(), m_sName.ClrStr());
				pSqlServer->executeSqlQuery(aBuf);
				//dbg_msg("test","2");
				if(pSqlServer->GetResults()->next())
				{
					int ClanID = (int)pSqlServer->GetResults()->getInt("ClanID");
					m_pServer->m_stClan[ClanID].ID = ClanID;
					m_pServer->m_stClan[ClanID].Level = (int)pSqlServer->GetResults()->getInt("Level");
					m_pServer->m_stClan[ClanID].Money = (int)pSqlServer->GetResults()->getInt("Money");
					m_pServer->m_stClan[ClanID].MaxMemberNum = (int)pSqlServer->GetResults()->getInt("MaxNum");
					m_pServer->m_stClan[ClanID].MemberNum = 1;
					
					str_copy(m_pServer->m_stClan[ClanID].Name, pSqlServer->GetResults()->getString("Clanname").c_str(), sizeof(m_pServer->m_stClan[ClanID].Name));
					str_copy(m_pServer->m_stClan[ClanID].Creator, pSqlServer->GetResults()->getString("LeaderName").c_str(), sizeof(m_pServer->m_stClan[ClanID].Creator));

					m_pServer->m_aClients[m_ClientID].AccData.m_ClanID = ClanID;
					str_copy(m_pServer->m_aClients[m_ClientID].AccData.m_Clan, pSqlServer->GetResults()->getString("Clanname").c_str(), sizeof(m_pServer->m_aClients[m_ClientID].AccData.m_Clan));
					//dbg_msg("test","3");
					//try
					//{
						//dbg_msg("test","4,%d", ClanID);
						str_format(aBuf, sizeof(aBuf), "UPDATE %s_Users SET ClanID = '%d' WHERE UserId = '%d';"
							, pSqlServer->GetPrefix()
							, ClanID, m_pServer->m_aClients[m_ClientID].m_UserID);
						pSqlServer->executeSql(aBuf);	// 麻了,鬼知道为啥这玩意执行以后,明明是成功的,读取到的值居然是失败的
						//dbg_msg("test","5");
					/*}
					catch(sql::SQLException const &e)
					{*/
						//dbg_msg("test","6");
						//dbg_msg("test","7");
						// 干脆把代码写这得了,好像没啥毛病
						//CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, ("创建成功,公会票已使用"));
						//m_pServer->AddGameServerCmd(pCmd);
						//dbg_msg("clan","%s 创建了 %s 公会", m_sNick, m_sName);

						//m_pServer->RemItem(m_ClientID, CLANTICKET, 1, -1);
						//return true;
					//}
					//dbg_msg("test","7");
					CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("创建成功,公会票已使用"));
					m_pServer->AddGameServerCmd(pCmd);
					//dbg_msg("clan","%s 创建了 %s 公会", m_sNick, m_sName);

					m_pServer->RemItem(m_ClientID, CLANTICKET, 1, -1);
					return true;
				}
			}
		}
		catch (sql::SQLException const &e)
		{
			//dbg_msg("test","0");
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("创建失败"));
			m_pServer->AddGameServerCmd(pCmd);
			dbg_msg("sql", "公会创建失败 MySQL 错误: %s", e.what());
			return false;
		}		
		
		return true;
	}
	
	virtual void CleanInstanceRef()
	{
		m_pServer->m_aClients[m_ClientID].m_LogInstance = -1;
	}
};

// Обновление коинтов
class CSqlJob_Server_UpClanCount : public CSqlJob
{
private:
	CServer* m_pServer;
	CSqlString<64> m_sName;
	int m_ClanID;
	
public:
	CSqlJob_Server_UpClanCount(CServer* pServer, int ClanID)
	{
		m_pServer = pServer;
		m_ClanID = ClanID;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[128];
		try
		{
			str_format(aBuf, sizeof(aBuf), 
				"SELECT ClanID FROM %s_Users "
				"WHERE ClanID = '%d';"
				, pSqlServer->GetPrefix()
				, m_ClanID);
			pSqlServer->executeSqlQuery(aBuf);
			
			int Num = 0;
			while(pSqlServer->GetResults()->next())
				Num++;

			m_pServer->m_stClan[m_ClanID].MemberNum = Num;
		}
		catch (sql::SQLException const &e)
		{
			dbg_msg("sql", "Error", e.what());
			return false;
		}
		return true;
	}
};

// Лист игроков
class CSqlJob_Server_Listclan : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ClientID;
	int m_ClanID;
	
public:
	CSqlJob_Server_Listclan(CServer* pServer, int ClientID, int ClanID)
	{
		m_pServer = pServer;
		m_ClientID = ClientID;
		m_ClanID = ClanID;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[128];
		if(m_pServer->m_aClients[m_ClientID].m_LogInstance != GetInstance())
			return true;
	
		try
		{
			str_format(aBuf, sizeof(aBuf), 
				"SELECT UserID, ClanID, Level, Nick, ClanAdded FROM %s_Users "
				"WHERE ClanID = '%d' ORDER BY Level DESC;", pSqlServer->GetPrefix(), m_ClanID);
			pSqlServer->executeSqlQuery(aBuf);
			
			int Num = 0;
			char aReform[MAX_NAME_LENGTH], aBufW[64], aBufCs[12];
			while(pSqlServer->GetResults()->next())
			{
				str_copy(aReform, pSqlServer->GetResults()->getString("Nick").c_str(), sizeof(aReform));
				
				int UserID = (int)pSqlServer->GetResults()->getInt("UserID");
				int Level = (int)pSqlServer->GetResults()->getInt("Level");
				int ClanAdded = (int)pSqlServer->GetResults()->getInt("ClanAdded");
				
				str_format(aBufCs, sizeof(aBufCs), "cs%d", Num);
				str_format(aBufW, sizeof(aBufW), "▹ 等级 %d:%s(ID:%d)", Level, aReform, UserID);
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_AddLocalizeVote_Language(m_ClientID, aBufCs, _(aBufW));
				m_pServer->AddGameServerCmd(pCmd);

				str_format(aBufW, sizeof(aBufW), "为公会贡献了 %d 黄金", ClanAdded);
				pCmd = new CGameServerCmd_AddLocalizeVote_Language(m_ClientID, aBufCs, _(aBufW));
				m_pServer->AddGameServerCmd(pCmd);
				
				str_copy(m_pServer->m_aClients[m_ClientID].m_aSelectPlayer[Num], aReform, sizeof(m_pServer->m_aClients[m_ClientID].m_aSelectPlayer[Num]));
				Num++;
			}
			m_pServer->m_stClan[m_ClanID].MemberNum = Num;
		}
		catch (sql::SQLException const &e)
		{
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("Error clan list say administrator."));
			m_pServer->AddGameServerCmd(pCmd);
			dbg_msg("sql", "Can't check clanname list (MySQL Error: %s)", e.what());
			
			return false;
		}
		return true;
	}
	
	virtual void CleanInstanceRef()
	{
		m_pServer->m_aClients[m_ClientID].m_LogInstance = -1;
	}
};