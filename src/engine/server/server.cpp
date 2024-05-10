/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include <base/math.h>
#include <base/system.h>
#include <base/tl/array.h>

#include <engine/config.h>
#include <engine/console.h>
#include <engine/engine.h>
#include <engine/map.h>
#include <engine/masterserver.h>
#include <engine/server.h>
#include <engine/storage.h>

#include <engine/shared/compression.h>
#include <engine/shared/config.h>
#include <engine/shared/datafile.h>
#include <engine/shared/econ.h>
#include <engine/shared/filecollection.h>
#include <engine/shared/map.h>
#include <engine/shared/netban.h>
#include <engine/shared/network.h>
#include <engine/shared/packer.h>
#include <engine/shared/protocol.h>
#include <engine/shared/snapshot.h>
#include <game/mapitems.h>
#include <game/gamecore.h>

#include <mastersrv/mastersrv.h>

#include "register.h"
#include "server.h"

#include <cstring>
#include <thread>
#include <memory>
#include <engine/server/mapconverter.h>
#include <engine/server/sql_job.h>
#include <engine/server/crypt.h>

#include <teeuniverses/components/localization.h>

#include <engine/server/sql_connector.h>

#include <engine/external/json-parser/json.h>

#if defined(CONF_FAMILY_WINDOWS)
	#define _WIN32_WINNT 0x0501
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
#endif

#include "sql_jobs.h"

static const char *StrLtrim(const char *pStr)
{
	while(*pStr && *pStr >= 0 && *pStr <= 32)
		pStr++;
	return pStr;
}

static void StrRtrim(char *pStr)
{
	int i = str_length(pStr);
	while(i >= 0)
	{
		if(pStr[i] < 0 || pStr[i] > 32)
			break;
		pStr[i] = 0;
		i--;
	}
}

CSnapIDPool::CSnapIDPool()
{
	Reset();
}

void CSnapIDPool::Reset()
{
	for(int i = 0; i < MAX_IDS; i++)
	{
		m_aIDs[i].m_Next = i+1;
		m_aIDs[i].m_State = 0;
	}

	m_aIDs[MAX_IDS-1].m_Next = -1;
	m_FirstFree = 0;
	m_FirstTimed = -1;
	m_LastTimed = -1;
	m_Usage = 0;
	m_InUsage = 0;
}


void CSnapIDPool::RemoveFirstTimeout()
{
	int NextTimed = m_aIDs[m_FirstTimed].m_Next;

	// add it to the free list
	m_aIDs[m_FirstTimed].m_Next = m_FirstFree;
	m_aIDs[m_FirstTimed].m_State = 0;
	m_FirstFree = m_FirstTimed;

	// remove it from the timed list
	m_FirstTimed = NextTimed;
	if(m_FirstTimed == -1)
		m_LastTimed = -1;

	m_Usage--;
}

int CSnapIDPool::NewID()
{
	int64 Now = time_get();

	// process timed ids
	while(m_FirstTimed != -1 && m_aIDs[m_FirstTimed].m_Timeout < Now)
		RemoveFirstTimeout();

	int ID = m_FirstFree;
	dbg_assert(ID != -1, "id error");
	if(ID == -1)
		return ID;
	m_FirstFree = m_aIDs[m_FirstFree].m_Next;
	m_aIDs[ID].m_State = 1;
	m_Usage++;
	m_InUsage++;
	return ID;
}

void CSnapIDPool::TimeoutIDs()
{
	// process timed ids
	while(m_FirstTimed != -1)
		RemoveFirstTimeout();
}

void CSnapIDPool::FreeID(int ID)
{
	if(ID < 0)
		return;
	dbg_assert(m_aIDs[ID].m_State == 1, "id is not alloced");

	m_InUsage--;
	m_aIDs[ID].m_State = 2;
	m_aIDs[ID].m_Timeout = time_get()+time_freq()*5;
	m_aIDs[ID].m_Next = -1;

	if(m_LastTimed != -1)
	{
		m_aIDs[m_LastTimed].m_Next = ID;
		m_LastTimed = ID;
	}
	else
	{
		m_FirstTimed = ID;
		m_LastTimed = ID;
	}
}

void CServerBan::InitServerBan(IConsole *pConsole, IStorage *pStorage, CServer* pServer)
{
	CNetBan::Init(pConsole, pStorage);

	m_pServer = pServer;

	// overwrites base command, todo: improve this
	Console()->Register("ban", "s<clientid> ?i<minutes> ?r<reason>", CFGFLAG_SERVER|CFGFLAG_STORE, ConBanExt, this, "Ban player with ip/client id for x minutes for any reason");
}

template<class T>
int CServerBan::BanExt(T *pBanPool, const typename T::CDataType *pData, int Seconds, const char *pReason)
{
	// validate address
	if(Server()->m_RconClientID >= 0 && Server()->m_RconClientID < MAX_PLAYERS &&
		Server()->m_aClients[Server()->m_RconClientID].m_State != CServer::CClient::STATE_EMPTY)
	{
		if(NetMatch(pData, Server()->m_NetServer.ClientAddr(Server()->m_RconClientID)))
		{
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (you can't ban yourself)");
			return -1;
		}

		for(int i = 0; i < MAX_PLAYERS; ++i)
		{
			if(i == Server()->m_RconClientID || Server()->m_aClients[i].m_State == CServer::CClient::STATE_EMPTY)
				continue;

			if(Server()->m_aClients[i].m_Authed >= Server()->m_RconAuthLevel && NetMatch(pData, Server()->m_NetServer.ClientAddr(i)))
			{
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (command denied)");
				return -1;
			}
		}
	}
	else if(Server()->m_RconClientID == IServer::RCON_CID_VOTE)
	{
		for(int i = 0; i < MAX_PLAYERS; ++i)
		{
			if(Server()->m_aClients[i].m_State == CServer::CClient::STATE_EMPTY)
				continue;

			if(Server()->m_aClients[i].m_Authed != CServer::AUTHED_NO && NetMatch(pData, Server()->m_NetServer.ClientAddr(i)))
			{
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (command denied)");
				return -1;
			}
		}
	}

	int Result = Ban(pBanPool, pData, Seconds, pReason);
	if(Result != 0)
		return Result;

	// drop banned clients

	// don't drop it like that. just kick the desired guy
	typename T::CDataType Data = *pData;
	for(int i = 0; i < MAX_PLAYERS; ++i)
	{
		if(Server()->m_aClients[i].m_State == CServer::CClient::STATE_EMPTY)
			continue;

		if(m_BanID != i) // don't drop it like that. just kick the desired guy
			continue;

		if(NetMatch(&Data, Server()->m_NetServer.ClientAddr(i)))
		{
			CNetHash NetHash(&Data);
			char aBuf[256];
			MakeBanInfo(pBanPool->Find(&Data, &NetHash), aBuf, sizeof(aBuf), MSGTYPE_PLAYER);
			Server()->m_NetServer.Drop(i, CLIENTDROPTYPE_BAN, aBuf);
		}
	}

	return Result;
}

int CServerBan::BanAddr(const NETADDR *pAddr, int Seconds, const char *pReason)
{
	return BanExt(&m_BanAddrPool, pAddr, Seconds, pReason);
}

int CServerBan::BanRange(const CNetRange *pRange, int Seconds, const char *pReason)
{
	if(pRange->IsValid())
		return BanExt(&m_BanRangePool, pRange, Seconds, pReason);

	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban failed (invalid range)");
	return -1;
}

bool CServerBan::ConBanExt(IConsole::IResult *pResult, void *pUser)
{
	CServerBan *pThis = static_cast<CServerBan*>(pUser);

	const char *pStr = pResult->GetString(0);
	int Minutes = pResult->NumArguments()>1 ? clamp(pResult->GetInteger(1), 0, 44640) : 30;
	const char *pReason = pResult->NumArguments()>2 ? pResult->GetString(2) : "No reason given";
	pThis->m_BanID = -1;

	if(StrAllnum(pStr))
	{
		int ClientID = str_toint(pStr);
		if(ClientID < 0 || ClientID >= MAX_PLAYERS || pThis->Server()->m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (invalid client id)");
		else
		{
			pThis->m_BanID = ClientID; //to ban the right guy, not his brother or so :P
			if(pThis->BanAddr(pThis->Server()->m_NetServer.ClientAddr(ClientID), Minutes*60, pReason) != 0) //error occured
				pThis->Server()->Kick(ClientID, pReason);
		}
	}
	else
		ConBan(pResult, pUser);
	
	return true;
}

void CServer::CClient::Reset(bool ResetScore)
{
	// reset input
	for(int i = 0; i < 200; i++)
		m_aInputs[i].m_GameTick = -1;
	
	m_CurrentInput = 0;
	mem_zero(&m_LatestInput, sizeof(m_LatestInput));

	m_Snapshots.PurgeAll();
	m_LastAckedSnapshot = -1;
	m_LastInputTick = -1;
	m_SnapRate = CClient::SNAPRATE_INIT;
	m_NextMapChunk = 0;
	
	if(ResetScore)
	{
		m_WaitingTime = 0;
		m_LogInstance = -1;
		mem_zero(&AccData, sizeof(AccData));
		mem_zero(&AccUpgrade, sizeof(AccUpgrade));
		m_UserID = -1;

		m_AntiPing = 0;
		str_copy(m_aLanguage, "en", sizeof(m_aLanguage));

		m_WaitingTime = 0;
	}
}

CServer::CServer()
{
	m_TickSpeed = SERVER_TICK_SPEED;

	m_CurrentGameTick = 0;
	m_RunServer = 1;

	m_MapReload = 0;

	m_RconClientID = IServer::RCON_CID_SERV;
	m_RconAuthLevel = AUTHED_ADMIN;
 
	for (int i = 0; i < MAX_SQLSERVERS; i++)
	{
		m_apSqlReadServers[i] = 0;
		m_apSqlWriteServers[i] = 0;
	}

	CSqlConnector::SetReadServers(m_apSqlReadServers);
	CSqlConnector::SetWriteServers(m_apSqlWriteServers);
	
	m_GameServerCmdLock = lock_create();
	
	Init();
}

CServer::~CServer()
{
	lock_destroy(m_GameServerCmdLock);
}

int CServer::TrySetClientName(int ClientID, const char *pName)
{
	char aTrimmedName[64];
	char aTrimmedName2[64];

	// trim the name
	str_copy(aTrimmedName, StrLtrim(pName), sizeof(aTrimmedName));
	StrRtrim(aTrimmedName);

	// check for empty names
	if(!aTrimmedName[0])
		return -1;
		
	// name not allowed to start with '/'
	if(aTrimmedName[0] == '/')
		return -1;

	pName = aTrimmedName;

	// make sure that two clients doesn't have the same name
	for(int i = 0; i < MAX_PLAYERS; i++)
	{
		if(i != ClientID && m_aClients[i].m_State >= CClient::STATE_READY)
		{
			str_copy(aTrimmedName2, ClientName(i), sizeof(aTrimmedName2));
			StrRtrim(aTrimmedName2);
			
			if(str_comp(pName, aTrimmedName2) == 0)
				return -1;
		}
	}

	// check if new and old name are the same
	if(m_aClients[ClientID].m_aName[0] && str_comp(m_aClients[ClientID].m_aName, pName) == 0)
		return 0;
	
	// set the client name
	str_copy(m_aClients[ClientID].m_aName, pName, MAX_NAME_LENGTH);
	return 0;
}

void CServer::SetClientName(int ClientID, const char *pName)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State < CClient::STATE_READY)
		return;

	if(!pName)
		return;

	char aCleanName[MAX_NAME_LENGTH];
	str_copy(aCleanName, pName, sizeof(aCleanName));

	if(TrySetClientName(ClientID, aCleanName))
	{
		// auto rename
		for(int i = 1;; i++)
		{
			char aNameTry[MAX_NAME_LENGTH];
			str_format(aNameTry, sizeof(aCleanName), "(%d)%s", i, aCleanName);
			if(TrySetClientName(ClientID, aNameTry) == 0)
				break;
		}
	}
}

void CServer::SetClientClan(int ClientID, const char *pClan)
{
	if(ClientID < 0 || ClientID >= MAX_PLAYERS || m_aClients[ClientID].m_State < CClient::STATE_READY || !pClan)
		return;

	str_copy(m_aClients[ClientID].AccData.m_Clan, pClan, MAX_CLAN_LENGTH);
}

void CServer::SetClientCountry(int ClientID, int Country)
{
	if(ClientID < 0 || ClientID >= MAX_PLAYERS || m_aClients[ClientID].m_State < CClient::STATE_READY)
		return;

	m_aClients[ClientID].m_Country = Country;
}

void CServer::Kick(int ClientID, const char *pReason)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State == CClient::STATE_EMPTY)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "invalid client id to kick");
		return;
	}
	else if(m_RconClientID == ClientID)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "you can't kick yourself");
 		return;
	}
	else if(m_aClients[ClientID].m_Authed > m_RconAuthLevel)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "kick command denied");
 		return;
	}

	m_NetServer.Drop(ClientID, CLIENTDROPTYPE_KICK, pReason);
}

int64 CServer::TickStartTime(int Tick)
{
	return m_GameStartTime + (time_freq()*Tick)/SERVER_TICK_SPEED;
}

int CServer::Init()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		m_aClients[i].m_State = CClient::STATE_EMPTY;
		m_aClients[i].m_aName[0] = 0;
		m_aClients[i].m_aClan[0] = 0;
		m_aClients[i].m_CustClt = 0;
		m_aClients[i].m_Country = -1;
		m_aClients[i].m_Snapshots.Init();
		m_aClients[i].m_WaitingTime = 0;
        m_aClients[i].m_Latency = 0;
	}

	m_CurrentGameTick = 0;
	memset(m_aPrevStates, CClient::STATE_EMPTY, MAX_CLIENTS * sizeof(int));

	return 0;
}

void CServer::SetRconCID(int ClientID)
{
	m_RconClientID = ClientID;
}

bool CServer::IsAuthed(int ClientID)
{
	return m_aClients[ClientID].m_Authed;
}

int CServer::GetClientInfo(int ClientID, CClientInfo *pInfo)
{
	dbg_assert(ClientID >= 0 && ClientID < MAX_CLIENTS, "client_id is not valid");
	dbg_assert(pInfo != 0, "info can not be null");

	if(m_aClients[ClientID].m_State == CClient::STATE_INGAME)
	{
		pInfo->m_pName = ClientName(ClientID);
		pInfo->m_Latency = m_aClients[ClientID].m_Latency;
		pInfo->m_CustClt = m_aClients[ClientID].m_CustClt;
		return 1;
	}
	return 0;
}

void CServer::GetClientAddr(int ClientID, char *pAddrStr, int Size)
{
	if(ClientID >= 0 && ClientID < MAX_PLAYERS && m_aClients[ClientID].m_State == CClient::STATE_INGAME)
		net_addr_str(m_NetServer.ClientAddr(ClientID), pAddrStr, Size, false);
}

const char *CServer::ClientName(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
		return "(invalid)";
		
	if(m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME)
	{
		if(ClientID < MAX_CLIENTS)
		{			
			remove_spaces(m_aClients[ClientID].m_aName);
			return m_aClients[ClientID].m_aName;
		}
		else
			return "(error)";
	}
	else
		return "(connecting)";

}

const char *CServer::ClientUsername(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_PLAYERS || m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY || !IsClientLogged(ClientID))
		return "(invalid)";
		
	if(m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME)
		return m_aClients[ClientID].m_aUsername;
	else
		return "(unfobie)";

}

const char *CServer::ClientClan(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_PLAYERS || m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
		return "";
	if(m_aClients[ClientID].AccData.m_ClanID > 0)
		return m_stClan[m_aClients[ClientID].AccData.m_ClanID].Name;
	if(m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME && !IsClientLogged(ClientID))
		return m_aClients[ClientID].AccData.m_Clan;
	else
		return "NOPE";
}

const char *CServer::GetSelectName(int ClientID, int SelID)
{
	if(ClientID < 0 || ClientID >= MAX_PLAYERS || m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
		return "";
	if(m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME)
		return m_aClients[ClientID].m_aSelectPlayer[SelID];
	else
		return "";
}

int CServer::ClientCountry(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_PLAYERS || m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
		return -1;
	if(m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME)
		return m_aClients[ClientID].m_Country;
	else
		return -1;
}

bool CServer::ClientIngame(int ClientID)
{
	return ClientID >= 0 && ClientID < MAX_CLIENTS && m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME;
}

int CServer::MaxClients() const
{
	return m_NetServer.MaxClients();
}

int CServer::SendMsg(CMsgPacker *pMsg, int Flags, int ClientID, int MapID)
{
	return SendMsgEx(pMsg, Flags, ClientID, false, MapID);
}

int CServer::SendMsgEx(CMsgPacker *pMsg, int Flags, int ClientID, bool System, int MapID)
{
	CNetChunk Packet;
	if(!pMsg)
		return -1;

	if (ClientID != -1 && (ClientID < 0 || ClientID >= MAX_PLAYERS || m_aClients[ClientID].m_State == CClient::STATE_EMPTY || m_aClients[ClientID].m_Quitting))
		return 0;

	mem_zero(&Packet, sizeof(CNetChunk));

	Packet.m_ClientID = ClientID;
	Packet.m_pData = pMsg->Data();
	Packet.m_DataSize = pMsg->Size();

	// HACK: modify the message id in the packet and store the system flag
	*((unsigned char*)Packet.m_pData) <<= 1;
	if(System)
		*((unsigned char*)Packet.m_pData) |= 1;

	if(Flags&MSGFLAG_VITAL)
		Packet.m_Flags |= NETSENDFLAG_VITAL;
	if(Flags&MSGFLAG_FLUSH)
		Packet.m_Flags |= NETSENDFLAG_FLUSH;

	if(!(Flags&MSGFLAG_NOSEND))
	{
		if(ClientID == -1)
		{
			// broadcast
			for(int i = 0; i < MAX_PLAYERS; i++)
				if (m_aClients[i].m_State == CClient::STATE_INGAME && !m_aClients[i].m_Quitting)
				{
					if (MapID != -1)
					{
						if (m_aClients[i].m_MapID == MapID)
						{
							Packet.m_ClientID = i;
							m_NetServer.Send(&Packet);
						}
						continue;
					}

					Packet.m_ClientID = i;
					m_NetServer.Send(&Packet);
				}
		}
		else
			m_NetServer.Send(&Packet);
	}
	return 0;
}

void CServer::DoSnapshot(int MapID)
{
	GameServer(MapID)->OnPreSnap();

	// create snapshots for all clients
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		// client must be ingame to recive snapshots
		if(m_aClients[i].m_State != CClient::STATE_INGAME || m_aClients[i].m_MapID != MapID)
			continue;

		// this client is trying to recover, don't spam snapshots
		if(m_aClients[i].m_SnapRate == CClient::SNAPRATE_RECOVER && (Tick()%50) != 0)
			continue;

		// this client is trying to recover, don't spam snapshots
		if(m_aClients[i].m_SnapRate == CClient::SNAPRATE_INIT && (Tick()%10) != 0)
			continue;

		{
			m_SnapshotBuilder.Init();

			GameServer(MapID)->OnSnap(i);

			char aData[CSnapshot::MAX_SIZE] = { 0 };
			CSnapshot *pData = (CSnapshot*)aData;	// Fix compiler warning for strict-aliasing
			char aDeltaData[CSnapshot::MAX_SIZE] = { 0 };
			char aCompData[CSnapshot::MAX_SIZE] = { 0 };
			int SnapshotSize;
			int Crc;
			static CSnapshot EmptySnap;
			CSnapshot *pDeltashot = &EmptySnap;
			int DeltashotSize;
			int DeltaTick = -1;
			int DeltaSize;

			// finish snapshot
			SnapshotSize = m_SnapshotBuilder.Finish(pData);
			Crc = pData->Crc();

			// remove old snapshos
			// keep 3 seconds worth of snapshots
			m_aClients[i].m_Snapshots.PurgeUntil(m_CurrentGameTick-SERVER_TICK_SPEED*3);

			// save it the snapshot
			m_aClients[i].m_Snapshots.Add(m_CurrentGameTick, time_get(), SnapshotSize, pData, 0);

			// find snapshot that we can preform delta against
			EmptySnap.Clear();

			{
				DeltashotSize = m_aClients[i].m_Snapshots.Get(m_aClients[i].m_LastAckedSnapshot, 0, &pDeltashot, 0);
				if(DeltashotSize >= 0)
					DeltaTick = m_aClients[i].m_LastAckedSnapshot;
				else
				{
					// no acked package found, force client to recover rate
					if(m_aClients[i].m_SnapRate == CClient::SNAPRATE_FULL)
						m_aClients[i].m_SnapRate = CClient::SNAPRATE_RECOVER;
				}
			}

			// create delta
			DeltaSize = m_SnapshotDelta.CreateDelta(pDeltashot, pData, aDeltaData);

			if(DeltaSize)
			{
				// compress it
				int SnapshotSize;
				const int MaxSize = MAX_SNAPSHOT_PACKSIZE;
				int NumPackets;

				SnapshotSize = CVariableInt::Compress(aDeltaData, DeltaSize, aCompData);
				NumPackets = (SnapshotSize+MaxSize-1)/MaxSize;

				for(int n = 0, Left = SnapshotSize; Left > 0; n++)
				{
					int Chunk = Left < MaxSize ? Left : MaxSize;
					Left -= Chunk;

					if(NumPackets == 1)
					{
						CMsgPacker Msg(NETMSG_SNAPSINGLE);
						Msg.AddInt(m_CurrentGameTick);
						Msg.AddInt(m_CurrentGameTick-DeltaTick);
						Msg.AddInt(Crc);
						Msg.AddInt(Chunk);
						Msg.AddRaw(&aCompData[n*MaxSize], Chunk);
						SendMsgEx(&Msg, MSGFLAG_FLUSH, i, true, MapID);
					}
					else
					{
						CMsgPacker Msg(NETMSG_SNAP);
						Msg.AddInt(m_CurrentGameTick);
						Msg.AddInt(m_CurrentGameTick-DeltaTick);
						Msg.AddInt(NumPackets);
						Msg.AddInt(n);
						Msg.AddInt(Crc);
						Msg.AddInt(Chunk);
						Msg.AddRaw(&aCompData[n*MaxSize], Chunk);
						SendMsgEx(&Msg, MSGFLAG_FLUSH, i, true, MapID);
					}
				}
			}
			else
			{
				CMsgPacker Msg(NETMSG_SNAPEMPTY);
				Msg.AddInt(m_CurrentGameTick);
				Msg.AddInt(m_CurrentGameTick-DeltaTick);
				SendMsgEx(&Msg, MSGFLAG_FLUSH, i, true, MapID);
			}
		}
	}

	GameServer(MapID)->OnPostSnap();
}

int CServer::NewClientCallback(int ClientID, void *pUser)
{
	CServer *pThis = (CServer *)pUser;
	pThis->m_aClients[ClientID].m_State = CClient::STATE_AUTH;
	pThis->m_aClients[ClientID].m_aName[0] = 0;
	pThis->m_aClients[ClientID].m_aClan[0] = 0;
	pThis->m_aClients[ClientID].m_Country = -1;
	pThis->m_aClients[ClientID].m_Authed = AUTHED_NO;
	pThis->m_aClients[ClientID].m_AuthTries = 0;
	pThis->m_aClients[ClientID].m_pRconCmdToSend = 0;
	pThis->m_aClients[ClientID].m_CustClt = 0;
	pThis->m_aClients[ClientID].m_MapID = DEFAULT_MAP_ID;
	pThis->m_aClients[ClientID].m_OldMapID = DEFAULT_MAP_ID;
	pThis->m_aClients[ClientID].m_IsChangeMap = false;
	pThis->m_aClients[ClientID].Reset();
	
	return 0;
}

int CServer::ClientRejoinCallback(int ClientID, void *pUser)
{
	CServer *pThis = (CServer *)pUser;

	pThis->m_aClients[ClientID].m_Authed = AUTHED_NO;
	pThis->m_aClients[ClientID].m_pRconCmdToSend = 0;

	pThis->m_aClients[ClientID].Reset(!pThis->m_aClients[ClientID].m_IsChangeMap);

	pThis->SendMap(ClientID, pThis->m_aClients[ClientID].m_MapID);

	return 0;
}

int CServer::DelClientCallback(int ClientID, int Type, const char *pReason, void *pUser)
{
	CServer *pThis = (CServer *)pUser;

	// notify the mod about the drop
	if (pThis->m_aClients[ClientID].m_State >= CClient::STATE_READY || pThis->GetClientChangeMap(ClientID))
	{
		pThis->m_aClients[ClientID].m_Quitting = true;

		for (int i = 0; i < pThis->m_NumGameServer; i++)
			pThis->GameServer(i)->OnClientDrop(ClientID, Type, pReason);
	}

	char aAddrStr[NETADDR_MAXSTRSIZE];
	net_addr_str(pThis->m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), true);

	pThis->m_aClients[ClientID].m_State = CClient::STATE_EMPTY;
	pThis->m_aClients[ClientID].m_aName[0] = 0;
	pThis->m_aClients[ClientID].m_aClan[0] = 0;
	pThis->m_aClients[ClientID].m_Country = -1;
	pThis->m_aClients[ClientID].m_Authed = AUTHED_NO;
	pThis->m_aClients[ClientID].m_AuthTries = 0;
	pThis->m_aClients[ClientID].m_pRconCmdToSend = 0;
	pThis->m_aClients[ClientID].m_WaitingTime = 0;

	pThis->m_aClients[ClientID].m_UserID = -1;
	pThis->m_aClients[ClientID].AccData.m_ClanID = -1;
	pThis->m_aClients[ClientID].AccData.m_Level = -1;
	pThis->m_aClients[ClientID].AccData.m_Jail = false;
	pThis->m_aClients[ClientID].AccData.m_Rel = -1;
	pThis->m_aClients[ClientID].AccData.m_Exp = -1;
	pThis->m_aClients[ClientID].AccData.m_Donate = -1;
	pThis->m_aClients[ClientID].AccData.m_Class = -1;
	pThis->m_aClients[ClientID].AccData.m_Quest = -1;
	pThis->m_aClients[ClientID].AccData.m_Kill = -1;
	pThis->m_aClients[ClientID].AccData.m_WinArea = -1;
	pThis->m_aClients[ClientID].AccData.m_ClanAdded = -1;
	pThis->m_aClients[ClientID].AccData.m_IsJailed = false;
	pThis->m_aClients[ClientID].AccData.m_JailLength = 0;
	pThis->m_aClients[ClientID].AccData.m_SummerHealingTimes = 0;
	pThis->m_aClients[ClientID].AccUpgrade.m_Health = 0;
	pThis->m_aClients[ClientID].AccUpgrade.m_Speed = 0;
	pThis->m_aClients[ClientID].AccUpgrade.m_Damage = 0;
	pThis->m_aClients[ClientID].AccUpgrade.m_Ammo = 0;
	pThis->m_aClients[ClientID].AccUpgrade.m_AmmoRegen = 0;
	pThis->m_aClients[ClientID].AccUpgrade.m_Spray = 0;
	pThis->m_aClients[ClientID].AccUpgrade.m_Mana = 0;
	pThis->m_aClients[ClientID].AccUpgrade.m_HPRegen = 0;
	pThis->m_aClients[ClientID].AccUpgrade.m_HammerRange = 0;
	pThis->m_aClients[ClientID].AccUpgrade.m_Pasive2 = 0;
	pThis->m_aClients[ClientID].AccUpgrade.m_Upgrade = 0;
	pThis->m_aClients[ClientID].AccUpgrade.m_SkillPoint = 0;

	for(int i = 0; i < 7; i++)
		pThis->m_aClients[ClientID].m_ItemCount[i] = 0;

	for(int i = 0; i < 20; i++)
	{
		pThis->m_aClients[ClientID].m_ItemNumReward[i] = -1;
		pThis->m_aClients[ClientID].m_ItemReward[i] = -1;
	}

	for(int i = 0; i < MAX_ITEM; ++i)
	{
		pThis->m_stInv[ClientID][i].i_count = 0;
		pThis->m_stInv[ClientID][i].i_settings = 0;
		pThis->m_stInv[ClientID][i].i_nlevel = 0;
		pThis->m_stInv[ClientID][i].i_nprice = 0;
		pThis->m_stInv[ClientID][i].i_enchant = 0;
		pThis->m_stInv[ClientID][i].i_id = 0;
	}

	pThis->m_aClients[ClientID].m_LogInstance = -1;
	pThis->m_aClients[ClientID].m_Snapshots.PurgeAll();
	pThis->m_aClients[ClientID].m_MapID = DEFAULT_MAP_ID;
	pThis->m_aClients[ClientID].m_OldMapID = DEFAULT_MAP_ID;
	pThis->m_aClients[ClientID].m_IsChangeMap = false;
	pThis->m_aClients[ClientID].m_Quitting = false;

	return 0;
	
}

void CServer::Logout(int ClientID)
{
	m_aClients[ClientID].m_UserID = -1;
}

void CServer::SendMap(int ClientID, int MapID)
{
	CMsgPacker Msg(NETMSG_MAP_CHANGE);
	Msg.AddString(GetMapName(), 0);
	Msg.AddInt(m_vMapData[MapID].m_CurrentMapCrc);
	Msg.AddInt(m_vMapData[MapID].m_CurrentMapSize);
	SendMsgEx(&Msg, MSGFLAG_VITAL|MSGFLAG_FLUSH, ClientID, true, MapID);

	m_aClients[ClientID].m_NextMapChunk = 0;
}

void CServer::SendMapData(int ClientID, int Chunk, int MapID)
{
 	unsigned int ChunkSize = 1024-128;
 	unsigned int Offset = Chunk * ChunkSize;
 	int Last = 0;
 
 	// drop faulty map data requests
 	if(Chunk < 0 || Offset > m_vMapData[MapID].m_CurrentMapSize)
 		return;
 
 	if(Offset+ChunkSize >= m_vMapData[MapID].m_CurrentMapSize)
 	{
 		ChunkSize = m_vMapData[MapID].m_CurrentMapSize-Offset;
 		Last = 1;
 	}
 
 	CMsgPacker Msg(NETMSG_MAP_DATA);
 	Msg.AddInt(Last);
 	Msg.AddInt(m_vMapData[MapID].m_CurrentMapCrc);
 	Msg.AddInt(Chunk);
 	Msg.AddInt(ChunkSize);
 	Msg.AddRaw(&m_vMapData[MapID].m_pCurrentMapData[Offset], ChunkSize);
 	SendMsgEx(&Msg, MSGFLAG_VITAL|MSGFLAG_FLUSH, ClientID, true, MapID);
 
 	if(g_Config.m_Debug)
 	{
 		char aBuf[256];
 		str_format(aBuf, sizeof(aBuf), "sending chunk %d with size %d", Chunk, ChunkSize);
 		Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
 	}
 }

void CServer::SendConnectionReady(int ClientID)
{
	CMsgPacker Msg(NETMSG_CON_READY);
	SendMsgEx(&Msg, MSGFLAG_VITAL|MSGFLAG_FLUSH, ClientID, true, -1);
}

void CServer::SendRconLine(int ClientID, const char *pLine)
{
	CMsgPacker Msg(NETMSG_RCON_LINE);
	Msg.AddString(pLine, 512);
	SendMsgEx(&Msg, MSGFLAG_VITAL, ClientID, true, -1);
}

void CServer::SendRconLineAuthed(const char *pLine, void *pUser)
{
	CServer *pThis = (CServer *)pUser;
	static volatile int ReentryGuard = 0;
	int i;

	if(ReentryGuard) return;
	ReentryGuard++;

	for(i = 0; i < MAX_PLAYERS; i++)
	{
		if(pThis->m_aClients[i].m_State != CClient::STATE_EMPTY && pThis->m_aClients[i].m_Authed >= pThis->m_RconAuthLevel)
			pThis->SendRconLine(i, pLine);
	}

	ReentryGuard--;
}

void CServer::SendRconCmdAdd(const IConsole::CCommandInfo *pCommandInfo, int ClientID)
{
	CMsgPacker Msg(NETMSG_RCON_CMD_ADD);
	Msg.AddString(pCommandInfo->m_pName, IConsole::TEMPCMD_NAME_LENGTH);
	Msg.AddString(pCommandInfo->m_pHelp, IConsole::TEMPCMD_HELP_LENGTH);
	Msg.AddString(pCommandInfo->m_pParams, IConsole::TEMPCMD_PARAMS_LENGTH);
	SendMsgEx(&Msg, MSGFLAG_VITAL, ClientID, true, -1);
}

void CServer::SendRconCmdRem(const IConsole::CCommandInfo *pCommandInfo, int ClientID)
{
	CMsgPacker Msg(NETMSG_RCON_CMD_REM);
	Msg.AddString(pCommandInfo->m_pName, 256);
	SendMsgEx(&Msg, MSGFLAG_VITAL, ClientID, true, -1);
}

void CServer::UpdateClientRconCommands()
{
	int ClientID = Tick() % MAX_CLIENTS;

	if(m_aClients[ClientID].m_State != CClient::STATE_EMPTY && m_aClients[ClientID].m_Authed)
	{
		int ConsoleAccessLevel = m_aClients[ClientID].m_Authed == AUTHED_ADMIN ? IConsole::ACCESS_LEVEL_ADMIN : IConsole::ACCESS_LEVEL_MOD;
		for(int i = 0; i < MAX_RCONCMD_SEND && m_aClients[ClientID].m_pRconCmdToSend; ++i)
		{
			SendRconCmdAdd(m_aClients[ClientID].m_pRconCmdToSend, ClientID);
			m_aClients[ClientID].m_pRconCmdToSend = m_aClients[ClientID].m_pRconCmdToSend->NextCommandInfo(ConsoleAccessLevel, CFGFLAG_SERVER);
		}
	}
}

void CServer::ProcessClientPacket(CNetChunk *pPacket)
{
	int ClientID = pPacket->m_ClientID;
	int MapID = m_aClients[ClientID].m_MapID;
	CUnpacker Unpacker;
	Unpacker.Reset(pPacket->m_pData, pPacket->m_DataSize);

	// unpack msgid and system flag
	int Msg = Unpacker.GetInt();
	int Sys = Msg&1;
	Msg >>= 1;

	if(Unpacker.Error())
		return;
	
	if(Sys)
	{
		// system message
		if(Msg == NETMSG_INFO)
		{
			if((pPacket->m_Flags&NET_CHUNKFLAG_VITAL) != 0 && m_aClients[ClientID].m_State == CClient::STATE_AUTH)
			{
				const char *pVersion = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				if(!str_utf8_check(pVersion))
					return;
				if(str_comp(pVersion, "0.6 626fce9a778df4d4") != 0 && str_comp(pVersion, GameServer()->NetVersion()) != 0)
				{
					m_NetServer.Drop(ClientID, CLIENTDROPTYPE_WRONG_VERSION, "Wrong version.");
					return;
				}

				const char *pPassword = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				if(!str_utf8_check(pPassword))
					return;
				if(g_Config.m_Password[0] != 0 && str_comp(g_Config.m_Password, pPassword) != 0)
				{
					m_NetServer.Drop(ClientID, CLIENTDROPTYPE_WRONG_PASSWORD, "Wrong password");
					return;
				}
				m_aClients[ClientID].m_State = CClient::STATE_CONNECTING;
				SendMap(ClientID, MapID);
			}
		}
		else if(Msg == NETMSG_REQUEST_MAP_DATA)
		{
			if((pPacket->m_Flags&NET_CHUNKFLAG_VITAL) == 0 || m_aClients[ClientID].m_State < CClient::STATE_CONNECTING)
				return;

			int Chunk = Unpacker.GetInt();
			if(Chunk != m_aClients[ClientID].m_NextMapChunk || !g_Config.m_InfFastDownload)
			{
				SendMapData(ClientID, Chunk, MapID);
				return;
			}

			if(Chunk == 0)
			{
				for(int i = 0; i < g_Config.m_InfMapWindow; i++)
				{
					SendMapData(ClientID, i, MapID);
				}
			}
			SendMapData(ClientID, g_Config.m_InfMapWindow + m_aClients[ClientID].m_NextMapChunk, MapID);
			m_aClients[ClientID].m_NextMapChunk++;
		}
		else if(Msg == NETMSG_READY)
		{
			if ((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && m_aClients[ClientID].m_State == CClient::STATE_CONNECTING)
			{
				if (!m_aClients[ClientID].m_IsChangeMap)
				{
					char aAddrStr[NETADDR_MAXSTRSIZE];
					net_addr_str(m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), true);

					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "player is ready. ClientID=%d addr=%s", ClientID, aAddrStr);
					Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBuf);

					const int MapID = m_aClients[ClientID].m_MapID;
					GameServer(MapID)->PrepareClientChangeMap(ClientID);
				}
				GameServer(MapID)->OnClientConnected(ClientID);

				m_aClients[ClientID].m_State = CClient::STATE_READY;
				SendConnectionReady(ClientID);
			}
		}
		else if(Msg == NETMSG_ENTERGAME)
		{
			const int MapID = m_aClients[ClientID].m_MapID;

			if ((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && m_aClients[ClientID].m_State == CClient::STATE_READY && GameServer(MapID)->IsClientReady(ClientID))
			{
				m_aClients[ClientID].m_State = CClient::STATE_INGAME;
				GameServer(MapID)->OnClientEnter(ClientID);
			}
		}
		else if(Msg == NETMSG_INPUT)
		{
			CClient::CInput *pInput;
			int64 TagTime;

			m_aClients[ClientID].m_LastAckedSnapshot = Unpacker.GetInt();
			int IntendedTick = Unpacker.GetInt();
			int Size = Unpacker.GetInt();

			// check for errors
			if(Unpacker.Error() || Size/4 > MAX_INPUT_SIZE)
				return;

			if(m_aClients[ClientID].m_LastAckedSnapshot > 0)
				m_aClients[ClientID].m_SnapRate = CClient::SNAPRATE_FULL;

			if(m_aClients[ClientID].m_Snapshots.Get(m_aClients[ClientID].m_LastAckedSnapshot, &TagTime, 0, 0) >= 0)
				m_aClients[ClientID].m_Latency = (int)(((time_get()-TagTime)*1000)/time_freq());

			// add message to report the input timing
			// skip packets that are old
			if(IntendedTick > m_aClients[ClientID].m_LastInputTick)
			{
				int TimeLeft = ((TickStartTime(IntendedTick)-time_get())*1000) / time_freq();

				CMsgPacker Msg(NETMSG_INPUTTIMING);
				Msg.AddInt(IntendedTick);
				Msg.AddInt(TimeLeft);
				SendMsgEx(&Msg, 0, ClientID, true, m_aClients[ClientID].m_MapID);
			}
			m_aClients[ClientID].m_LastInputTick = IntendedTick;

			pInput = &m_aClients[ClientID].m_aInputs[m_aClients[ClientID].m_CurrentInput];

			if(IntendedTick <= Tick())
				IntendedTick = Tick()+1;

			pInput->m_GameTick = IntendedTick;

			for(int i = 0; i < Size/4; i++)
				pInput->m_aData[i] = Unpacker.GetInt();

			mem_copy(m_aClients[ClientID].m_LatestInput.m_aData, pInput->m_aData, MAX_INPUT_SIZE*sizeof(int));

			m_aClients[ClientID].m_CurrentInput++;
			m_aClients[ClientID].m_CurrentInput %= 200;

			// call the mod with the fresh input data
			if(m_aClients[ClientID].m_State == CClient::STATE_INGAME)
				GameServer(m_aClients[ClientID].m_MapID)->OnClientDirectInput(ClientID, m_aClients[ClientID].m_LatestInput.m_aData);
		}
		else if(Msg == NETMSG_RCON_CMD)
		{
			const char *pCmd = Unpacker.GetString();
			if(Unpacker.Error() == 0 && !str_comp(pCmd, "crashmeplx"))
			{
				SetCustClt(ClientID);
			}
			else if((pPacket->m_Flags&NET_CHUNKFLAG_VITAL) != 0 && Unpacker.Error() == 0 && m_aClients[ClientID].m_Authed)
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "ClientID=%d rcon='%s'", ClientID, pCmd);
				Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBuf);
				m_RconClientID = ClientID;
				m_RconAuthLevel = m_aClients[ClientID].m_Authed;
				switch(m_aClients[ClientID].m_Authed)
				{
					case AUTHED_ADMIN:
						Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_ADMIN);
						break;
					case AUTHED_MOD:
						Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_MOD);
						break;
					default:
						Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_USER);
				}	
				Console()->ExecuteLineFlag(pCmd, ClientID, false, CFGFLAG_SERVER);
				Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_ADMIN);
				m_RconClientID = IServer::RCON_CID_SERV;
				m_RconAuthLevel = AUTHED_ADMIN;
			}
		}
		else if(Msg == NETMSG_RCON_AUTH)
		{
			const char *pPw;
			Unpacker.GetString(); // login name, not used
			pPw = Unpacker.GetString(CUnpacker::SANITIZE_CC);

			if((pPacket->m_Flags&NET_CHUNKFLAG_VITAL) != 0 && Unpacker.Error() == 0)
			{
				if(g_Config.m_SvRconPassword[0] == 0 && g_Config.m_SvRconModPassword[0] == 0)
				{
					SendRconLine(ClientID, "No rcon password set on server. Set sv_rcon_password and/or sv_rcon_mod_password to enable the remote console.");
				}
				else if(g_Config.m_SvRconPassword[0] && str_comp(pPw, g_Config.m_SvRconPassword) == 0)
				{
					CMsgPacker Msg(NETMSG_RCON_AUTH_STATUS);
					Msg.AddInt(1);	//authed
					Msg.AddInt(1);	//cmdlist
					SendMsgEx(&Msg, MSGFLAG_VITAL, ClientID, true, -1);

					m_aClients[ClientID].m_Authed = AUTHED_ADMIN;
					GameServer(m_aClients[ClientID].m_MapID)->OnSetAuthed(ClientID, m_aClients[ClientID].m_Authed);
					int SendRconCmds = Unpacker.GetInt();
					if(Unpacker.Error() == 0 && SendRconCmds)
						m_aClients[ClientID].m_pRconCmdToSend = Console()->FirstCommandInfo(IConsole::ACCESS_LEVEL_ADMIN, CFGFLAG_SERVER);
					SendRconLine(ClientID, "Admin authentication successful. Full remote console access granted.");
					
					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "ClientID=%d authed (admin)", ClientID);
					Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
				}
				else if(g_Config.m_SvRconModPassword[0] && str_comp(pPw, g_Config.m_SvRconModPassword) == 0)
				{
					CMsgPacker Msg(NETMSG_RCON_AUTH_STATUS);
					Msg.AddInt(1);	//authed
					Msg.AddInt(1);	//cmdlist
					SendMsgEx(&Msg, MSGFLAG_VITAL, ClientID, true, -1);

					m_aClients[ClientID].m_Authed = AUTHED_MOD;
					int SendRconCmds = Unpacker.GetInt();
					if(Unpacker.Error() == 0 && SendRconCmds)
						m_aClients[ClientID].m_pRconCmdToSend = Console()->FirstCommandInfo(IConsole::ACCESS_LEVEL_MOD, CFGFLAG_SERVER);
					SendRconLine(ClientID, "Moderator authentication successful. Limited remote console access granted.");
					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "ClientID=%d authed (moderator)", ClientID);
					Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
				}
				else if(g_Config.m_SvWarningPassword[0] && str_comp(pPw, g_Config.m_SvWarningPassword) == 0)
				{
					SendRconLine(ClientID, "? Fuck off.");

					char aAddrStr[64];
					char aBuf[128];
					net_addr_str(m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), false);
					str_format(aBuf, sizeof(aBuf), "!警告! 陷阱被触发! 用户ID: %d, 游戏名:%s, IP:%s", m_aClients[ClientID].m_UserID, ClientName(ClientID), aAddrStr);
					LogWarning(aBuf);
					
					Ban(ClientID, -1, "尝试黑入服务器");
				}
				else if(g_Config.m_SvRconMaxTries)
				{
					m_aClients[ClientID].m_AuthTries++;
					char aBuf[128];
					str_format(aBuf, sizeof(aBuf), "Wrong password %d/%d.", m_aClients[ClientID].m_AuthTries, g_Config.m_SvRconMaxTries);
					SendRconLine(ClientID, aBuf);
					
					char aAddrStr[64];
					net_addr_str(m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), false);
					str_format(aBuf, sizeof(aBuf), "!警告! !错误的密码! 用户ID: %d, 游戏名:%s, IP:%s", m_aClients[ClientID].m_UserID, ClientName(ClientID), aAddrStr);
					LogWarning(aBuf);

					if(m_aClients[ClientID].m_AuthTries >= g_Config.m_SvRconMaxTries)
					{
						if(!g_Config.m_SvRconBantime)
							m_NetServer.Drop(ClientID, CLIENTDROPTYPE_KICK, "Too many remote console authentication tries");
						else
							m_ServerBan.BanAddr(m_NetServer.ClientAddr(ClientID), g_Config.m_SvRconBantime*60, "Too many remote console authentication tries");
					}
				}
				else
				{
					SendRconLine(ClientID, "Wrong password.");
				}
			}
		}
		else if(Msg == NETMSG_PING)
		{
			CMsgPacker Msg(NETMSG_PING_REPLY);
			SendMsgEx(&Msg, 0, ClientID, true, m_aClients[ClientID].m_MapID);
		}
		else
		{
			if(g_Config.m_Debug)
			{
				char aHex[] = "0123456789ABCDEF";
				char aBuf[512];

				for(int b = 0; b < pPacket->m_DataSize && b < 32; b++)
				{
					aBuf[b*3] = aHex[((const unsigned char *)pPacket->m_pData)[b]>>4];
					aBuf[b*3+1] = aHex[((const unsigned char *)pPacket->m_pData)[b]&0xf];
					aBuf[b*3+2] = ' ';
					aBuf[b*3+3] = 0;
				}

				char aBufMsg[256];
				str_format(aBufMsg, sizeof(aBufMsg), "strange message ClientID=%d msg=%d data_size=%d", ClientID, Msg, pPacket->m_DataSize);
				Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBufMsg);
				Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
			}
		}
	}
	else
	{
		// game message
		if((pPacket->m_Flags&NET_CHUNKFLAG_VITAL) != 0 && m_aClients[ClientID].m_State >= CClient::STATE_READY)
			GameServer(m_aClients[ClientID].m_MapID)->OnMessage(Msg, &Unpacker, ClientID);
	}
}

void CServer::SendServerInfo(const NETADDR *pAddr, int Token, bool Extended, int Offset)
{
	CNetChunk Packet;
	CPacker p;
	char aBuf[256];

	// count the players
	int PlayerCount = 0, ClientCount = 0;
	for(int i = 0; i < MAX_PLAYERS; i++)
	{
		if(m_aClients[i].m_State != CClient::STATE_EMPTY)
		{
			if(GameServer()->IsClientPlayer(i))
				PlayerCount++;

			ClientCount++;
		}
	}

	p.Reset();

	p.AddRaw(Extended?SERVERBROWSE_INFO64:SERVERBROWSE_INFO, sizeof(Extended?SERVERBROWSE_INFO64:SERVERBROWSE_INFO));
	str_format(aBuf, sizeof(aBuf), "%d", Token);
	p.AddString(aBuf, 6);

	p.AddString(GameServer()->Version(), 32);
	
	if (Extended)
	{
		p.AddString(g_Config.m_SvName, 256);
	}
	else
	{
		if (ClientCount < VANILLA_MAX_CLIENTS){
			p.AddString(g_Config.m_SvName, 64);
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "%s [%d/%d]", g_Config.m_SvName, ClientCount, VANILLA_MAX_CLIENTS);
			p.AddString(aBuf, 64);
		}
	}
	p.AddString(GetMapName(), 32);

	// gametype
	p.AddString("MMOTee-Azataz-F", 16);

	// flags
	int i = 0;
	if(g_Config.m_Password[0]) // password set
		i |= SERVER_FLAG_PASSWORD;
	str_format(aBuf, sizeof(aBuf), "%d", i);
	p.AddString(aBuf, 2);

	int MaxClients = MAX_PLAYERS;
	if (!Extended)
	{
		if (ClientCount >= VANILLA_MAX_CLIENTS)
		{
			if (ClientCount < MaxClients)
				ClientCount = VANILLA_MAX_CLIENTS - 1;
			else
				ClientCount = VANILLA_MAX_CLIENTS;
		}
		if (MaxClients > VANILLA_MAX_CLIENTS) 
			MaxClients = VANILLA_MAX_CLIENTS;
	}

	if (PlayerCount > ClientCount)
		PlayerCount = ClientCount;

	str_format(aBuf, sizeof(aBuf), "%d", PlayerCount); p.AddString(aBuf, 3); // num players
	str_format(aBuf, sizeof(aBuf), "%d", MaxClients-g_Config.m_SvSpectatorSlots); p.AddString(aBuf, 3); // max players
	str_format(aBuf, sizeof(aBuf), "%d", ClientCount); p.AddString(aBuf, 3); // num clients
	str_format(aBuf, sizeof(aBuf), "%d", MaxClients); p.AddString(aBuf, 3); // max clients

	if (Extended)
		p.AddInt(Offset);

	int ClientsPerPacket = Extended ? 24 : VANILLA_MAX_CLIENTS;
	int Skip = Offset;
	int Take = ClientsPerPacket;

	for(i = 0; i < MAX_PLAYERS; i++)
	{
		if(m_aClients[i].m_State != CClient::STATE_EMPTY)
		{
			if (Skip-- > 0)
				continue;
			if (--Take < 0)
				break;

			p.AddString(ClientName(i), MAX_NAME_LENGTH); // client name
			p.AddString(ClientClan(i), MAX_CLAN_LENGTH); // client clan

			str_format(aBuf, sizeof(aBuf), "%d", m_aClients[i].m_Country); p.AddString(aBuf, 6); // client country
			str_format(aBuf, sizeof(aBuf), "%d", m_aClients[i].AccData.m_Level); p.AddString(aBuf, 6); // client score
			str_format(aBuf, sizeof(aBuf), "%d", GameServer()->IsClientPlayer(i)?1:0); p.AddString(aBuf, 2); // is player?
		}
	}

	Packet.m_ClientID = -1;
	Packet.m_Address = *pAddr;
	Packet.m_Flags = NETSENDFLAG_CONNLESS;
	Packet.m_DataSize = p.Size();
	Packet.m_pData = p.Data();
	m_NetServer.Send(&Packet);

	if (Extended && Take < 0)
		SendServerInfo(pAddr, Token, Extended, Offset + ClientsPerPacket);
}

void CServer::UpdateServerInfo()
{
	for(int i = 0; i < MAX_PLAYERS; ++i)
	{
		if(m_aClients[i].m_State != CClient::STATE_EMPTY)
			SendServerInfo(m_NetServer.ClientAddr(i), -1);
	}
}

void CServer::PumpNetwork()
{
	CNetChunk Packet;
	m_NetServer.Update();
	while(m_NetServer.Recv(&Packet))
	{
		if(Packet.m_ClientID == -1)
		{
			// stateless
			if(!m_Register.RegisterProcessPacket(&Packet))
			{
				if(Packet.m_DataSize == sizeof(SERVERBROWSE_GETINFO)+1 &&
					mem_comp(Packet.m_pData, SERVERBROWSE_GETINFO, sizeof(SERVERBROWSE_GETINFO)) == 0)
				{
					SendServerInfo(&Packet.m_Address, ((unsigned char *)Packet.m_pData)[sizeof(SERVERBROWSE_GETINFO)]);
				}
				else if(Packet.m_DataSize == sizeof(SERVERBROWSE_GETINFO64)+1 &&
					mem_comp(Packet.m_pData, SERVERBROWSE_GETINFO64, sizeof(SERVERBROWSE_GETINFO64)) == 0)
				{
					SendServerInfo(&Packet.m_Address, ((unsigned char *)Packet.m_pData)[sizeof(SERVERBROWSE_GETINFO64)], true);
				}
			}
		}
		else
			ProcessClientPacket(&Packet);
	}

	m_ServerBan.Update();
	m_Econ.Update();
}

char *CServer::GetMapName()
{
	// get the name of the map without his path
	char *pMapShortName = &g_Config.m_SvMap[0];
	for(int i = 0; i < str_length(g_Config.m_SvMap)-1; i++)
	{
		if(g_Config.m_SvMap[i] == '/' || g_Config.m_SvMap[i] == '\\')
			pMapShortName = &g_Config.m_SvMap[i+1];
	}
	return pMapShortName;
}

int CServer::LoadMap(const char *pMapName)
{
	CMapData data;
	char aBufMultiMap[512];
	for(int i = 0; i < (int)m_vMapData.size(); ++i)
	{
		if(str_comp(m_vMapData[i].m_aCurrentMap, pMapName) == 0)
		{
			str_format(aBufMultiMap, sizeof(aBufMultiMap), "Map %s already loaded (MapID=%d)", pMapName, i);
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "multimap", aBufMultiMap);
			return 1;
		}
	}
	m_vMapData.push_back(data);
	m_vpMap.push_back(new CMap());

	int MapID = m_vMapData.size()-1;
	m_vMapData[MapID].m_pCurrentMapData = 0;
	m_vMapData[MapID].m_CurrentMapSize = 0;


	//DATAFILE *df;
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "maps/%s.map", pMapName);


	str_format(aBufMultiMap, sizeof(aBufMultiMap), "Loading Map with ID '%d' and name '%s'", MapID, pMapName);

	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "multimap", aBufMultiMap);


	if(!m_vpMap[MapID]->Load(aBuf, Kernel(), Storage()))
		return 0;

	// reinit snapshot ids
	m_IDPool.TimeoutIDs();

	// get the crc of the map
	m_vMapData[MapID].m_CurrentMapCrc = m_vpMap[MapID]->Crc();
	char aBufMsg[256];
	str_format(aBufMsg, sizeof(aBufMsg), "%s crc is %08x", aBuf, m_vMapData[MapID].m_CurrentMapCrc);
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBufMsg);

	str_copy(m_vMapData[MapID].m_aCurrentMap, pMapName, sizeof(m_vMapData[MapID].m_aCurrentMap));
	//map_set(df);

	// load complete map into memory for download
	{
		IOHANDLE File = Storage()->OpenFile(aBuf, IOFLAG_READ, IStorage::TYPE_ALL);
		m_vMapData[MapID].m_CurrentMapSize = (int)io_length(File);
		//if(m_vMapData[MapID].m_pCurrentMapData)
		//	mem_free(m_vMapData[MapID].m_pCurrentMapData);
		m_vMapData[MapID].m_pCurrentMapData = (unsigned char *)malloc(m_vMapData[MapID].m_CurrentMapSize);
		io_read(File, m_vMapData[MapID].m_pCurrentMapData, m_vMapData[MapID].m_CurrentMapSize);
		io_close(File);
	}

	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", "### Map is loaded!!");

	m_vpGameServer[MapID] = CreateGameServer();
	Kernel()->RegisterInterface(m_vpMap[MapID], MapID);
	Kernel()->RegisterInterface(static_cast<IMap*>(m_vpMap[MapID]), MapID);
	Kernel()->RegisterInterface(m_vpGameServer[MapID], MapID);
	GameServer(MapID)->OnInit(MapID);
	GameServer(MapID)->OnConsoleInit();
	m_NumGameServer++;
	return 1;
}

void CServer::InitRegister(CNetServer *pNetServer, IEngineMasterServer *pMasterServer, IConsole *pConsole)
{
	m_Register.Init(pNetServer, pMasterServer, pConsole);
}

int CServer::Run()
{
	m_PrintCBIndex = Console()->RegisterPrintCallback(g_Config.m_ConsoleOutputLevel, SendRconLineAuthed, this);

	// read file data into buffer
	char aFileBuf[512];
	str_format(aFileBuf, sizeof(aFileBuf), "maps.json");
	const IOHANDLE File = m_pStorage->OpenFile(aFileBuf, IOFLAG_READ, IStorage::TYPE_ALL);
	if(!File)
	{
		dbg_msg("Maps", "Probably deleted or error when the file is invalid.");
		return false;
	}
	
	const int FileSize = (int)io_length(File);
	char* pFileData = (char*)malloc(FileSize);
	io_read(File, pFileData, FileSize);
	io_close(File);

	// parse json data
	json_settings JsonSettings;
	mem_zero(&JsonSettings, sizeof(JsonSettings));
	char aError[256];
	json_value* pJsonData = json_parse_ex(&JsonSettings, pFileData, aError);
	free(pFileData);
	if(pJsonData == nullptr)
	{
		return false;
	}

	// extract data
	const json_value& rStart = (*pJsonData)["maps"];
	if(rStart.type == json_array)
	{
		for(unsigned i = 0; i < rStart.u.array.length; ++i)
			LoadMap(rStart[i]["map"]);
	}

	// clean up
	json_value_free(pJsonData);

	// start server
	NETADDR BindAddr;
	if(g_Config.m_Bindaddr[0] && net_host_lookup(g_Config.m_Bindaddr, &BindAddr, NETTYPE_ALL) == 0)
	{
		// sweet!
		BindAddr.type = NETTYPE_ALL;
		BindAddr.port = g_Config.m_SvPort;
	}
	else
	{
		mem_zero(&BindAddr, sizeof(BindAddr));
		BindAddr.type = NETTYPE_ALL;
		BindAddr.port = g_Config.m_SvPort;
	}

	if(!m_NetServer.Open(BindAddr, &m_ServerBan, g_Config.m_SvMaxClients, g_Config.m_SvMaxClientsPerIP, 0))
	{
		dbg_msg("server", "couldn't open socket. port %d might already be in use", g_Config.m_SvPort);
		return -1;
	}

	m_NetServer.SetCallbacks(NewClientCallback, ClientRejoinCallback, DelClientCallback, this);

	m_Econ.Init(Console(), &m_ServerBan);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "server name is '%s'", g_Config.m_SvName);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	str_format(aBuf, sizeof(aBuf), "version %s", GameServer()->NetVersion());
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	{
		// THIS IS FKING COOL!!!!
		short Random = random_int(0, 2);
		switch (Random)
		{
		case 0:
			{
				dbg_msg("server", "########################################################################################################");
				dbg_msg("server", "########################################################################################################");
				dbg_msg("server", "########################################################################################################");
				dbg_msg("server", "      ___           ___           ___           ___           ___           ___     ####################");
				dbg_msg("server", "     /\\__\\         /\\__\\         /\\  \\         /   \\         /\\  \\         /\\  \\    ####################");
				dbg_msg("server", "    /::|  |       /::|  |       /::\\  \\        \\:\\  \\       /::\\  \\       /::\\  \\   ####################");
				dbg_msg("server", "   /:|:|  |      /:|:|  |      /:/\\:\\  \\        \\:\\  \\     /:/\\:\\  \\     /:/\\:\\  \\  ####################");
				dbg_msg("server", "  /:/|:|__|__   /:/|:|__|__   /:/  \\:\\  \\       /::\\  \\   /::\\~\\:\\  \\   /::\\~\\:\\  \\ ####################");
				dbg_msg("server", " /:/ |::::\\__\\ /:/ |::::\\__\\ /:/__/ \\:\\__\\     /:/\\:\\__\\ /:/\\:\\ \\:\\__\\ /:/\\:\\ \\:\\__\\####################");
				dbg_msg("server", " \\/__/~~/:/  / \\/__/~~/:/  / \\:\\  \\ /:/  /    /:/  \\/__/ \\:\\~\\:\\ \\/__/ \\:\\~\\:\\ \\/__/####################");
				dbg_msg("server", "       /:/  /        /:/  /   \\:\\  /:/  /    /:/  /       \\:\\ \\:\\__\\    \\:\\ \\:\\__\\  ####################");
				dbg_msg("server", "      /:/  /        /:/  /     \\:\\/:/  /     \\/__/         \\:\\ \\/__/     \\:\\ \\/__/  ####################");
				dbg_msg("server", "     /:/  /        /:/  /       \\::/  /                     \\:\\__\\        \\:\\__\\    ####################");
				dbg_msg("server", "     \\/__/         \\/__/         \\/__/                       \\/__/         \\/__/    ####################");
				dbg_msg("server", "########################################################################################################");
				dbg_msg("server", "########################################################################################################");
				dbg_msg("server", "########################################################################################################");
			}
			break;
		
		case 1:
			{
				dbg_msg("server", "==========================================================");
				dbg_msg("server", "---------------------------------------------------------=");
				dbg_msg("server", "███╗   ███╗███╗   ███╗ ██████╗ ████████╗███████╗███████╗-=");
				dbg_msg("server", "████╗ ████║████╗ ████║██╔═══██╗╚══██╔══╝██╔════╝██╔════╝-=");
				dbg_msg("server", "██╔████╔██║██╔████╔██║██║   ██║   ██║   █████╗  █████╗  -=");
				dbg_msg("server", "██║╚██╔╝██║██║╚██╔╝██║██║   ██║   ██║   ██╔══╝  ██╔══╝  -=");
				dbg_msg("server", "██║ ╚═╝ ██║██║ ╚═╝ ██║╚██████╔╝   ██║   ███████╗███████╗-=");
				dbg_msg("server", "╚═╝     ╚═╝╚═╝     ╚═╝ ╚═════╝    ╚═╝   ╚══════╝╚══════╝-=");
				dbg_msg("server", "---------------------------------------------------------=");
				dbg_msg("server", "==========================================================");
			}
		break;

		case 2:
		{
			dbg_msg("server", "██████████████████████████████████████████████████████████████████████████████████████████████████");
			dbg_msg("server", "█░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░█");
			dbg_msg("server", "██░░▒▓██████████████▓▒░░▒▓██████████████▓▒░░░▒▓██████▓▒░▒▓████████▓▒░▒▓████████▓▒░▒▓████████▓▒░░██");
			dbg_msg("server", "█░░░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░▒▓█▓▒░░▒▓█▓▒░░░▒▓█▓▒░░░░░▒▓█▓▒░░░░░░░░▒▓█▓▒░░░░░░░░░░█");
			dbg_msg("server", "█░░░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░▒▓█▓▒░░▒▓█▓▒░░░▒▓█▓▒░░░░░▒▓█▓▒░░░░░░░░▒▓█▓▒░░░░░░░░░░█");
			dbg_msg("server", "██░░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░▒▓█▓▒░░▒▓█▓▒░░░▒▓█▓▒░░░░░▒▓██████▓▒░░░▒▓██████▓▒░░░░██");
			dbg_msg("server", "█░░░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░▒▓█▓▒░░▒▓█▓▒░░░▒▓█▓▒░░░░░▒▓█▓▒░░░░░░░░▒▓█▓▒░░░░░░░░░░█");
			dbg_msg("server", "█░░░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░▒▓█▓▒░░▒▓█▓▒░░░▒▓█▓▒░░░░░▒▓█▓▒░░░░░░░░▒▓█▓▒░░░░░░░░░░█");
			dbg_msg("server", "██░░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░░▒▓██████▓▒░░░░▒▓█▓▒░░░░░▒▓████████▓▒░▒▓████████▓▒░░██");
			dbg_msg("server", "█░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░█");
			dbg_msg("server", "██████████████████████████████████████████████████████████████████████████████████████████████████");
		}
		break;
		
		default:
			break;
		}
	}

	// process pending commands
	m_pConsole->StoreCommands(false);

	// start game
	{
		bool NonActive = false;
		int64 ReportTime = time_get();
		int ReportInterval = 3;

		m_Lastheartbeat = 0;
		m_GameStartTime = time_get();

		while(m_RunServer)
		{
			if(NonActive)
				PumpNetwork();

			int64 t = time_get();
			int NewTicks = 0;
			bool ShouldSnap = false;

			while(t > TickStartTime(m_CurrentGameTick+1))
			{
				NewTicks++;

				m_CurrentGameTick++;
				if((m_CurrentGameTick % g_Config.m_SvSnapTick) == 0)
					ShouldSnap = true;

				// apply new input
				for(int c = 0; c < MAX_CLIENTS; c++)
				{
					if(m_aClients[c].m_State != CClient::STATE_INGAME)
						continue;
					for(int i = 0; i < 200; i++)
					{
						if(m_aClients[c].m_aInputs[i].m_GameTick == Tick())
						{
							GameServer(m_aClients[c].m_MapID)->OnClientPredictedInput(c, m_aClients[c].m_aInputs[i].m_aData);
							break;
						}
					}
				}

				for (int i = 0; i < m_NumGameServer; i++)
					GameServer(i)->OnTick();
				
				if(m_lGameServerCmds.size())
				{
					lock_wait(m_GameServerCmdLock);
					for(int i=0; i<m_lGameServerCmds.size(); i++)
					{
						m_lGameServerCmds[i]->Execute(GameServer());
						delete m_lGameServerCmds[i];
					}
					m_lGameServerCmds.clear();
					lock_unlock(m_GameServerCmdLock);
				} 
			}

			// snap game
			if(NewTicks)
			{
				if(g_Config.m_SvHighBandwidth || ShouldSnap)
				{
					for (int i = 0; i < m_NumGameServer; i++)
						DoSnapshot(i);
				}
				UpdateClientRconCommands();
			}

			// master server stuff
			m_Register.RegisterUpdate(m_NetServer.NetType());

			if(!NonActive)
				PumpNetwork();

			NonActive = true;
			for(int c = 0; c < MAX_CLIENTS; c++)
				if(m_aClients[c].m_State != CClient::STATE_EMPTY)
					NonActive = false;

			if (NonActive)
				net_socket_read_wait(m_NetServer.Socket(), 1000000);
			else
			{
				set_new_tick();
				int64 t = time_get();
				int x = (TickStartTime(m_CurrentGameTick+1) - t) * 1000000 / time_freq() + 1;

				if(x > 0)
				{
					net_socket_read_wait(m_NetServer.Socket(), x);
				}
			}

			if(ReportTime < time_get())
				ReportTime += time_freq()*ReportInterval;
		}
	}
	
	// disconnect all clients on shutdown
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(m_aClients[i].m_State != CClient::STATE_EMPTY)
			m_NetServer.Drop(i, CLIENTDROPTYPE_SHUTDOWN, "服务器倒闭了");
	}

	for (int i = 0; i < m_NumGameServer; i++)
		GameServer(i)->OnShutdown();

	for(int i = 0; i < (int)m_vpMap.size(); ++i)
	{
		m_vpMap[i]->Unload();

			if(m_vMapData[i].m_pCurrentMapData)
				free(m_vMapData[i].m_pCurrentMapData);
	}

	return 0;
}

bool CServer::ConKick(IConsole::IResult *pResult, void *pUser)
{
	CServer* pThis = (CServer *)pUser;
	
	char aBuf[128];
	const char *pStr = pResult->GetString(0);
	const char *pReason = pResult->NumArguments()>1 ? pResult->GetString(1) : "No reason given";
	str_format(aBuf, sizeof(aBuf), "Kicked (%s)", pReason);

	if(CNetDatabase::StrAllnum(pStr))
	{
		int ClientID = str_toint(pStr);
		if(ClientID < 0 || ClientID >= MAX_CLIENTS || pThis->m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "Invalid client id");
		else
			pThis->Kick(ClientID, aBuf);
	}
	else
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "Invalid client id");
	
	return true;
}

/* INFECTION MODIFICATION START ***************************************/
bool CServer::ConOptionStatus(IConsole::IResult *pResult, void *pUser)
{
	char aBuf[256];
	CServer* pThis = static_cast<CServer *>(pUser);

	for(int i = 0; i < MAX_PLAYERS; i++)
	{
		if(pThis->m_aClients[i].m_State == CClient::STATE_INGAME)
		{
			str_format(aBuf, sizeof(aBuf), "(#%02i) %s: [lang=%s] [antiping=%d]",
				i,
				pThis->ClientName(i),
				pThis->m_aClients[i].m_aLanguage,
				pThis->GetClientAntiPing(i)
			);
			
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", aBuf);
		}
	}
	
	return true;
}

bool CServer::ConStatus(IConsole::IResult *pResult, void *pUser)
{
	char aBuf[1024];
	char aAddrStr[NETADDR_MAXSTRSIZE];
	CServer* pThis = static_cast<CServer *>(pUser);

	for(int i = 0; i < MAX_PLAYERS; i++)
	{
		if(pThis->m_aClients[i].m_State != CClient::STATE_EMPTY)
		{
			net_addr_str(pThis->m_NetServer.ClientAddr(i), aAddrStr, sizeof(aAddrStr), true);
			if(pThis->m_aClients[i].m_State == CClient::STATE_INGAME)
			{				
				//Add some padding to make the command more readable
				char aBufName[18];
				str_copy(aBufName, pThis->ClientName(i), sizeof(aBufName));
				for(int c=str_length(aBufName); c<((int)sizeof(aBufName))-1; c++)
					aBufName[c] = ' ';
				aBufName[sizeof(aBufName)-1] = 0;
				
				int AuthLevel = pThis->m_aClients[i].m_Authed == CServer::AUTHED_ADMIN ? 2 :
										pThis->m_aClients[i].m_Authed == CServer::AUTHED_MOD ? 1 : 0;
				
				str_format(aBuf, sizeof(aBuf), "(#%02i) %s: [antispoof=%d] [login=%d] [level=%d] [ip=%s]",
					i,
					aBufName,
					pThis->m_NetServer.HasSecurityToken(i),
					pThis->IsClientLogged(i),
					AuthLevel,
					aAddrStr
				);
			}
			else
				str_format(aBuf, sizeof(aBuf), "id=%d addr=%s connecting", i, aAddrStr);
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", aBuf);
		}
	}
	
	return true;
/* INFECTION MODIFICATION END *****************************************/
}

bool CServer::ConShutdown(IConsole::IResult *pResult, void *pUser)
{
	((CServer *)pUser)->m_RunServer = 0;
	
	return true;
}

bool CServer::ConMapReload(IConsole::IResult *pResult, void *pUser)
{
	((CServer *)pUser)->m_MapReload = 1;
	
	return true;
}

bool CServer::ConLogout(IConsole::IResult *pResult, void *pUser)
{
	CServer *pServer = (CServer *)pUser;

	if(pServer->m_RconClientID >= 0 && pServer->m_RconClientID < MAX_PLAYERS &&
		pServer->m_aClients[pServer->m_RconClientID].m_State != CServer::CClient::STATE_EMPTY)
	{
		CMsgPacker Msg(NETMSG_RCON_AUTH_STATUS);
		Msg.AddInt(0);	//authed
		Msg.AddInt(0);	//cmdlist
		pServer->SendMsgEx(&Msg, MSGFLAG_VITAL, pServer->m_RconClientID, true, -1);

		pServer->m_aClients[pServer->m_RconClientID].m_Authed = AUTHED_NO;
		pServer->m_aClients[pServer->m_RconClientID].m_AuthTries = 0;
		pServer->m_aClients[pServer->m_RconClientID].m_pRconCmdToSend = 0;
		pServer->SendRconLine(pServer->m_RconClientID, "Logout successful.");
	}
	
	return true;
}

bool CServer::ConchainSpecialInfoupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
		((CServer *)pUserData)->UpdateServerInfo();
	
	return true;
}

bool CServer::ConchainMaxclientsperipUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
		((CServer *)pUserData)->m_NetServer.SetMaxClientsPerIP(pResult->GetInteger(0));
	
	return true;
}

bool CServer::ConchainModCommandUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	if(pResult->NumArguments() == 2)
	{
		CServer *pThis = static_cast<CServer *>(pUserData);
		const IConsole::CCommandInfo *pInfo = pThis->Console()->GetCommandInfo(pResult->GetString(0), CFGFLAG_SERVER, false);
		int OldAccessLevel = 0;
		if(pInfo)
			OldAccessLevel = pInfo->GetAccessLevel();
		pfnCallback(pResult, pCallbackUserData);
		if(pInfo && OldAccessLevel != pInfo->GetAccessLevel())
		{
			for(int i = 0; i < MAX_CLIENTS; ++i)
			{
				if(pThis->m_aClients[i].m_State == CServer::CClient::STATE_EMPTY || pThis->m_aClients[i].m_Authed != CServer::AUTHED_MOD ||
					(pThis->m_aClients[i].m_pRconCmdToSend && str_comp(pResult->GetString(0), pThis->m_aClients[i].m_pRconCmdToSend->m_pName) >= 0))
					continue;

				if(OldAccessLevel == IConsole::ACCESS_LEVEL_ADMIN)
					pThis->SendRconCmdAdd(pInfo, i);
				else
					pThis->SendRconCmdRem(pInfo, i);
			}
		}
	}
	else
		pfnCallback(pResult, pCallbackUserData);
	
	return true;
}

bool CServer::ConchainConsoleOutputLevelUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() == 1)
	{
		CServer *pThis = static_cast<CServer *>(pUserData);
		pThis->Console()->SetPrintOutputLevel(pThis->m_PrintCBIndex, pResult->GetInteger(0));
	}
	
	return true;
}

bool CServer::ConAddSqlServer(IConsole::IResult *pResult, void *pUserData)
{
	CServer *pSelf = (CServer *)pUserData;

	if (pResult->NumArguments() != 7 && pResult->NumArguments() != 8)
		return false;

	bool ReadOnly;
	if (str_comp_nocase(pResult->GetString(0), "w") == 0)
		ReadOnly = false;
	else if (str_comp_nocase(pResult->GetString(0), "r") == 0)
		ReadOnly = true;
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "choose either 'r' for SqlReadServer or 'w' for SqlWriteServer");
		return true;
	}

	//bool SetUpDb = pResult->NumArguments() == 8 ? pResult->GetInteger(7) : false;

	CSqlServer** apSqlServers = ReadOnly ? pSelf->m_apSqlReadServers : pSelf->m_apSqlWriteServers;

	for (int i = 0; i < MAX_SQLSERVERS; i++)
	{
		if (!apSqlServers[i])
		{
			//apSqlServers[i] = new CSqlServer(pResult->GetString(1), pResult->GetString(2), pResult->GetString(3), pResult->GetString(4), pResult->GetString(5), pResult->GetInteger(6), ReadOnly, SetUpDb);
			apSqlServers[i] = new CSqlServer(pResult->GetString(1), pResult->GetString(2), pResult->GetString(3), pResult->GetString(4), pResult->GetString(5), pResult->GetInteger(6), ReadOnly);

			char aBuf[512];
			str_format(aBuf, sizeof(aBuf), "Added new Sql%sServer: %d: DB: '%s' Prefix: '%s' User: '%s' IP: '%s' Port: %d", ReadOnly ? "Read" : "Write", i, apSqlServers[i]->GetDatabase(), apSqlServers[i]->GetPrefix(), apSqlServers[i]->GetUser(), apSqlServers[i]->GetIP(), apSqlServers[i]->GetPort());
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return true;
		}
	}
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "failed to add new sqlserver: limit of sqlservers reached");

	return true;
}

bool CServer::ConDumpSqlServers(IConsole::IResult *pResult, void *pUserData)
{
	CServer *pSelf = (CServer *)pUserData;

	bool ReadOnly;
	if (str_comp_nocase(pResult->GetString(0), "w") == 0)
		ReadOnly = false;
	else if (str_comp_nocase(pResult->GetString(0), "r") == 0)
		ReadOnly = true;
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "choose either 'r' for SqlReadServer or 'w' for SqlWriteServer");
		return true;
	}

	CSqlServer** apSqlServers = ReadOnly ? pSelf->m_apSqlReadServers : pSelf->m_apSqlWriteServers;

	for (int i = 0; i < MAX_SQLSERVERS; i++)
		if (apSqlServers[i])
		{
			char aBuf[512];
			str_format(aBuf, sizeof(aBuf), "SQL-%s %d: DB: '%s' Prefix: '%s' User: '%s' Pass: '%s' IP: '%s' Port: %d", ReadOnly ? "Read" : "Write", i, apSqlServers[i]->GetDatabase(), apSqlServers[i]->GetPrefix(), apSqlServers[i]->GetUser(), apSqlServers[i]->GetPass(), apSqlServers[i]->GetIP(), apSqlServers[i]->GetPort());
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		}
	
	return true;
}

void CServer::RegisterCommands()
{
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();

	// register console commands
	Console()->Register("kick", "s<username or uid> ?r<reason>", CFGFLAG_SERVER, ConKick, this, "Kick player with specified id for any reason");
	Console()->Register("status", "", CFGFLAG_SERVER, ConStatus, this, "List players");
	Console()->Register("option_status", "", CFGFLAG_SERVER, ConOptionStatus, this, "List player options");
	Console()->Register("shutdown", "", CFGFLAG_SERVER, ConShutdown, this, "Shut down");
	Console()->Register("logout", "", CFGFLAG_SERVER, ConLogout, this, "Logout of rcon");

	Console()->Register("reload", "", CFGFLAG_SERVER, ConMapReload, this, "Reload the map");

	Console()->Chain("sv_name", ConchainSpecialInfoupdate, this);
	Console()->Chain("password", ConchainSpecialInfoupdate, this);

	Console()->Chain("sv_max_clients_per_ip", ConchainMaxclientsperipUpdate, this);
	Console()->Chain("mod_command", ConchainModCommandUpdate, this);
	Console()->Chain("console_output_level", ConchainConsoleOutputLevelUpdate, this);
	
	Console()->Register("inf_add_sqlserver", "ssssssi?i", CFGFLAG_SERVER, ConAddSqlServer, this, "add a sqlserver");
	Console()->Register("inf_list_sqlservers", "s", CFGFLAG_SERVER, ConDumpSqlServers, this, "list all sqlservers readservers = r, writeservers = w");

	// register console commands in sub parts
	m_ServerBan.InitServerBan(Console(), Storage(), this);
}

int CServer::SnapNewID()
{
	return m_IDPool.NewID();
}

void CServer::SnapFreeID(int ID)
{
	m_IDPool.FreeID(ID);
}


void *CServer::SnapNewItem(int Type, int ID, int Size)
{
	dbg_assert(Type >= 0 && Type <=0xffff, "incorrect type");
	dbg_assert(ID >= 0 && ID <=0xffff, "incorrect id");
	return ID < 0 ? 0 : m_SnapshotBuilder.NewItem(Type, ID, Size);
}

void CServer::SnapSetStaticsize(int ItemType, int Size)
{
	m_SnapshotDelta.SetStaticsize(ItemType, Size);
}

static CServer *CreateServer() { return new CServer(); }

int main(int argc, const char **argv) // ignore_convention
{
#if defined(CONF_FAMILY_WINDOWS)
	for(int i = 1; i < argc; i++) // ignore_convention
	{
		if(str_comp("-s", argv[i]) == 0 || str_comp("--silent", argv[i]) == 0) // ignore_convention
		{
			ShowWindow(GetConsoleWindow(), SW_HIDE);
			break;
		}
	}
#endif

	if(secure_random_init() != 0)
	{
		dbg_msg("secure", "could not initialize secure RNG");
		return -1;
	}

	CServer *pServer = CreateServer();
	IKernel *pKernel = IKernel::Create();

	// create the components
	IEngine *pEngine = CreateEngine("Teeworlds");
	IConsole *pConsole = CreateConsole(CFGFLAG_SERVER|CFGFLAG_ECON);
	IEngineMasterServer *pEngineMasterServer = CreateEngineMasterServer();
	IStorage *pStorage = CreateStorage("Teeworlds", IStorage::STORAGETYPE_SERVER, argc, argv); // ignore_convention
	IConfig *pConfig = CreateConfig();
	
	pServer->m_pLocalization = new CLocalization(pStorage);
	pServer->m_pLocalization->InitConfig(0, NULL);
	if(!pServer->m_pLocalization->Init())
	{
		dbg_msg("localization", "could not initialize localization");
		return -1;
	}
	
	pServer->InitRegister(&pServer->m_NetServer, pEngineMasterServer, pConsole);

	{
		bool RegisterFail = false;

		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pServer); // register as both
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pEngine);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pConsole);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pStorage);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pConfig);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IEngineMasterServer*>(pEngineMasterServer)); // register as both
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IMasterServer*>(pEngineMasterServer));

		if(RegisterFail)
			return -1;
	}
	
	pEngine->Init();
	pConfig->Init();
	pEngineMasterServer->Init();
	pEngineMasterServer->Load();

	// register all console commands
	pServer->RegisterCommands();

	// execute autoexec file
	pConsole->ExecuteFile("autoexec.cfg");

	// parse the command line arguments
	if(argc > 1) // ignore_convention
		pConsole->ParseArguments(argc-1, &argv[1]); // ignore_convention

	// restore empty config strings to their defaults
	pConfig->RestoreStrings();

	pEngine->InitLogfile();

	// run the server
	pServer->Run();
	
	delete pServer->m_pLocalization;
	
	// free
	delete pServer;
	delete pKernel;
	delete pConsole;
	delete pEngineMasterServer;
	delete pStorage;
	delete pConfig;
	return 0;
}

int CServer::GetClan(Clan Type, int ClanID)
{
	switch (Type)
	{
	case Clan::Money: return m_stClan[ClanID].Money; break;
	case Clan::Exp: return m_stClan[ClanID].Exp; break;
	case Clan::Level: return m_stClan[ClanID].Level; break;
	case Clan::MaxMemberNum: return m_stClan[ClanID].MaxMemberNum; break;
	case Clan::MemberNum: return m_stClan[ClanID].MemberNum; break;
	case Clan::Relevance: return m_stClan[ClanID].Relevance; break;
	case Clan::ExpAdd: return m_stClan[ClanID].ExpAdd; break;
	case Clan::MoneyAdd: return m_stClan[ClanID].MoneyAdd; break;
	case Clan::ChairLevel: return m_stClan[ClanID].ChairLevel; break;
	default: dbg_msg("sys", "Invalid value %d in %s:%d", Type, __FILE__, __LINE__); return 0; break;
	}
}

const char *CServer::LeaderName(int ClanID)
{
	if(ClanID > 0) return m_stClan[ClanID].Creator;
	else
		return "";
}

const char *CServer::AdminName(int ClanID)
{
	if(ClanID > 0) return m_stClan[ClanID].Admin;
	else
		return "";
}

const char *CServer::GetClanName(int ClanID)
{
	if(ClanID > 0) return m_stClan[ClanID].Name;
	else
		return "";
}

void CServer::ResetBotInfo(int ClientID, int BotType, int BotSubType, int CityStart)
{
	switch (BotType)
	{
	case BOT_L1MONSTER:
		if (!BotSubType)
			str_copy(m_aClients[ClientID].m_aName, "Pig", MAX_NAME_LENGTH);
		else if (BotSubType == 1)
			str_copy(m_aClients[ClientID].m_aName, "Zombie", MAX_NAME_LENGTH);
		break;
	case BOT_L2MONSTER:
		if (!BotSubType)
			str_copy(m_aClients[ClientID].m_aName, "Kwah", MAX_NAME_LENGTH);
		else if (BotSubType == 1)
			str_copy(m_aClients[ClientID].m_aName, "Skelet", MAX_NAME_LENGTH);
		break;
	case BOT_L3MONSTER:
		if (!BotSubType)
			str_copy(m_aClients[ClientID].m_aName, "Boom", MAX_NAME_LENGTH);
		else if (BotSubType == 1)
			str_copy(m_aClients[ClientID].m_aName, "Nimfie", MAX_NAME_LENGTH);
		break;
	case BOT_GUARD:
		if (!BotSubType)
			str_copy(m_aClients[ClientID].m_aName, "Guard", MAX_NAME_LENGTH);
		else if (BotSubType == 1)
			str_copy(m_aClients[ClientID].m_aName, "Fighter", MAX_NAME_LENGTH);
		break;
	case BOT_BOSSSLIME:
		str_copy(m_aClients[ClientID].m_aName, "Slime", MAX_NAME_LENGTH);
		break;
	case BOT_BOSSVAMPIRE:
		str_copy(m_aClients[ClientID].m_aName, "Vampire", MAX_NAME_LENGTH);
		break;
	case BOT_BOSSPIGKING:
		str_copy(m_aClients[ClientID].m_aName, "BadPigges", MAX_NAME_LENGTH);
		break;
	case BOT_BOSSGUARD:
		str_copy(m_aClients[ClientID].m_aName, "GUARD", MAX_NAME_LENGTH);
		break;
	case BOT_FARMER:
		str_copy(m_aClients[ClientID].m_aName, "Nesquik", MAX_NAME_LENGTH);
		break;
	case BOT_NPCW:
	{
		const char *Name = "Nope";
		if (BotSubType == 0)
		{
			if (!CityStart)
				Name = "NPC:J.Johan";
			else if (CityStart == 1)
				Name = "NPC:Grem";
		}
		else if (BotSubType == 1)
		{
			if (!CityStart)
				Name = "NPC:Lusi";
			else if (CityStart == 1)
				Name = "NPC:Afra";
		}
		else
		{
			if (!CityStart)
				Name = "NPC:Miki";
			else if (CityStart == 1)
				Name = "NPC:Saki";
		}
		str_copy(m_aClients[ClientID].m_aName, Name, MAX_NAME_LENGTH);
		break;
	}
	default:
		str_copy(m_aClients[ClientID].m_aName, "Keke", MAX_NAME_LENGTH);
		break;
	}
}

void CServer::InitClientBot(int ClientID, int MapID)
{
	if (ClientID < MAX_PLAYERS || ClientID > MAX_CLIENTS)
		return;
		
	m_aClients[ClientID].m_State = CServer::CClient::STATE_INGAME;
	m_aClients[ClientID].m_MapID = MapID;
}

int CServer::GetClientAntiPing(int ClientID)
{
	return m_aClients[ClientID].m_AntiPing;
}

void CServer::SetClientAntiPing(int ClientID, int Value)
{
	m_aClients[ClientID].m_AntiPing = Value;
}

const char* CServer::GetClientLanguage(int ClientID)
{
	return m_aClients[ClientID].m_aLanguage;
}

void CServer::SetClientLanguage(int ClientID, const char* pLanguage)
{
	str_copy(m_aClients[ClientID].m_aLanguage, pLanguage, sizeof(m_aClients[ClientID].m_aLanguage));
}
	
int CServer::GetFireDelay(int ClientID, int WID)
{
	return m_InfFireDelay[ClientID][WID];
}

void CServer::SetFireDelay(int ClientID, int WID, int Time)
{
	m_InfFireDelay[ClientID][WID] = Time;
}

int CServer::GetAmmoRegenTime(int ClientID, int WID)
{
	return m_InfAmmoRegenTime[ClientID][WID];
}

void CServer::SetAmmoRegenTime(int ClientID, int WID, int Time)
{
	m_InfAmmoRegenTime[ClientID][WID] = Time;
}

int CServer::GetMaxAmmo(int ClientID, int WID)
{
	return m_InfMaxAmmo[ClientID][WID];
}

void CServer::SetMaxAmmo(int ClientID, int WID, int n)
{
	m_InfMaxAmmo[ClientID][WID] = n;
}

int CServer::GetSecurity(int ClientID)
{
	return m_aClients[ClientID].AccData.m_Security;
}

void CServer::SetSecurity(int ClientID, int n)
{
	m_aClients[ClientID].AccData.m_Security = n;
	UpdateStats(ClientID, 0);
}

bool CServer::IsClientLogged(int ClientID)
{
	return m_aClients[ClientID].m_UserID >= 0;
}

int CServer::GetUserID(int ClientID)
{
	return m_aClients[ClientID].m_UserID;
}

int CServer::GetClanID(int ClientID)
{
	return m_aClients[ClientID].AccData.m_ClanID;
}

void CServer::AddGameServerCmd(CGameServerCmd* pCmd)
{
	lock_wait(m_GameServerCmdLock);
	m_lGameServerCmds.add(pCmd);
	lock_unlock(m_GameServerCmdLock);
}

///////////////////////////////////////////////////////////// ИНВЕНТАРЬ
//#####################################################################
const char *CServer::GetItemName(int ClientID, int ItemID, bool ntlang)
{
	if(ItemID < 0 || ItemID >= MAX_ITEM)
		return "(nope)";
	else
	{
		if(ntlang) return Localization()->Localize(GetClientLanguage(ClientID), _(m_stInv[ClientID][ItemID].i_name));
		else return m_stInv[ClientID][ItemID].i_name;
	}
}
const char *CServer::GetItemName_en(int ItemID)
{
	if(ItemID < 0 || ItemID >= MAX_ITEM)
	{
		return "(nope)";
	}
	else
	{
		return ItemName_en[ItemID].i_name;
	}
}
int CServer::GetItemCountType(int ClientID, int Type)
{
	return m_aClients[ClientID].m_ItemCount[Type];
}
int CServer::GetItemEnchant(int ClientID, int ItemID)
{
	return m_stInv[ClientID][ItemID].i_enchant;
}
void CServer::SetItemEnchant(int ClientID, int ItemID, int Price)
{
	m_stInv[ClientID][ItemID].i_enchant = Price;
	UpdateItemSettings(ItemID, ClientID);
}
const char *CServer::GetItemDesc(int ClientID, int ItemID)
{
	if(ItemID < 0 || ItemID >= MAX_ITEM)
		return "(invalid)";
	else return m_stInv[ClientID][ItemID].i_desc;
}
const char *CServer::GetItemDesc_en(int ItemID)
{
	if(ItemID < 0 || ItemID >= MAX_ITEM)
		return "(invalid)";
	else return ItemName_en[ItemID].i_desc;
}
int CServer::GetItemCount(int ClientID, int ItemID)
{
	if(ClientID >= MAX_PLAYERS)
		return 0;
		
	return m_stInv[ClientID][ItemID].i_count;
}

int CServer::GetBonusEnchant(int ClientID, int ItemID, int Armor)
{
	if(Armor == 15)
	{
		switch (ItemID)
		{
			case LEATHERBODY: return 100*(m_stInv[ClientID][ItemID].i_enchant+1);
			case COOPERBODY: return 150*(m_stInv[ClientID][ItemID].i_enchant+1);
			case IRONBODY: return 200*(m_stInv[ClientID][ItemID].i_enchant+1);
			case GOLDBODY: return 250*(m_stInv[ClientID][ItemID].i_enchant+1);
			case DIAMONDBODY: return 300*(m_stInv[ClientID][ItemID].i_enchant+1);
			case DRAGONBODY: return 500*(m_stInv[ClientID][ItemID].i_enchant+1);
			default: return 0;
		}
	}
	else if(Armor == 16)
	{
		switch (ItemID)
		{
			case LEATHERFEET: return 50*(m_stInv[ClientID][ItemID].i_enchant+1);
			case COOPERFEET: return 100*(m_stInv[ClientID][ItemID].i_enchant+1);
			case IRONFEET: return 150*(m_stInv[ClientID][ItemID].i_enchant+1);
			case GOLDFEET: return 200*(m_stInv[ClientID][ItemID].i_enchant+1);
			case DIAMONDFEET: return 250*(m_stInv[ClientID][ItemID].i_enchant+1);
			case DRAGONFEET: return 400*(m_stInv[ClientID][ItemID].i_enchant+1);
			default: return 0;
		}
	}
	else if(Armor == 17)
	{
		if(ItemID == STCLASIC)
			return 1*(m_stInv[ClientID][ItemID].i_enchant+1);
		else return 0;			
	}
	else return 0;
}

void CServer::SetItemPrice(int ClientID, int ItemID, int Level, int Price)
{
	if(ItemID < 0 || ItemID >= MAX_ITEM)
		return;
	
	if(IsClientLogged(ClientID) && GetItemSettings(ClientID, PIGPIG) && Price > 10)
		Price -= (int)(Price/100)*5;

	m_stInv[ClientID][ItemID].i_nlevel = Level;
	m_stInv[ClientID][ItemID].i_nprice = Price;
	return;
}

int CServer::GetItemPrice(int ClientID, int ItemID, int Type)
{
	if(ItemID < 0 || ItemID >= MAX_ITEM)
		return 0;

	if(!m_stInv[ClientID][ItemID].i_nlevel)
		m_stInv[ClientID][ItemID].i_nlevel = 1;

	if(!Type) return m_stInv[ClientID][ItemID].i_nlevel;
	else return m_stInv[ClientID][ItemID].i_nprice;
}

///////////////// ################################ ##########
///////////////// ########################### MATERIALS #####
///////////////// ################################ ##########

int CServer::GetMaterials(int ID)
{
	return m_Materials[ID];
}

void CServer::SetMaterials(int ID, int Count)
{
	m_Materials[ID] = Count;
	SaveMaterials(ID);
}

void CServer::ChangeClientMap(int ClientID, int MapID)
{
	if (ClientID < 0 || ClientID >= MAX_PLAYERS || MapID == m_aClients[ClientID].m_MapID || !GameServer(MapID) || m_aClients[ClientID].m_State < CClient::STATE_READY)
		return;

	m_aClients[ClientID].m_OldMapID = m_aClients[ClientID].m_MapID;
	GameServer(m_aClients[ClientID].m_OldMapID)->PrepareClientChangeMap(ClientID);

	m_aClients[ClientID].m_MapID = MapID;
	GameServer(m_aClients[ClientID].m_MapID)->PrepareClientChangeMap(ClientID);

	int *pIdMap = GetIdMap(ClientID);
	memset(pIdMap, -1, sizeof(int) * VANILLA_MAX_CLIENTS);
	pIdMap[0] = ClientID;

	m_aClients[ClientID].Reset(false);
	m_aClients[ClientID].m_IsChangeMap = true;
	m_aClients[ClientID].m_State = CClient::STATE_CONNECTING;
	SendMap(ClientID, MapID);
}

int CServer::GetClientMapID(int CID)
{
	if(CID < 0 || CID >= MAX_CLIENTS || m_aClients[CID].m_State < CClient::STATE_READY)
		return DEFAULT_MAP_ID;
	return m_aClients[CID].m_MapID;
}

bool CServer::GetClientChangeMap(int CID)
{
	if(CID < 0 || CID >= MAX_CLIENTS)
		return false;

	return m_aClients[CID].m_IsChangeMap && m_aClients[CID].m_State >= CClient::STATE_CONNECTING && m_aClients[CID].m_State < CClient::STATE_INGAME;
}

SAccData *CServer::GetAccData(int ClientID)
{
	return &m_aClients[ClientID].AccData;
}

SAccUpgrade *CServer::GetAccUpgrade(int ClientID)
{
	return &m_aClients[ClientID].AccUpgrade;
}

void CServer::Login(int ClientID, const char* pUsername, const char* pPassword)
{	
	if(m_aClients[ClientID].m_LogInstance >= 0)
		return;

	char aHash[64]; //Result
	mem_zero(aHash, sizeof(aHash));
	Crypt(pPassword, (const unsigned char*) "d9", 1, 16, aHash);
	
	CSqlJob* pJob = new CSqlJob_Server_Login(this, ClientID, pUsername, aHash);
	m_aClients[ClientID].m_LogInstance = pJob->GetInstance();
	pJob->Start();
}

inline void CServer::Register(int ClientID, const char* pUsername, const char* pPassword, const char* pEmail)
{
	if(m_aClients[ClientID].m_LogInstance >= 0)
		return;
	
	char aHash[64];
	Crypt(pPassword, (const unsigned char*) "d9", 1, 16, aHash);
	
	CSqlJob* pJob = new CSqlJob_Server_Register(this, ClientID, pUsername, aHash, pEmail);
	m_aClients[ClientID].m_LogInstance = pJob->GetInstance();
	pJob->Start();
}

inline void CServer::ChangePassword_Admin(int ClientID, const char* pNick, const char* pPassword) // 更改密码(管理员)
{
	
	char aHash[64];
	Crypt(pPassword, (const unsigned char*) "d9", 1, 16, aHash);
	
	CSqlJob* pJob = new CSqlJob_Server_ChangePassword_Admin(this,ClientID, pNick, aHash);
	pJob->Start();
}

inline void CServer::ChangePassword(int ClientID, const char* pPassword) // 更改密码
{
	char aHash[64];
	Crypt(pPassword, (const unsigned char*) "d9", 1, 16, aHash);
	
	CSqlJob* pJob = new CSqlJob_Server_ChangePassword(this, ClientID, aHash);
	m_aClients[ClientID].m_LogInstance = pJob->GetInstance();
	pJob->Start();
}

void CServer::GiveDonate(const char Username[64], int Donate, int Who)
{
	CSqlJob* pJob = new CSqlJob_Server_GiveDonate(this, Username, Donate, Who);
	pJob->Start();
}

void CServer::InitMaterialID()
{
	CSqlJob* pJob = new CSqlJob_Server_InitMaterialID(this);
	pJob->Start();
}

void CServer::RemItems(int ItemID, int ClientID, int Count, int Type)
{
	if(m_aClients[ClientID].m_UserID < 0 && m_vpGameServer[DEFAULT_MAP_ID])
		return;

	CSqlJob* pJob = new CSqlJob_Server_RemItems(this, ItemID, ClientID, Count, Type);
	pJob->Start();
}

void CServer::RemItem(int ClientID, int ItemID, int Count, int Type)
{
	RemItems(ItemID, ClientID, Count, Type);
}

int CServer::GetItemType(int ClientID, int ItemID)
{
	return m_stInv[ClientID][ItemID].i_type;
}
int CServer::GetItemSettings(int ClientID, int ItemID)
{
	if(!GetItemCount(ClientID, ItemID))
		return 0;

	if(m_stInv[ClientID][ItemID].i_settings)
		return m_stInv[ClientID][ItemID].i_settings;
	else
		return 0;
}

int CServer::GetItemEnquip(int ClientID, int ItemType)
{
	int back = -1;
	for(int i = 0; i < MAX_ITEM; i++)
	{
		if(ItemType == m_stInv[ClientID][i].i_type)
		{
			if(m_stInv[ClientID][i].i_settings == true)
			{
				back = i;
				break;
			}
		}
	}
	return back;
}

void CServer::SetItemSettings(int ClientID, int ItemID, int ItemType)
{
	if(!GetItemCount(ClientID, ItemID))
		return;
		
	if(ItemType > 10 && m_stInv[ClientID][ItemID].i_type == ItemType)
	{
		for(int i = 0; i < MAX_ITEM; i++)
		{
			if(i != ItemID && m_stInv[ClientID][i].i_type == ItemType)
			{
				m_stInv[ClientID][i].i_settings = false;
				UpdateItemSettings(i, ClientID);
			}
		}
	}

	m_stInv[ClientID][ItemID].i_settings ^= true;
	UpdateItemSettings(ItemID, ClientID);
}

void CServer::SetItemSettingsCount(int ClientID, int ItemID, int Count)
{
	if(!GetItemCount(ClientID, ItemID))
		return;
		
	m_stInv[ClientID][ItemID].i_settings = Count;
	UpdateItemSettings(ItemID, ClientID);
}

void CServer::GetItem(int ItemID, int ClientID, int Count, int Settings, int Enchant)
{
	CSqlJob* pJob = new CSqlJob_Server_GetItem(this, ItemID, ClientID, Count, Settings, Enchant);
	pJob->Start();
	//TODO I don't know why we can't use smart pointer like this :(
	// std::unique_ptr<CSqlJob_Server_GetItem> pJob(new CSqlJob_Server_GetItem(this, ItemID, ClientID, Count, Settings, Enchant));
	// pJob->Start();
}
void CServer::GiveItem(int ClientID, int ItemID, int Count, int Settings, int Enchant)
{
	GetItem(ItemID, ClientID, Count, Settings, Enchant);
}

void CServer::InitInvID(int ClientID, int ItemID)
{
	CSqlJob* pJob = new CSqlJob_Server_InitInvID(this, ClientID, ItemID);
	pJob->Start();
}

void CServer::UpdateItemSettings(int ItemID, int ClientID)
{
	if(m_aClients[ClientID].AccData.m_Level <= 0) return;
	CSqlJob* pJob = new CSqlJob_Server_UpdateItemSettings(this, ItemID, ClientID);
	pJob->Start();
}

void CServer::SaveMaterials(int ID)
{
	CSqlJob* pJob = new CSqlJob_Server_SaveMaterial(this, ID);
	pJob->Start();
}

///////////////// ################################ ##########
///////////////// ########################### MAIL ##########
///////////////// ################################ ##########

void CServer::SetRewardMail(int ClientID, int ID, int ItemID, int ItemNum)
{
	if(!IsClientLogged(ClientID))
		return;

	m_aClients[ClientID].m_ItemReward[ID] = ItemID;
	m_aClients[ClientID].m_ItemNumReward[ID] = ItemNum;
}
int CServer::GetRewardMail(int ClientID, int ID, int Type)
{
	if(!IsClientLogged(ClientID))
		return -1;
		
	return Type ? m_aClients[ClientID].m_ItemNumReward[ID] : m_aClients[ClientID].m_ItemReward[ID];
}

int CServer::GetMailRewardDell(int ClientID, int ID)
{
	return m_aClients[ClientID].m_MailID[ID];
}

void CServer::SendMail(int AuthedID, int MailType, int ItemID, int ItemNum)
{
	CSqlJob* pJob = new CSqlJob_Server_SendMail(this, AuthedID, MailType, ItemID, ItemNum);
	pJob->Start();
}

void CServer::RemMail_OnlineBonus(int ClientID)
{
	CSqlJob* pJob = new CSqlJob_Server_RemMail_OnlineBonus(this, ClientID);
	m_aClients[ClientID].m_LogInstance = pJob->GetInstance();
	pJob->Start();
}

void CServer::RemMail(int ClientID, int IDMail)
{
	CSqlJob* pJob = new CSqlJob_Server_RemMail(this, ClientID, IDMail);
	pJob->Start();
}

void CServer::InitMailID(int ClientID)
{
	CSqlJob* pJob = new CSqlJob_Server_InitMailID(this, ClientID);
	pJob->Start();
}

void CServer::LogWarning(const char Warning[256])
{
	CSqlJob* pJob = new CSqlJob_Server_LogWarning(this, Warning);
	pJob->Start();
}

void CServer::InitAuction()
{
	CSqlJob* pJob = new CSqlJob_Server_InitAuction(this);
	pJob->Start();
}

inline void CServer::UpdateOffline()
{
	CSqlJob* pJob = new CSqlJob_Server_UpdateOffline(this);
	pJob->Start();
}

inline void CServer::UpdateOnline(int ClientID)
{
	CSqlJob* pJob = new CSqlJob_Server_UpdateOnline(this, ClientID);
	pJob->Start();
}

inline void CServer::SetOffline(int ClientID, const char* pNick)
{
	CSqlJob* pJob = new CSqlJob_Server_SetOffline(this, ClientID, pNick);
	pJob->Start();
}

inline void CServer::Unban_DB(int ClientID, const char* Nick)
{
	CSqlJob* pJob = new CSqlJob_Server_Unban_DB(this, ClientID, Nick);
	pJob->Start();
}

inline void CServer::Ban_DB(int ClientID, int ClientID_Ban, const char* Reason)
{
	CSqlJob* pJob = new CSqlJob_Server_Ban_DB(this, ClientID, ClientID_Ban, Reason);
	pJob->Start();
}

inline void CServer::SyncOffline(int ClientID)
{
	CSqlJob* pJob = new CSqlJob_Server_SyncOffline(this, ClientID);
	pJob->Start();
}

inline void CServer::SyncOnline(int ClientID)
{
	//dbg_msg("test","1");
	if(m_aClients[ClientID].m_LogInstance >= 0)
		return;
	/*while(m_aClients[ClientID].m_LogInstance != GetInstance())
	{
		sleep(1);
	}
	*/
	//dbg_msg("test","2");
	CSqlJob* pJob = new CSqlJob_Server_SyncOnline(this, ClientID);
	//dbg_msg("test","3");
	m_aClients[ClientID].m_LogInstance = pJob->GetInstance();
	//dbg_msg("test","4 %d",m_aClients[ClientID].m_LogInstance);
	pJob->Start();
}

void CServer::GetTopClanHouse() 
{
	CSqlJob* pJob = new CSqlJob_Server_GetTopClanHouse(this);
	pJob->Start();
}

int CServer::GetTopHouse(int HouseID)
{
	return m_HouseClanID[HouseID];
}

int CServer::GetOwnHouse(int ClientID)
{
	if(!GetClanID(ClientID))
		return -1;

	for (short int i = 0; i < COUNT_CLANHOUSE; i++)
	{
		if(GetClanID(ClientID) == m_HouseClanID[i])
			return i;
	}

	return -1;
}

bool CServer::GetHouse(int ClientID)
{
	if(GetClanID(ClientID) && (GetClanID(ClientID) == m_HouseClanID[0] || GetClanID(ClientID) == m_HouseClanID[1]  || GetClanID(ClientID) == m_HouseClanID[2]))
		return true;

	return false;
}
bool CServer::GetSpawnInClanHouse(int ClientID, int HouseID)
{
	if(GetClanID(ClientID) != m_HouseClanID[HouseID])
		return false;

	return m_stClan[GetClanID(ClientID)].IsSpawnInHouse;
}

bool CServer::SetOpenHouse(int HouseID)
{
	if(HouseID == -1)
		return false;

	m_stClan[m_HouseClanID[HouseID]].IsHouseOpen ^= true;
	return true;
}

bool CServer::GetOpenHouse(int HouseID)
{
	if(HouseID == -1)
		return false;

	return m_stClan[m_HouseClanID[HouseID]].IsHouseOpen;
}

void CServer::Ban(int ClientID, int Seconds, const char* pReason)
{
	m_ServerBan.BanAddr(m_NetServer.ClientAddr(ClientID), Seconds, pReason);
}

int* CServer::GetIdMap(int ClientID)
{
	return (int*)(IdMap + VANILLA_MAX_CLIENTS * ClientID);
}

void CServer::SetCustClt(int ClientID)
{
	m_aClients[ClientID].m_CustClt = 1;
}

void CServer::ShowTop10Clans(int ClientID, const char* Type, int TypeGet)
{
	CSqlJob* pJob = new CSqlJob_Server_ShowTop10Clans(this, ClientID, Type, TypeGet);
	pJob->Start();
}

void CServer::ShowTop10(int ClientID, const char* Type, int TypeGet)
{
	CSqlJob* pJob = new CSqlJob_Server_ShowTop10(this, ClientID, Type, TypeGet);
	pJob->Start();
}

void CServer::FirstInit(int ClientID)
{
	CSqlJob* pJob = new CSqlJob_Server_FirstInit(this, ClientID);
	pJob->Start();
}

void CServer::UpdateStats(int ClientID, int Type)
{
	if(m_aClients[ClientID].AccData.m_Class < 0 || (m_aClients[ClientID].m_UserID < 0 && m_vpGameServer[DEFAULT_MAP_ID]) || m_aClients[ClientID].AccData.m_Level <= 0)
		return;
	
	CSqlJob* pJob = new CSqlJob_Server_UpdateStat(this, ClientID, m_aClients[ClientID].m_UserID, Type);
	pJob->Start();
}

void CServer::ListInventory(int ClientID, int Type, int GetCount)
{
	if(m_aClients[ClientID].m_LogInstance >= 0 && (m_aClients[ClientID].m_UserID < 0 && m_vpGameServer[DEFAULT_MAP_ID]))
		return;

	CSqlJob* pJob = new CSqlJob_Server_ListInventory(this, ClientID, Type, GetCount);
	m_aClients[ClientID].m_LogInstance = pJob->GetInstance();
	pJob->Start();
}

///////////////////////////////////////////////////////////// СОЗДАНИЕ КЛАНА
bool CServer::GetLeader(int ClientID, int ClanID)
{
	if(m_aClients[ClientID].AccData.m_ClanID < 1)
		return false;
	
	if(str_comp_nocase(m_stClan[ClanID].Creator, ClientName(ClientID)) == 0)
		return true;
	else 
		return false;
}

bool CServer::GetAdmin(int ClientID, int ClanID)
{
	if(m_aClients[ClientID].AccData.m_ClanID < 1)
		return false;
	
	if(str_comp_nocase(m_stClan[ClanID].Admin, ClientName(ClientID)) == 0)
		return true;
	else 
		return false;
}

void CServer::InitClan()
{
	CSqlJob* pJob = new CSqlJob_Server_InitClan(this);
	pJob->Start();
}

void CServer::InitClanID(int ClanID, Sign Need, const char* SubType, int Price, bool Save)
{
	if(!ClanID)
		return;

	CSqlJob* pJob = new CSqlJob_Server_InitClanID(this, ClanID, Need, SubType, Price, Save);
	pJob->Start();
}

void CServer::NewClan(int ClientID, const char* pName)
{
	if(m_aClients[ClientID].m_LogInstance >= 0 || (m_aClients[ClientID].m_UserID < 0 && m_vpGameServer[DEFAULT_MAP_ID]))
		return;

	CSqlJob* pJob = new CSqlJob_Server_Newclan(this, ClientID, pName);
	m_aClients[ClientID].m_LogInstance = pJob->GetInstance();
	pJob->Start();
}

void CServer::ListClan(int ClientID, int ClanID)
{
	if(m_aClients[ClientID].m_LogInstance >= 0 || (m_aClients[ClientID].m_UserID < 0 && m_vpGameServer[DEFAULT_MAP_ID]))
		return;

	CSqlJob* pJob = new CSqlJob_Server_Listclan(this, ClientID, ClanID);
	m_aClients[ClientID].m_LogInstance = pJob->GetInstance();
	pJob->Start();
}

void CServer::UpdClanCount(int ClanID)
{
	CSqlJob* pJob = new CSqlJob_Server_UpClanCount(this, ClanID);
	pJob->Start();
}

// Присоединение к клану
void CServer::EnterClan(int ClientID, int ClanID)
{
	m_stClan[ClanID].MemberNum++;
	m_aClients[ClientID].AccData.m_ClanAdded = 0;
	m_aClients[ClientID].AccData.m_ClanID = ClanID;
	UpdateStats(ClientID, 3);
}

// Смена лидера
void CServer::ChangeLeader(int ClanID, const char* pName)
{
	str_copy(m_stClan[ClanID].Creator, pName, sizeof(m_stClan[ClanID].Creator));
	InitClanID(ClanID, PLUS, "Leader", 0, false);
}

void CServer::ChangeAdmin(int ClanID, const char* pName)
{
	str_copy(m_stClan[ClanID].Admin, pName, sizeof(m_stClan[ClanID].Admin));
	InitClanID(ClanID, PLUS, "Admin", 0, false);
}

void CServer::ExitClanOff(int ClientID, const char* pName)
{
	for(int i = 0; i < MAX_PLAYERS; ++i)
	{
		if(ClientIngame(i) && m_aClients[i].m_UserID)
			if(str_comp_nocase(pName, ClientName(i)) == 0)
				m_aClients[i].AccData.m_ClanID = 0;
	}

	CSqlJob* pJob = new CSqlJob_Server_ExitClanOff(this, ClientID, pName);
	pJob->Start();
}

void CServer::InitClientDB(int ClientID)
{
	if(m_aClients[ClientID].m_UserID < 0 && m_vpGameServer[DEFAULT_MAP_ID])
		return;

	CSqlJob* pJob = new CSqlJob_Server_InitClient(this, ClientID);
	pJob->Start();
}

void CServer::SyncPlayer(int ClientID, class CPlayer *pPlayer)
{
	for(auto& pGameServer : m_vpGameServer)
	{
		pGameServer.second->SyncPlayer(ClientID, pPlayer);
	}
}