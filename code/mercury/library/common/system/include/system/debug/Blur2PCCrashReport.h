// [Blur2PC] Not retail: crash and hang reports, so a crash on a player's PC says where it happened.
//
// Retail Blur has no crash handler: the process just ends. With this, an unhandled exception on any thread writes
// crash.txt (exception, the faulting thread's stack with function names and source lines when Amax.pdb is next to
// the exe, and the last game files opened) and crash.dmp (a minidump) to the folder of the profile save,
// %APPDATA%\Bizarre Creations\Blur\ (or BLUR2PC_SAVE_ROOT). A watchdog writes hang.txt, the main thread's stack and
// the same file list, when the main loop has not run for 20 seconds; the game carries on.
//
// Two parts, so the file loaders need only <windows.h>:
//   NoteFile        always available; IoDevice.cpp and C_File.cpp record every open, failed ones with their error.
//   Install/Beat    with BLUR2PC_CRASH_REPORT_WRITER defined before the include (ApplicationPC.cpp only, which links
//                   dbghelp): the handler, the minidump and the watchdog.

#ifndef SYSTEM_DEBUG_BLUR2PC_CRASH_REPORT_H
#define SYSTEM_DEBUG_BLUR2PC_CRASH_REPORT_H

#if defined(PLATFORM_PC)

#include <windows.h>
#include <stdio.h>

namespace Blur2PCCrashReport
{
	static const LONG kFileCount = 24;

	struct Files
	{
		char			mPaths[ kFileCount ][ 300 ];
		volatile LONG	mNext;
	};

	inline Files& GetFiles()
	{
		static Files s_files;	// zero initialised; inline function statics are one instance across the libraries
		return s_files;
	}

	//! A game file was opened (error 0) or failed to open (the Windows error).
	inline void NoteFile( const char* pPath, const DWORD error )
	{
		if ( !pPath )
			return;
		const DWORD lastError = GetLastError();	// the caller reads it next
		Files& files = GetFiles();
		const LONG slot = ( InterlockedIncrement( &files.mNext ) - 1 ) % kFileCount;
		if ( error )
			_snprintf_s( files.mPaths[ slot ], sizeof( files.mPaths[ slot ] ), _TRUNCATE, "FAILED (error %lu) %s", error, pPath );
		else
			_snprintf_s( files.mPaths[ slot ], sizeof( files.mPaths[ slot ] ), _TRUNCATE, "%s", pPath );
		SetLastError( lastError );
	}
}

#if defined(BLUR2PC_CRASH_REPORT_WRITER)

#include <dbghelp.h>
#include <shlobj.h>
#include <time.h>

namespace Blur2PCCrashReport
{
	struct State
	{
		volatile LONG	mBeat;			// main loop passes
		DWORD			mMainThreadId;
		HANDLE			mMainThread;	// a real handle, for the watchdog
		volatile LONG	mWriting;
		bool			mSymbols;
	};

	inline State& GetState()
	{
		static State s_state;
		return s_state;
	}

	inline void ReportPath( char* pPath, const size_t uSize, const char* pName )
	{
		if ( const char* pOverride = getenv( "BLUR2PC_SAVE_ROOT" ) )
		{
			_snprintf_s( pPath, uSize, _TRUNCATE, "%s\\%s", pOverride, pName );
			return;
		}
		char appData[ MAX_PATH ] = { 0 };
		SHGetFolderPathA( NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, appData );
		_snprintf_s( pPath, uSize, _TRUNCATE, "%s\\Bizarre Creations\\Blur\\%s", appData, pName );
	}

	inline void InitSymbols()
	{
		State& state = GetState();
		if ( state.mSymbols )
			return;
		char exeDir[ MAX_PATH ] = { 0 };
		GetModuleFileNameA( NULL, exeDir, MAX_PATH );
		if ( char* pSlash = strrchr( exeDir, '\\' ) )
			*pSlash = '\0';
		SymSetOptions( SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS );
		state.mSymbols = SymInitialize( GetCurrentProcess(), exeDir, TRUE ) ? true : false;
	}

	inline void WriteAddress( FILE* pFile, const int index, const DWORD64 address )
	{
		HANDLE process = GetCurrentProcess();
		char moduleName[ MAX_PATH ] = "?";
		DWORD64 moduleBase = 0;
		HMODULE module = NULL;
		if ( GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)address, &module ) && module )
		{
			char path[ MAX_PATH ] = "";
			GetModuleFileNameA( module, path, MAX_PATH );
			const char* pName = strrchr( path, '\\' );
			strncpy_s( moduleName, pName ? pName + 1 : path, _TRUNCATE );
			moduleBase = (DWORD64)module;
		}
		fprintf( pFile, "  #%-2d %s+0x%llx", index, moduleName, (unsigned long long)( address - moduleBase ) );

		char symbolBuffer[ sizeof( SYMBOL_INFO ) + 256 ];
		SYMBOL_INFO* pSymbol = (SYMBOL_INFO*)symbolBuffer;
		memset( symbolBuffer, 0, sizeof( symbolBuffer ) );
		pSymbol->SizeOfStruct = sizeof( SYMBOL_INFO );
		pSymbol->MaxNameLen = 255;
		DWORD64 displacement = 0;
		if ( GetState().mSymbols && SymFromAddr( process, address, &displacement, pSymbol ) )
		{
			fprintf( pFile, "  %s+0x%llx", pSymbol->Name, (unsigned long long)displacement );
			IMAGEHLP_LINE64 line;
			memset( &line, 0, sizeof( line ) );
			line.SizeOfStruct = sizeof( line );
			DWORD lineDisplacement = 0;
			if ( SymGetLineFromAddr64( process, address, &lineDisplacement, &line ) && line.FileName )
			{
				const char* pShort = strstr( line.FileName, "\\code\\" );
				fprintf( pFile, "  (%s:%lu)", pShort ? pShort + 1 : line.FileName, line.LineNumber );
			}
		}
		fputc( '\n', pFile );
	}

	inline void WriteStack( FILE* pFile, HANDLE thread, CONTEXT context )
	{
		STACKFRAME64 frame;
		memset( &frame, 0, sizeof( frame ) );
		frame.AddrPC.Offset = context.Rip;
		frame.AddrPC.Mode = AddrModeFlat;
		frame.AddrFrame.Offset = context.Rbp;
		frame.AddrFrame.Mode = AddrModeFlat;
		frame.AddrStack.Offset = context.Rsp;
		frame.AddrStack.Mode = AddrModeFlat;
		for ( int i = 0; i < 48; i++ )
		{
			if ( !StackWalk64( IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), thread, &frame, &context, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL ) )
				break;
			if ( frame.AddrPC.Offset == 0 )
				break;
			WriteAddress( pFile, i, frame.AddrPC.Offset );
		}
	}

	inline void WriteHeader( FILE* pFile, const char* pWhat )
	{
		char exe[ MAX_PATH ] = "";
		GetModuleFileNameA( NULL, exe, MAX_PATH );
		SYSTEMTIME now;
		GetLocalTime( &now );
		const IMAGE_DOS_HEADER* pDos = (const IMAGE_DOS_HEADER*)GetModuleHandleA( NULL );
		const IMAGE_NT_HEADERS* pNt = (const IMAGE_NT_HEADERS*)( (const char*)pDos + pDos->e_lfanew );
		const time_t built = (time_t)pNt->FileHeader.TimeDateStamp;
		struct tm builtTm;
		gmtime_s( &builtTm, &built );
		fprintf( pFile, "Blur Rebuilt %s, %04u-%02u-%02u %02u:%02u:%02u\n", pWhat, now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond );
		fprintf( pFile, "exe: %s (built %04d-%02d-%02d %02d:%02d UTC)\n", exe, builtTm.tm_year + 1900, builtTm.tm_mon + 1, builtTm.tm_mday, builtTm.tm_hour, builtTm.tm_min );
		fprintf( pFile, "symbols: %s\n\n", GetState().mSymbols ? "loaded (Amax.pdb next to the exe gives function names)" : "none" );
	}

	inline void WriteFiles( FILE* pFile )
	{
		Files& files = GetFiles();
		const LONG next = files.mNext;
		fprintf( pFile, "\nLast game files opened (oldest first):\n" );
		for ( LONG i = ( next > kFileCount ? next - kFileCount : 0 ); i < next; i++ )
			fprintf( pFile, "  %s\n", files.mPaths[ i % kFileCount ] );
	}

	inline const char* ExceptionName( const DWORD code )
	{
		switch ( code )
		{
			case EXCEPTION_ACCESS_VIOLATION:		return "access violation";
			case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:	return "array bounds exceeded";
			case EXCEPTION_DATATYPE_MISALIGNMENT:	return "misaligned data";
			case EXCEPTION_FLT_DIVIDE_BY_ZERO:
			case EXCEPTION_INT_DIVIDE_BY_ZERO:		return "divide by zero";
			case EXCEPTION_ILLEGAL_INSTRUCTION:		return "illegal instruction";
			case EXCEPTION_IN_PAGE_ERROR:			return "in page error (a file could not be read)";
			case EXCEPTION_STACK_OVERFLOW:			return "stack overflow";
			case EXCEPTION_BREAKPOINT:				return "breakpoint (assert)";
			case 0xC0000409:						return "stack buffer overrun or abort";
			case 0xE06D7363:						return "C++ exception";
			default:								return "exception";
		}
	}

	inline LONG WINAPI UnhandledException( EXCEPTION_POINTERS* pException )
	{
		State& state = GetState();
		if ( InterlockedExchange( &state.mWriting, 1 ) )
			return EXCEPTION_CONTINUE_SEARCH;
		InitSymbols();

		char path[ MAX_PATH ];
		ReportPath( path, MAX_PATH, "crash.txt" );
		FILE* pFile = NULL;
		if ( fopen_s( &pFile, path, "w" ) == 0 && pFile )
		{
			WriteHeader( pFile, "crash" );
			const EXCEPTION_RECORD* pRecord = pException->ExceptionRecord;
			fprintf( pFile, "%s (0x%08lx) on thread %lu%s\n", ExceptionName( pRecord->ExceptionCode ), pRecord->ExceptionCode,
				GetCurrentThreadId(), GetCurrentThreadId() == state.mMainThreadId ? " (main thread)" : "" );
			if ( pRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && pRecord->NumberParameters >= 2 )
			{
				const ULONG_PTR kind = pRecord->ExceptionInformation[ 0 ];
				fprintf( pFile, "%s address 0x%llx\n", kind == 0 ? "reading" : ( kind == 1 ? "writing" : "executing" ), (unsigned long long)pRecord->ExceptionInformation[ 1 ] );
			}
			fprintf( pFile, "\nWhere:\n" );
			WriteAddress( pFile, 0, (DWORD64)pRecord->ExceptionAddress );
			fprintf( pFile, "\nStack:\n" );
			WriteStack( pFile, GetCurrentThread(), *pException->ContextRecord );
			WriteFiles( pFile );
			fclose( pFile );
		}

		ReportPath( path, MAX_PATH, "crash.dmp" );
		HANDLE dump = CreateFileA( path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
		if ( dump != INVALID_HANDLE_VALUE )
		{
			MINIDUMP_EXCEPTION_INFORMATION info;
			info.ThreadId = GetCurrentThreadId();
			info.ExceptionPointers = pException;
			info.ClientPointers = FALSE;
			MiniDumpWriteDump( GetCurrentProcess(), GetCurrentProcessId(), dump,
				(MINIDUMP_TYPE)( MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules ), &info, NULL, NULL );
			CloseHandle( dump );
		}
		return EXCEPTION_CONTINUE_SEARCH;
	}

	inline void WriteHang( const LONG seconds )
	{
		State& state = GetState();
		if ( InterlockedExchange( &state.mWriting, 1 ) )
			return;
		InitSymbols();

		char path[ MAX_PATH ];
		ReportPath( path, MAX_PATH, "hang.txt" );
		FILE* pFile = NULL;
		if ( fopen_s( &pFile, path, "w" ) == 0 && pFile )
		{
			WriteHeader( pFile, "hang" );
			fprintf( pFile, "The main loop has not run for %ld seconds. Main thread:\n", seconds );
			if ( SuspendThread( state.mMainThread ) != (DWORD)-1 )
			{
				CONTEXT context;
				memset( &context, 0, sizeof( context ) );
				context.ContextFlags = CONTEXT_FULL;
				if ( GetThreadContext( state.mMainThread, &context ) )
				{
					WriteAddress( pFile, 0, context.Rip );
					fprintf( pFile, "\nStack:\n" );
					WriteStack( pFile, state.mMainThread, context );
				}
				ResumeThread( state.mMainThread );
			}
			WriteFiles( pFile );
			fclose( pFile );
		}
		InterlockedExchange( &state.mWriting, 0 );
	}

	inline DWORD WINAPI Watchdog( LPVOID )
	{
		State& state = GetState();
		LONG lastBeat = state.mBeat;
		LONG stuck = 0;
		bool reported = false;
		for ( ;; )
		{
			Sleep( 1000 );
			const LONG beat = state.mBeat;
			if ( beat != lastBeat || beat == 0 )
			{
				lastBeat = beat;
				stuck = 0;
				reported = false;	// running again: a later hang is reported too
				continue;
			}
			if ( ++stuck >= 20 && !reported )
			{
				reported = true;
				WriteHang( stuck );
			}
		}
	}

	//! Once, early on the main thread.
	inline void Install()
	{
		State& state = GetState();
		state.mMainThreadId = GetCurrentThreadId();
		DuplicateHandle( GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &state.mMainThread, 0, FALSE, DUPLICATE_SAME_ACCESS );
		SetUnhandledExceptionFilter( UnhandledException );
		HANDLE thread = CreateThread( NULL, 0, Watchdog, NULL, 0, NULL );
		if ( thread )
			CloseHandle( thread );
	}

	//! Each pass of the main loop.
	inline void Beat()
	{
		InterlockedIncrement( &GetState().mBeat );
	}
}

#endif // BLUR2PC_CRASH_REPORT_WRITER

#endif // PLATFORM_PC

#endif // SYSTEM_DEBUG_BLUR2PC_CRASH_REPORT_H
