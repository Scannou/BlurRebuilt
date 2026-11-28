// [Blur2PC] Not retail: the web page of a dedicated LAN server (server_mode=1, server_web_port in BlurRebuilt.ini).
// Included once, by GameLoggingManager.cpp, inside namespace Game: that file already builds with Winsock for the race
// events' UDP output, and the page shows those events.
//
// A worker thread answers HTTP requests on the LAN; it never touches the game. The game thread hands it a snapshot
// (the server's lobby state from MpLobby/MpGUIBank.lua and the players from the network player manager), the server
// log lines and the race event lines, all under one lock. Buttons on the page queue commands, which the game thread
// takes (Blur2PCServer.WebCommand in Lua) and carries out the way the lobby's own logic does.
//
//   GET  /                       the page
//   GET  /api/state?log=N&ev=M   snapshot, log lines after N, race events after M
//   POST /api/cmd?c=NAME&key=K   command NAME with the request body as its argument (key: server_web_password)

static const U32 kServerWebRingSize		= 128;
static const U32 kServerWebLineSize		= 1024;
static const U32 kServerWebMaxCommands	= 16;

struct ServerWebCommand
{
	char mName[ 32 ];
	char mArg[ 4096 ];
};

static CRITICAL_SECTION	s_ServerWebLock;
static bool				s_ServerWebStarted = false;
static char				s_ServerWebPassword[ 64 ] = "";
static char*			s_ServerWebSnapshot = NULL;		// {"server":...,"players":[...]}
static char				s_ServerWebLog[ kServerWebRingSize ][ kServerWebLineSize ];
static U32				s_ServerWebLogSeq = 0;			// lines ever added; line n is at n % kServerWebRingSize
static char				s_ServerWebEvents[ kServerWebRingSize ][ kServerWebLineSize ];
static U32				s_ServerWebEventSeq = 0;
static ServerWebCommand	s_ServerWebCommands[ kServerWebMaxCommands ];
static U32				s_ServerWebCommandCount = 0;

// growing text buffer for JSON
class C_ServerWebText
{
public:
	C_ServerWebText() : m_pText( NULL ), m_Length( 0 ), m_Size( 0 ) { Append( "" ); }
	~C_ServerWebText() { free( m_pText ); }

	void Append( const char* pText, size_t length = (size_t)-1 )
	{
		if ( length == (size_t)-1 )
			length = strlen( pText );
		if ( m_Length + length + 1 > m_Size )
		{
			size_t size = m_Size ? m_Size : 4096;
			while ( m_Length + length + 1 > size )
				size *= 2;
			char* pNew = (char*)realloc( m_pText, size );
			if ( !pNew )
				return;
			m_pText = pNew;
			m_Size = size;
		}
		memcpy( m_pText + m_Length, pText, length );
		m_Length += length;
		m_pText[ m_Length ] = '\0';
	}

	void AppendF( const char* fmt, ... )
	{
		char buffer[ 256 ];
		va_list args;
		va_start( args, fmt );
		_vsnprintf_s( buffer, sizeof( buffer ), _TRUNCATE, fmt, args );
		va_end( args );
		Append( buffer );
	}

	// a JSON string, quotes included
	void AppendString( const char* pText )
	{
		Append( "\"" );
		for ( const unsigned char* p = (const unsigned char*)pText; *p; ++p )
		{
			if ( *p == '"' || *p == '\\' )
			{
				const char escaped[ 2 ] = { '\\', (char)*p };
				Append( escaped, 2 );
			}
			else if ( *p < 0x20 )
				AppendF( "\\u%04x", *p );
			else
				Append( (const char*)p, 1 );
		}
		Append( "\"" );
	}

	const char* Text() const { return m_pText ? m_pText : ""; }
	size_t Length() const { return m_Length; }
	char* Take() { char* p = m_pText; m_pText = NULL; m_Length = m_Size = 0; return p; }

private:
	char*	m_pText;
	size_t	m_Length;
	size_t	m_Size;
};

static void ServerWebAddLine( char ( *pRing )[ kServerWebLineSize ], U32& seq, const char* pLine )
{
	if ( !s_ServerWebStarted )
		return;
	EnterCriticalSection( &s_ServerWebLock );
	char* pSlot = pRing[ seq % kServerWebRingSize ];
	strncpy_s( pSlot, kServerWebLineSize, pLine, _TRUNCATE );
	pSlot[ strcspn( pSlot, "\r\n" ) ] = '\0';
	seq++;
	LeaveCriticalSection( &s_ServerWebLock );
}

static const char* ServerWebPlayerState( const S32 state )
{
	switch ( state )
	{
		case RacerNet::eNetPlayerStateJoining:				return "joining";
		case RacerNet::eNetPlayerStateIdle:					return "lobby";
		case RacerNet::eNetPlayerStateChangingSettings:		return "settings";
		case RacerNet::eNetPlayerStateChangingCar:			return "garage";
		case RacerNet::eNetPlayerStateLoadingRace:			return "loading";
		case RacerNet::eNetPlayerStateLoadingUi:			return "returning";
		case RacerNet::eNetPlayerStateWaitingForOthers:		return "waiting";
		case RacerNet::eNetPlayerStateRacing:				return "racing";
		case RacerNet::eNetPlayerStateFinished:				return "finished";
		case RacerNet::eNetPlayerStateViewingResults:
		case RacerNet::eNetPlayerStateViewingResultsIdle:	return "results";
		default:											return "connecting";
	}
}

// ---------------------------------------------------------------------------------------------------------------
// the page

static const char* const s_ServerWebPage[] =
{
R"(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Blur LAN Server</title><style>
:root{--bg:#101318;--panel:#181d25;--line:#2a313d;--text:#e6e9ef;--dim:#8f98a8;--accent:#4fa3ff;--ok:#43c37a;--warn:#e0a63a;--bad:#e5584f}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:14px/1.45 system-ui,Segoe UI,sans-serif}
header{padding:14px 16px;border-bottom:1px solid var(--line);display:flex;gap:16px;align-items:baseline;flex-wrap:wrap}
h1{font-size:18px;margin:0}h2{font-size:13px;margin:0 0 8px;color:var(--dim);text-transform:uppercase;letter-spacing:.06em}
main{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:12px;padding:12px 16px;max-width:1400px}
section{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:12px;min-width:0}
.big{font-size:22px;font-weight:600}.dim{color:var(--dim)}.ok{color:var(--ok)}.warn{color:var(--warn)}.bad{color:var(--bad)}
table{width:100%;border-collapse:collapse}td,th{padding:4px 6px;border-bottom:1px solid var(--line);text-align:left;vertical-align:top}th{color:var(--dim);font-weight:500}
button{background:var(--accent);color:#fff;border:0;border-radius:6px;padding:7px 12px;font:inherit;cursor:pointer;margin:0 6px 6px 0}
button.alt{background:#2c3442}button:disabled{opacity:.5;cursor:default}
textarea,input,select{width:100%;background:#0c0f13;color:var(--text);border:1px solid var(--line);border-radius:6px;padding:6px;font:13px/1.4 ui-monospace,Consolas,monospace}
textarea{min-height:150px;resize:vertical}label{display:block;margin:6px 0 2px;color:var(--dim)}
.row{display:flex;gap:8px}.row>*{flex:1}.feed{max-height:340px;overflow:auto;font:12px/1.5 ui-monospace,Consolas,monospace;white-space:pre-wrap;word-break:break-word}
.pill{display:inline-block;padding:1px 8px;border-radius:10px;background:#2c3442;font-size:12px}
#msg{min-height:1.4em}
</style></head><body>
<header><h1>Blur LAN Server</h1><span id="conn" class="dim">connecting…</span></header>
<main>
<section><h2>Lobby</h2><div class="big" id="state">–</div><div id="race" class="dim"></div><div id="need" class="dim"></div>
<div style="margin-top:10px"><button id="b-start">Start now</button><button id="b-skip" class="alt">Skip to next race</button></div><div id="msg" class="dim"></div></section>
<section><h2>Players</h2><table><thead><tr><th>Name</th><th>State</th></tr></thead><tbody id="players"></tbody></table></section>
<section><h2>Settings</h2><div class="row"><div><label>Countdown (s)</label><input id="s-countdown" type="number" min="5"></div>
<div><label>Players needed</label><input id="s-min" type="number" min="1"></div>
<div><label>Order</label><select id="s-order"><option value="inorder">in order</option><option value="shuffle">shuffle</option></select></div></div>
<div style="margin-top:10px"><button id="b-settings">Save settings</button></div></section>
)",
R"(<section><h2>Playlist</h2><div class="dim">One race per line: <code>mode track laps class</code>, e.g. <code>race random 2 A</code> or <code>motormash random 3 any</code>. Empty: random tracks.</div>
<textarea id="playlist" spellcheck="false"></textarea><div style="margin-top:8px"><button id="b-playlist">Save playlist</button></div>
<details><summary class="dim">Tracks</summary><div id="tracks" class="feed"></div></details></section>
<section><h2>Race events</h2><div id="events" class="feed"></div></section>
<section><h2>Server log</h2><div id="log" class="feed"></div></section>
</main>
<script>
let logSeq=0,evSeq=0,loaded=false,key=localStorage.getItem('blurkey')||'';
const $=id=>document.getElementById(id);
const esc=s=>String(s).replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]));
const stateText={WaitingForHost:'Waiting for players',Countdown:'Countdown',Ready:'Starting race',Waiting:'Waiting',Menus:'On its way to the lobby'};
function add(el,html){const atBottom=el.scrollTop+el.clientHeight>=el.scrollHeight-4;el.insertAdjacentHTML('beforeend',html);
 while(el.childNodes.length>300)el.removeChild(el.firstChild);if(atBottom)el.scrollTop=el.scrollHeight;}
function who(p){return p&&typeof p==='object'?(p.name||('#'+p.id)):'';}
function eventText(e){const t=e.t!==undefined?Number(e.t).toFixed(1).padStart(6)+'  ':'';switch(e.event){
 case 'race_start':return 'Race start: '+(e.route||'')+' ('+(e.mode||'')+(e.laps?', '+e.laps+' laps':'')+', '+(e.players||0)+' cars)';
 case 'player':return '';
 case 'lap':return t+who(e.player)+' lap '+e.lap+(e.lap_time!==undefined?' ('+Number(e.lap_time).toFixed(2)+' s)':'');
 case 'overtake':return t+who(e.player)+' overtook '+who(e.overtaken);
 case 'powerup_hit':return t+who(e.player)+' hit '+who(e.hit)+(e.powerup?' ('+e.powerup+')':'');
 case 'wreck':return t+who(e.wrecked)+' wrecked'+(e.player?' by '+who(e.player):'');
 case 'finish':return t+who(e.player)+(e.finish_order?' finished #'+e.finish_order:' '+(e.state||'out'))+(e.race_time?' ('+Number(e.race_time).toFixed(2)+' s)':'');
 case 'result':return 'Result '+(e.position||'')+': '+who(e.player)+(e.state&&e.state!=='finished'?' ('+e.state+')':'');
 case 'race_end':return 'Race end';
 default:return e.event||JSON.stringify(e);}}
)",
R"(function render(d){
 const s=d.server||{};$('conn').textContent=s.lobby?'online':'game starting…';$('conn').className=s.lobby?'ok':'dim';
 let st=stateText[s.lobby]||s.lobby||'–';if(s.race_state&&s.race_state!=='None')st='Race: '+s.race_state;
 if(s.lobby==='Countdown'&&s.race_state==='None')st+=' '+Math.ceil(s.timer||0)+' s';$('state').textContent=st;
 $('race').textContent=s.current?('Race: '+s.current):'';
 $('need').textContent=s.lobby==='WaitingForHost'?('Players: '+(s.players_ready||0)+' of '+(s.min_players||1)+' needed'):('Players: '+(s.players_ready||0));
 $('players').innerHTML=(d.players||[]).map(p=>'<tr><td>'+esc(p.name)+(p.server?' <span class="pill">server</span>':'')+'</td><td>'+esc(p.state)+'</td></tr>').join('')||'<tr><td class="dim" colspan="2">nobody yet</td></tr>';
 if(!loaded&&s.lobby){loaded=true;$('s-countdown').value=s.countdown;$('s-min').value=s.min_players;$('s-order').value=s.order||'inorder';
  $('playlist').value=(s.playlist||[]).join('\n');
  const t=s.tracks||{};$('tracks').textContent=Object.keys(t).map(m=>m+':\n  '+t[m].join('\n  ')).join('\n\n');}
 (d.log||[]).forEach(l=>add($('log'),'<div>'+esc(l)+'</div>'));
 (d.events||[]).forEach(e=>{const x=eventText(e);if(x)add($('events'),'<div>'+esc(x)+'</div>');});
 logSeq=d.log_seq;evSeq=d.ev_seq;}
async function poll(){try{const r=await fetch('/api/state?log='+logSeq+'&ev='+evSeq,{cache:'no-store'});render(await r.json());}
 catch(e){$('conn').textContent='no connection';$('conn').className='bad';}setTimeout(poll,1000);}
async function cmd(c,body){let r=await fetch('/api/cmd?c='+c+'&key='+encodeURIComponent(key),{method:'POST',body:body||''});
 if(r.status===403){key=prompt('Server password (server_web_password)')||'';localStorage.setItem('blurkey',key);
  r=await fetch('/api/cmd?c='+c+'&key='+encodeURIComponent(key),{method:'POST',body:body||''});}
 $('msg').textContent=r.ok?'Sent.':'Not accepted ('+r.status+')';setTimeout(()=>$('msg').textContent='',3000);}
$('b-start').onclick=()=>cmd('start');$('b-skip').onclick=()=>cmd('skip');
$('b-playlist').onclick=()=>cmd('playlist',$('playlist').value);
$('b-settings').onclick=()=>cmd('settings','countdown='+$('s-countdown').value+'\nmin_players='+$('s-min').value+'\norder='+$('s-order').value);
poll();
</script></body></html>
)",
};

// ---------------------------------------------------------------------------------------------------------------
// HTTP

static bool ServerWebQueryValue( const char* pQuery, const char* pName, char* pValue, const size_t uSize )
{
	pValue[ 0 ] = '\0';
	const size_t nameLength = strlen( pName );
	for ( const char* p = pQuery; p && *p; )
	{
		if ( strncmp( p, pName, nameLength ) == 0 && p[ nameLength ] == '=' )
		{
			p += nameLength + 1;
			size_t out = 0;
			while ( *p && *p != '&' && out + 1 < uSize )
			{
				if ( *p == '%' && isxdigit( (unsigned char)p[ 1 ] ) && isxdigit( (unsigned char)p[ 2 ] ) )
				{
					const char hex[ 3 ] = { p[ 1 ], p[ 2 ], 0 };
					pValue[ out++ ] = (char)strtol( hex, NULL, 16 );
					p += 3;
				}
				else
				{
					pValue[ out++ ] = ( *p == '+' ) ? ' ' : *p;
					p++;
				}
			}
			pValue[ out ] = '\0';
			return true;
		}
		p = strchr( p, '&' );
		if ( p )
			p++;
	}
	return false;
}

static void ServerWebSend( SOCKET client, const char* pStatus, const char* pType, const char* pBody, const size_t length )
{
	char header[ 256 ];
	const int headerLength = _snprintf_s( header, sizeof( header ), _TRUNCATE,
		"HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
		pStatus, pType, (unsigned)length );
	send( client, header, headerLength, 0 );
	size_t sent = 0;
	while ( sent < length )
	{
		const int chunk = send( client, pBody + sent, (int)( length - sent ), 0 );
		if ( chunk <= 0 )
			break;
		sent += chunk;
	}
}

static void ServerWebAppendLines( C_ServerWebText& text, char ( *pRing )[ kServerWebLineSize ], const U32 seq, U32 since, const bool asJson )
{
	if ( since > seq )
		since = 0;	// the server restarted
	if ( seq - since > kServerWebRingSize )
		since = seq - kServerWebRingSize;
	bool first = true;
	for ( U32 i = since; i < seq; i++ )
	{
		if ( !first )
			text.Append( "," );
		first = false;
		if ( asJson )
			text.Append( pRing[ i % kServerWebRingSize ] );
		else
			text.AppendString( pRing[ i % kServerWebRingSize ] );
	}
}

static void ServerWebAnswer( SOCKET client, const char* pRequest, const char* pBody )
{
	char method[ 8 ] = "";
	char target[ 512 ] = "";
	sscanf_s( pRequest, "%7s %511s", method, (unsigned)sizeof( method ), target, (unsigned)sizeof( target ) );
	char* pQuery = strchr( target, '?' );
	if ( pQuery )
		*pQuery++ = '\0';

	if ( strcmp( method, "GET" ) == 0 && ( strcmp( target, "/" ) == 0 || strcmp( target, "/index.html" ) == 0 ) )
	{
		C_ServerWebText page;
		for ( size_t i = 0; i < sizeof( s_ServerWebPage ) / sizeof( s_ServerWebPage[ 0 ] ); i++ )
			page.Append( s_ServerWebPage[ i ] );
		ServerWebSend( client, "200 OK", "text/html; charset=utf-8", page.Text(), page.Length() );
		return;
	}

	if ( strcmp( method, "GET" ) == 0 && strcmp( target, "/api/state" ) == 0 )
	{
		char value[ 16 ];
		const U32 logSince = ServerWebQueryValue( pQuery, "log", value, sizeof( value ) ) ? (U32)strtoul( value, NULL, 10 ) : 0;
		const U32 eventSince = ServerWebQueryValue( pQuery, "ev", value, sizeof( value ) ) ? (U32)strtoul( value, NULL, 10 ) : 0;

		C_ServerWebText text;
		EnterCriticalSection( &s_ServerWebLock );
		text.Append( "{" );
		text.Append( s_ServerWebSnapshot ? s_ServerWebSnapshot : "\"server\":null,\"players\":[]" );
		text.Append( ",\"log\":[" );
		ServerWebAppendLines( text, s_ServerWebLog, s_ServerWebLogSeq, logSince, false );
		text.Append( "],\"events\":[" );
		ServerWebAppendLines( text, s_ServerWebEvents, s_ServerWebEventSeq, eventSince, true );
		text.AppendF( "],\"log_seq\":%u,\"ev_seq\":%u}", s_ServerWebLogSeq, s_ServerWebEventSeq );
		LeaveCriticalSection( &s_ServerWebLock );
		ServerWebSend( client, "200 OK", "application/json", text.Text(), text.Length() );
		return;
	}

	if ( strcmp( method, "POST" ) == 0 && strcmp( target, "/api/cmd" ) == 0 )
	{
		char name[ 32 ];
		char key[ 64 ];
		ServerWebQueryValue( pQuery, "c", name, sizeof( name ) );
		ServerWebQueryValue( pQuery, "key", key, sizeof( key ) );
		if ( s_ServerWebPassword[ 0 ] && strcmp( key, s_ServerWebPassword ) != 0 )
		{
			ServerWebSend( client, "403 Forbidden", "text/plain", "password", 8 );
			return;
		}
		bool queued = false;
		EnterCriticalSection( &s_ServerWebLock );
		if ( name[ 0 ] && s_ServerWebCommandCount < kServerWebMaxCommands )
		{
			ServerWebCommand& command = s_ServerWebCommands[ s_ServerWebCommandCount++ ];
			strcpy_s( command.mName, name );
			strncpy_s( command.mArg, pBody ? pBody : "", _TRUNCATE );
			queued = true;
		}
		LeaveCriticalSection( &s_ServerWebLock );
		if ( queued )
			ServerWebSend( client, "200 OK", "application/json", "{\"ok\":true}", 11 );
		else
			ServerWebSend( client, "503 Service Unavailable", "text/plain", "busy", 4 );
		return;
	}

	ServerWebSend( client, "404 Not Found", "text/plain", "not found", 9 );
}

static DWORD WINAPI ServerWebThread( LPVOID pParam )
{
	SOCKET listener = (SOCKET)pParam;
	static char s_request[ 16384 ];
	for ( ;; )
	{
		SOCKET client = accept( listener, NULL, NULL );
		if ( client == INVALID_SOCKET )
		{
			Sleep( 100 );
			continue;
		}
		DWORD timeout = 3000;
		setsockopt( client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof( timeout ) );

		// headers, then as much of the body as Content-Length says
		int length = 0;
		char* pBody = NULL;
		int bodyLength = 0;
		while ( length < (int)sizeof( s_request ) - 1 )
		{
			const int got = recv( client, s_request + length, (int)sizeof( s_request ) - 1 - length, 0 );
			if ( got <= 0 )
				break;
			length += got;
			s_request[ length ] = '\0';
			if ( !pBody )
			{
				char* pEnd = strstr( s_request, "\r\n\r\n" );
				if ( pEnd )
				{
					*pEnd = '\0';
					pBody = pEnd + 4;
					const char* pLength = strstr( s_request, "Content-Length:" );
					if ( !pLength )
						pLength = strstr( s_request, "content-length:" );
					bodyLength = pLength ? atoi( pLength + 15 ) : 0;
				}
			}
			if ( pBody && ( s_request + length ) - pBody >= bodyLength )
				break;
		}
		s_request[ length ] = '\0';
		if ( pBody )
		{
			if ( bodyLength >= 0 && pBody + bodyLength <= s_request + length )
				pBody[ bodyLength ] = '\0';
			ServerWebAnswer( client, s_request, pBody );
		}
		shutdown( client, SD_SEND );
		closesocket( client );
	}
}

static void ServerWebStart( const U16 port )
{
	InitializeCriticalSection( &s_ServerWebLock );
	s_ServerWebStarted = true;

	WSADATA wsaData;
	if ( WSAStartup( MAKEWORD( 2, 2 ), &wsaData ) != 0 )
		return;
	SOCKET listener = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
	if ( listener == INVALID_SOCKET )
		return;
	sockaddr_in address;
	memset( &address, 0, sizeof( address ) );
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl( INADDR_ANY );
	address.sin_port = htons( port );
	if ( bind( listener, (const sockaddr*)&address, sizeof( address ) ) != 0 || listen( listener, 8 ) != 0 )
	{
		char line[ 128 ];
		sprintf_s( line, "web page: port %u is in use (server_web_port)", (unsigned)port );
		ServerWebAddLine( s_ServerWebLog, s_ServerWebLogSeq, line );
		closesocket( listener );
		return;
	}
	HANDLE thread = CreateThread( NULL, 0, ServerWebThread, (LPVOID)listener, 0, NULL );
	if ( thread )
		CloseHandle( thread );
}

// ---------------------------------------------------------------------------------------------------------------
// game thread

void C_GameLoggingManager::ServerWebUpdate( const char* pServerJson )
{
	if ( !s_ServerWebStarted )
	{
		char value[ 64 ];
		const U16 port = UI::GetBlurRebuiltSetting( "server_web_port", value, sizeof( value ) ) ? (U16)atoi( value ) : 8080;
		if ( UI::GetBlurRebuiltSetting( "server_web_password", value, sizeof( value ) ) )
			strcpy_s( s_ServerWebPassword, value );
		if ( port == 0 )
			return;
		ServerWebStart( port );
		char line[ 96 ];
		sprintf_s( line, "web page on port %u", (unsigned)port );
		ServerWebAddLine( s_ServerWebLog, s_ServerWebLogSeq, line );
	}

	C_ServerWebText text;
	text.Append( "\"server\":" );
	text.Append( pServerJson && pServerJson[ 0 ] ? pServerJson : "null" );
	text.Append( ",\"players\":[" );
	RacerNet::C_NetServices& services = RacerNet::C_NetServices::Instance();
	if ( services.IsInitialised() )
	{
		RacerNet::C_NetPlayerManager& mgr = services.PlayerManager();
		bool first = true;
		for ( U8 i = 0; i < mgr.TotalSize(); i++ )
		{
			RacerNet::C_NetPlayer* p = mgr.Index( i );
			if ( !p || !p->m_RaceConnection.IsActive() )
				continue;
			char name[ 64 ] = "";
			if ( const WCHAR* pName = p->m_User.Name() )
				WideCharToMultiByte( CP_UTF8, 0, pName, -1, name, sizeof( name ), NULL, NULL );
			text.Append( first ? "{" : ",{" );
			first = false;
			text.AppendF( "\"id\":%u,\"server\":%s,\"state\":", (unsigned)p->m_RaceConnection.m_JoinId, p->LocalPlayer() ? "true" : "false" );
			text.AppendString( ServerWebPlayerState( (S32)p->m_RaceConnection.m_PlayerState ) );
			text.Append( ",\"name\":" );
			text.AppendString( name );
			text.Append( "}" );
		}
	}
	text.Append( "]" );

	EnterCriticalSection( &s_ServerWebLock );
	free( s_ServerWebSnapshot );
	s_ServerWebSnapshot = text.Take();
	LeaveCriticalSection( &s_ServerWebLock );
}

void C_GameLoggingManager::ServerWebLog( const char* pLine )
{
	ServerWebAddLine( s_ServerWebLog, s_ServerWebLogSeq, pLine );
}

bool C_GameLoggingManager::ServerWebTakeCommand( char* pName, const size_t uNameSize, char* pArg, const size_t uArgSize )
{
	if ( !s_ServerWebStarted )
		return false;
	bool taken = false;
	EnterCriticalSection( &s_ServerWebLock );
	if ( s_ServerWebCommandCount > 0 )
	{
		strncpy_s( pName, uNameSize, s_ServerWebCommands[ 0 ].mName, _TRUNCATE );
		strncpy_s( pArg, uArgSize, s_ServerWebCommands[ 0 ].mArg, _TRUNCATE );
		memmove( &s_ServerWebCommands[ 0 ], &s_ServerWebCommands[ 1 ], ( s_ServerWebCommandCount - 1 ) * sizeof( ServerWebCommand ) );
		s_ServerWebCommandCount--;
		taken = true;
	}
	LeaveCriticalSection( &s_ServerWebLock );
	return taken;
}
