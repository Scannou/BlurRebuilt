#ifndef GAME_LOGGING_MANAGER_H
#define GAME_LOGGING_MANAGER_H

#include <btl/Containers/containers.h>

#include <GameLogic\Structures\LevelStateConstants.h>
#include <GameLogic\RaceLogicCore\Data\RLEnums.h>

namespace Game
{

const int AMAX_GAME_LOG_VERSION=7;

#pragma pack(push, 1)

typedef struct AmaxGameLogHeader
{
	U64 mCommunityGroupID;

	U32 mVersion;
	U32 mRaceMode;		//points / elim / race / desctruction
	U32 mCity;
	U32 mRoute;
	U32 mNumCars;
	U32 mLaps;

	S32 mTimeLimit;

}AmaxGameLogHeader;

typedef struct AmaxGameVehicleLog
{
	WCHAR mCharacterString[32];

	U64 mSystemID;

	float mHealthRemaining;
	float mDistanceTravelled;

	S32 mLoadOut1;
	S32 mLoadOut2;
	S32 mLoadOut3;

	S32 mRank;
	S32 mLegendLevel;

	U32 mCarID;
	U32 mManufacturerID;
	U32 mTotalFansGained;

	U32 mNumPowerups;
	U16 mPowerupsUsed[E_PICKUP_TYPE_MAX];
	U16 mPowerUpsHit[E_PICKUP_TYPE_MAX];

	U8 mStartPosition;
	U8 mFinalPosition;
	U8 mFinalState;

	U8 pad;

}AmaxGameVehicleLog;

typedef struct AmaxGameLog
{
	AmaxGameLogHeader mHeader;
	AmaxGameVehicleLog mVehicles[MAX_LEVELSTATE_CARS];	//purposely not making this a Btl as it's a pure binary upload
}AmaxGameLog;

#pragma pack(pop)

class C_GameLoggingManager
{
public:

	C_GameLoggingManager()
	{
	};

	void UploadGameLog();

	void DebugExportEnumNames();

#if defined ( PLATFORM_PC )
	// [Blur2PC] Not retail: race events of LAN and online races as JSON lines; see GameLoggingManager.cpp.
	void RaceEventRaceStart();
	void RaceEventLap( const U8 vehicleIndex, const F32 lapTime );
	void RaceEventOvertake( const U8 overtakerIndex, const U8 overtakenIndex );
	void RaceEventPowerupHit( const U8 firingIndex, const U8 hitIndex, const S32 pickupType, const S32 level, const U32 pickupId );
	void RaceEventWreck( const S32 attackerIndex, const U8 wreckedIndex, const bool remote, const bool lastHit = false );
	void RaceEventFinish( const U8 vehicleIndex, const S32 vehicleState, const F32 raceTime );
	void RaceEventResults();
	void RaceEventUpdate( const bool force = false );

	// [Blur2PC] Not retail: the dedicated LAN server's web page (GameLoggingServerWeb.inl). Game thread only: the
	// server's state as a JSON object (from Lua), its log lines and the commands the page queued.
	void ServerWebUpdate( const char* pServerJson );
	void ServerWebLog( const char* pLine );
	bool ServerWebTakeCommand( char* pName, const size_t uNameSize, char* pArg, const size_t uArgSize );
#endif

	static C_GameLoggingManager& Instance(){return s_instance;};

private:

	static C_GameLoggingManager s_instance;

	AmaxGameLog mGameLog;
};

} // namespace Game

#endif // GAME_LOGGING_MANAGER_H