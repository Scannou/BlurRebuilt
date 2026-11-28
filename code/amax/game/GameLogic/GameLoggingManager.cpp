#include "stdafx.h"

#include <GameLogic/GameLoggingManager.h>

#include <Game/Game.h>
#include <GameApp.h>
#include <GameLogic/Layers/RaceBookLayer.h>
#include <GameLogic/Layers/AmaxWorldLayer.h>
#include <GameLogic/RaceLogicCore/Core/RaceLogicCore.h>

#include <GameLogic\ContentServer\RecordEventManager.h>

#include <Game\gameactors\vehiclelogic\VehicleLogicActor.h>

#include <GameLogic/Structures/LevelResult.h>
#include <GameLogic/Profile/GameProfileManager.h>

#include <Racebook\RaceBookManager.h>

#include <Ui\game\Multiplayer\MultiplayerCache.h>
#include <GameLogic\Structures\MpResults.h>

#include <system/serialise/C_FileSystem.h>
#include <system/serialise/TextSerialise.h>

#include <Ui\game\Multiplayer\MultiplayerDefines.h>
#include "bdLobby\bdLobbyCommon\bdAuthCommon.h"

#if defined ( PLATFORM_PC )
// [Blur2PC] race events
#include <GameLogic/GameData/GameData.h>
#include <GameLogic/GameData/GameVehicleData.h>
#include <GameLogic/GameModes/GameModeBase.h>
#include <GameLogic/RaceModes/RaceModeDefines.h>
#include <Network/Core/NetServices.h>
#include <Network/Player/NetPlayer.h>
#include <Network/Player/NetPlayerConnection.h>
#include <Network/Player/NetPlayerManager.h>
#include <Network/Player/NetPlayerStates.h>
#include <float.h>
#include <ui/Game/SaveSystem/SaveSystem.h>
#include <ws2tcpip.h>
#endif

Debug::Logger &g_DemonwareGameLogging( Debug::GetLogger("Demonware.GameLogging") );

namespace Game
{

/*
	These check that the data structures we upload are multiples of 4 bytes. This ensures that
	when stored in memory there's no risk of writing floats to addresses that span a cache line
	(data misalignment crash)
*/
BIZ_STATIC_ASSERT((sizeof(AmaxGameLogHeader) % 4) == 0 );
BIZ_STATIC_ASSERT((sizeof(AmaxGameVehicleLog) % 4) == 0 );

C_GameLoggingManager C_GameLoggingManager::s_instance;

void C_GameLoggingManager::DebugExportEnumNames()
{
}

#if defined ( PLATFORM_PC )
// [Blur2PC] Not retail: LAN and online races write their events as JSON lines (one object per line) to
// race_events.jsonl in the profile save folder (%APPDATA%\Bizarre Creations\Blur\, or BLUR2PC_SAVE_ROOT as in
// SaveSystem_pc.cpp), so other tools can follow races. BLUR_RACE_EVENTS=off turns this off; any other value is
// the file path to use. Every line has "time" (local), "race" (id of the race), "t" (seconds since the race
// started) and "event". Players are {"index","name"} with the race's vehicle index. Each PC writes what it
// sees: events about its own car are complete, events between other cars depend on what this PC is told.
static const char* const s_RaceEventModeNames[] =
{
	"none", "street_race", "racing", "elimination", "points", "motor_mash",
	"destruction", "checkpoint", "fan_demand", "boss_battle",
	"friend_street_race", "friend_destruction", "friend_checkpoint", "friend_fan_demand", "friend_boss_battle",
};
BIZ_STATIC_ASSERT( sizeof( s_RaceEventModeNames ) / sizeof( s_RaceEventModeNames[ 0 ] ) == E_RaceMode_MAX );

static const char* const s_RaceEventPickupNames[] =
{
	"nitro", "shock", "shunt", "bolt", "barge", "mine", "repair", "shield", "fan", "stopwatch", "super_shield", "all",
};
BIZ_STATIC_ASSERT( sizeof( s_RaceEventPickupNames ) / sizeof( s_RaceEventPickupNames[ 0 ] ) == E_PICKUP_TYPE_MAX );

static const char* const s_RaceEventStateNames[] =
{
	"unknown", "racing", "finished", "timed_out", "left_race", "eliminated", "wrecked", "waiting_to_start",
};
BIZ_STATIC_ASSERT( sizeof( s_RaceEventStateNames ) / sizeof( s_RaceEventStateNames[ 0 ] ) == E_VEHICLE_STATE_MAX );

// UI::E_MultiplayerStat order
static const char* const s_RaceEventStatNames[] =
{
	"race_time", "fastest_lap", "slowest_lap", "fans", "health_remaining", "logic_position", "distance_travelled",
	"laps_completed", "on_track_points", "elimination_points", "powerup_hits", "hits_by_powerups", "kill_streak",
	"death_streak", "accuracy", "longest_chain", "powerups_evaded", "top_speed_mph", "car_to_car_hits",
	"car_to_car_victim_hits", "environmental_crashes", "pickups_collected", "longest_drift", "times_in_water",
	"times_off_edge", "damage_dealt", "longest_time_without_being_wrecked", "cars_wrecked", "times_hit",
	"mines_fragmented_with_mastermine", "time_invisible_with_silent_running", "powerups_absorbed_with_safety_net",
	"players_scrambled", "players_hit_with_decoys", "cars_battering_rammed", "times_protected_by_stable_frame",
	"players_hit_with_iron_fist", "powerups_dropped", "longest_time_not_damaged", "time_in_first_place",
	"longest_jump", "players_hit_with_barge", "nitro_overtakes", "blocked_powerups", "powerups_shielded",
	"players_hit_with_mine", "players_hit_with_shunt", "players_hit_with_bolt",
};
BIZ_STATIC_ASSERT( sizeof( s_RaceEventStatNames ) / sizeof( s_RaceEventStatNames[ 0 ] ) == UI::E_MultiplayerStat_Count );

static char		s_RaceEventRaceId[ 32 ]	= "";
static U64		s_RaceEventStartMs		= 0;
static U32		s_RaceEventFinishedMask	= 0;	// vehicles with a finish event this race
static S32		s_RaceEventFinishOrder	= 0;	// vehicles that crossed the line so far
static S32		s_RaceEventLaps[ 32 ];			// laps completed per vehicle this race
static S32		s_RaceEventLastHitBy[ 32 ];		// per vehicle: who last hit it with a power-up (-1: nobody)
static U64		s_RaceEventLastHitMs[ 32 ];
static U64		s_RaceEventLastWreckMs[ 32 ];	// per vehicle: when its last wreck was written (0: none)
struct S_RaceEventPendingWreck { S32 attacker; const char* by; U64 ms; };	// ms 0: none
static S_RaceEventPendingWreck s_RaceEventPendingWrecks[ 32 ];
static const U64 kRaceEventWreckCreditWaitMs = 1000;
static const S32 kRaceEventRecentHits	= 64;
static U32		s_RaceEventRecentHits[ kRaceEventRecentHits ];	// power-up id and hit vehicle already written
static S32		s_RaceEventRecentHitNext = 0;

static const char* RaceEventName( const char* const* pNames, const S32 count, const S32 value )
{
	return ( value >= 0 && value < count ) ? pNames[ value ] : "unknown";
}

// Where the events go, read at every race start so BlurRebuilt.ini can be changed between races. The file comes
// from "race_events_file" in BlurRebuilt.ini, else BLUR_RACE_EVENTS: "off" (or "0") stops it, any other value is
// the path; default race_events.jsonl beside the profile save. "race_events_udp" (else BLUR_RACE_EVENTS_UDP) set to
// host:port also sends every event line, without the line break, as one UDP datagram the moment it happens, for
// live use such as room lights. The host can be a name, an address or a broadcast address (e.g. 192.168.1.255).
static bool		s_RaceEventsFileOn = true;
static char		s_RaceEventsPath[ MAX_PATH ] = "";

static char				s_RaceEventsUdpTarget[ 256 ] = "";
static SOCKET			s_RaceEventsUdpSocket = INVALID_SOCKET;
static sockaddr_in		s_RaceEventsUdpAddress;
static volatile LONG	s_RaceEventsUdpReady = 0;		// 1 once s_RaceEventsUdpAddress holds the resolved target
static volatile LONG	s_RaceEventsUdpGeneration = 0;	// bumped when the target changes; stale lookups are dropped

static bool GetRaceEventsSetting( const char* pIniKey, const char* pEnvName, char* pValue, const size_t uSize )
{
	if ( UI::GetBlurRebuiltSetting( pIniKey, pValue, uSize ) )
		return true;

	const char* pEnv = getenv( pEnvName );
	if ( !pEnv )
		return false;
	strncpy_s( pValue, uSize, pEnv, _TRUNCATE );
	return true;
}

struct RaceEventsUdpLookup
{
	char	mHost[ 256 ];
	char	mPort[ 16 ];
	LONG	mGeneration;
};

// Name lookups can take seconds, so they run off the game thread; events are only sent once it has finished.
static DWORD WINAPI ResolveRaceEventsUdpTarget( LPVOID pParam )
{
	RaceEventsUdpLookup* pLookup = (RaceEventsUdpLookup*)pParam;

	addrinfo hints;
	memset( &hints, 0, sizeof( hints ) );
	hints.ai_family		= AF_INET;
	hints.ai_socktype	= SOCK_DGRAM;
	hints.ai_protocol	= IPPROTO_UDP;

	addrinfo* pResult = NULL;
	if ( getaddrinfo( pLookup->mHost, pLookup->mPort, &hints, &pResult ) == 0 && pResult )
	{
		if ( pLookup->mGeneration == s_RaceEventsUdpGeneration && pResult->ai_addrlen <= sizeof( s_RaceEventsUdpAddress ) )
		{
			memcpy( &s_RaceEventsUdpAddress, pResult->ai_addr, pResult->ai_addrlen );
			InterlockedExchange( &s_RaceEventsUdpReady, 1 );
		}
		freeaddrinfo( pResult );
	}

	delete pLookup;
	return 0;
}

static void UpdateRaceEventsOutput()
{
	char value[ MAX_PATH ] = "";
	s_RaceEventsFileOn = true;
	if ( GetRaceEventsSetting( "race_events_file", "BLUR_RACE_EVENTS", value, sizeof( value ) ) && value[ 0 ] )
	{
		if ( _stricmp( value, "off" ) == 0 || strcmp( value, "0" ) == 0 )
			s_RaceEventsFileOn = false;
		else
			strcpy_s( s_RaceEventsPath, value );
	}
	else
	{
		UI::GetBlurRebuiltFilePath( "race_events.jsonl", s_RaceEventsPath, sizeof( s_RaceEventsPath ) );
	}

	char target[ 256 ] = "";
	GetRaceEventsSetting( "race_events_udp", "BLUR_RACE_EVENTS_UDP", target, sizeof( target ) );
	if ( strcmp( target, s_RaceEventsUdpTarget ) == 0 )
		return;

	strcpy_s( s_RaceEventsUdpTarget, target );
	InterlockedExchange( &s_RaceEventsUdpReady, 0 );
	const LONG generation = InterlockedIncrement( &s_RaceEventsUdpGeneration );

	char* pPort = strrchr( target, ':' );
	if ( !pPort || pPort == target || !pPort[ 1 ] )
		return;
	*pPort++ = '\0';

	if ( s_RaceEventsUdpSocket == INVALID_SOCKET )
	{
		WSADATA wsaData;
		if ( WSAStartup( MAKEWORD( 2, 2 ), &wsaData ) != 0 )
			return;

		s_RaceEventsUdpSocket = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
		if ( s_RaceEventsUdpSocket == INVALID_SOCKET )
			return;

		BOOL broadcast = TRUE;
		setsockopt( s_RaceEventsUdpSocket, SOL_SOCKET, SO_BROADCAST, (const char*)&broadcast, sizeof( broadcast ) );
	}

	RaceEventsUdpLookup* pLookup = new RaceEventsUdpLookup;
	strcpy_s( pLookup->mHost, target );
	strncpy_s( pLookup->mPort, pPort, _TRUNCATE );
	pLookup->mGeneration = generation;

	HANDLE thread = CreateThread( NULL, 0, ResolveRaceEventsUdpTarget, pLookup, 0, NULL );
	if ( thread )
		CloseHandle( thread );
	else
		delete pLookup;
}

static void SendRaceEventUdp( const char* pText, const size_t length )
{
	if ( s_RaceEventsUdpReady && s_RaceEventsUdpSocket != INVALID_SOCKET )
		sendto( s_RaceEventsUdpSocket, pText, (int)length, 0, (const sockaddr*)&s_RaceEventsUdpAddress, sizeof( s_RaceEventsUdpAddress ) );
}

// Once this PC starts leaving the race (NetRace.EnterDisconnecting), every other car is dropped as "left race" on
// its way out; that is not something that happened in the race, so nothing more is written.
static bool LocalPlayerLeavingRace()
{
	RacerNet::C_NetServices& services = RacerNet::C_NetServices::Instance();
	if ( !services.IsInitialised() )
		return false;

	RacerNet::C_NetPlayerManager& mgr = services.PlayerManager();
	for ( U8 i = 0; i < mgr.LocalSize(); i++ )
	{
		const RacerNet::C_NetPlayer* p = mgr.Index( i );
		if ( p && p->LocalPlayer() && p->m_RaceConnection.m_PlayerState == RacerNet::eNetPlayerStateDisconnecting )
			return true;
	}
	return false;
}

static bool RaceEventsActive()
{
	C_RaceBookLayer* pLayer = C_GameApp::GetInstance()->GetRaceBookLayer();
	return s_RaceEventRaceId[ 0 ] && pLayer && pLayer->GetGameModePtr() && pLayer->GetGameModePtr()->IsNetwork() && !LocalPlayerLeavingRace();
}

// One JSON object, written as one line; text that does not fit is dropped rather than cut mid-value.
class C_RaceEventLine
{
public:
	// atMs: when it happened (GetTickCount64), for an event written after a short wait; 0 for now
	C_RaceEventLine( const char* pEvent, const U64 atMs = 0 ) : m_Length( 0 ), m_NeedComma( false ), m_Overflow( false )
	{
		SYSTEMTIME now;
		GetLocalTime( &now );

		char time[ 32 ];
		sprintf_s( time, "%04u-%02u-%02uT%02u:%02u:%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond );

		Append( "{" );
		Str( "time", time );
		Str( "race", s_RaceEventRaceId );
		Float( "t", (F32)( ( atMs ? atMs : GetTickCount64() ) - s_RaceEventStartMs ) / 1000.0f );
		Str( "event", pEvent );
	}

	void Int( const char* pKey, const S32 value )
	{
		char text[ 16 ];
		sprintf_s( text, "%d", value );
		Key( pKey );
		Append( text );
	}

	void Float( const char* pKey, const F32 value )
	{
		char text[ 32 ];
		sprintf_s( text, "%.3f", _finite( value ) ? value : 0.0f );
		Key( pKey );
		Append( text );
	}

	void Str( const char* pKey, const char* pValue )
	{
		Key( pKey );
		Append( "\"" );
		for ( const char* p = pValue; p && *p; p++ )
		{
			char escaped[ 8 ];
			if ( *p == '"' || *p == '\\' )
				sprintf_s( escaped, "\\%c", *p );
			else if ( (U8)*p < 0x20 )
				sprintf_s( escaped, "\\u%04x", (U8)*p );
			else
				sprintf_s( escaped, "%c", *p );
			Append( escaped );
		}
		Append( "\"" );
	}

	void WideStr( const char* pKey, const WCHAR* pValue )
	{
		char utf8[ 256 ] = { 0 };
		if ( pValue )
			WideCharToMultiByte( CP_UTF8, 0, pValue, -1, utf8, sizeof( utf8 ) - 1, NULL, NULL );
		Str( pKey, utf8 );
	}

	void Player( const char* pKey, const S32 vehicleIndex )
	{
		const Game::C_LevelState& levelState = C_Game::GetInstance()->GetLevelState();

		BeginObject( pKey );
		Int( "index", vehicleIndex );
		if ( vehicleIndex >= 0 && vehicleIndex < (S32)levelState.mNumCars )
		{
			Int( "id", levelState.mCars[ vehicleIndex ].mNetJoinId );	// the same on every PC, unlike the index
			WideStr( "name", levelState.mCars[ vehicleIndex ].mNameAboveCar.Chars() );
		}
		EndObject();
	}

	void BeginObject( const char* pKey )
	{
		Key( pKey );
		Append( "{" );
		m_NeedComma = false;
	}

	void EndObject()
	{
		Append( "}" );
		m_NeedComma = true;
	}

	void Write()
	{
		Append( "}\n" );

		if ( m_Overflow )
			return;

		SendRaceEventUdp( m_Text, m_Length - 1 );

		if ( !s_RaceEventsFileOn )
			return;

		FILE* pFile = NULL;
		if ( fopen_s( &pFile, s_RaceEventsPath, "ab" ) == 0 && pFile )
		{
			fwrite( m_Text, 1, m_Length, pFile );
			fclose( pFile );
		}
	}

private:
	void Key( const char* pKey )
	{
		if ( m_NeedComma )
			Append( "," );
		Append( "\"" );
		Append( pKey );
		Append( "\":" );
		m_NeedComma = true;
	}

	void Append( const char* pText )
	{
		const size_t length = strlen( pText );
		if ( m_Length + length >= sizeof( m_Text ) )
		{
			m_Overflow = true;
			return;
		}
		memcpy( m_Text + m_Length, pText, length + 1 );
		m_Length += length;
	}

	char	m_Text[ 4096 ];
	size_t	m_Length;
	bool	m_NeedComma;
	bool	m_Overflow;
};

static const char* RaceEventCarTag( const U32 carId )
{
	const AmaxVehicle* pVehicle = C_GameVehicleData::Instance().mVehicles[ (GAMEDATA_ID)carId ];
	return pVehicle ? pVehicle->tag.Text() : "";
}

void C_GameLoggingManager::RaceEventRaceStart()
{
	SYSTEMTIME now;
	GetLocalTime( &now );
	sprintf_s( s_RaceEventRaceId, "%04u%02u%02u-%02u%02u%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond );
	s_RaceEventStartMs = GetTickCount64();
	UpdateRaceEventsOutput();
	s_RaceEventFinishedMask = 0;
	s_RaceEventFinishOrder = 0;
	memset( s_RaceEventLaps, 0, sizeof( s_RaceEventLaps ) );
	memset( s_RaceEventLastHitBy, 0xff, sizeof( s_RaceEventLastHitBy ) );
	memset( s_RaceEventLastWreckMs, 0, sizeof( s_RaceEventLastWreckMs ) );
	memset( s_RaceEventPendingWrecks, 0, sizeof( s_RaceEventPendingWrecks ) );
	memset( s_RaceEventRecentHits, 0xff, sizeof( s_RaceEventRecentHits ) );

	if ( !RaceEventsActive() )
		return;

	const Game::C_LevelState& levelState = C_Game::GetInstance()->GetLevelState();
	const AmaxCity* pCity = C_GameData::Instance().mCities[ (GAMEDATA_ID)levelState.mCity ];
	const AmaxRoute* pRoute = C_GameData::Instance().mRoutes[ (GAMEDATA_ID)levelState.mRoute ];

	C_RaceEventLine line( "race_start" );
	line.Str( "mode", RaceEventName( s_RaceEventModeNames, E_RaceMode_MAX, levelState.mRaceMode ) );
	line.Str( "city", pCity ? pCity->tag.Text() : "" );
	line.Str( "route", pRoute ? pRoute->tag.Text() : "" );
	line.Int( "laps", (S32)levelState.mLaps );
	line.Int( "time_limit", levelState.mTimeLimit );
	line.Int( "players", (S32)levelState.mNumCars );
	line.Player( "logged_by", 0 );
	line.Write();

	for ( U32 i = 0; i < levelState.mNumCars; i++ )
	{
		C_RaceEventLine player( "player" );
		player.Player( "player", (S32)i );
		player.Int( "team", levelState.mCars[ i ].mTeam );
		player.Str( "car", RaceEventCarTag( levelState.mCars[ i ].mCarID ) );
		player.Int( "start_position", (S32)levelState.mCars[ i ].mStartPosition + 1 );
		player.Write();
	}
}

// Any car crossing the start/finish line to complete a lap ("lap" counts its completed laps this race). The race
// logic tracks the line for every car on every PC.
void C_GameLoggingManager::RaceEventLap( const U8 vehicleIndex, const F32 lapTime )
{
	if ( !RaceEventsActive() || vehicleIndex >= 32 || lapTime <= 1.0f )	// not a lap: the grid crossing at the start
		return;

	C_RaceEventLine line( "lap" );
	line.Player( "player", vehicleIndex );
	line.Int( "lap", ++s_RaceEventLaps[ vehicleIndex ] );
	line.Float( "lap_time", lapTime );
	line.Write();
}

void C_GameLoggingManager::RaceEventOvertake( const U8 overtakerIndex, const U8 overtakenIndex )
{
	if ( !RaceEventsActive() )
		return;

	C_RaceEventLine line( "overtake" );
	line.Player( "player", overtakerIndex );
	line.Player( "overtaken", overtakenIndex );
	line.Write();
}

// A power-up hitting any car, reported by the hit car's damage component (local cars and the network copies of
// remote cars both get the hit). One event per power-up and hit car.
void C_GameLoggingManager::RaceEventPowerupHit( const U8 firingIndex, const U8 hitIndex, const S32 pickupType, const S32 level, const U32 pickupId )
{
	if ( !RaceEventsActive() || hitIndex >= 32 )
		return;

	const U32 key = ( pickupId << 5 ) ^ hitIndex;
	for ( S32 i = 0; i < kRaceEventRecentHits; i++ )
	{
		if ( s_RaceEventRecentHits[ i ] == key )
			return;
	}
	s_RaceEventRecentHits[ s_RaceEventRecentHitNext ] = key;
	s_RaceEventRecentHitNext = ( s_RaceEventRecentHitNext + 1 ) % kRaceEventRecentHits;

	if ( firingIndex < 32 && firingIndex != hitIndex )
	{
		s_RaceEventLastHitBy[ hitIndex ] = firingIndex;
		s_RaceEventLastHitMs[ hitIndex ] = GetTickCount64();
	}

	C_RaceEventLine line( "powerup_hit" );
	line.Player( "player", firingIndex );
	line.Player( "hit", hitIndex );
	line.Str( "powerup", RaceEventName( s_RaceEventPickupNames, E_PICKUP_TYPE_MAX, pickupType ) );
	line.Int( "level", level + 1 );
	line.Write();
}

static void WriteRaceEventWreck( const S32 attacker, const U8 wreckedIndex, const char* pBy, const U64 atMs )
{
	s_RaceEventLastWreckMs[ wreckedIndex ] = atMs;

	C_RaceEventLine line( "wreck", atMs );
	if ( attacker >= 0 )
		line.Player( "player", attacker );
	line.Player( "wrecked", wreckedIndex );
	line.Str( "by", pBy );
	line.Write();
}

// A wreck the game credits to a local car names that attacker ("by": "attacker"). A local car's own wreck names
// the car that last hit it ("last_hit"), or nobody ("self"). A remote car's wreck seen only as its health running
// out is credited to the last power-up hit within 5 seconds ("last_hit") or to nobody ("unknown"). The wrecked car
// often reports before the game's credit arrives, so its report waits up to a second (RaceEventUpdate) for the
// credit, which replaces it; a car's first wreck written within 3 seconds is the only one.
void C_GameLoggingManager::RaceEventWreck( const S32 attackerIndex, const U8 wreckedIndex, const bool remote, const bool lastHit )
{
	if ( !RaceEventsActive() || wreckedIndex >= 32 )
		return;

	const U64 now = GetTickCount64();
	S_RaceEventPendingWreck& pending = s_RaceEventPendingWrecks[ wreckedIndex ];
	const bool recentlyWritten = s_RaceEventLastWreckMs[ wreckedIndex ] != 0 && now - s_RaceEventLastWreckMs[ wreckedIndex ] < 3000;

	if ( attackerIndex >= 0 && !remote && !lastHit )
	{
		if ( pending.ms )
		{
			WriteRaceEventWreck( attackerIndex, wreckedIndex, "attacker", pending.ms );
			pending.ms = 0;
		}
		else if ( !recentlyWritten )
			WriteRaceEventWreck( attackerIndex, wreckedIndex, "attacker", now );
		return;
	}

	if ( pending.ms || recentlyWritten )
		return;

	S32 attacker = attackerIndex;
	const char* pBy = ( attacker < 0 ) ? "self" : "last_hit";
	if ( remote )
	{
		const bool recent = s_RaceEventLastHitBy[ wreckedIndex ] >= 0 && now - s_RaceEventLastHitMs[ wreckedIndex ] <= 5000;
		attacker = recent ? s_RaceEventLastHitBy[ wreckedIndex ] : -1;
		pBy = recent ? "last_hit" : "unknown";
	}

	pending.attacker = attacker;
	pending.by = pBy;
	pending.ms = now;
}

// Writes the wrecked cars' reports that waited for the game's credit long enough (all of them with force).
void C_GameLoggingManager::RaceEventUpdate( const bool force )
{
	if ( !s_RaceEventRaceId[ 0 ] )
		return;

	const U64 now = GetTickCount64();
	for ( U8 i = 0; i < 32; i++ )
	{
		S_RaceEventPendingWreck& pending = s_RaceEventPendingWrecks[ i ];
		if ( !pending.ms || ( !force && now - pending.ms < kRaceEventWreckCreditWaitMs ) )
			continue;
		if ( RaceEventsActive() )
			WriteRaceEventWreck( pending.attacker, i, pending.by, pending.ms );
		pending.ms = 0;
	}
}

// The first time a vehicle stops racing: crossed the line ("finished", with its place in the finishing order),
// timed out, eliminated, wrecked out or left. For a remote car this is when its result data arrives.
void C_GameLoggingManager::RaceEventFinish( const U8 vehicleIndex, const S32 vehicleState, const F32 raceTime )
{
	if ( !RaceEventsActive() || vehicleIndex >= 32 || ( s_RaceEventFinishedMask & ( 1u << vehicleIndex ) ) )
		return;
	s_RaceEventFinishedMask |= 1u << vehicleIndex;

	C_RaceEventLine line( "finish" );
	line.Player( "player", vehicleIndex );
	line.Str( "state", RaceEventName( s_RaceEventStateNames, E_VEHICLE_STATE_MAX, vehicleState ) );
	if ( vehicleState == E_VEHICLE_STATE_FINISHED )
		line.Int( "finish_order", ++s_RaceEventFinishOrder );
	line.Float( "race_time", raceTime );
	line.Write();
}

void C_GameLoggingManager::RaceEventResults()
{
	if ( !RaceEventsActive() )
		return;

	RaceEventUpdate( true );

	const Game::C_LevelState& levelState = C_Game::GetInstance()->GetLevelState();

	for ( U32 i = 0; i < levelState.mNumCars; i++ )
	{
		NetVehicleData result;
		if ( !C_MpResults::Instance().GetPlayerResults( (U8)i, result ) )
			continue;

		C_RaceEventLine line( "result" );
		line.Player( "player", (S32)i );
		line.Int( "position", result.m_Position );
		line.Str( "state", RaceEventName( s_RaceEventStateNames, E_VEHICLE_STATE_MAX, result.m_State ) );
		line.Int( "team", levelState.mCars[ i ].mTeam );
		line.Str( "car", RaceEventCarTag( levelState.mCars[ i ].mCarID ) );
		line.Int( "start_position", (S32)levelState.mCars[ i ].mStartPosition + 1 );

		line.BeginObject( "stats" );
		for ( S32 s = 0; s < UI::E_MultiplayerStat_Count; s++ )
			line.Float( s_RaceEventStatNames[ s ], result.m_Statistics[ s ] );
		line.EndObject();

		line.Write();
	}

	C_RaceEventLine end( "race_end" );
	end.Write();
}
#endif

void C_GameLoggingManager::UploadGameLog()
{
	//	TO DO: This method really needs to only upload data about local vehicles, because we don't possess very many stats for remote vehicles.
	const Game::C_LevelState& levelState = C_Game::GetInstance()->GetLevelState();

	if ((levelState.mNumCars == 0) || (levelState.mCars[0].mController == INVALID_CONTROLLER))
		return;

	const Game::C_LevelResult& results=C_Game::GetInstance()->GetLevelResult();

	if (results.mFake)
		return;

	RaceBook::C_RaceBookProfile *rb_prof = RaceBook::C_RaceBookManager::Instance().GetPlayerProfile();
	C_GameProfile *game_prof=C_GameProfileManager::Instance().GetPrimaryProfile();

	Game::C_GameModeBase *pGameModeBase = C_GameApp::GetInstance()->GetRaceBookLayer()->GetGameModePtr();

	memset(&mGameLog, sizeof(AmaxGameLog), 0);

	//sort out the header

	mGameLog.mHeader.mVersion=AMAX_GAME_LOG_VERSION;
	LOGDEBUG(g_DemonwareGameLogging, "--- Starting Log Game (Version %d)", mGameLog.mHeader.mVersion);

	mGameLog.mHeader.mCommunityGroupID=UI::C_MultiplayerCache::Instance().GetCurrentEventID();
	LOGDEBUG(g_DemonwareGameLogging, "GameLog : Community Group ID %ld", mGameLog.mHeader.mCommunityGroupID);

	mGameLog.mHeader.mRaceMode=levelState.mRaceMode;
	LOGDEBUG(g_DemonwareGameLogging, "GameLog : RaceMode %d", mGameLog.mHeader.mRaceMode);
	
	mGameLog.mHeader.mCity=levelState.mCity;
	LOGDEBUG(g_DemonwareGameLogging, "GameLog : City %d", mGameLog.mHeader.mCity);

	mGameLog.mHeader.mRoute=levelState.mRoute;
	LOGDEBUG(g_DemonwareGameLogging, "GameLog : Route %d", mGameLog.mHeader.mRoute);

	mGameLog.mHeader.mNumCars=levelState.mNumCars;
	LOGDEBUG(g_DemonwareGameLogging, "GameLog : Num Cars %d", mGameLog.mHeader.mNumCars);

	mGameLog.mHeader.mLaps=levelState.mLaps;
	LOGDEBUG(g_DemonwareGameLogging, "GameLog : Laps %d", mGameLog.mHeader.mLaps);
	
	mGameLog.mHeader.mTimeLimit=levelState.mTimeLimit;
	LOGDEBUG(g_DemonwareGameLogging, "GameLog : Time Limit %d", mGameLog.mHeader.mTimeLimit);

	for(U32 a=0;a<mGameLog.mHeader.mNumCars;a++)
	{
		NetVehicleData result;
		C_MpResults::Instance().GetPlayerResults(a,result);

		LOGDEBUG(g_DemonwareGameLogging, "GameLog : Vehicle %d", a);

		wcsncpy(mGameLog.mVehicles[a].mCharacterString,levelState.mCars[a].mNameAboveCar,32);
		LOGDEBUG(g_DemonwareGameLogging, "GameLog : Vehicle %d : Name %S", a, mGameLog.mVehicles[a].mCharacterString);

#ifndef PLATFORM_PS3
		mGameLog.mVehicles[a].mSystemID=levelState.mCars[a].mSystemPlayerId.m_Uid;
		LOGDEBUG(g_DemonwareGameLogging, "GameLog : Vehicle %d : System ID %ld", a, mGameLog.mVehicles[a].mSystemID);
#else
		mGameLog.mVehicles[a].mSystemID=bdAuthUtility::getUserID(levelState.mCars[a].mSystemPlayerId.m_Uid.handle.data);
#endif

		mGameLog.mVehicles[a].mHealthRemaining=result.m_Statistics[UI::E_MultiplayerStat_HealthRemaining];
		LOGDEBUG(g_DemonwareGameLogging, "GameLog : Vehicle %d : Health Left %0.2f", a, mGameLog.mVehicles[a].mHealthRemaining);

		mGameLog.mVehicles[a].mDistanceTravelled=result.m_Statistics[UI::E_MultiplayerStat_DistanceTravelled];
		LOGDEBUG(g_DemonwareGameLogging, "GameLog : Vehicle %d : Distance Travelled %0.2f", a, mGameLog.mVehicles[a].mDistanceTravelled);

		if(levelState.mCars[a].mLoadOuts.Size() == 3)
		{
			mGameLog.mVehicles[a].mLoadOut1=levelState.mCars[a].mLoadOuts[0];
			LOGDEBUG(g_DemonwareGameLogging, "GameLog : Vehicle %d : Loadout 1 %d", a, mGameLog.mVehicles[a].mLoadOut1);
			mGameLog.mVehicles[a].mLoadOut2=levelState.mCars[a].mLoadOuts[1];
			LOGDEBUG(g_DemonwareGameLogging, "GameLog : Vehicle %d : Loadout 2 %d", a, mGameLog.mVehicles[a].mLoadOut2);
			mGameLog.mVehicles[a].mLoadOut3=levelState.mCars[a].mLoadOuts[2];
			LOGDEBUG(g_DemonwareGameLogging, "GameLog : Vehicle %d : Loadout 3 %d", a, mGameLog.mVehicles[a].mLoadOut3);
		}
		else
		{
			mGameLog.mVehicles[a].mLoadOut1=-1;
			mGameLog.mVehicles[a].mLoadOut2=-1;
			mGameLog.mVehicles[a].mLoadOut3=-1;
		}

		mGameLog.mVehicles[a].mRank=levelState.mCars[a].mMultiplayerRank;
		LOGDEBUG(g_DemonwareGameLogging, "GameLog : Vehicle %d : Rank %d", a, mGameLog.mVehicles[a].mRank);

		mGameLog.mVehicles[a].mLegendLevel=levelState.mCars[a].mMultiplayerLegendLevel;
		LOGDEBUG(g_DemonwareGameLogging, "GameLog : Vehicle %d : Legend Level %d", a, mGameLog.mVehicles[a].mLegendLevel);

		mGameLog.mVehicles[a].mCarID=levelState.mCars[a].mCarID;
		LOGDEBUG(g_DemonwareGameLogging, "GameLog : Vehicle %d : CarID %d", a, mGameLog.mVehicles[a].mCarID);

		mGameLog.mVehicles[a].mManufacturerID=levelState.mCars[a].mManufacturerID;
		LOGDEBUG(g_DemonwareGameLogging, "GameLog : Vehicle %d : ManufacturerID %d", a, mGameLog.mVehicles[a].mManufacturerID);

		mGameLog.mVehicles[a].mTotalFansGained=result.m_Statistics[UI::E_MultiplayerStat_Fans];
		LOGDEBUG(g_DemonwareGameLogging, "GameLog : Vehicle %d : Total Fans Gained %d", a, mGameLog.mVehicles[a].mTotalFansGained);

		mGameLog.mVehicles[a].mNumPowerups=E_PICKUP_TYPE_MAX;
		for(int b=0;b<E_PICKUP_TYPE_MAX;b++)
		{
			mGameLog.mVehicles[a].mPowerupsUsed[b]= 0;//results.mCars[a].mStatistics.mNumPerksFired[b];
			LOGDEBUG(g_DemonwareGameLogging, "GameLog : Vehicle %d : Power Up %d : Fired %d", a, b, mGameLog.mVehicles[a].mPowerupsUsed[b]);

			mGameLog.mVehicles[a].mPowerUpsHit[b]= 0;//results.mCars[a].mStatistics.mNumPerksHit[b];
			LOGDEBUG(g_DemonwareGameLogging, "GameLog : Vehicle %d : Power Up %d : Hit %d", a, b, mGameLog.mVehicles[a].mPowerUpsHit[b]);
		}

		mGameLog.mVehicles[a].mStartPosition=levelState.mCars[a].mStartPosition;
		LOGDEBUG(g_DemonwareGameLogging, "GameLog : Vehicle %d : Start Position %d", a, mGameLog.mVehicles[a].mStartPosition);

		mGameLog.mVehicles[a].mFinalPosition=result.m_Position;
		LOGDEBUG(g_DemonwareGameLogging, "GameLog : Vehicle %d : Final Position %d", a, mGameLog.mVehicles[a].mFinalPosition);

		mGameLog.mVehicles[a].mFinalState=result.m_State;
		LOGDEBUG(g_DemonwareGameLogging, "GameLog : Vehicle %d : Final State %d", a, mGameLog.mVehicles[a].mFinalState);
	}
	LOGDEBUG(g_DemonwareGameLogging, "--- Ending Log Game");

	C_RecordEventManager::Instance().WriteEventRecordBin(&mGameLog, sizeof(AmaxGameLog), EVENT_RECORD_CATEGORY_RACELOG);
}

}; // namespace Game
