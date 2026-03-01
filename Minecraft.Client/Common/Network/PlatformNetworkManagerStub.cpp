#include "stdafx.h"	
#include "..\..\..\Minecraft.World\Socket.h"
#include "..\..\..\Minecraft.World\StringHelpers.h"
#include "PlatformNetworkManagerStub.h"
#include "..\..\Xbox\Network\NetworkPlayerXbox.h"		// TODO - stub version of this?

CPlatformNetworkManagerStub *g_pPlatformNetworkManager;

namespace
{
void AppendUniquePlayer(std::vector<INetworkPlayer *> &players, INetworkPlayer *player)
{
	if(player == NULL)
	{
		return;
	}

	for(std::vector<INetworkPlayer *>::iterator it = players.begin(); it != players.end(); ++it)
	{
		if(*it == player)
		{
			return;
		}

		if((*it)->GetSmallId() == player->GetSmallId() &&
		   (*it)->IsHost() == player->IsHost() &&
		   (*it)->IsLocal() == player->IsLocal())
		{
			return;
		}
	}

	players.push_back(player);
}
}

CPlatformNetworkManagerStub::CPlatformNetworkManagerStub()
	: m_pIQNet(NULL)
{
}

void CPlatformNetworkManagerStub::NotifyPlayerJoined(IQNetPlayer *pQNetPlayer	)
{
	const char * pszDescription;

	// 4J Stu - We create a fake socket for every where that we need an INBOUND queue of game data. Outbound
	// is all handled by QNet so we don't need that. Therefore each client player has one, and the host has one
	// for each client player.
	bool createFakeSocket = false;
	bool localPlayer = false;

	NetworkPlayerXbox *networkPlayer = (NetworkPlayerXbox *)addNetworkPlayer(pQNetPlayer);

    if( pQNetPlayer->IsLocal() )
    {
		localPlayer = true;
        if( pQNetPlayer->IsHost() )
        {
            pszDescription = "local host";
			// 4J Stu - No socket for the localhost as it uses a special loopback queue

			m_machineQNetPrimaryPlayers.push_back( pQNetPlayer );
        }
        else
        {
            pszDescription = "local";

			// We need an inbound queue on all local players to receive data from the host
			createFakeSocket = true;
        }
    }
    else
    {
        if( pQNetPlayer->IsHost() )
        {
            pszDescription = "remote host";
        }
        else
        {
            pszDescription = "remote";

			// If we are the host, then create a fake socket for every remote player
			if( m_pIQNet->IsHost() )
			{
				createFakeSocket = true;
			}
        }

		if( m_pIQNet->IsHost() && !m_bHostChanged )
		{
			// Do we already have a primary player for this system?
			bool systemHasPrimaryPlayer = false;
			for(AUTO_VAR(it, m_machineQNetPrimaryPlayers.begin()); it < m_machineQNetPrimaryPlayers.end(); ++it)
			{
				IQNetPlayer *pQNetPrimaryPlayer = *it;
				if( pQNetPlayer->IsSameSystem(pQNetPrimaryPlayer) )
				{
					systemHasPrimaryPlayer = true;
					break;
				}
			}
			if( !systemHasPrimaryPlayer )
				m_machineQNetPrimaryPlayers.push_back( pQNetPlayer );
		}
    }
	g_NetworkManager.PlayerJoining( networkPlayer );
	
	if( createFakeSocket == true && !m_bHostChanged )
	{
		g_NetworkManager.CreateSocket( networkPlayer, localPlayer );
	}

    app.DebugPrintf( "Player 0x%p \"%ls\" joined; %s; voice %i; camera %i.\n",
        pQNetPlayer,
        pQNetPlayer->GetGamertag(),
        pszDescription,
        (int) pQNetPlayer->HasVoice(),
        (int) pQNetPlayer->HasCamera() );


	if( m_pIQNet->IsHost() )
	{
		// 4J-PB - only the host should do this
//		g_NetworkManager.UpdateAndSetGameSessionData();
		SystemFlagAddPlayer( networkPlayer );
	}
	
	for( int idx = 0; idx < XUSER_MAX_COUNT; ++idx)
	{
		if(playerChangedCallback[idx] != NULL)
			playerChangedCallback[idx]( playerChangedCallbackParam[idx], networkPlayer, false );
	}

	if(m_pIQNet->GetState() == QNET_STATE_GAME_PLAY)
	{
		int localPlayerCount = 0;
		for(unsigned int idx = 0; idx < XUSER_MAX_COUNT; ++idx)
		{
			if( m_pIQNet->GetLocalPlayerByUserIndex(idx) != NULL ) ++localPlayerCount;
		}

		float appTime = app.getAppTime();

		// Only record stats for the primary player here
		m_lastPlayerEventTimeStart = appTime;
	}
}

bool CPlatformNetworkManagerStub::Initialise(CGameNetworkManager *pGameNetworkManager, int flagIndexSize)
{
	m_pGameNetworkManager = pGameNetworkManager;
	m_flagIndexSize = flagIndexSize;
	g_pPlatformNetworkManager = this;

	if (m_pIQNet != NULL)
	{
		delete m_pIQNet;
		m_pIQNet = NULL;
	}
	m_pIQNet = new IQNet();
	m_pIQNet->EndGame();

	for( int i = 0; i < XUSER_MAX_COUNT; i++ )
	{
		playerChangedCallback[ i ] = NULL;
	}
	
	m_bLeavingGame = false;
	m_bLeaveGameOnTick = false;
	m_bHostChanged = false;

	m_bSearchResultsReady = false;
	m_bSearchPending = false;

	m_bIsOfflineGame = false;
	m_pSearchParam = NULL;
	m_SessionsUpdatedCallback = NULL;

	for(unsigned int i = 0; i < XUSER_MAX_COUNT; ++i)
	{
		m_searchResultsCount[i] = 0;
		m_lastSearchStartTime[i] = 0;

		// The results that will be filled in with the current search
		m_pSearchResults[i] = NULL;
		m_pQoSResult[i] = NULL;
		m_pCurrentSearchResults[i] = NULL;
		m_pCurrentQoSResult[i] = NULL;
		m_currentSearchResultsCount[i] = 0;
	}

    // Success!
    return true;
}

void CPlatformNetworkManagerStub::Terminate()
{
	delete m_pIQNet;
	m_pIQNet = NULL;
}

int CPlatformNetworkManagerStub::GetJoiningReadyPercentage()
{
	return 100;
}

int CPlatformNetworkManagerStub::CorrectErrorIDS(int IDS)
{
	return IDS;
}

bool CPlatformNetworkManagerStub::isSystemPrimaryPlayer(IQNetPlayer *pQNetPlayer)
{
	return true;
}

// We call this twice a frame, either side of the render call so is a good place to "tick" things
void CPlatformNetworkManagerStub::DoWork()
{
}

int CPlatformNetworkManagerStub::GetPlayerCount()
{
	std::vector<INetworkPlayer *> players;

	const int qnetCount = m_pIQNet->GetPlayerCount();
	for(int i = 0; i < qnetCount; ++i)
	{
		IQNetPlayer *qnetPlayer = m_pIQNet->GetPlayerByIndex(i);
		INetworkPlayer *player = getNetworkPlayer(qnetPlayer);
		if(player == NULL && qnetPlayer != NULL)
		{
			player = addNetworkPlayer(qnetPlayer);
		}
		AppendUniquePlayer(players, player);
	}

	const int directCount = Socket::GetDirectPlayerCount();
	for(int i = 0; i < directCount; ++i)
	{
		AppendUniquePlayer(players, Socket::GetDirectPlayerByIndex(i));
	}

	return (int)players.size();
}

bool CPlatformNetworkManagerStub::ShouldMessageForFullSession()
{
	return false;
}

int CPlatformNetworkManagerStub::GetOnlinePlayerCount()
{
	return GetPlayerCount();
}

int CPlatformNetworkManagerStub::GetLocalPlayerMask(int playerIndex)
{
	return 1 << playerIndex;
}

bool CPlatformNetworkManagerStub::AddLocalPlayerByUserIndex( int userIndex )
{
	const HRESULT result = m_pIQNet->AddLocalPlayerByUserIndex(userIndex);
	if(result != S_OK)
	{
		return false;
	}

	NotifyPlayerJoined(m_pIQNet->GetLocalPlayerByUserIndex(userIndex));
	return true;
}

bool CPlatformNetworkManagerStub::RemoveLocalPlayerByUserIndex( int userIndex )
{
	return true;
}

bool CPlatformNetworkManagerStub::IsInStatsEnabledSession()
{
	return true;
}

bool CPlatformNetworkManagerStub::SessionHasSpace(unsigned int spaceRequired /*= 1*/)
{
	return true;
}

void CPlatformNetworkManagerStub::SendInviteGUI(int quadrant)
{
}

bool CPlatformNetworkManagerStub::IsAddingPlayer()
{
	return false;
}

bool CPlatformNetworkManagerStub::LeaveGame(bool bMigrateHost)
{
	if( m_bLeavingGame ) return true;

	m_bLeavingGame = true;

	// If we are the host wait for the game server to end
	if(m_pIQNet->IsHost() && g_NetworkManager.ServerStoppedValid())
	{
		m_pIQNet->EndGame();
		g_NetworkManager.ServerStoppedWait();
		g_NetworkManager.ServerStoppedDestroy();
	}
	return true;
}

bool CPlatformNetworkManagerStub::_LeaveGame(bool bMigrateHost, bool bLeaveRoom)
{
	return true;
}

void CPlatformNetworkManagerStub::HostGame(int localUsersMask, bool bOnlineGame, bool bIsPrivate, unsigned char publicSlots /*= MINECRAFT_NET_MAX_PLAYERS*/, unsigned char privateSlots /*= 0*/)
{
// #ifdef _XBOX
	// 4J Stu - We probably did this earlier as well, but just to be sure!
	SetLocalGame( !bOnlineGame );
	SetPrivateGame( bIsPrivate );
	SystemFlagReset();

	// Make sure that the Primary Pad is in by default
	localUsersMask |= GetLocalPlayerMask( g_NetworkManager.GetPrimaryPad() );

	m_bLeavingGame = false;

	m_pIQNet->HostGame();

	_HostGame( localUsersMask, publicSlots, privateSlots );
//#endif
}

void CPlatformNetworkManagerStub::_HostGame(int usersMask, unsigned char publicSlots /*= MINECRAFT_NET_MAX_PLAYERS*/, unsigned char privateSlots /*= 0*/)
{
}

bool CPlatformNetworkManagerStub::_StartGame()
{
	return true;
}

int CPlatformNetworkManagerStub::JoinGame(FriendSessionInfo *searchResult, int localUsersMask, int primaryUserIndex)
{
	return CGameNetworkManager::JOINGAME_SUCCESS;
}

bool CPlatformNetworkManagerStub::SetLocalGame(bool isLocal)
{
	m_bIsOfflineGame = isLocal;

	return true;
}

void CPlatformNetworkManagerStub::SetPrivateGame(bool isPrivate)
{
	app.DebugPrintf("Setting as private game: %s\n", isPrivate ? "yes" : "no" );
	m_bIsPrivateGame = isPrivate;
}

void CPlatformNetworkManagerStub::RegisterPlayerChangedCallback(int iPad, void (*callback)(void *callbackParam, INetworkPlayer *pPlayer, bool leaving), void *callbackParam)
{
	playerChangedCallback[iPad] = callback;
	playerChangedCallbackParam[iPad] = callbackParam;
}

void CPlatformNetworkManagerStub::UnRegisterPlayerChangedCallback(int iPad, void (*callback)(void *callbackParam, INetworkPlayer *pPlayer, bool leaving), void *callbackParam)
{
	if(playerChangedCallbackParam[iPad] == callbackParam)
	{
		playerChangedCallback[iPad] = NULL;
		playerChangedCallbackParam[iPad] = NULL;
	}
}

void CPlatformNetworkManagerStub::HandleSignInChange()
{
	return;	
}

bool CPlatformNetworkManagerStub::_RunNetworkGame()
{
	return true;
}

void CPlatformNetworkManagerStub::UpdateAndSetGameSessionData(INetworkPlayer *pNetworkPlayerLeaving /*= NULL*/)
{
// 	DWORD playerCount = m_pIQNet->GetPlayerCount();
// 
// 	if( this->m_bLeavingGame )
// 		return;
// 
// 	if( GetHostPlayer() == NULL )
// 		return;
// 
// 	for(unsigned int i = 0; i < MINECRAFT_NET_MAX_PLAYERS; ++i)
// 	{
// 		if( i < playerCount )
// 		{
// 			INetworkPlayer *pNetworkPlayer = GetPlayerByIndex(i);
// 
// 			// We can call this from NotifyPlayerLeaving but at that point the player is still considered in the session
// 			if( pNetworkPlayer != pNetworkPlayerLeaving )
// 			{
// 				m_hostGameSessionData.players[i] = ((NetworkPlayerXbox *)pNetworkPlayer)->GetUID();
// 
// 				char *temp;
// 				temp = (char *)wstringtofilename( pNetworkPlayer->GetOnlineName() );
// 				memcpy(m_hostGameSessionData.szPlayers[i],temp,XUSER_NAME_SIZE);
// 			}
// 			else
// 			{
// 				m_hostGameSessionData.players[i] = NULL;
// 				memset(m_hostGameSessionData.szPlayers[i],0,XUSER_NAME_SIZE);
// 			}
// 		}
// 		else
// 		{
// 			m_hostGameSessionData.players[i] = NULL;
// 			memset(m_hostGameSessionData.szPlayers[i],0,XUSER_NAME_SIZE);
// 		}
// 	}
// 
// 	m_hostGameSessionData.hostPlayerUID = ((NetworkPlayerXbox *)GetHostPlayer())->GetQNetPlayer()->GetXuid();
// 	m_hostGameSessionData.m_uiGameHostSettings = app.GetGameHostOption(eGameHostOption_All);
}

int CPlatformNetworkManagerStub::RemovePlayerOnSocketClosedThreadProc( void* lpParam )
{
	INetworkPlayer *pNetworkPlayer = (INetworkPlayer *)lpParam;

	Socket *socket = pNetworkPlayer->GetSocket();

	if( socket != NULL )
	{
		//printf("Waiting for socket closed event\n");
		socket->m_socketClosedEvent->WaitForSignal(INFINITE);

		//printf("Socket closed event has fired\n");
		// 4J Stu - Clear our reference to this socket
		pNetworkPlayer->SetSocket( NULL );
		delete socket;
	}

	return g_pPlatformNetworkManager->RemoveLocalPlayer( pNetworkPlayer );
}

bool CPlatformNetworkManagerStub::RemoveLocalPlayer( INetworkPlayer *pNetworkPlayer )
{
	return true;
}

CPlatformNetworkManagerStub::PlayerFlags::PlayerFlags(INetworkPlayer *pNetworkPlayer, unsigned int count)
{
	// 4J Stu - Don't assert, just make it a multiple of 8! This count is calculated from a load of separate values,
	// and makes tweaking world/render sizes a pain if we hit an assert here
	count = (count + 8 - 1) & ~(8 - 1);
	//assert( ( count % 8 ) == 0 );
	this->m_pNetworkPlayer = pNetworkPlayer;
	this->flags = new unsigned char [ count / 8 ];
	memset( this->flags, 0, count / 8 );
	this->count = count;
}
CPlatformNetworkManagerStub::PlayerFlags::~PlayerFlags()
{
	delete [] flags;
}

// Add a player to the per system flag storage - if we've already got a player from that system, copy its flags over
void CPlatformNetworkManagerStub::SystemFlagAddPlayer(INetworkPlayer *pNetworkPlayer)
{
	if( pNetworkPlayer == NULL )
	{
		return;
	}

	// Remove stale entries before copying per-system chunk flags. Without this,
	// a reconnect can inherit fully-set flags from a disconnected player on the
	// same machine, which prevents chunks from being re-sent.
	std::vector<INetworkPlayer *> activePlayers;
	const int activeCount = GetPlayerCount();
	for( int i = 0; i < activeCount; ++i )
	{
		INetworkPlayer *activePlayer = GetPlayerByIndex(i);
		if( activePlayer != NULL )
		{
			activePlayers.push_back(activePlayer);
		}
	}

	for( unsigned int i = 0; i < m_playerFlags.size(); )
	{
		bool stillActive = false;
		for( unsigned int j = 0; j < activePlayers.size(); ++j )
		{
			if( m_playerFlags[i]->m_pNetworkPlayer == activePlayers[j] )
			{
				stillActive = true;
				break;
			}
		}

		if( !stillActive )
		{
			delete m_playerFlags[i];
			m_playerFlags[i] = m_playerFlags.back();
			m_playerFlags.pop_back();
			continue;
		}

		++i;
	}

	for( unsigned int i = 0; i < m_playerFlags.size(); ++i )
	{
		if( m_playerFlags[i]->m_pNetworkPlayer == pNetworkPlayer )
		{
			return;
		}
	}

	PlayerFlags *newPlayerFlags = new PlayerFlags( pNetworkPlayer,  m_flagIndexSize);

	// If a same-system player with the same UID is still present, treat this as
	// a reconnect and do not inherit chunk-sent flags. Otherwise a rejoin can be
	// considered "fully synced" and only the spawn chunk appears.
	bool copyFlagsFromSystem = true;
	PlayerUID newUid = pNetworkPlayer->GetUID();
	for( unsigned int i = 0; i < m_playerFlags.size(); ++i )
	{
		INetworkPlayer *existingPlayer = m_playerFlags[i]->m_pNetworkPlayer;
		if( existingPlayer != NULL &&
			pNetworkPlayer->IsSameSystem(existingPlayer) &&
			ProfileManager.AreXUIDSEqual(existingPlayer->GetUID(), newUid) )
		{
			copyFlagsFromSystem = false;
			break;
		}
	}

	// If any of our existing players are on the same system, then copy over flags from that one
	for( unsigned int i = 0; copyFlagsFromSystem && i < m_playerFlags.size(); i++ )
	{
		if( pNetworkPlayer->IsSameSystem(m_playerFlags[i]->m_pNetworkPlayer) )
		{
			memcpy( newPlayerFlags->flags, m_playerFlags[i]->flags, m_playerFlags[i]->count / 8 );
			break;
		}
	}
	m_playerFlags.push_back(newPlayerFlags);
}

// Remove a player from the per system flag storage - just maintains the m_playerFlags vector without any gaps in it
void CPlatformNetworkManagerStub::SystemFlagRemovePlayer(INetworkPlayer *pNetworkPlayer)
{
	for( unsigned int i = 0; i < m_playerFlags.size(); i++ )
	{
		if( m_playerFlags[i]->m_pNetworkPlayer == pNetworkPlayer )
		{
			delete m_playerFlags[i];
			m_playerFlags[i] = m_playerFlags.back();
			m_playerFlags.pop_back();
			return;
		}
	}
}

void CPlatformNetworkManagerStub::SystemFlagReset()
{
	for( unsigned int i = 0; i < m_playerFlags.size(); i++ )
	{
		delete m_playerFlags[i];
	}
	m_playerFlags.clear();
}

// Set a per system flag - this is done by setting the flag on every player that shares that system
void CPlatformNetworkManagerStub::SystemFlagSet(INetworkPlayer *pNetworkPlayer, int index)
{
	if( ( index < 0 ) || ( index >= m_flagIndexSize ) ) return;
	if( pNetworkPlayer == NULL ) return;

	// Drop stale records before using IsSameSystem() against tracked players.
	std::vector<INetworkPlayer *> activePlayers;
	const int activeCount = GetPlayerCount();
	for( int i = 0; i < activeCount; ++i )
	{
		INetworkPlayer *activePlayer = GetPlayerByIndex(i);
		if( activePlayer != NULL )
		{
			activePlayers.push_back(activePlayer);
		}
	}

	for( unsigned int i = 0; i < m_playerFlags.size(); )
	{
		bool stillActive = false;
		for( unsigned int j = 0; j < activePlayers.size(); ++j )
		{
			if( m_playerFlags[i]->m_pNetworkPlayer == activePlayers[j] )
			{
				stillActive = true;
				break;
			}
		}

		if( !stillActive )
		{
			delete m_playerFlags[i];
			m_playerFlags[i] = m_playerFlags.back();
			m_playerFlags.pop_back();
			continue;
		}

		++i;
	}

	// Direct TCP players are not announced through IQNet join callbacks, so they may not
	// have a flag record yet. Ensure one exists before attempting to set any flags.
	bool hasEntry = false;
	for( unsigned int i = 0; i < m_playerFlags.size(); i++ )
	{
		if( m_playerFlags[i]->m_pNetworkPlayer == pNetworkPlayer )
		{
			hasEntry = true;
			break;
		}
	}
	if( !hasEntry )
	{
		SystemFlagAddPlayer(pNetworkPlayer);
	}

	for( unsigned int i = 0; i < m_playerFlags.size(); i++ )
	{
		if( pNetworkPlayer->IsSameSystem(m_playerFlags[i]->m_pNetworkPlayer) )
		{
			m_playerFlags[i]->flags[ index / 8 ] |= ( 128 >> ( index % 8 ) );
		}
	}
}

// Get value of a per system flag - can be read from the flags of the passed in player as anything else sent to that
// system should also have been duplicated here
bool CPlatformNetworkManagerStub::SystemFlagGet(INetworkPlayer *pNetworkPlayer, int index)
{
	if( ( index < 0 ) || ( index >= m_flagIndexSize ) ) return false;
	if( pNetworkPlayer == NULL )
	{
		return false;
	}

	for( unsigned int i = 0; i < m_playerFlags.size(); i++ )
	{
		if( m_playerFlags[i]->m_pNetworkPlayer == pNetworkPlayer )
		{
			return ( ( m_playerFlags[i]->flags[ index / 8 ] & ( 128 >> ( index % 8 ) ) ) != 0 );
		}
	}
	return false;
}

wstring CPlatformNetworkManagerStub::GatherStats()
{
	return L"";
}

wstring CPlatformNetworkManagerStub::GatherRTTStats()
{
	wstring stats(L"Rtt: ");

	wchar_t stat[32];

	for(unsigned int i = 0; i < GetPlayerCount(); ++i)
	{
		INetworkPlayer *networkPlayer = GetPlayerByIndex(i);
		if(networkPlayer == NULL)
		{
			continue;
		}
		IQNetPlayer *pQNetPlayer = m_pIQNet->GetPlayerBySmallId(networkPlayer->GetSmallId());
		if(pQNetPlayer == NULL)
		{
			continue;
		}

		if(!pQNetPlayer->IsLocal())
		{
			ZeroMemory(stat,32*sizeof(WCHAR));
			swprintf(stat, 32, L"%d: %d/", i, pQNetPlayer->GetCurrentRtt() );
			stats.append(stat);
		}
	}
	return stats;
}

void CPlatformNetworkManagerStub::TickSearch()
{
}

void CPlatformNetworkManagerStub::SearchForGames()
{
}

int CPlatformNetworkManagerStub::SearchForGamesThreadProc( void* lpParameter )
{
	return 0;
}

void CPlatformNetworkManagerStub::SetSearchResultsReady(int resultCount)
{
	m_bSearchResultsReady = true;
	m_searchResultsCount[m_lastSearchPad] = resultCount;
}

vector<FriendSessionInfo *> *CPlatformNetworkManagerStub::GetSessionList(int iPad, int localPlayers, bool partyOnly)
{
	vector<FriendSessionInfo *> *filteredList = new vector<FriendSessionInfo *>();;
	return filteredList;
}

bool CPlatformNetworkManagerStub::GetGameSessionInfo(int iPad, SessionID sessionId, FriendSessionInfo *foundSessionInfo)
{
	return false;
}

void CPlatformNetworkManagerStub::SetSessionsUpdatedCallback( void (*SessionsUpdatedCallback)(LPVOID pParam), LPVOID pSearchParam )
{
	m_SessionsUpdatedCallback = SessionsUpdatedCallback; m_pSearchParam = pSearchParam;
}

void CPlatformNetworkManagerStub::GetFullFriendSessionInfo( FriendSessionInfo *foundSession, void (* FriendSessionUpdatedFn)(bool success, void *pParam), void *pParam )
{
	FriendSessionUpdatedFn(true, pParam);
}

void CPlatformNetworkManagerStub::ForceFriendsSessionRefresh()
{
	app.DebugPrintf("Resetting friends session search data\n");
	
	for(unsigned int i = 0; i < XUSER_MAX_COUNT; ++i)
	{
		m_searchResultsCount[i] = 0;
		m_lastSearchStartTime[i] = 0;
		delete m_pSearchResults[i];
		m_pSearchResults[i] = NULL;
	}
}

INetworkPlayer *CPlatformNetworkManagerStub::addNetworkPlayer(IQNetPlayer *pQNetPlayer)
{
	NetworkPlayerXbox *pNetworkPlayer = new NetworkPlayerXbox(pQNetPlayer);
	pQNetPlayer->SetCustomDataValue((ULONG_PTR)pNetworkPlayer);
	currentNetworkPlayers.push_back( pNetworkPlayer );
	return pNetworkPlayer;
}

void CPlatformNetworkManagerStub::removeNetworkPlayer(IQNetPlayer *pQNetPlayer)
{
	INetworkPlayer *pNetworkPlayer = getNetworkPlayer(pQNetPlayer);
	for( AUTO_VAR(it, currentNetworkPlayers.begin()); it != currentNetworkPlayers.end(); it++ )
	{
		if( *it == pNetworkPlayer )
		{
			currentNetworkPlayers.erase(it);
			return;
		}
	}
}

INetworkPlayer *CPlatformNetworkManagerStub::getNetworkPlayer(IQNetPlayer *pQNetPlayer)
{
	return pQNetPlayer ? (INetworkPlayer *)(pQNetPlayer->GetCustomDataValue()) : NULL;
}


INetworkPlayer *CPlatformNetworkManagerStub::GetLocalPlayerByUserIndex(int userIndex )
{
	IQNetPlayer *qnetPlayer = m_pIQNet->GetLocalPlayerByUserIndex(userIndex);
	INetworkPlayer *player = getNetworkPlayer(qnetPlayer);
	if(player == NULL && qnetPlayer != NULL)
	{
		player = addNetworkPlayer(qnetPlayer);
	}
	return player;
}

INetworkPlayer *CPlatformNetworkManagerStub::GetPlayerByIndex(int playerIndex)
{
	std::vector<INetworkPlayer *> players;

	const int qnetCount = m_pIQNet->GetPlayerCount();
	for(int i = 0; i < qnetCount; ++i)
	{
		IQNetPlayer *qnetPlayer = m_pIQNet->GetPlayerByIndex(i);
		INetworkPlayer *player = getNetworkPlayer(qnetPlayer);
		if(player == NULL && qnetPlayer != NULL)
		{
			player = addNetworkPlayer(qnetPlayer);
		}
		AppendUniquePlayer(players, player);
	}

	const int directCount = Socket::GetDirectPlayerCount();
	for(int i = 0; i < directCount; ++i)
	{
		AppendUniquePlayer(players, Socket::GetDirectPlayerByIndex(i));
	}

	if(playerIndex < 0 || playerIndex >= (int)players.size())
	{
		return NULL;
	}

	return players[playerIndex];
}

INetworkPlayer * CPlatformNetworkManagerStub::GetPlayerByXuid(PlayerUID xuid)
{
	IQNetPlayer *qnetPlayer = m_pIQNet->GetPlayerByXuid(xuid);
	INetworkPlayer *player = getNetworkPlayer(qnetPlayer);
	if(player == NULL && qnetPlayer != NULL)
	{
		player = addNetworkPlayer(qnetPlayer);
	}
	if(player != NULL)
	{
		return player;
	}

	const int directCount = Socket::GetDirectPlayerCount();
	for(int i = 0; i < directCount; ++i)
	{
		INetworkPlayer *directPlayer = Socket::GetDirectPlayerByIndex(i);
		if(directPlayer != NULL && ProfileManager.AreXUIDSEqual(directPlayer->GetUID(), xuid))
		{
			return directPlayer;
		}
	}
	return NULL;
}

INetworkPlayer * CPlatformNetworkManagerStub::GetPlayerBySmallId(unsigned char smallId)
{
	IQNetPlayer *qnetPlayer = m_pIQNet->GetPlayerBySmallId(smallId);
	INetworkPlayer *player = getNetworkPlayer(qnetPlayer);
	if(player == NULL && qnetPlayer != NULL)
	{
		player = addNetworkPlayer(qnetPlayer);
	}
	if(player != NULL && player->GetSmallId() == smallId)
	{
		return player;
	}

	return Socket::GetDirectPlayerBySmallId(smallId);
}

INetworkPlayer *CPlatformNetworkManagerStub::GetHostPlayer()
{
	const int directCount = Socket::GetDirectPlayerCount();
	for(int i = 0; i < directCount; ++i)
	{
		INetworkPlayer *directPlayer = Socket::GetDirectPlayerByIndex(i);
		if(directPlayer != NULL && directPlayer->IsHost())
		{
			return directPlayer;
		}
	}

	IQNetPlayer *qnetPlayer = m_pIQNet->GetHostPlayer();
	INetworkPlayer *player = getNetworkPlayer(qnetPlayer);
	if(player == NULL && qnetPlayer != NULL)
	{
		player = addNetworkPlayer(qnetPlayer);
	}
	return player;
}

bool CPlatformNetworkManagerStub::IsHost()
{
	return m_pIQNet->IsHost() && !m_bHostChanged;
}

bool CPlatformNetworkManagerStub::JoinGameFromInviteInfo( int userIndex, int userMask, const INVITE_INFO *pInviteInfo)
{
	return ( m_pIQNet->JoinGameFromInviteInfo( userIndex, userMask, pInviteInfo ) == S_OK);
}

void CPlatformNetworkManagerStub::SetSessionTexturePackParentId( int id )
{
	m_hostGameSessionData.texturePackParentId = id;
}

void CPlatformNetworkManagerStub::SetSessionSubTexturePackId( int id )
{
	m_hostGameSessionData.subTexturePackId = id;
}

void CPlatformNetworkManagerStub::Notify(int ID, ULONG_PTR Param)
{
}

bool CPlatformNetworkManagerStub::IsInSession()
{
	return m_pIQNet->GetState() != QNET_STATE_IDLE;
}

bool CPlatformNetworkManagerStub::IsInGameplay()
{
	return m_pIQNet->GetState() == QNET_STATE_GAME_PLAY;
}

bool CPlatformNetworkManagerStub::IsReadyToPlayOrIdle()
{
	return true;
}
