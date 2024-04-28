#include "protocol.h"

#include <engine/shared/packer.h>
#include <engine/shared/protocol.h>
#include <engine/shared/uuid_manager.h>

#include <game/mapitems_ex.h>

CNetObjHandler::CNetObjHandler()
{
	m_pMsgFailedOn = "";
	m_pObjFailedOn = "";
	m_pObjCorrectedOn = "";
	m_NumObjCorrections = 0;
}

int CNetObjHandler::NumObjCorrections() const { return m_NumObjCorrections; }
const char *CNetObjHandler::CorrectedObjOn() const { return m_pObjCorrectedOn; }
const char *CNetObjHandler::FailedObjOn() const { return m_pObjFailedOn; }
const char *CNetObjHandler::FailedMsgOn() const { return m_pMsgFailedOn; }

static const int max_int = 0x7fffffff;
static const int min_int = 0x80000000;

int CNetObjHandler::ClampInt(const char *pErrorMsg, int Value, int Min, int Max)
{
	if(Value < Min) { m_pObjCorrectedOn = pErrorMsg; m_NumObjCorrections++; return Min; }
	if(Value > Max) { m_pObjCorrectedOn = pErrorMsg; m_NumObjCorrections++; return Max; }
	return Value;
}
	
const char *CNetObjHandler::ms_apObjNames[] = {
	"EX/UUID",
	"PlayerInput",
	"Projectile",
	"Laser",
	"Pickup",
	"Flag",
	"GameInfo",
	"GameData",
	"CharacterCore",
	"Character",
	"PlayerInfo",
	"ClientInfo",
	"SpectatorInfo",
	"Common",
	"Explosion",
	"Spawn",
	"HammerHit",
	"Death",
	"SoundGlobal",
	"SoundWorld",
	"DamageInd",
	""
};

const char *CNetObjHandler::ms_apExObjNames[] = {
	"invalid",
	"MyOwnObject",
	"DDNetCharacter",
	"DDNetPlayer",
	"GameInfoEx",
	"DDNetProjectile",
	"DDNetLaser",
	"MyOwnEvent",
	"SpecChar",
	"SwitchState",
	"EntityEx",
	""
};

int CNetObjHandler::ms_aObjSizes[] = {
	0,
	sizeof(CNetObj_PlayerInput),
	sizeof(CNetObj_Projectile),
	sizeof(CNetObj_Laser),
	sizeof(CNetObj_Pickup),
	sizeof(CNetObj_Flag),
	sizeof(CNetObj_GameInfo),
	sizeof(CNetObj_GameData),
	sizeof(CNetObj_CharacterCore),
	sizeof(CNetObj_Character),
	sizeof(CNetObj_PlayerInfo),
	sizeof(CNetObj_ClientInfo),
	sizeof(CNetObj_SpectatorInfo),
	sizeof(CNetEvent_Common),
	sizeof(CNetEvent_Explosion),
	sizeof(CNetEvent_Spawn),
	sizeof(CNetEvent_HammerHit),
	sizeof(CNetEvent_Death),
	sizeof(CNetEvent_SoundGlobal),
	sizeof(CNetEvent_SoundWorld),
	sizeof(CNetEvent_DamageInd),
	0
};

int CNetObjHandler::ms_aUnpackedObjSizes[] = {
	16,
	sizeof(CNetObj_PlayerInput),
	sizeof(CNetObj_Projectile),
	sizeof(CNetObj_Laser),
	sizeof(CNetObj_Pickup),
	sizeof(CNetObj_Flag),
	sizeof(CNetObj_GameInfo),
	sizeof(CNetObj_GameData),
	sizeof(CNetObj_CharacterCore),
	sizeof(CNetObj_Character),
	sizeof(CNetObj_PlayerInfo),
	sizeof(CNetObj_ClientInfo),
	sizeof(CNetObj_SpectatorInfo),
	sizeof(CNetEvent_Common),
	sizeof(CNetEvent_Explosion),
	sizeof(CNetEvent_Spawn),
	sizeof(CNetEvent_HammerHit),
	sizeof(CNetEvent_Death),
	sizeof(CNetEvent_SoundGlobal),
	sizeof(CNetEvent_SoundWorld),
	sizeof(CNetEvent_DamageInd),
};

int CNetObjHandler::ms_aUnpackedExObjSizes[] = {
	0,
	sizeof(CNetObj_MyOwnObject),
	sizeof(CNetObj_DDNetCharacter),
	sizeof(CNetObj_DDNetPlayer),
	sizeof(CNetObj_GameInfoEx),
	sizeof(CNetObj_DDNetProjectile),
	sizeof(CNetObj_DDNetLaser),
	sizeof(CNetObj_MyOwnEvent),
	sizeof(CNetObj_SpecChar),
	sizeof(CNetObj_SwitchState),
	sizeof(CNetObj_EntityEx),
};

const char *CNetObjHandler::ms_apMsgNames[] = {
	"invalid",
	"Sv_Motd",
	"Sv_Broadcast",
	"Sv_Chat",
	"Sv_KillMsg",
	"Sv_SoundGlobal",
	"Sv_TuneParams",
	"Sv_ExtraProjectile",
	"Sv_ReadyToEnter",
	"Sv_WeaponPickup",
	"Sv_Emoticon",
	"Sv_VoteClearOptions",
	"Sv_VoteOptionListAdd",
	"Sv_VoteOptionAdd",
	"Sv_VoteOptionRemove",
	"Sv_VoteSet",
	"Sv_VoteStatus",
	"Cl_Say",
	"Cl_SetTeam",
	"Cl_SetSpectatorMode",
	"Cl_StartInfo",
	"Cl_ChangeInfo",
	"Cl_Kill",
	"Cl_Emoticon",
	"Cl_Vote",
	"Cl_CallVote",
	"Cl_IsDDNetLegacy",
	"Sv_DDRaceTimeLegacy",
	"Sv_RecordLegacy",
	"Unused",
	"Sv_TeamsStateLegacy",
	"Cl_ShowOthersLegacy",
	""
};

const char *CNetObjHandler::ms_apExMsgNames[] = {
	"invalid",
	"Sv_MyOwnMessage",
	"Cl_ShowDistance",
	"Cl_ShowOthers",
	"Sv_TeamsState",
	"Sv_DDRaceTime",
	"Sv_Record",
	""
};

const char *CNetObjHandler::GetObjName(int Type) const
{
	if(Type >= 0 && Type < NUM_NETOBJTYPES)
	{
		return ms_apObjNames[Type];
	}
	else if(Type > __NETOBJTYPE_UUID_HELPER && Type < OFFSET_NETMSGTYPE_UUID)
	{
		return ms_apExObjNames[Type - __NETOBJTYPE_UUID_HELPER];
	}
	return "(out of range)";
}

int CNetObjHandler::GetObjSize(int Type) const
{
	if(Type < 0 || Type >= NUM_NETOBJTYPES) return 0;
	return ms_aObjSizes[Type];
}

int CNetObjHandler::GetUnpackedObjSize(int Type) const
{
	if(Type >= 0 && Type < NUM_NETOBJTYPES)
	{
		return ms_aUnpackedObjSizes[Type];
	}
	else if(Type > __NETOBJTYPE_UUID_HELPER && Type < OFFSET_NETMSGTYPE_UUID)
	{
		return ms_aUnpackedExObjSizes[Type - __NETOBJTYPE_UUID_HELPER];
	}
	return 0;
}

const char *CNetObjHandler::GetMsgName(int Type) const
{
	if(Type >= 0 && Type < NUM_NETMSGTYPES)
	{
		return ms_apMsgNames[Type];
	}
	else if(Type > __NETMSGTYPE_UUID_HELPER && Type < OFFSET_MAPITEMTYPE_UUID)
	{
		return ms_apExMsgNames[Type - __NETMSGTYPE_UUID_HELPER];
	}
	return "(out of range)";
}
	
void *CNetObjHandler::SecureUnpackObj(int Type, CUnpacker *pUnpacker)
{
	m_pObjFailedOn = 0;
	switch(Type)
	{
	case NETOBJTYPE_EX:
	{
		const unsigned char *pPtr = pUnpacker->GetRaw(sizeof(CUuid));
		if(pPtr != 0)
		{
			mem_copy(m_aUnpackedData, pPtr, sizeof(CUuid));
		}
		break;
	}
	
	case NETOBJTYPE_PLAYERINPUT:
	{
		CNetObj_PlayerInput *pData = (CNetObj_PlayerInput *)m_aUnpackedData;
		pData->m_Direction = pUnpacker->GetUncompressedInt();
		pData->m_TargetX = pUnpacker->GetUncompressedInt();
		pData->m_TargetY = pUnpacker->GetUncompressedInt();
		pData->m_Jump = pUnpacker->GetUncompressedInt();
		pData->m_Fire = pUnpacker->GetUncompressedInt();
		pData->m_Hook = pUnpacker->GetUncompressedInt();
		pData->m_PlayerFlags = pUnpacker->GetUncompressedInt();
		pData->m_WantedWeapon = pUnpacker->GetUncompressedInt();
		pData->m_NextWeapon = pUnpacker->GetUncompressedInt();
		pData->m_PrevWeapon = pUnpacker->GetUncompressedInt();
		pData->m_PlayerFlags = ClampInt("m_PlayerFlags", pData->m_PlayerFlags, 0, 256);
	} break;
	
	case NETOBJTYPE_PROJECTILE:
	{
		CNetObj_Projectile *pData = (CNetObj_Projectile *)m_aUnpackedData;
		pData->m_X = pUnpacker->GetUncompressedInt();
		pData->m_Y = pUnpacker->GetUncompressedInt();
		pData->m_VelX = pUnpacker->GetUncompressedInt();
		pData->m_VelY = pUnpacker->GetUncompressedInt();
		pData->m_Type = pUnpacker->GetUncompressedInt();
		pData->m_StartTick = pUnpacker->GetUncompressedInt();
		pData->m_Type = ClampInt("m_Type", pData->m_Type, 0, NUM_WEAPONS-1);
	} break;
	
	case NETOBJTYPE_LASER:
	{
		CNetObj_Laser *pData = (CNetObj_Laser *)m_aUnpackedData;
		pData->m_X = pUnpacker->GetUncompressedInt();
		pData->m_Y = pUnpacker->GetUncompressedInt();
		pData->m_FromX = pUnpacker->GetUncompressedInt();
		pData->m_FromY = pUnpacker->GetUncompressedInt();
		pData->m_StartTick = pUnpacker->GetUncompressedInt();
	} break;
	
	case NETOBJTYPE_PICKUP:
	{
		CNetObj_Pickup *pData = (CNetObj_Pickup *)m_aUnpackedData;
		pData->m_X = pUnpacker->GetUncompressedInt();
		pData->m_Y = pUnpacker->GetUncompressedInt();
		pData->m_Type = pUnpacker->GetUncompressedInt();
		pData->m_Subtype = pUnpacker->GetUncompressedInt();
		pData->m_Type = ClampInt("m_Type", pData->m_Type, 0, max_int);
		pData->m_Subtype = ClampInt("m_Subtype", pData->m_Subtype, 0, max_int);
	} break;
	
	case NETOBJTYPE_FLAG:
	{
		CNetObj_Flag *pData = (CNetObj_Flag *)m_aUnpackedData;
		pData->m_X = pUnpacker->GetUncompressedInt();
		pData->m_Y = pUnpacker->GetUncompressedInt();
		pData->m_Team = pUnpacker->GetUncompressedInt();
		pData->m_Team = ClampInt("m_Team", pData->m_Team, TEAM_RED, TEAM_BLUE);
	} break;
	
	case NETOBJTYPE_GAMEINFO:
	{
		CNetObj_GameInfo *pData = (CNetObj_GameInfo *)m_aUnpackedData;
		pData->m_GameFlags = pUnpacker->GetUncompressedInt();
		pData->m_GameStateFlags = pUnpacker->GetUncompressedInt();
		pData->m_RoundStartTick = pUnpacker->GetUncompressedInt();
		pData->m_WarmupTimer = pUnpacker->GetUncompressedInt();
		pData->m_ScoreLimit = pUnpacker->GetUncompressedInt();
		pData->m_TimeLimit = pUnpacker->GetUncompressedInt();
		pData->m_RoundNum = pUnpacker->GetUncompressedInt();
		pData->m_RoundCurrent = pUnpacker->GetUncompressedInt();
		pData->m_GameFlags = ClampInt("m_GameFlags", pData->m_GameFlags, 0, 256);
		pData->m_GameStateFlags = ClampInt("m_GameStateFlags", pData->m_GameStateFlags, 0, 256);
		pData->m_WarmupTimer = ClampInt("m_WarmupTimer", pData->m_WarmupTimer, min_int, max_int);
		pData->m_ScoreLimit = ClampInt("m_ScoreLimit", pData->m_ScoreLimit, 0, max_int);
		pData->m_TimeLimit = ClampInt("m_TimeLimit", pData->m_TimeLimit, 0, max_int);
		pData->m_RoundNum = ClampInt("m_RoundNum", pData->m_RoundNum, 0, max_int);
		pData->m_RoundCurrent = ClampInt("m_RoundCurrent", pData->m_RoundCurrent, 0, max_int);
	} break;
	
	case NETOBJTYPE_GAMEDATA:
	{
		CNetObj_GameData *pData = (CNetObj_GameData *)m_aUnpackedData;
		pData->m_TeamscoreRed = pUnpacker->GetUncompressedInt();
		pData->m_TeamscoreBlue = pUnpacker->GetUncompressedInt();
		pData->m_FlagCarrierRed = pUnpacker->GetUncompressedInt();
		pData->m_FlagCarrierBlue = pUnpacker->GetUncompressedInt();
		pData->m_FlagCarrierRed = ClampInt("m_FlagCarrierRed", pData->m_FlagCarrierRed, FLAG_MISSING, MAX_CLIENTS-1);
		pData->m_FlagCarrierBlue = ClampInt("m_FlagCarrierBlue", pData->m_FlagCarrierBlue, FLAG_MISSING, MAX_CLIENTS-1);
	} break;
	
	case NETOBJTYPE_CHARACTERCORE:
	{
		CNetObj_CharacterCore *pData = (CNetObj_CharacterCore *)m_aUnpackedData;
		pData->m_Tick = pUnpacker->GetUncompressedInt();
		pData->m_X = pUnpacker->GetUncompressedInt();
		pData->m_Y = pUnpacker->GetUncompressedInt();
		pData->m_VelX = pUnpacker->GetUncompressedInt();
		pData->m_VelY = pUnpacker->GetUncompressedInt();
		pData->m_Angle = pUnpacker->GetUncompressedInt();
		pData->m_Direction = pUnpacker->GetUncompressedInt();
		pData->m_Jumped = pUnpacker->GetUncompressedInt();
		pData->m_HookedPlayer = pUnpacker->GetUncompressedInt();
		pData->m_HookState = pUnpacker->GetUncompressedInt();
		pData->m_HookTick = pUnpacker->GetUncompressedInt();
		pData->m_HookX = pUnpacker->GetUncompressedInt();
		pData->m_HookY = pUnpacker->GetUncompressedInt();
		pData->m_HookDx = pUnpacker->GetUncompressedInt();
		pData->m_HookDy = pUnpacker->GetUncompressedInt();
		pData->m_Direction = ClampInt("m_Direction", pData->m_Direction, -1, 1);
		pData->m_Jumped = ClampInt("m_Jumped", pData->m_Jumped, 0, 3);
		pData->m_HookedPlayer = ClampInt("m_HookedPlayer", pData->m_HookedPlayer, -1, MAX_CLIENTS-1);
		pData->m_HookState = ClampInt("m_HookState", pData->m_HookState, -1, 5);
	} break;
	
	case NETOBJTYPE_CHARACTER:
	{
		CNetObj_Character *pData = (CNetObj_Character *)m_aUnpackedData;
		pData->m_Tick = pUnpacker->GetUncompressedInt();
		pData->m_X = pUnpacker->GetUncompressedInt();
		pData->m_Y = pUnpacker->GetUncompressedInt();
		pData->m_VelX = pUnpacker->GetUncompressedInt();
		pData->m_VelY = pUnpacker->GetUncompressedInt();
		pData->m_Angle = pUnpacker->GetUncompressedInt();
		pData->m_Direction = pUnpacker->GetUncompressedInt();
		pData->m_Jumped = pUnpacker->GetUncompressedInt();
		pData->m_HookedPlayer = pUnpacker->GetUncompressedInt();
		pData->m_HookState = pUnpacker->GetUncompressedInt();
		pData->m_HookTick = pUnpacker->GetUncompressedInt();
		pData->m_HookX = pUnpacker->GetUncompressedInt();
		pData->m_HookY = pUnpacker->GetUncompressedInt();
		pData->m_HookDx = pUnpacker->GetUncompressedInt();
		pData->m_HookDy = pUnpacker->GetUncompressedInt();
		pData->m_PlayerFlags = pUnpacker->GetUncompressedInt();
		pData->m_Health = pUnpacker->GetUncompressedInt();
		pData->m_Armor = pUnpacker->GetUncompressedInt();
		pData->m_AmmoCount = pUnpacker->GetUncompressedInt();
		pData->m_Weapon = pUnpacker->GetUncompressedInt();
		pData->m_Emote = pUnpacker->GetUncompressedInt();
		pData->m_AttackTick = pUnpacker->GetUncompressedInt();
		pData->m_Direction = ClampInt("m_Direction", pData->m_Direction, -1, 1);
		pData->m_Jumped = ClampInt("m_Jumped", pData->m_Jumped, 0, 3);
		pData->m_HookedPlayer = ClampInt("m_HookedPlayer", pData->m_HookedPlayer, -1, MAX_CLIENTS-1);
		pData->m_HookState = ClampInt("m_HookState", pData->m_HookState, -1, 5);
		pData->m_PlayerFlags = ClampInt("m_PlayerFlags", pData->m_PlayerFlags, 0, 256);
		pData->m_Health = ClampInt("m_Health", pData->m_Health, 0, 10);
		pData->m_Armor = ClampInt("m_Armor", pData->m_Armor, 0, 10);
		pData->m_AmmoCount = ClampInt("m_AmmoCount", pData->m_AmmoCount, 0, 10);
		pData->m_Weapon = ClampInt("m_Weapon", pData->m_Weapon, 0, NUM_WEAPONS-1);
		pData->m_Emote = ClampInt("m_Emote", pData->m_Emote, 0, 6);
		pData->m_AttackTick = ClampInt("m_AttackTick", pData->m_AttackTick, 0, max_int);
	} break;
	
	case NETOBJTYPE_PLAYERINFO:
	{
		CNetObj_PlayerInfo *pData = (CNetObj_PlayerInfo *)m_aUnpackedData;
		pData->m_Local = pUnpacker->GetUncompressedInt();
		pData->m_ClientID = pUnpacker->GetUncompressedInt();
		pData->m_Team = pUnpacker->GetUncompressedInt();
		pData->m_Score = pUnpacker->GetUncompressedInt();
		pData->m_Latency = pUnpacker->GetUncompressedInt();
		pData->m_Local = ClampInt("m_Local", pData->m_Local, 0, 1);
		pData->m_ClientID = ClampInt("m_ClientID", pData->m_ClientID, 0, MAX_CLIENTS-1);
		pData->m_Team = ClampInt("m_Team", pData->m_Team, TEAM_SPECTATORS, TEAM_BLUE);
	} break;
	
	case NETOBJTYPE_CLIENTINFO:
	{
		CNetObj_ClientInfo *pData = (CNetObj_ClientInfo *)m_aUnpackedData;
		pData->m_Name0 = pUnpacker->GetUncompressedInt();
		pData->m_Name1 = pUnpacker->GetUncompressedInt();
		pData->m_Name2 = pUnpacker->GetUncompressedInt();
		pData->m_Name3 = pUnpacker->GetUncompressedInt();
		pData->m_Clan0 = pUnpacker->GetUncompressedInt();
		pData->m_Clan1 = pUnpacker->GetUncompressedInt();
		pData->m_Clan2 = pUnpacker->GetUncompressedInt();
		pData->m_Country = pUnpacker->GetUncompressedInt();
		pData->m_Skin0 = pUnpacker->GetUncompressedInt();
		pData->m_Skin1 = pUnpacker->GetUncompressedInt();
		pData->m_Skin2 = pUnpacker->GetUncompressedInt();
		pData->m_Skin3 = pUnpacker->GetUncompressedInt();
		pData->m_Skin4 = pUnpacker->GetUncompressedInt();
		pData->m_Skin5 = pUnpacker->GetUncompressedInt();
		pData->m_UseCustomColor = pUnpacker->GetUncompressedInt();
		pData->m_ColorBody = pUnpacker->GetUncompressedInt();
		pData->m_ColorFeet = pUnpacker->GetUncompressedInt();
		pData->m_UseCustomColor = ClampInt("m_UseCustomColor", pData->m_UseCustomColor, 0, 1);
	} break;
	
	case NETOBJTYPE_SPECTATORINFO:
	{
		CNetObj_SpectatorInfo *pData = (CNetObj_SpectatorInfo *)m_aUnpackedData;
		pData->m_SpectatorID = pUnpacker->GetUncompressedInt();
		pData->m_X = pUnpacker->GetUncompressedInt();
		pData->m_Y = pUnpacker->GetUncompressedInt();
		pData->m_SpectatorID = ClampInt("m_SpectatorID", pData->m_SpectatorID, SPEC_FREEVIEW, MAX_CLIENTS-1);
	} break;
	
	case NETOBJTYPE_MYOWNOBJECT:
	{
		CNetObj_MyOwnObject *pData = (CNetObj_MyOwnObject *)m_aUnpackedData;
		pData->m_Test = pUnpacker->GetUncompressedInt();
	} break;
	
	case NETOBJTYPE_DDNETCHARACTER:
	{
		CNetObj_DDNetCharacter *pData = (CNetObj_DDNetCharacter *)m_aUnpackedData;
		pData->m_Flags = pUnpacker->GetUncompressedIntOrDefault(0);
		pData->m_FreezeEnd = pUnpacker->GetUncompressedIntOrDefault(0);
		pData->m_Jumps = pUnpacker->GetUncompressedIntOrDefault(2);
		pData->m_TeleCheckpoint = pUnpacker->GetUncompressedIntOrDefault(-1);
		pData->m_StrongWeakID = pUnpacker->GetUncompressedIntOrDefault(0);
		pData->m_JumpedTotal = pUnpacker->GetUncompressedIntOrDefault(-1);
		pData->m_NinjaActivationTick = pUnpacker->GetUncompressedIntOrDefault(-1);
		pData->m_FreezeStart = pUnpacker->GetUncompressedIntOrDefault(-1);
		pData->m_TargetX = pUnpacker->GetUncompressedIntOrDefault(0);
		pData->m_TargetY = pUnpacker->GetUncompressedIntOrDefault(0);
		pData->m_Jumps = ClampInt("m_Jumps", pData->m_Jumps, -1, 255);
		pData->m_StrongWeakID = ClampInt("m_StrongWeakID", pData->m_StrongWeakID, 0, MAX_CLIENTS-1);
		pData->m_JumpedTotal = ClampInt("m_JumpedTotal", pData->m_JumpedTotal, -1, 255);
	} break;
	
	case NETOBJTYPE_DDNETPLAYER:
	{
		CNetObj_DDNetPlayer *pData = (CNetObj_DDNetPlayer *)m_aUnpackedData;
		pData->m_Flags = pUnpacker->GetUncompressedInt();
		pData->m_AuthLevel = pUnpacker->GetUncompressedInt();
		pData->m_AuthLevel = ClampInt("m_AuthLevel", pData->m_AuthLevel, AUTHED_NO, AUTHED_ADMIN);
	} break;
	
	case NETOBJTYPE_GAMEINFOEX:
	{
		CNetObj_GameInfoEx *pData = (CNetObj_GameInfoEx *)m_aUnpackedData;
		pData->m_Flags = pUnpacker->GetUncompressedIntOrDefault(0);
		pData->m_Version = pUnpacker->GetUncompressedIntOrDefault(0);
		pData->m_Flags2 = pUnpacker->GetUncompressedIntOrDefault(0);
	} break;
	
	case NETOBJTYPE_DDNETPROJECTILE:
	{
		CNetObj_DDNetProjectile *pData = (CNetObj_DDNetProjectile *)m_aUnpackedData;
		pData->m_X = pUnpacker->GetUncompressedInt();
		pData->m_Y = pUnpacker->GetUncompressedInt();
		pData->m_Angle = pUnpacker->GetUncompressedInt();
		pData->m_Data = pUnpacker->GetUncompressedInt();
		pData->m_Type = pUnpacker->GetUncompressedInt();
		pData->m_StartTick = pUnpacker->GetUncompressedInt();
		pData->m_Type = ClampInt("m_Type", pData->m_Type, 0, NUM_WEAPONS-1);
	} break;
	
	case NETOBJTYPE_DDNETLASER:
	{
		CNetObj_DDNetLaser *pData = (CNetObj_DDNetLaser *)m_aUnpackedData;
		pData->m_ToX = pUnpacker->GetUncompressedInt();
		pData->m_ToY = pUnpacker->GetUncompressedInt();
		pData->m_FromX = pUnpacker->GetUncompressedInt();
		pData->m_FromY = pUnpacker->GetUncompressedInt();
		pData->m_StartTick = pUnpacker->GetUncompressedInt();
		pData->m_Owner = pUnpacker->GetUncompressedInt();
		pData->m_Type = pUnpacker->GetUncompressedInt();
		pData->m_Owner = ClampInt("m_Owner", pData->m_Owner, 0, MAX_CLIENTS-1);
	} break;
	
	case NETEVENTTYPE_COMMON:
	{
		CNetEvent_Common *pData = (CNetEvent_Common *)m_aUnpackedData;
		pData->m_X = pUnpacker->GetUncompressedInt();
		pData->m_Y = pUnpacker->GetUncompressedInt();
	} break;
	
	case NETEVENTTYPE_EXPLOSION:
	{
		CNetEvent_Explosion *pData = (CNetEvent_Explosion *)m_aUnpackedData;
		pData->m_X = pUnpacker->GetUncompressedInt();
		pData->m_Y = pUnpacker->GetUncompressedInt();
	} break;
	
	case NETEVENTTYPE_SPAWN:
	{
		CNetEvent_Spawn *pData = (CNetEvent_Spawn *)m_aUnpackedData;
		pData->m_X = pUnpacker->GetUncompressedInt();
		pData->m_Y = pUnpacker->GetUncompressedInt();
	} break;
	
	case NETEVENTTYPE_HAMMERHIT:
	{
		CNetEvent_HammerHit *pData = (CNetEvent_HammerHit *)m_aUnpackedData;
		pData->m_X = pUnpacker->GetUncompressedInt();
		pData->m_Y = pUnpacker->GetUncompressedInt();
	} break;
	
	case NETEVENTTYPE_DEATH:
	{
		CNetEvent_Death *pData = (CNetEvent_Death *)m_aUnpackedData;
		pData->m_X = pUnpacker->GetUncompressedInt();
		pData->m_Y = pUnpacker->GetUncompressedInt();
		pData->m_ClientID = pUnpacker->GetUncompressedInt();
		pData->m_ClientID = ClampInt("m_ClientID", pData->m_ClientID, 0, MAX_CLIENTS-1);
	} break;
	
	case NETEVENTTYPE_SOUNDGLOBAL:
	{
		CNetEvent_SoundGlobal *pData = (CNetEvent_SoundGlobal *)m_aUnpackedData;
		pData->m_X = pUnpacker->GetUncompressedInt();
		pData->m_Y = pUnpacker->GetUncompressedInt();
		pData->m_SoundID = pUnpacker->GetUncompressedInt();
		pData->m_SoundID = ClampInt("m_SoundID", pData->m_SoundID, 0, NUM_SOUNDS-1);
	} break;
	
	case NETEVENTTYPE_SOUNDWORLD:
	{
		CNetEvent_SoundWorld *pData = (CNetEvent_SoundWorld *)m_aUnpackedData;
		pData->m_X = pUnpacker->GetUncompressedInt();
		pData->m_Y = pUnpacker->GetUncompressedInt();
		pData->m_SoundID = pUnpacker->GetUncompressedInt();
		pData->m_SoundID = ClampInt("m_SoundID", pData->m_SoundID, 0, NUM_SOUNDS-1);
	} break;
	
	case NETEVENTTYPE_DAMAGEIND:
	{
		CNetEvent_DamageInd *pData = (CNetEvent_DamageInd *)m_aUnpackedData;
		pData->m_X = pUnpacker->GetUncompressedInt();
		pData->m_Y = pUnpacker->GetUncompressedInt();
		pData->m_Angle = pUnpacker->GetUncompressedInt();
	} break;
	
	case NETOBJTYPE_MYOWNEVENT:
	{
		CNetObj_MyOwnEvent *pData = (CNetObj_MyOwnEvent *)m_aUnpackedData;
		pData->m_Test = pUnpacker->GetUncompressedInt();
	} break;
	
	case NETOBJTYPE_SPECCHAR:
	{
		CNetObj_SpecChar *pData = (CNetObj_SpecChar *)m_aUnpackedData;
		pData->m_X = pUnpacker->GetUncompressedInt();
		pData->m_Y = pUnpacker->GetUncompressedInt();
	} break;
	
	case NETOBJTYPE_SWITCHSTATE:
	{
		CNetObj_SwitchState *pData = (CNetObj_SwitchState *)m_aUnpackedData;
		pData->m_HighestSwitchNumber = pUnpacker->GetUncompressedIntOrDefault(0);
		pData->m_aStatus[0] = pUnpacker->GetUncompressedIntOrDefault(0);
		pData->m_aStatus[1] = pUnpacker->GetUncompressedIntOrDefault(0);
		pData->m_aStatus[2] = pUnpacker->GetUncompressedIntOrDefault(0);
		pData->m_aStatus[3] = pUnpacker->GetUncompressedIntOrDefault(0);
		pData->m_aStatus[4] = pUnpacker->GetUncompressedIntOrDefault(0);
		pData->m_aStatus[5] = pUnpacker->GetUncompressedIntOrDefault(0);
		pData->m_aStatus[6] = pUnpacker->GetUncompressedIntOrDefault(0);
		pData->m_aStatus[7] = pUnpacker->GetUncompressedIntOrDefault(0);
		pData->m_aSwitchNumbers[0] = pUnpacker->GetUncompressedIntOrDefault(0);
		pData->m_aSwitchNumbers[1] = pUnpacker->GetUncompressedIntOrDefault(0);
		pData->m_aSwitchNumbers[2] = pUnpacker->GetUncompressedIntOrDefault(0);
		pData->m_aSwitchNumbers[3] = pUnpacker->GetUncompressedIntOrDefault(0);
		pData->m_aEndTicks[0] = pUnpacker->GetUncompressedIntOrDefault(0);
		pData->m_aEndTicks[1] = pUnpacker->GetUncompressedIntOrDefault(0);
		pData->m_aEndTicks[2] = pUnpacker->GetUncompressedIntOrDefault(0);
		pData->m_aEndTicks[3] = pUnpacker->GetUncompressedIntOrDefault(0);
	} break;
	
	case NETOBJTYPE_ENTITYEX:
	{
		CNetObj_EntityEx *pData = (CNetObj_EntityEx *)m_aUnpackedData;
		pData->m_SwitchNumber = pUnpacker->GetUncompressedInt();
		pData->m_Layer = pUnpacker->GetUncompressedInt();
		pData->m_EntityClass = pUnpacker->GetUncompressedInt();
	} break;
	
	default:
		m_pObjFailedOn = "(type out of range)";
		break;
	}
	
	if(pUnpacker->Error())
		m_pObjFailedOn = "(unpack error)";
	
	if(m_pObjFailedOn)
		return 0;
	m_pObjFailedOn = "";
	return m_aUnpackedData;
}
	
void *CNetObjHandler::SecureUnpackMsg(int Type, CUnpacker *pUnpacker)
{
	m_pMsgFailedOn = 0;
	switch(Type)
	{
	
	case NETMSGTYPE_SV_MOTD:
	{
		CNetMsg_Sv_Motd *pData = (CNetMsg_Sv_Motd *)m_aUnpackedData;
		pData->m_pMessage = pUnpacker->GetString();
	} break;
	
	case NETMSGTYPE_SV_BROADCAST:
	{
		CNetMsg_Sv_Broadcast *pData = (CNetMsg_Sv_Broadcast *)m_aUnpackedData;
		pData->m_pMessage = pUnpacker->GetString();
	} break;
	
	case NETMSGTYPE_SV_CHAT:
	{
		CNetMsg_Sv_Chat *pData = (CNetMsg_Sv_Chat *)m_aUnpackedData;
		pData->m_Team = pUnpacker->GetInt();
		pData->m_ClientID = pUnpacker->GetInt();
		pData->m_pMessage = pUnpacker->GetString(CUnpacker::SANITIZE_CC);
		if(pData->m_Team < -2 || pData->m_Team > 3) { m_pMsgFailedOn = "m_Team"; break; }
		if(pData->m_ClientID < -1 || pData->m_ClientID > MAX_CLIENTS-1) { m_pMsgFailedOn = "m_ClientID"; break; }
	} break;
	
	case NETMSGTYPE_SV_KILLMSG:
	{
		CNetMsg_Sv_KillMsg *pData = (CNetMsg_Sv_KillMsg *)m_aUnpackedData;
		pData->m_Killer = pUnpacker->GetInt();
		pData->m_Victim = pUnpacker->GetInt();
		pData->m_Weapon = pUnpacker->GetInt();
		pData->m_ModeSpecial = pUnpacker->GetInt();
		if(pData->m_Killer < 0 || pData->m_Killer > MAX_CLIENTS-1) { m_pMsgFailedOn = "m_Killer"; break; }
		if(pData->m_Victim < 0 || pData->m_Victim > MAX_CLIENTS-1) { m_pMsgFailedOn = "m_Victim"; break; }
		if(pData->m_Weapon < -3 || pData->m_Weapon > NUM_WEAPONS-1) { m_pMsgFailedOn = "m_Weapon"; break; }
	} break;
	
	case NETMSGTYPE_SV_SOUNDGLOBAL:
	{
		CNetMsg_Sv_SoundGlobal *pData = (CNetMsg_Sv_SoundGlobal *)m_aUnpackedData;
		pData->m_SoundID = pUnpacker->GetInt();
		if(pData->m_SoundID < 0 || pData->m_SoundID > NUM_SOUNDS-1) { m_pMsgFailedOn = "m_SoundID"; break; }
	} break;
	
	case NETMSGTYPE_SV_TUNEPARAMS:
	{
		CNetMsg_Sv_TuneParams *pData = (CNetMsg_Sv_TuneParams *)m_aUnpackedData;
		(void)pData;
	} break;
	
	case NETMSGTYPE_SV_EXTRAPROJECTILE:
	{
		CNetMsg_Sv_ExtraProjectile *pData = (CNetMsg_Sv_ExtraProjectile *)m_aUnpackedData;
		(void)pData;
	} break;
	
	case NETMSGTYPE_SV_READYTOENTER:
	{
		CNetMsg_Sv_ReadyToEnter *pData = (CNetMsg_Sv_ReadyToEnter *)m_aUnpackedData;
		(void)pData;
	} break;
	
	case NETMSGTYPE_SV_WEAPONPICKUP:
	{
		CNetMsg_Sv_WeaponPickup *pData = (CNetMsg_Sv_WeaponPickup *)m_aUnpackedData;
		pData->m_Weapon = pUnpacker->GetInt();
		if(pData->m_Weapon < 0 || pData->m_Weapon > NUM_WEAPONS-1) { m_pMsgFailedOn = "m_Weapon"; break; }
	} break;
	
	case NETMSGTYPE_SV_EMOTICON:
	{
		CNetMsg_Sv_Emoticon *pData = (CNetMsg_Sv_Emoticon *)m_aUnpackedData;
		pData->m_ClientID = pUnpacker->GetInt();
		pData->m_Emoticon = pUnpacker->GetInt();
		if(pData->m_ClientID < 0 || pData->m_ClientID > MAX_CLIENTS-1) { m_pMsgFailedOn = "m_ClientID"; break; }
		if(pData->m_Emoticon < 0 || pData->m_Emoticon > NUM_EMOTICONS-1) { m_pMsgFailedOn = "m_Emoticon"; break; }
	} break;
	
	case NETMSGTYPE_SV_VOTECLEAROPTIONS:
	{
		CNetMsg_Sv_VoteClearOptions *pData = (CNetMsg_Sv_VoteClearOptions *)m_aUnpackedData;
		(void)pData;
	} break;
	
	case NETMSGTYPE_SV_VOTEOPTIONLISTADD:
	{
		CNetMsg_Sv_VoteOptionListAdd *pData = (CNetMsg_Sv_VoteOptionListAdd *)m_aUnpackedData;
		pData->m_NumOptions = pUnpacker->GetInt();
		pData->m_pDescription0 = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
		pData->m_pDescription1 = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
		pData->m_pDescription2 = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
		pData->m_pDescription3 = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
		pData->m_pDescription4 = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
		pData->m_pDescription5 = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
		pData->m_pDescription6 = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
		pData->m_pDescription7 = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
		pData->m_pDescription8 = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
		pData->m_pDescription9 = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
		pData->m_pDescription10 = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
		pData->m_pDescription11 = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
		pData->m_pDescription12 = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
		pData->m_pDescription13 = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
		pData->m_pDescription14 = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
		if(pData->m_NumOptions < 1 || pData->m_NumOptions > 15) { m_pMsgFailedOn = "m_NumOptions"; break; }
	} break;
	
	case NETMSGTYPE_SV_VOTEOPTIONADD:
	{
		CNetMsg_Sv_VoteOptionAdd *pData = (CNetMsg_Sv_VoteOptionAdd *)m_aUnpackedData;
		pData->m_pDescription = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
	} break;
	
	case NETMSGTYPE_SV_VOTEOPTIONREMOVE:
	{
		CNetMsg_Sv_VoteOptionRemove *pData = (CNetMsg_Sv_VoteOptionRemove *)m_aUnpackedData;
		pData->m_pDescription = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
	} break;
	
	case NETMSGTYPE_SV_VOTESET:
	{
		CNetMsg_Sv_VoteSet *pData = (CNetMsg_Sv_VoteSet *)m_aUnpackedData;
		pData->m_Timeout = pUnpacker->GetInt();
		pData->m_pDescription = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
		pData->m_pReason = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
		if(pData->m_Timeout < 0 || pData->m_Timeout > 60) { m_pMsgFailedOn = "m_Timeout"; break; }
	} break;
	
	case NETMSGTYPE_SV_VOTESTATUS:
	{
		CNetMsg_Sv_VoteStatus *pData = (CNetMsg_Sv_VoteStatus *)m_aUnpackedData;
		pData->m_Yes = pUnpacker->GetInt();
		pData->m_No = pUnpacker->GetInt();
		pData->m_Pass = pUnpacker->GetInt();
		pData->m_Total = pUnpacker->GetInt();
		if(pData->m_Yes < 0 || pData->m_Yes > MAX_CLIENTS) { m_pMsgFailedOn = "m_Yes"; break; }
		if(pData->m_No < 0 || pData->m_No > MAX_CLIENTS) { m_pMsgFailedOn = "m_No"; break; }
		if(pData->m_Pass < 0 || pData->m_Pass > MAX_CLIENTS) { m_pMsgFailedOn = "m_Pass"; break; }
		if(pData->m_Total < 0 || pData->m_Total > MAX_CLIENTS) { m_pMsgFailedOn = "m_Total"; break; }
	} break;
	
	case NETMSGTYPE_CL_SAY:
	{
		CNetMsg_Cl_Say *pData = (CNetMsg_Cl_Say *)m_aUnpackedData;
		pData->m_Team = pUnpacker->GetInt();
		pData->m_pMessage = pUnpacker->GetString(CUnpacker::SANITIZE_CC);
		if(pData->m_Team < 0 || pData->m_Team > 1) { m_pMsgFailedOn = "m_Team"; break; }
	} break;
	
	case NETMSGTYPE_CL_SETTEAM:
	{
		CNetMsg_Cl_SetTeam *pData = (CNetMsg_Cl_SetTeam *)m_aUnpackedData;
		pData->m_Team = pUnpacker->GetInt();
		if(pData->m_Team < TEAM_SPECTATORS || pData->m_Team > TEAM_BLUE) { m_pMsgFailedOn = "m_Team"; break; }
	} break;
	
	case NETMSGTYPE_CL_SETSPECTATORMODE:
	{
		CNetMsg_Cl_SetSpectatorMode *pData = (CNetMsg_Cl_SetSpectatorMode *)m_aUnpackedData;
		pData->m_SpectatorID = pUnpacker->GetInt();
		if(pData->m_SpectatorID < SPEC_FREEVIEW || pData->m_SpectatorID > MAX_CLIENTS-1) { m_pMsgFailedOn = "m_SpectatorID"; break; }
	} break;
	
	case NETMSGTYPE_CL_STARTINFO:
	{
		CNetMsg_Cl_StartInfo *pData = (CNetMsg_Cl_StartInfo *)m_aUnpackedData;
		pData->m_pName = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
		pData->m_pClan = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
		pData->m_Country = pUnpacker->GetInt();
		pData->m_pSkin = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
		pData->m_UseCustomColor = pUnpacker->GetInt();
		pData->m_ColorBody = pUnpacker->GetInt();
		pData->m_ColorFeet = pUnpacker->GetInt();
		if(pData->m_UseCustomColor < 0 || pData->m_UseCustomColor > 1) { m_pMsgFailedOn = "m_UseCustomColor"; break; }
	} break;
	
	case NETMSGTYPE_CL_CHANGEINFO:
	{
		CNetMsg_Cl_ChangeInfo *pData = (CNetMsg_Cl_ChangeInfo *)m_aUnpackedData;
		pData->m_pName = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
		pData->m_pClan = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
		pData->m_Country = pUnpacker->GetInt();
		pData->m_pSkin = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
		pData->m_UseCustomColor = pUnpacker->GetInt();
		pData->m_ColorBody = pUnpacker->GetInt();
		pData->m_ColorFeet = pUnpacker->GetInt();
		if(pData->m_UseCustomColor < 0 || pData->m_UseCustomColor > 1) { m_pMsgFailedOn = "m_UseCustomColor"; break; }
	} break;
	
	case NETMSGTYPE_CL_KILL:
	{
		CNetMsg_Cl_Kill *pData = (CNetMsg_Cl_Kill *)m_aUnpackedData;
		(void)pData;
	} break;
	
	case NETMSGTYPE_CL_EMOTICON:
	{
		CNetMsg_Cl_Emoticon *pData = (CNetMsg_Cl_Emoticon *)m_aUnpackedData;
		pData->m_Emoticon = pUnpacker->GetInt();
		if(pData->m_Emoticon < 0 || pData->m_Emoticon > NUM_EMOTICONS-1) { m_pMsgFailedOn = "m_Emoticon"; break; }
	} break;
	
	case NETMSGTYPE_CL_VOTE:
	{
		CNetMsg_Cl_Vote *pData = (CNetMsg_Cl_Vote *)m_aUnpackedData;
		pData->m_Vote = pUnpacker->GetInt();
		if(pData->m_Vote < -1 || pData->m_Vote > 1) { m_pMsgFailedOn = "m_Vote"; break; }
	} break;
	
	case NETMSGTYPE_CL_CALLVOTE:
	{
		CNetMsg_Cl_CallVote *pData = (CNetMsg_Cl_CallVote *)m_aUnpackedData;
		pData->m_pType = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
		pData->m_pValue = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
		pData->m_pReason = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
	} break;
	
	case NETMSGTYPE_CL_ISDDNETLEGACY:
	{
		CNetMsg_Cl_IsDDNetLegacy *pData = (CNetMsg_Cl_IsDDNetLegacy *)m_aUnpackedData;
		(void)pData;
	} break;
	
	case NETMSGTYPE_SV_DDRACETIMELEGACY:
	{
		CNetMsg_Sv_DDRaceTimeLegacy *pData = (CNetMsg_Sv_DDRaceTimeLegacy *)m_aUnpackedData;
		pData->m_Time = pUnpacker->GetInt();
		pData->m_Check = pUnpacker->GetInt();
		pData->m_Finish = pUnpacker->GetInt();
		if(pData->m_Finish < 0 || pData->m_Finish > 1) { m_pMsgFailedOn = "m_Finish"; break; }
	} break;
	
	case NETMSGTYPE_SV_RECORDLEGACY:
	{
		CNetMsg_Sv_RecordLegacy *pData = (CNetMsg_Sv_RecordLegacy *)m_aUnpackedData;
		pData->m_ServerTimeBest = pUnpacker->GetInt();
		pData->m_PlayerTimeBest = pUnpacker->GetInt();
	} break;
	
	case NETMSGTYPE_UNUSED:
	{
		CNetMsg_Unused *pData = (CNetMsg_Unused *)m_aUnpackedData;
		(void)pData;
	} break;
	
	case NETMSGTYPE_SV_TEAMSSTATELEGACY:
	{
		CNetMsg_Sv_TeamsStateLegacy *pData = (CNetMsg_Sv_TeamsStateLegacy *)m_aUnpackedData;
		(void)pData;
	} break;
	
	case NETMSGTYPE_CL_SHOWOTHERSLEGACY:
	{
		CNetMsg_Cl_ShowOthersLegacy *pData = (CNetMsg_Cl_ShowOthersLegacy *)m_aUnpackedData;
		pData->m_Show = pUnpacker->GetInt();
		if(pData->m_Show < 0 || pData->m_Show > 1) { m_pMsgFailedOn = "m_Show"; break; }
	} break;
	
	case NETMSGTYPE_SV_MYOWNMESSAGE:
	{
		CNetMsg_Sv_MyOwnMessage *pData = (CNetMsg_Sv_MyOwnMessage *)m_aUnpackedData;
		pData->m_Test = pUnpacker->GetInt();
	} break;
	
	case NETMSGTYPE_CL_SHOWDISTANCE:
	{
		CNetMsg_Cl_ShowDistance *pData = (CNetMsg_Cl_ShowDistance *)m_aUnpackedData;
		pData->m_X = pUnpacker->GetInt();
		pData->m_Y = pUnpacker->GetInt();
	} break;
	
	case NETMSGTYPE_CL_SHOWOTHERS:
	{
		CNetMsg_Cl_ShowOthers *pData = (CNetMsg_Cl_ShowOthers *)m_aUnpackedData;
		pData->m_Show = pUnpacker->GetInt();
		if(pData->m_Show < 0 || pData->m_Show > 2) { m_pMsgFailedOn = "m_Show"; break; }
	} break;
	
	case NETMSGTYPE_SV_TEAMSSTATE:
	{
		CNetMsg_Sv_TeamsState *pData = (CNetMsg_Sv_TeamsState *)m_aUnpackedData;
		(void)pData;
	} break;
	
	case NETMSGTYPE_SV_DDRACETIME:
	{
		CNetMsg_Sv_DDRaceTime *pData = (CNetMsg_Sv_DDRaceTime *)m_aUnpackedData;
		pData->m_Time = pUnpacker->GetInt();
		pData->m_Check = pUnpacker->GetInt();
		pData->m_Finish = pUnpacker->GetInt();
		if(pData->m_Finish < 0 || pData->m_Finish > 1) { m_pMsgFailedOn = "m_Finish"; break; }
	} break;
	
	case NETMSGTYPE_SV_RECORD:
	{
		CNetMsg_Sv_Record *pData = (CNetMsg_Sv_Record *)m_aUnpackedData;
		pData->m_ServerTimeBest = pUnpacker->GetInt();
		pData->m_PlayerTimeBest = pUnpacker->GetInt();
	} break;
	
	default:
		m_pMsgFailedOn = "(type out of range)";
		break;
	}
	
	if(pUnpacker->Error())
		m_pMsgFailedOn = "(unpack error)";
	
	if(m_pMsgFailedOn)
		return 0;
	m_pMsgFailedOn = "";
	return m_aUnpackedData;
}
	
bool CNetObjHandler::TeeHistorianRecordMsg(int Type)
{
	switch(Type)
	{
	
	case NETMSGTYPE_CL_SAY:
	case NETMSGTYPE_CL_VOTE:
	case NETMSGTYPE_CL_CALLVOTE:
		return false;
	default:
		return true;
	}
}
	
void RegisterGameUuids(CUuidManager *pManager)
{
	
	pManager->RegisterName(NETOBJTYPE_MYOWNOBJECT, "my-own-object@heinrich5991.de");
	pManager->RegisterName(NETOBJTYPE_DDNETCHARACTER, "character@netobj.ddnet.tw");
	pManager->RegisterName(NETOBJTYPE_DDNETPLAYER, "player@netobj.ddnet.tw");
	pManager->RegisterName(NETOBJTYPE_GAMEINFOEX, "gameinfo@netobj.ddnet.tw");
	pManager->RegisterName(NETOBJTYPE_DDNETPROJECTILE, "projectile@netobj.ddnet.tw");
	pManager->RegisterName(NETOBJTYPE_DDNETLASER, "laser@netobj.ddnet.tw");
	pManager->RegisterName(NETOBJTYPE_MYOWNEVENT, "my-own-event@heinrich5991.de");
	pManager->RegisterName(NETOBJTYPE_SPECCHAR, "spec-char@netobj.ddnet.tw");
	pManager->RegisterName(NETOBJTYPE_SWITCHSTATE, "switch-state@netobj.ddnet.tw");
	pManager->RegisterName(NETOBJTYPE_ENTITYEX, "entity-ex@netobj.ddnet.tw");
	pManager->RegisterName(NETMSGTYPE_SV_MYOWNMESSAGE, "my-own-message@heinrich5991.de");
	pManager->RegisterName(NETMSGTYPE_CL_SHOWDISTANCE, "show-distance@netmsg.ddnet.tw");
	pManager->RegisterName(NETMSGTYPE_CL_SHOWOTHERS, "showothers@netmsg.ddnet.tw");
	pManager->RegisterName(NETMSGTYPE_SV_TEAMSSTATE, "teamsstate@netmsg.ddnet.tw");
	pManager->RegisterName(NETMSGTYPE_SV_DDRACETIME, "ddrace-time@netmsg.ddnet.tw");
	pManager->RegisterName(NETMSGTYPE_SV_RECORD, "record@netmsg.ddnet.tw");

	RegisterMapItemTypeUuids(pManager);
}
	
