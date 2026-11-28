#include "stdafx.h"

#include "ui/Game/Profiles/ProfileInterface.h"
#include "ui/Game/SaveSystem/SaveSystem.h"

#include "ui/UiInterface.h"
#include "ui/GameSaveSystem.h"
#include "ui/Graphics/Localisation/loc.h"

#include <system/misc/StringUtil.h>

#include <shlobj.h>		// [Blur2PC] T250: SHGetFolderPathA (save location)
#include <system/misc/BlurRebuiltSettings.h>	// [Blur2PC] BlurRebuilt.ini

#ifdef CFG_ALLOW_OPTIMIZE_OFF
#pragma optimize( "", off )
#endif

namespace UI
{

// [Blur2PC] T250: retail Blur PC 1.2 (0x004fc4a0, used by all four save threads) keeps saves under
// "%APPDATA%\Bizarre Creations\Blur\" (created if missing) followed by the interface's save root, and builds each
// path with "%s%s\%s" (root, save root, file name). BLUR2PC_SAVE_ROOT replaces that folder for scripted runs so
// they never touch the player's saves; not in retail.
static void GetUserDataRoot( char* pRoot, const size_t uSize )
{
	if( const char* pOverride = getenv( "BLUR2PC_SAVE_ROOT" ) )
	{
		sprintf_s( pRoot, uSize, "%s\\", pOverride );
	}
	else
	{
		char appData[ MAX_PATH ] = { 0 };
		SHGetFolderPathA( NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, appData );
		sprintf_s( pRoot, uSize, "%s\\Bizarre Creations\\Blur\\", appData );
	}

	if ( !Sys::C_FileSystem::Instance().FileExists( pRoot ) )
		Sys::C_FileSystem::Instance().CreateDirectory( pRoot );
}

// [Blur2PC] Not retail: BlurRebuilt keeps its own files in the same folder as the profile save (retail ignores
// them). BlurRebuilt.ini holds its settings as "key=value" lines, read with system/misc/BlurRebuiltSettings.h.
static const U32 kBlurRebuiltSettingsMaxLines	= 64;
static const U32 kBlurRebuiltSettingsLineSize	= 512;

void GetBlurRebuiltFilePath( const char* pFileName, char* pPath, const size_t uSize )
{
	GetUserDataRoot( pPath, uSize );
	strcat_s( pPath, uSize, pFileName );
}

// Written when there is no BlurRebuilt.ini yet, so the settings can be found; every line is a comment.
static void WriteBlurRebuiltSettingsTemplate( const char* pPath )
{
	FILE* pFile = NULL;
	if ( fopen_s( &pFile, pPath, "w" ) != 0 || !pFile )
		return;

	fputs(	"; Blur Rebuilt settings: remove the ; in front of a line to use it.\n"
			";\n"
			"; Your name in LAN and offline games (up to 15 plain characters)\n"
			";player_name=YourName\n"
			";\n"
			"; Speed on the race map: kmh or mph (also in Options)\n"
			";speed_units=kmh\n"
			";\n"
			"; Race events file: off, or a full file path (default: race_events.jsonl in this folder)\n"
			";race_events_file=off\n"
			";\n"
			"; Also send every race event as a UDP packet to host:port (a name, an address or a broadcast address)\n"
			";race_events_udp=lights.local:5555\n"
			";\n"
			"; Sound buffer in the menus, in milliseconds (default 64; 0 uses the race buffer there too)\n"
			";audio_menu_buffer_ms=64\n"
			";\n"
			"; Sound buffer in races, in milliseconds: larger stops stuttering, but sounds play later (default about 30)\n"
			";audio_buffer_ms=40\n"
			";\n"
			"; Dedicated LAN server: this PC hosts a LAN game on its own, starts the races and only watches them\n"
			";server_mode=1\n"
			"; Players (not counting the server) needed before the countdown to a race starts (default 1)\n"
			";server_min_players=2\n"
			"; Seconds of countdown before each race (default 60)\n"
			";server_countdown=60\n"
			"; Races, from server_race1 up to server_race32: mode (race or motormash), track (a name from server_tracks.txt,\n"
			"; or random), laps (minutes for motormash) and class (A to E, or any). Without any, every race is a random track.\n"
			";server_race1=race random 2 A\n"
			";server_race2=motormash random 3 any\n"
			"; Order of the races: inorder or shuffle (default inorder)\n"
			";server_order=shuffle\n"
			"; The server's web page, at http://<this PC>:8080 on the LAN (default 8080; 0 turns it off)\n"
			";server_web_port=8080\n"
			"; Password for the web page's buttons (default: none, anyone on the LAN can use them)\n"
			";server_web_password=secret\n", pFile );
	fclose( pFile );
}

bool GetBlurRebuiltSetting( const char* pKey, char* pValue, const size_t uSize )
{
	char path[ MAX_PATH ];
	GetBlurRebuiltFilePath( "BlurRebuilt.ini", path, MAX_PATH );

	if ( GetFileAttributesA( path ) == INVALID_FILE_ATTRIBUTES )
	{
		WriteBlurRebuiltSettingsTemplate( path );
		return false;
	}
	return Blur2PCSettings::Read( pKey, pValue, uSize );
}

// Replaces the key's line (or adds it), keeping every other line, comment and their order.
void SetBlurRebuiltSetting( const char* pKey, const char* pValue )
{
	char path[ MAX_PATH ];
	GetBlurRebuiltFilePath( "BlurRebuilt.ini", path, MAX_PATH );

	static char s_Lines[ kBlurRebuiltSettingsMaxLines ][ kBlurRebuiltSettingsLineSize ];
	U32 numLines = 0;
	bool replaced = false;

	FILE* pFile = NULL;
	if ( fopen_s( &pFile, path, "r" ) == 0 && pFile )
	{
		while ( numLines < kBlurRebuiltSettingsMaxLines && fgets( s_Lines[ numLines ], kBlurRebuiltSettingsLineSize, pFile ) )
		{
			char* pLine = s_Lines[ numLines ];
			pLine[ strcspn( pLine, "\r\n" ) ] = '\0';

			char parsed[ kBlurRebuiltSettingsLineSize ];
			strcpy_s( parsed, pLine );
			char* pLineKey;
			char* pLineValue;
			if ( Blur2PCSettings::ParseLine( parsed, pLineKey, pLineValue ) && _stricmp( pLineKey, pKey ) == 0 )
			{
				if ( replaced )
					continue;	// drop repeats of the key
				sprintf_s( pLine, kBlurRebuiltSettingsLineSize, "%s=%s", pKey, pValue );
				replaced = true;
			}
			numLines++;
		}
		fclose( pFile );
	}

	if ( !replaced && numLines < kBlurRebuiltSettingsMaxLines )
		sprintf_s( s_Lines[ numLines++ ], kBlurRebuiltSettingsLineSize, "%s=%s", pKey, pValue );

	if ( fopen_s( &pFile, path, "w" ) == 0 && pFile )
	{
		for ( U32 i = 0; i < numLines; i++ )
			fprintf( pFile, "%s\n", s_Lines[ i ] );
		fclose( pFile );
	}
}

// -----------------
// C_AsyncLoadThread
// -----------------
void C_AsyncLoadThread::ThreadMain()
{
	C_SaveInfoInterface* pSaveInfoInterface = C_SaveSystem::Instance().SaveInfoInterface();

	if ( !pSaveInfoInterface )
		return;

	if ( ( pSaveInfoInterface->m_eType == E_UiSaveInfoType_Enumerate ) && ( pSaveInfoInterface->m_sEnumeration == -1 ) )
	{
		C_SaveSystem::Instance().m_sEnumerationCount = 0;	// reset

		Sys::C_DirectoryInfo dirInfo;

		// [Blur2PC] T250: retail 0x0075dc24
		char enumPath[ MAX_PATH ] = { 0 };
		GetUserDataRoot( enumPath, MAX_PATH );
		strcat_s( enumPath, MAX_PATH, pSaveInfoInterface->m_SaveRoot.Chars() );

		bool bSuccess = Sys::C_FileSystem::Instance().DirectoryInfo( enumPath, &dirInfo );

		if ( !bSuccess )
		{
			m_eErrorCode = 	E_UiGameProfile_Error_CannotEnumerateFiles;
		}
		else
		{
			for ( U32 i = 0; i < dirInfo.NumFiles(); i++ )
			{
				Sys::C_FileInfo& fileInfo = dirInfo.FileInfo( i );
				
				if ( fileInfo.m_FileType != Sys::E_FileType_File )
					continue;

				// check file types
				if ( Sys::ExtractFileExtension( fileInfo.m_FileName ) != Util::C_String( pSaveInfoInterface->m_EnumerationFileExtension.Chars() ) )
					continue;

				Sys::C_FilePath s = Sys::ExtractFilename( fileInfo.m_FileName );

				if ( s.Length() >= MAX_SAVE_FILENAME_SIZE )
					continue;

				// found one
				strcpy_s( C_SaveSystem::Instance().m_EnumeratedFileInfo[ C_SaveSystem::Instance().m_sEnumerationCount++ ].m_FileName, MAX_SAVE_FILENAME_SIZE, s.Chars() );
			}
		}
	}
	else
	{
		char loadPath[ MAX_PATH ] = { 0 };
		char userRoot[ MAX_PATH ] = { 0 };
		GetUserDataRoot( userRoot, MAX_PATH );	// [Blur2PC] T250: retail 0x0075de21

		S32 sNumFiles = ( pSaveInfoInterface->m_eType == E_UiSaveInfoType_Enumerate ) ? 1 : pSaveInfoInterface->m_FileInfo.Size();

		for ( S32 i = 0; i < sNumFiles; i++ )
		{
			if ( pSaveInfoInterface->m_eType == E_UiSaveInfoType_Enumerate )
				sprintf_s( loadPath, MAX_PATH, "%s%s\\%s", userRoot, pSaveInfoInterface->m_SaveRoot.Chars(), C_SaveSystem::Instance().m_EnumeratedFileInfo[ pSaveInfoInterface->m_sEnumeration ].m_FileName );
			else
				sprintf_s( loadPath, MAX_PATH, "%s%s\\%s", userRoot, pSaveInfoInterface->m_SaveRoot.Chars(), pSaveInfoInterface->m_FileInfo[ i ].m_FileName.Chars() );

			if ( !Sys::C_FileSystem::Instance().FileExists( loadPath ) )
			{
				m_eErrorCode = E_UiGameProfile_Error_FileDoesNotExist;
			}
			else
			{
				Sys::C_OpenParams params;
				params.m_IsCritical = false;

				// open save file
				Sys::I_File* pFile = Sys::C_FileSystem::Instance().Open( loadPath, Sys::E_FileAccess_Read, params );

				if ( !pFile )
				{
					// failed to open the file
					m_eErrorCode = E_UiGameProfile_Error_CannotReadFile;
				}
				else
				{
					const U32 uBufferSize = (U32)pFile->Length(); //pSaveInfoInterface->m_FileInfo[ i ].m_uFileSize;

					// read and process
					U8* pBuffer = new U8[ uBufferSize ];	// use fixed data size
					memset( pBuffer, 0xfd, uBufferSize );	// debug clear

					bool bReadSucceeded = pFile->Read( pBuffer, uBufferSize );

					if ( bReadSucceeded )		
						bReadSucceeded = pSaveInfoInterface->Load( m_sPadNo, pBuffer, uBufferSize, pSaveInfoInterface->m_FileInfo[ i ].m_sUserData );	// load in

					SAFE_DELETE_PTR( pFile );
					SAFE_DELETE_ARY( pBuffer );

					if ( !bReadSucceeded )	// failed to read in the variables - out-of-date file
						m_eErrorCode = E_UiGameProfile_Error_CannotReadFile;
					else
						m_eErrorCode = E_UiGameProfile_Error_Okay;
				}
			}

			// check for an optional file
			if ( ( m_eErrorCode == E_UiGameProfile_Error_FileDoesNotExist ) && pSaveInfoInterface->m_FileInfo[ i ].m_bOptional )
				m_eErrorCode = E_UiGameProfile_Error_Okay;

			if ( m_eErrorCode != E_UiGameProfile_Error_Okay )
				break;
		}
	}

	pSaveInfoInterface->LoadFinished();

#ifndef CFG_GOLD
	// force error code
	if ( g_SaveSystemForceErrorCode != E_UiGameProfile_Error_None )
	{
		if ( ( g_SaveSystemForceErrorCode == E_UiGameProfile_Error_Okay )
			|| ( g_SaveSystemForceErrorCode == E_UiGameProfile_Error_FileDoesNotExist )
			|| ( g_SaveSystemForceErrorCode == E_UiGameProfile_Error_CannotReadFile )
			|| ( g_SaveSystemForceErrorCode == E_UiGameProfile_Error_CannotEnumerateFiles ) )
		{
			m_eErrorCode = g_SaveSystemForceErrorCode;
		}
	}
#endif
}

// -----------------
// C_AsyncSaveThread
// -----------------
void C_AsyncSaveThread::ThreadMain()
{
	C_SaveInfoInterface* pSaveInfoInterface = C_SaveSystem::Instance().SaveInfoInterface();

	if ( !pSaveInfoInterface )
		return;

	// TODO - free space checks?

	char savePath[ MAX_PATH ] = { 0 };
	char userRoot[ MAX_PATH ] = { 0 };
	GetUserDataRoot( userRoot, MAX_PATH );	// [Blur2PC] T250: retail 0x0075e0c0
	sprintf_s( savePath, MAX_PATH, "%s%s", userRoot, pSaveInfoInterface->m_SaveRoot.Chars() );

	// Create the directory if it doesnt exist.
	if ( !Sys::C_FileSystem::Instance().FileExists( savePath ) )
	{
		Sys::C_FileSystem::Instance().CreateDirectory( savePath );
	}
	else
	{
		for ( S32 i = 0; i < pSaveInfoInterface->m_FileInfo.Size(); i++ )
		{
			// open or create the save file.
			sprintf_s( savePath, MAX_PATH, "%s%s\\%s", userRoot, pSaveInfoInterface->m_SaveRoot.Chars(), pSaveInfoInterface->m_FileInfo[ i ].m_FileName.Chars() );

			Sys::C_OpenParams params;
			params.m_IsCritical = false;

			Sys::I_File* pFile = Sys::C_FileSystem::Instance().Open( savePath, Sys::E_FileAccess_Write, params );

			if ( !pFile )
			{
				// failed to open the file
				m_eErrorCode = E_UiGameProfile_Error_CannotWriteFile;
			}
			else
			{
				if ( pSaveInfoInterface->m_bCheckOverwrites )
				{
					m_eErrorCode = E_UiGameProfile_Error_FileExists;
				}
				else
				{
					const U32 uBufferSize = pSaveInfoInterface->m_FileInfo[ i ].m_uFileSize;

					// process and write
					U8* pBuffer = new U8[ uBufferSize ];	// use fixed data size
					memset( pBuffer, 0xfe, uBufferSize );	// debug clear

					// save out
					if ( pSaveInfoInterface->Save( m_sPadNo, pBuffer, uBufferSize, pSaveInfoInterface->m_FileInfo[ i ].m_sUserData ) )
					{
						if ( !pFile->Write( pBuffer, uBufferSize ) )
							m_eErrorCode = E_UiGameProfile_Error_CannotWriteFile;

						pFile->Flush();	
					}
					else
						m_eErrorCode = E_UiGameProfile_Error_CannotWriteFile;			

					SAFE_DELETE_PTR( pFile );
					SAFE_DELETE_ARY( pBuffer );
				}
			}

			if ( m_eErrorCode != E_UiGameProfile_Error_Okay )
				break;
		}
	}

	pSaveInfoInterface->SaveFinished();

#ifndef CFG_GOLD
	// force error code
	if ( g_SaveSystemForceErrorCode != E_UiGameProfile_Error_None )
	{
		if ( ( g_SaveSystemForceErrorCode == E_UiGameProfile_Error_Okay )
			|| ( g_SaveSystemForceErrorCode == E_UiGameProfile_Error_FileExists )
			|| ( g_SaveSystemForceErrorCode == E_UiGameProfile_Error_CannotWriteFile ) )
		{
			m_eErrorCode = g_SaveSystemForceErrorCode;
		}
	}
#endif
}

// -------------------
// C_AsyncDeleteThread
// -------------------
void C_AsyncDeleteThread::ThreadMain()
{
	C_SaveInfoInterface* pSaveInfoInterface = C_SaveSystem::Instance().SaveInfoInterface();

	if ( !pSaveInfoInterface )
		return;

	char loadPath[ MAX_PATH ] = { 0 };
	char userRoot[ MAX_PATH ] = { 0 };
	GetUserDataRoot( userRoot, MAX_PATH );	// [Blur2PC] T250: retail 0x0075e37f

	for ( S32 i = 0; i < pSaveInfoInterface->m_FileInfo.Size(); i++ )
	{
		sprintf_s( loadPath, MAX_PATH, "%s%s\\%s", userRoot, pSaveInfoInterface->m_SaveRoot.Chars(), pSaveInfoInterface->m_FileInfo[ i ].m_FileName.Chars() );

		// if save file exists, delete it.
		if ( Sys::C_FileSystem::Instance().FileExists( loadPath ) )
		{
			if ( Sys::C_FileSystem::Instance().DeleteFile( loadPath ) )
				m_eErrorCode = E_UiGameProfile_Error_Okay;
			else
				m_eErrorCode = E_UiGameProfile_Error_CannotDeleteFile;
		}

		if ( m_eErrorCode != E_UiGameProfile_Error_Okay )
			break;
	}

	pSaveInfoInterface->DeleteFinished();

#ifndef CFG_GOLD
	// force error code
	if ( g_SaveSystemForceErrorCode != E_UiGameProfile_Error_None )
	{
		if ( ( g_SaveSystemForceErrorCode == E_UiGameProfile_Error_Okay )
			|| ( g_SaveSystemForceErrorCode == E_UiGameProfile_Error_CannotDeleteFile ) )
		{
			m_eErrorCode = g_SaveSystemForceErrorCode;
		}
	}
#endif
}

// ------------
// C_SaveSystem
// ------------
S32 C_SaveSystem::GetCurrentSaveSize()
{
	return 0;
}

}; // namespace UI

#ifdef CFG_ALLOW_OPTIMIZE_OFF
#pragma optimize( "", on )
#endif
