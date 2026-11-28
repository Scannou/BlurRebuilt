#ifndef SYSTEM_MISC_BLURREBUILTSETTINGS_H
#define SYSTEM_MISC_BLURREBUILTSETTINGS_H

// [Blur2PC] Not retail: reading BlurRebuilt.ini, Blur Rebuilt's own settings ("key=value" lines) in the folder of
// the profile save, %APPDATA%\Bizarre Creations\Blur\ (or BLUR2PC_SAVE_ROOT, as SaveSystem_pc.cpp). Keys are not
// case sensitive, spaces around keys and values are ignored, and lines starting with ';' or '#' are comments.
// Header-only so libraries below the UI (e.g. audio) can read it; the UI owns the file (ui/Game/SaveSystem/
// SaveSystem.h: GetBlurRebuiltSetting creates it with a commented template, SetBlurRebuiltSetting writes keys).

#if defined(PLATFORM_PC)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlobj.h>

namespace Blur2PCSettings
{
	//! The settings folder, with a trailing backslash.
	inline void GetFolder( char* pPath, const size_t uSize )
	{
		if ( const char* pOverride = getenv( "BLUR2PC_SAVE_ROOT" ) )
		{
			sprintf_s( pPath, uSize, "%s\\", pOverride );
		}
		else
		{
			char appData[ MAX_PATH ] = { 0 };
			SHGetFolderPathA( NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, appData );
			sprintf_s( pPath, uSize, "%s\\Bizarre Creations\\Blur\\", appData );
		}
	}

	//! Splits a settings line into its trimmed key and value; false for comments, blank lines and lines without '='.
	inline bool ParseLine( char* pLine, char*& pKey, char*& pValue )
	{
		pLine[ strcspn( pLine, "\r\n" ) ] = '\0';

		pKey = pLine + strspn( pLine, " \t" );
		if ( *pKey == ';' || *pKey == '#' )
			return false;

		char* pEquals = strchr( pKey, '=' );
		if ( !pEquals || pEquals == pKey )
			return false;

		char* pKeyEnd = pEquals;
		while ( pKeyEnd > pKey && ( pKeyEnd[ -1 ] == ' ' || pKeyEnd[ -1 ] == '\t' ) )
			pKeyEnd--;
		*pKeyEnd = '\0';

		pValue = pEquals + 1;
		pValue += strspn( pValue, " \t" );
		size_t length = strlen( pValue );
		while ( length > 0 && ( pValue[ length - 1 ] == ' ' || pValue[ length - 1 ] == '\t' ) )
			pValue[ --length ] = '\0';

		return true;
	}

	//! The value of a key; false when the file or the key is missing.
	inline bool Read( const char* pKey, char* pValue, const size_t uSize )
	{
		char path[ MAX_PATH ];
		GetFolder( path, MAX_PATH );
		strcat_s( path, MAX_PATH, "BlurRebuilt.ini" );

		FILE* pFile = NULL;
		if ( fopen_s( &pFile, path, "r" ) != 0 || !pFile )
			return false;

		bool found = false;
		char line[ 512 ];
		while ( !found && fgets( line, sizeof( line ), pFile ) )
		{
			char* pLineKey;
			char* pLineValue;
			if ( ParseLine( line, pLineKey, pLineValue ) && _stricmp( pLineKey, pKey ) == 0 )
			{
				strncpy_s( pValue, uSize, pLineValue, _TRUNCATE );
				found = true;
			}
		}
		fclose( pFile );
		return found;
	}

	//! [Blur2PC] Not retail: true with server_mode=1, a dedicated LAN server (UiManager.cpp drives the menus and
	//! the lobby, RaceModeMultiplayer.cpp takes the server's own car out of each race). Read once per run.
	inline bool ServerMode()
	{
		static int s_mode = -1;
		if ( s_mode < 0 )
		{
			char value[ 16 ];
			s_mode = ( Read( "server_mode", value, sizeof( value ) ) && atoi( value ) != 0 ) ? 1 : 0;
		}
		return s_mode == 1;
	}
}

#endif // PLATFORM_PC

#endif // SYSTEM_MISC_BLURREBUILTSETTINGS_H
