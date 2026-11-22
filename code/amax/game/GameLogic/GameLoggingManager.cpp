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
#include <float.h>
#include <shlobj.h>
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

static const char* RaceEventName( const char* const* pNames, const S32 count, const S32 value )
{
	return ( value >= 0 && value < count ) ? pNames[ value ] : "unknown";
}

static bool GetRaceEventsPath( char (&path)[ MAX_PATH ] )
{
	if ( const char* pSetting = getenv( "BLUR_RACE_EVENTS" ) )
	{
		if ( _stricmp( pSetting, "off" ) == 0 || strcmp( pSetting, "0" ) == 0 )
			return false;
		if ( pSetting[ 0 ] )
		{
			strcpy_s( path, pSetting );
			return true;
		}
	}

	if ( const char* pOverride = getenv( "BLUR2PC_SAVE_ROOT" ) )
	{
		sprintf_s( path, "%s\\race_events.jsonl", pOverride );
	}
	else
	{
		char appData[ MAX_PATH ] = { 0 };
		SHGetFolderPathA( NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, appData );
		sprintf_s( path, "%s\\Bizarre Creations\\Blur\\race_events.jsonl", appData );
	}
	return true;
}

static bool RaceEventsActive()
{
	C_RaceBookLayer* pLayer = C_GameApp::GetInstance()->GetRaceBookLayer();
	return s_RaceEventRaceId[ 0 ] && pLayer && pLayer->GetGameModePtr() && pLayer->GetGameModePtr()->IsNetwork();
}

// One JSON object, written as one line; text that does not fit is dropped rather than cut mid-value.
class C_RaceEventLine
{
public:
	C_RaceEventLine( const char* pEvent ) : m_Length( 0 ), m_NeedComma( false ), m_Overflow( false )
	{
		SYSTEMTIME now;
		GetLocalTime( &now );

		char time[ 32 ];
		sprintf_s( time, "%04u-%02u-%02uT%02u:%02u:%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond );

		Append( "{" );
		Str( "time", time );
		Str( "race", s_RaceEventRaceId );
		Float( "t", (F32)( GetTickCount64() - s_RaceEventStartMs ) / 1000.0f );
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
			WideStr( "name", levelState.mCars[ vehicleIndex ].mNameAboveCar.Chars() );
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

		char path[ MAX_PATH ];
		if ( m_Overflow || !GetRaceEventsPath( path ) )
			return;

		FILE* pFile = NULL;
		if ( fopen_s( &pFile, path, "ab" ) == 0 && pFile )
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
	s_RaceEventFinishedMask = 0;
	s_RaceEventFinishOrder = 0;

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

void C_GameLoggingManager::RaceEventLap( const U8 vehicleIndex, const S32 lap, const F32 lapTime )
{
	if ( !RaceEventsActive() )
		return;

	C_RaceEventLine line( "lap" );
	line.Player( "player", vehicleIndex );
	line.Int( "lap", lap );
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

void C_GameLoggingManager::RaceEventPowerupHit( const U8 firingIndex, const U8 hitIndex, const S32 pickupType, const S32 level )
{
	if ( !RaceEventsActive() )
		return;

	C_RaceEventLine line( "powerup_hit" );
	line.Player( "player", firingIndex );
	line.Player( "hit", hitIndex );
	line.Str( "powerup", RaceEventName( s_RaceEventPickupNames, E_PICKUP_TYPE_MAX, pickupType ) );
	line.Int( "level", level + 1 );
	line.Write();
}

void C_GameLoggingManager::RaceEventWreck( const S32 attackerIndex, const U8 wreckedIndex )
{
	if ( !RaceEventsActive() )
		return;

	C_RaceEventLine line( "wreck" );
	if ( attackerIndex >= 0 )
		line.Player( "player", attackerIndex );
	line.Player( "wrecked", wreckedIndex );
	line.Write();
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
