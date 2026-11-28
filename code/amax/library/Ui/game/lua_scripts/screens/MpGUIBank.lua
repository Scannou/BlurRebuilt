UISystem.LoadLuaScript( "Screens\\Multiplayer\\Shared\\MpCustomRaceHelper.lua" )

function ServiceConnectionLost()
	-- only in multiplayer and if the user hasnt signed out since you get the disconect message next when a sign out occurs
	-- the xbox live disconnection can overide the lost connection to blur servers
	if Amax.GetGameMode() ~= UIEnums.GameMode.Online or ( UIGlobals.UserKickBackMode ~= UIEnums.UserKickBackMode.XLSPConnectionLost and UIGlobals.UserKickBackMode ~= UIEnums.UserKickBackMode.None ) then
		return
	end
	
	-- [Blur2PC] Retail PC keeps ordinary service loss on the current screen,
	-- allowing an active profile upload to finish or time out there.
	if Amax.GetGameMode() == UIEnums.GameMode.Online and UIGlobals.UserKickBackMode == UIEnums.UserKickBackMode.None then
		return
	end

	-- disable the logic while we are loading
	if UIGlobals.IsLoading == true then
		print( "ERROR - ServiceConnectionLost while on the loading screen" )
		return
	end

	print( "Connection to service lost!" )
	Amax.KickedOutOfGame();

	-- [Blur2PC] Retail retains the existing platform-disconnection reason.
	ReturnToStartScreen( UIGlobals.UserKickBackMode )
end

function XLSPConnectionLost()
	-- The 360 can live without demonware
	if UIEnums.CurrentPlatform == UIEnums.Platform.Xenon then
		return
	end	

	if Amax.GetGameMode() ~= UIEnums.GameMode.Online or UserKickBackActive() == true then
		return
	end

	-- disable the logic while we are loading
	if UIGlobals.IsLoading == true then
		print( "ERROR - XLSPConnectionLost while on the loading screen" )
		return
	end
	
	print( "Connection to LSP lost!" )
	Amax.KickedOutOfGame();

	ReturnToStartScreen( UIEnums.UserKickBackMode.XLSPConnectionLost )
end

function LobbyConnectionLost()
	if UserKickBackActive() == true or UIGlobals.IdlePlayerKicked == true then
		return
	end

	print( "LobbyConnectionLost!" )
	UIGlobals.Multiplayer.LobbyConnectionLost = true
	net_DisconnectFromRace()	
end

function NetworkCableRemoved()
	-- only listen in system link
	if Amax.GetGameMode() ~= UIEnums.GameMode.SystemLink or UserKickBackActive() == true then
		return
	end

	print( "NetworkCableRemoved!" )
	ReturnToStartScreen( UIEnums.UserKickBackMode.NetworkCableRemoved )
end

function MultiplayerBetaExpired()
	if UserKickBackActive() == true then
		return
	end
	
	print( "Multiplayer Beta has expired!" )
	
	ReturnToStartScreen( UIEnums.UserKickBackMode.MpBetaExpired )
end

function Mp_SetupMenuItem( uListID, sValue, uTextID, sIconName, sIconSheet )

	local root_id = UIButtons.CloneXtGadgetByName( "SCUIBank", "node_item" )
	local text_id = UIButtons.FindChildByName( root_id, "text" )
	local icon_id = UIButtons.FindChildByName( root_id, "icon" )

	if sIconSheet ~= nil then
		UIShape.ChangeSceneName( icon_id, sIconSheet )
	end

	UIButtons.ChangeText( text_id, uTextID )
	UIShape.ChangeObjectName( icon_id, sIconName )

	UIButtons.AddListItem( uListID, root_id, sValue )

	-- managle the animation name to stop it playing. A better
	-- way might be to attach the text to the help text and have the animation in that? Another day...
	UIButtons.SetXtVar( text_id, "time_lines.0.label", "unknown" )

	return root_id
end

function Mp_SetupMenuItemHelpText( uListID, sValue, uTextID, uHelpTextID, sIconName, sIconSheet, panel )

	local root_id = UIButtons.CloneXtGadgetByName( "SCUIBank", "node_item" )
	local help_id = UIButtons.CloneXtGadgetByName( "SCUIBank", "helptext" )
	local text_id = UIButtons.FindChildByName( root_id, "text" )
	local icon_id = UIButtons.FindChildByName( root_id, "icon" )

	if IsString( sIconSheet ) == true then
		UIShape.ChangeSceneName( icon_id, sIconSheet )
	end

	UIButtons.SetParent( help_id, text_id, UIEnums.Justify.BottomLeft )

	--local posX, posY, posZ = UIButtons.GetPosition( icon_id ) 
	--UIButtons.ChangePosition( icon_id, posX, posY - 1, posZ )

	UIButtons.ChangeText( text_id, uTextID )
	UIButtons.ChangeText( help_id, uHelpTextID )
	UIShape.ChangeObjectName( icon_id, sIconName )

	UIButtons.AddListItem( uListID, root_id, sValue )
	
	if IsNumber( panel ) == true then
		UIButtons.ChangePanel( root_id, panel, true )
		UIButtons.ChangePanel( help_id, panel, true )
	end

	return root_id
end

function Mp_SetupMenuItemCheckbox( uListID, sValue, uTextID, sIconName, sIconSheet )

	local root_id = UIButtons.CloneXtGadgetByName( "SCUIBank", "node_item" )
	local check_id = UIButtons.CloneXtGadgetByName( "SCUIBank", "checkbox_item" )
	local text_id = UIButtons.FindChildByName( root_id, "text" )
	local icon_id = UIButtons.FindChildByName( root_id, "icon" )

	if sIconSheet ~= nil then
		UIShape.ChangeSceneName( icon_id, sIconSheet )
	end

	UIButtons.SetParent( check_id, root_id, UIEnums.Justify.MiddleRight )
	UIButtons.ChangeText( text_id, uTextID )
	UIShape.ChangeObjectName( icon_id, sIconName )

	UIButtons.AddListItem( uListID, root_id, sValue )
	return root_id
end

function Mp_LockMenuItem( uListID, sIndex, sUnlockRank )

	local node_id = UIButtons.GetNodeID( uListID, sIndex )
	local text_id = UIButtons.FindChildByName( node_id, "text" )
	local icon_id = UIButtons.FindChildByName( node_id, "icon" )

	UIButtons.ChangeColour( text_id, "dark_white" )
	UIButtons.ChangeColour( icon_id, "dark_white" )

	-- decide weather to override the helptext with the rank
	if sUnlockRank ~= nil then
		local helptext_id = UIButtons.FindChildByName( node_id, "helptext" )

		UIButtons.ChangeColour( helptext_id, "dark_white" )
		UIButtons.ChangeText( helptext_id, "MPL_UNLOCK_AT_RANK" .. sUnlockRank )
	end

	UIButtons.SetNodeItemLocked( uListID, sIndex, true )
end

function Mp_UnlockMenuItem( uListID, sIndex )

	local node_id = UIButtons.GetNodeID( uListID, sIndex )
	local text_id = UIButtons.FindChildByName( node_id, "text" )
	local icon_id = UIButtons.FindChildByName( node_id, "icon" )

	UIButtons.ChangeColour( text_id, "NonBloomWhite" )
	UIButtons.ChangeColour( icon_id, "Main_2" )

	UIButtons.SetNodeItemLocked( uListID, sIndex, false )
end

function Mp_ShowItemNew( uListID, sIndex )
	local node_id = UIButtons.GetNodeID( uListID, sIndex )

	UIButtons.SetActive( UIButtons.FindChildByName( node_id, "new" ), true )
end

function Mp_HideItemNew( uListID, sIndex )
	local node_id = UIButtons.GetNodeID( uListID, sIndex )

	UIButtons.SetActive( UIButtons.FindChildByName( node_id, "new" ), false )
end

function Mp_SetupGroupDetailItem( sList, uText, sIcon, sScene, bValue )
	local node_id = UIButtons.CloneXtGadgetByName( "SCUIBank", "detail_node" )
	local icon_id = UIButtons.FindChildByName( node_id, "detail_icon" )

	UIShape.ChangeSceneName( icon_id, sScene )
	UIShape.ChangeObjectName( icon_id, sIcon )

	UIButtons.AddListItem( sList, node_id )

	return node_id
end

function Mp_RankIcon( id, rank, legend )

	if legend ~= nil and rank ~= nil then
		-- offset legend by max rank
		if legend > 0 then
			rank = Multiplayer.GetMaxRank() + legend - 1
		end

		local texture = 
		{
			filename = "rank_icons",
			name = "rank_icons",
			pos = { u = 0.015625 * rank, v = 0 },
			size = { u = 0.015625, v = 1 },
		}
		UIButtons.ChangeTexture( texture, 0, id )
	end
end


function Mp_RouteThumbnail( id, name, index )
	if name == nil then
		return
	end

	if index == nil then
		index = 0
	end

	local texture = 
	{
		filename = name,
		name = name,
		pos = { u = 0 , v = 0 },
		size = { u = 1, v = 1 },
	}
	UIButtons.ChangeTexture( texture, index, id )
end


function Mp_RaceTypeIcon( id, race )
	if race == nil then
		return
	end

	local icon_name = ""
	if race.type == UIEnums.MpRaceType.Racing then
		icon_name = "style_racing"
	elseif race.type == UIEnums.MpRaceType.Elimination then
		icon_name = "style_elimination"
	elseif race.type == UIEnums.MpRaceType.Points then
		icon_name = "style_elimination"
	elseif race.type == UIEnums.MpRaceType.Destruction then
		icon_name = "survivor"
	elseif race.type == UIEnums.MpRaceType.Hardcore then
		icon_name = "style_racing"	
	end
	UIShape.ChangeObjectName( id, icon_name )
end

function Mp_RaceEndIcon( id, race )
	if race == nil then
		return
	end

	local icon_name = ""
	if race.type == UIEnums.MpRaceType.Racing then
		if race.laps > 0 then
			icon_name = "lap"
		else
			icon_name = "stopwatch"
		end
	elseif race.type == UIEnums.MpRaceType.Elimination then
		icon_name = "stopwatch"
	elseif race.type == UIEnums.MpRaceType.Points then
		if race.laps > 0 then
			icon_name = "lap"
		elseif race.score_limit > 0 then
			icon_name = "style_points"
		else
			icon_name = "stopwatch"
		end
	elseif race.type == UIEnums.MpRaceType.Destruction then
		icon_name = "stopwatch"
	end
	UIShape.ChangeObjectName( id, icon_name )
end

function Mp_RaceWinIcon( id, race )
	if race == nil then
		return
	end

	UIShape.ChangeObjectName( id, "end_of_race" )
end

function Mp_ReturnToPartyLobby()
	local _GUI = ContextTable[ UIEnums.Context.Main ].GUI
	
	-- [WillB] - close all screens
	FlushSubscreens()

	if UIGlobals.IsIngame == true then
		UIGlobals.Multiplayer.LaunchScreen = UIEnums.MpLaunchScreen.PartyLobby
		GoScreen( "Loading\\LoadingUi.lua" )
	else
		if UIScreen.GetCurrentScreen( UIEnums.Context.CarouselApp ) ~= GUINetwork.multiplayer_party_root then
			GoScreen( GUINetwork.multiplayer_party_root, UIEnums.Context.CarouselApp )
		end
	end
end

function Mp_RefreshRaceOverview( node_id, race_data, race_index, centre_text )

	if race_index == nil then
		race_index = Multiplayer.GetCurrentRaceIndex()
	end

	local prefix = Select( (Amax.GetPlayMode() == UIEnums.PlayMode.Playlist), "MPL_PLAYLIST_RACE" .. race_index, "MPL_CUSTOM_RACE" )

	local city_id = UIButtons.FindChildByName( node_id, "city" )
	local route_id = UIButtons.FindChildByName( node_id, "route" )

	-- setup city and route name
	UIButtons.ChangeText( city_id, prefix .. "_CITY" )
	UIButtons.ChangeText( route_id, prefix .. "_ROUTE" )

	-- game mode and car class
	UIButtons.ChangeText( UIButtons.FindChildByName( node_id, "mode" ), prefix .. "_TYPE" )
	UIButtons.ChangeText( UIButtons.FindChildByName( node_id, "class" ), prefix .. "_CLASS" )

	-- setup game limits( either time or laps )
	local laps_id		= UIButtons.FindChildByName( node_id, "laps" )
	local max_laps_id	= UIButtons.FindChildByName( node_id, "num_laps" )
	local time_id		= UIButtons.FindChildByName( node_id, "time" )

	if race_data.type == UIEnums.MpRaceType.Racing or race_data.type == UIEnums.MpRaceType.Hardcore then
		if laps_id ~= nil and max_laps_id ~= nil and time_id ~= nil then
			UIButtons.SetActive( laps_id, true, true )
			UIButtons.SetActive( time_id, false, true )

			UIButtons.ChangeText( max_laps_id, prefix .. "_LAPS" )
			UIButtons.ChangeText( laps_id, prefix .. "_LAP_TEXT" )
		end
	else
		if laps_id ~= nil and max_laps_id ~= nil and time_id ~= nil then
			UIButtons.SetActive( laps_id, false, true )
			UIButtons.SetActive( time_id, true, true )

			UIButtons.ChangeText( time_id, prefix .. "_TIME" )
		end
	end

	-- change icons
	local city_icon_id = UIButtons.FindChildByName( node_id, "city_icon" )
	if city_icon_id ~= nil then
		UIShape.ChangeObjectName( city_icon_id, Amax.GetCityIconName( race_data.city ) )
	end
	
	local class_icon_id = UIButtons.FindChildByName( node_id, "class_icon" )
	if class_icon_id ~= nil then
		UIShape.ChangeObjectName( class_icon_id, UIGlobals.ClassIcons[race_data.class] )
	end

	Mp_RaceTypeIcon( UIButtons.FindChildByName( node_id, "mode_icon" ), race_data )

	-- setup thumbnail image
	local thumbnail_id = UIButtons.FindChildByName( node_id, "thumbnail" )
	if thumbnail_id ~= nil then
		Mp_RouteThumbnail( thumbnail_id, race_data.thumbnail, 1 )
	end

	-- offset the city and route based on the longest item
	if centre_text ~= false then
		local city_size_x = UIButtons.GetStaticTextLength( city_id )
		local route_size_x = UIButtons.GetStaticTextLength( route_id )

		local pos_x, pos_y = UIButtons.GetPosition( city_id )

		if city_size_x > route_size_x then
			UIButtons.ChangePosition( city_id, 2.5 - ( city_size_x * 0.5 ), pos_y, 0 )
		else
			UIButtons.ChangePosition( city_id, 2.5 - ( route_size_x * 0.5 ), pos_y, 0 )
		end
	end
end

function Mp_SetupPlayerProgressBar( root_id )
	-- progress bar data
	GUI.Progression		= Multiplayer.ProfileProgression()
	local level_data	= Multiplayer.LevelProgressionData( GUI.Progression.level )
	
	local previous		= level_data.fans_previous
	local target_fans	= level_data.fans_needed

	local range = target_fans - previous
	local amount = 1
	
	if GUI.Progression.maxed == false then
		amount = ( GUI.Progression.fans - previous ) / range
	end

	local size_x, size_y, size_z = UIButtons.GetSize( UIButtons.FindChildByName( root_id, "backing" ) )
	UIButtons.ChangeSize( UIButtons.FindChildByName( root_id, "progress" ), size_x * amount, size_y, size_z )
end


-- should we allow the user to bring up the guide on xbox 360 to send an invite. 
-- party may be full or host may be out of lobby.
function Mp_XenonGameInviteAvailable()
	if UIEnums.CurrentPlatform == UIEnums.Platform.Xenon then
		if NetServices.GameInviteBeingSent( ) == false then
			if NetRace.CanSendGameInvite( ) == true then
				return true				
			end
		end
	end
	
	return false
end

function Mp_XenonPartyInviteAvailable()
	if UIEnums.CurrentPlatform == UIEnums.Platform.Xenon then
		if NetServices.GameInviteBeingSent( ) == false then
			if NetParty.CanSendGameInvite( ) == true then
				return true				
			end
		end
	end
	
	return false
end

function Mp_XenonPartyActive()
	if UIEnums.CurrentPlatform ~= UIEnums.Platform.Xenon then
		return false
	end
		
	return NetServices.IsInExternalPartySession()
end

function Mp_ResetCustomRace()
	local defaults = Multiplayer.GetCustomRaceDefaults()

	UIGlobals.CustomRaceSettings = 
	{
		num_players	= nil,
		game_mode	= defaults.game_mode,
		city_id		= defaults.city,
		route_id	= defaults.route,
		laps		= defaults.laps,
		time		= defaults.time,
		class		= defaults.class,
		damage		= defaults.damage,
		respawn		= defaults.respawn,
		timeout		= defaults.timeout,
		mods		= defaults.mods,
		upgrades	= defaults.upgrades,
		lock_host	= defaults.lock_host,
		ai			= defaults.ai,
		ai_padding	= defaults.ai_padding,
		handicap	= defaults.handicap,

		powerups =
		{
			nitro	= true,
			repair	= true,
			shock	= true,
			barge	= true,
			mine	= true,
			shunt	= true,
			bolt	= true,
			shield	= true,
		},
		powerup_respawn = defaults.powerup_respawn,
		random_powerups = defaults.random_powerups,
	}
end

function Mp_ReadCustomRaceSettings()
	local settings = Multiplayer.GetCustomRaceSettings()

	UIGlobals.CustomRaceSettings = 
	{
		num_players	= Select( Amax.GetGameMode() == UIEnums.GameMode.SplitScreen, #UIGlobals.Splitscreen.players, 1 ),
		game_mode	= settings.game_mode,
		city_id		= settings.city_id,
		route_id	= settings.route_id,
		laps		= settings.laps,
		time		= settings.time,
		class		= settings.class,
		damage		= settings.damage,
		respawn		= settings.respawn,
		timeout		= settings.timeout,
		mods		= settings.mods,
		upgrades	= settings.upgrades,
		lock_host	= settings.lock_host,
		ai			= settings.ai,
		ai_padding	= settings.ai_padding,
		handicap	= settings.handicap,

		powerups		= settings.powerups,
		powerup_respawn = settings.powerup_respawn,
		random_powerups = settings.random_powerups,
	}
end

function Mp_ResetSpectateVars()
	UIGlobals.Mp_SpectateCurrentVehicle = Amax.GetPlayerVehicleIndex( 0 )
	UIGlobals.Mp_SpectateCurrentCamera = 1
end

-- [Blur2PC] Not retail: dedicated LAN server (server_mode=1 in BlurRebuilt.ini). Nobody sits at the server: from the
-- start screen it hosts a LAN custom race through the menus' own handlers, as a player pressing the buttons would,
-- then MpLobby_ServerUpdate (MpLobby.lua) starts races when players are in, and RaceModeMultiplayer.cpp takes the
-- server's own car out of each race. UiManager.cpp calls Blur2PC_ServerUpdate once per UI frame and registers the
-- Blur2PCServer helpers (Log, Setting, SaveSetting, WriteFile, Contexts, Screen, Random, RaceState, WebStatus,
-- WebCommand) only in server mode. The web page (GameLoggingServerWeb.inl) shows Blur2PC_ServerPublish's state and
-- queues the commands Blur2PC_ServerCommands carries out.
Blur2PCServerState =
{
	wait			= 0,		-- seconds until the next menu step, so each screen has time to react
	step			= nil,		-- last logged menu step
	popup			= false,	-- a popup was open at the last check
	race_index		= 0,		-- last playlist entry used
	playlist		= nil,		-- parsed server_race lines
	tracks_written	= false,
	hold			= 0,		-- seconds waited at the end of a countdown for players still in the garage
	last_players	= -1,
	last_note		= nil,
	overrides		= {},		-- settings changed on the web page this run (also saved to BlurRebuilt.ini)
	force_start		= false,	-- web page: start now
	force_skip		= false,	-- web page: skip to the next race
	lobby_state		= nil,		-- MpLobby_ServerUpdate: lobby state name, timer, players for the web page
	lobby_timer		= 0,
	players_ready	= 0,
	current			= nil,		-- the lobby's race as set by the server
	tracks_json		= nil,
	status_wait		= 0,
}

function Blur2PC_IsServer()
	return Blur2PCServer ~= nil
end

function Blur2PC_ServerLog( text )
	if Blur2PCServer ~= nil then
		Blur2PCServer.Log( text )
	end
end

-- a setting: changed on the web page this run, else from BlurRebuilt.ini; nil when missing or empty
function Blur2PC_ServerSetting( key )
	value = Blur2PCServerState.overrides[ key ]
	if value == nil then
		value = Blur2PCServer.Setting( key )
	end
	if value == "" then
		return nil
	end
	return value
end

function Blur2PC_ServerNumber( key, default )
	local value = tonumber( Blur2PC_ServerSetting( key ) or "" )
	if value == nil then
		return default
	end
	return value
end

-- server_skip_tracks: words matched against track and city names, left out of random picks (default "dumbo": the NY
-- Dumbo tracks do not load on some copies of the game; "server_skip_tracks=none" leaves nothing out)
function Blur2PC_ServerSkipWords()
	local words = {}
	local line = string.lower( Blur2PC_ServerSetting( "server_skip_tracks" ) or "dumbo" )
	for word in string.gmatch( line, "[^%s,]+" ) do
		if word ~= "none" then
			table.insert( words, word )
		end
	end
	return words
end

-- every track of a mode: { city, route, tag, point_to_point, skip }
function Blur2PC_ServerTracks( mode )
	local skip_words = Blur2PC_ServerSkipWords()
	local tracks = {}
	for _, city in ipairs( Multiplayer.GetCustomRaceCities( mode ) ) do
		for _, route in ipairs( Multiplayer.GetCustomRaceRoutes( city.id, mode ) ) do
			local track = { city = city.id, route = route.id, tag = string.lower( route.debug_tag ), point_to_point = route.point_to_point, skip = false }
			local names = track.tag .. " " .. string.lower( city.debug_tag or "" )
			for _, word in ipairs( skip_words ) do
				if string.find( names, word, 1, true ) ~= nil then
					track.skip = true
				end
			end
			table.insert( tracks, track )
		end
	end
	return tracks
end

-- server_tracks.txt beside BlurRebuilt.ini lists the track names server_race lines take
function Blur2PC_ServerWriteTracks()
	Blur2PCServerState.tracks_written = true
	local text = "; Tracks for server_race lines in BlurRebuilt.ini (written by the server at every start)\n"
	for _, mode in ipairs( { { "race", UIEnums.MpRaceType.Racing }, { "motormash", UIEnums.MpRaceType.Destruction } } ) do
		text = text .. "\n" .. mode[1] .. ":\n"
		for _, track in ipairs( Blur2PC_ServerTracks( mode[2] ) ) do
			text = text .. "  " .. track.tag .. Select( track.point_to_point == true, "   (one lap only)", "" ) .. Select( track.skip == true, "   (left out of random picks: server_skip_tracks)", "" ) .. "\n"
		end
	end
	Blur2PCServer.WriteFile( "server_tracks.txt", text )

	local json = {}
	for _, mode in ipairs( { { "race", UIEnums.MpRaceType.Racing }, { "motormash", UIEnums.MpRaceType.Destruction } } ) do
		local names = {}
		for _, track in ipairs( Blur2PC_ServerTracks( mode[2] ) ) do
			table.insert( names, Blur2PC_Json( track.tag .. Select( track.point_to_point == true, " (one lap)", "" ) .. Select( track.skip == true, " (left out of random)", "" ) ) )
		end
		table.insert( json, Blur2PC_Json( mode[1] ) .. ":[" .. table.concat( names, "," ) .. "]" )
	end
	Blur2PCServerState.tracks_json = "{" .. table.concat( json, "," ) .. "}"
end

-- a JSON string
function Blur2PC_Json( text )
	text = string.gsub( tostring( text ), '[%c"\\]', function( c ) return string.format( "\\u%04x", string.byte( c ) ) end )
	return '"' .. text .. '"'
end

-- server_race1 ... server_race32: "<race|motormash> <track|random> <laps or minutes> <A-E|any>"; the last three may be
-- left out (random track, the lobby's laps or time and class)
function Blur2PC_ServerPlaylist()
	if Blur2PCServerState.playlist ~= nil then
		return Blur2PCServerState.playlist
	end
	local playlist = {}
	for i = 1, 32 do
		local line = Blur2PCServer.Setting( "server_race" .. i )
		if line ~= nil and string.find( line, "%S" ) ~= nil then
			local entry = Blur2PC_ServerParseRace( line )
			if entry == nil then
				Blur2PC_ServerLog( "server_race" .. i .. " ignored, not understood: " .. line )
			else
				table.insert( playlist, entry )
			end
		end
	end
	Blur2PC_ServerLog( #playlist .. " playlist races" )
	Blur2PCServerState.playlist = playlist
	return playlist
end

-- one playlist line; nil when it is not understood
function Blur2PC_ServerParseRace( line )
	local classes = { a = UIEnums.MpVehicleClass.ClassA, b = UIEnums.MpVehicleClass.ClassB, c = UIEnums.MpVehicleClass.ClassC,
					  d = UIEnums.MpVehicleClass.ClassD, e = UIEnums.MpVehicleClass.ClassE, any = UIEnums.MpVehicleClass.Any }
	local words = {}
	for word in string.gmatch( string.lower( line ), "%S+" ) do
		table.insert( words, word )
	end
	local entry = { text = line, track = words[2] or "random", limit = tonumber( words[3] or "" ), class = classes[ words[4] or "" ] }
	if words[1] == "motormash" or words[1] == "mash" or words[1] == "destruction" then
		entry.mode = UIEnums.MpRaceType.Destruction
	elseif words[1] == "race" then
		entry.mode = UIEnums.MpRaceType.Racing
	end
	if entry.mode == nil or ( words[3] ~= nil and entry.limit == nil ) or ( words[4] ~= nil and entry.class == nil ) then
		return nil
	end
	return entry
end

-- web page commands; run every UI frame
function Blur2PC_ServerCommands()
	local S = Blur2PCServerState
	local name, arg = Blur2PCServer.WebCommand()
	while name ~= nil do
		if name == "start" or name == "skip" then
			if UIGlobals.Multiplayer.InLobby == true then
				S[ Select( name == "start", "force_start", "force_skip" ) ] = true
				Blur2PC_ServerLog( "web page: " .. Select( name == "start", "start now", "skip to the next race" ) )
			else
				Blur2PC_ServerLog( "web page: " .. name .. " ignored, the server is not in the lobby" )
			end

		elseif name == "playlist" then
			local playlist = {}
			local bad = 0
			for line in string.gmatch( arg .. "\n", "([^\r\n]*)\r?\n" ) do
				if string.find( line, "%S" ) ~= nil then
					local entry = Blur2PC_ServerParseRace( line )
					if entry ~= nil and #playlist < 32 then
						table.insert( playlist, entry )
					else
						bad = bad + 1
						Blur2PC_ServerLog( "web page: playlist line not understood: " .. line )
					end
				end
			end
			for i = 1, 32 do
				Blur2PCServer.SaveSetting( "server_race" .. i, Select( i <= #playlist, playlist[i] ~= nil and playlist[i].text or "", "" ) )
			end
			S.playlist = playlist
			S.race_index = 0
			Blur2PC_ServerLog( "web page: playlist of " .. #playlist .. " races saved" .. Select( bad > 0, ", " .. bad .. " lines left out", "" ) )

		elseif name == "settings" then
			for key, value in string.gmatch( arg, "([%w_]+)=([^\r\n]*)" ) do
				local ini_key = "server_" .. key
				if key == "countdown" or key == "min_players" or key == "order" or key == "skip_tracks" then
					S.overrides[ ini_key ] = value
					Blur2PCServer.SaveSetting( ini_key, value )
				end
			end
			Blur2PC_ServerLog( "web page: settings saved" )
		end
		name, arg = Blur2PCServer.WebCommand()
	end
end

-- the server's state for the web page, twice a second
function Blur2PC_ServerPublish( dt )
	local S = Blur2PCServerState
	S.status_wait = S.status_wait - dt
	if S.status_wait > 0 then
		return
	end
	S.status_wait = 0.5

	local lobby = "Menus"
	if UIGlobals.Multiplayer.InLobby == true and S.lobby_state ~= nil then
		lobby = S.lobby_state
	end
	local playlist = {}
	for _, entry in ipairs( Blur2PC_ServerPlaylist() ) do
		table.insert( playlist, Blur2PC_Json( entry.text ) )
	end
	local json = "{" ..
		'"lobby":' .. Blur2PC_Json( lobby ) ..
		',"timer":' .. string.format( "%.1f", S.lobby_timer or 0 ) ..
		',"race_state":' .. Blur2PC_Json( Blur2PCServer.RaceState() ) ..
		',"players_ready":' .. ( S.players_ready or 0 ) ..
		',"min_players":' .. math.max( 1, Blur2PC_ServerNumber( "server_min_players", 1 ) ) ..
		',"countdown":' .. math.max( 5, Blur2PC_ServerNumber( "server_countdown", 60 ) ) ..
		',"order":' .. Blur2PC_Json( string.lower( Blur2PC_ServerSetting( "server_order" ) or "inorder" ) ) ..
		',"skip_tracks":' .. Blur2PC_Json( Blur2PC_ServerSetting( "server_skip_tracks" ) or "dumbo" ) ..
		',"current":' .. Select( S.current ~= nil, Blur2PC_Json( S.current or "" ), "null" ) ..
		',"playlist":[' .. table.concat( playlist, "," ) .. "]" ..
		',"tracks":' .. ( S.tracks_json or "{}" ) .. "}"
	Blur2PCServer.WebStatus( json )
end

-- sets the next race of the playlist (or a random track) as the lobby's race and sends it to the players, the way the
-- custom race screen does when the host changes it
function Blur2PC_ServerApplyNextRace()
	local S = Blur2PCServerState
	local playlist = Blur2PC_ServerPlaylist()
	Mp_ReadCustomRaceSettings()
	local settings = UIGlobals.CustomRaceSettings

	local entry = { mode = settings.game_mode, track = "random" }
	if #playlist > 0 then
		local order = string.lower( Blur2PC_ServerSetting( "server_order" ) or "" )
		if order == "shuffle" and #playlist > 1 then
			local index = S.race_index
			while index == S.race_index do
				index = Blur2PCServer.Random( #playlist )
			end
			S.race_index = index
		else
			S.race_index = Select( S.race_index >= #playlist, 1, S.race_index + 1 )
		end
		entry = playlist[ S.race_index ]
	end

	local tracks = Blur2PC_ServerTracks( entry.mode )
	if #tracks == 0 then
		Blur2PC_ServerLog( "no tracks for mode " .. tostring( entry.mode ) .. ", lobby race kept" )
		return
	end
	local pick = nil
	if entry.track ~= "random" then
		for _, track in ipairs( tracks ) do
			if track.tag == entry.track then
				pick = track
			end
		end
		if pick == nil then
			Blur2PC_ServerLog( "unknown track " .. entry.track .. " (see server_tracks.txt), random track instead" )
		end
	end
	if pick == nil then
		local candidates = {}
		for _, track in ipairs( tracks ) do
			if track.skip == false then
				table.insert( candidates, track )
			end
		end
		if #candidates == 0 then
			Blur2PC_ServerLog( "server_skip_tracks leaves out every track, lobby race kept" )
			return
		end
		pick = candidates[ Blur2PCServer.Random( #candidates ) ]
	end

	local limits = Multiplayer.GetCustomRaceLimits()
	settings.game_mode	= entry.mode
	settings.city_id	= pick.city
	settings.route_id	= pick.route
	if entry.mode == UIEnums.MpRaceType.Destruction then
		local time = entry.limit or settings.time
		time = math.max( limits.time_step, math.min( limits.max_time, time ) )
		settings.time = math.floor( time / limits.time_step ) * limits.time_step
		settings.laps = 1
	else
		local laps = entry.limit or settings.laps
		settings.laps = Select( pick.point_to_point == true, 1, math.max( 1, math.min( limits.max_laps, laps ) ) )
		settings.time = 1
	end
	if entry.class ~= nil then
		settings.class = entry.class
	end

	Amax.SetupRace( settings )
	NetRace.SendNewRaceSettings()
	S.current = pick.tag .. Select( entry.mode == UIEnums.MpRaceType.Destruction, ", Motor Mash " .. settings.time, ", " .. settings.laps .. " laps" )
	Blur2PC_ServerLog( "next race: " .. pick.tag .. Select( entry.mode == UIEnums.MpRaceType.Destruction, ", " .. settings.time .. " time", ", " .. settings.laps .. " laps" ) .. ", class " .. tostring( settings.class ) .. Select( entry.text ~= nil, " (" .. tostring( entry.text ) .. ")", "" ) )
end

-- the context showing a screen, or nil
function Blur2PC_ServerFindScreen( screen )
	for i = 0, Blur2PCServer.Contexts() - 1 do
		if Blur2PCServer.Screen( i ) == screen then
			return i
		end
	end
	return nil
end

-- one menu step: logged, then run in that context after the UI manager returns it
local function ServerStep( context, text, action, wait )
	if Blur2PCServerState.step ~= text then
		Blur2PCServerState.step = text
		Blur2PC_ServerLog( text )
	end
	Blur2PCServerState.wait = wait or 1
	return context, action
end

-- called by UiManager.cpp each UI frame in server mode, with the main context's GUI bound; returns nothing, or a
-- context and a function to run there
function Blur2PC_ServerUpdate( dt )
	local S = Blur2PCServerState
	Blur2PC_ServerCommands()
	Blur2PC_ServerPublish( dt )
	S.wait = S.wait - dt
	if S.wait > 0 then
		return
	end

	local popup = UIScreen.IsPopupActive()
	if popup ~= S.popup then
		S.popup = popup
		Blur2PC_ServerLog( Select( popup == true, "popup open, waiting", "popup closed" ) )
	end
	if popup == true then
		return
	end

	-- the lobby and races run on their own (MpLobby.lua, RaceModeMultiplayer.cpp)
	-- (the lobby opens as a sub screen, so its own flag says when it is up)
	if UIGlobals.Multiplayer.InLobby == true or Blur2PC_ServerFindScreen( [[multiplayer\mplobby.lua]] ) ~= nil or Blur2PCServer.RaceState() ~= "None" then
		S.step = nil
		return
	end

	local context = Blur2PC_ServerFindScreen( [[intro\previouslyonblur.lua]] )
	if context ~= nil then
		return ServerStep( context, "closing Previously on Blur", function()
			ContextTable[ context ].MessageUpdate( UIEnums.Message.MenuNext, 0, true )
		end )
	end

	local main = Blur2PCServer.Screen( 0 )
	if main == [[intro\startscreen.lua]] and GUI ~= nil and UIScreen.GetScreenActive( UIEnums.Context.Main ) == true then
		if GUI.current_stage == GUI.STAGE_PRESS_START then
			return ServerStep( 0, "start screen: press start", function()
				ContextTable[ UIEnums.Context.Main ].MessageUpdate( UIEnums.Message.MenuNext, 0, true )
			end )
		elseif GUI.current_stage == GUI.STAGE_MODE_SELECT and UIGlobals.ProfileBootFinished == true then
			if GUI.CurrentMode ~= GUI.Mode.Multiplayer then
				return ServerStep( 0, "start screen: multiplayer", function()
					ContextTable[ UIEnums.Context.Main ].MessageUpdate( UIEnums.Message.ButtonRight, 0, true )
				end )
			end
			return ServerStep( 0, "start screen: enter multiplayer", function()
				ContextTable[ UIEnums.Context.Main ].MessageUpdate( UIEnums.Message.MenuNext, 0, true )
			end )
		end
		return
	end

	context = Blur2PC_ServerFindScreen( [[multiplayer\shared\mpmodeselect.lua]] )
	if context ~= nil then
		return ServerStep( context, "mode select: custom race (hosts the LAN game)", function()
			UIButtons.SetSelection( GUI.menu_id, GUI.options.custom_race )
			ContextTable[ context ].MessageUpdate( UIEnums.Message.MenuNext, 0, true )
		end )
	end

	context = Blur2PC_ServerFindScreen( [[multiplayer\mplan.lua]] )
	if context ~= nil then
		return ServerStep( context, "LAN games: host a game", function()
			ContextTable[ context ].MessageUpdate( UIEnums.Message.ButtonY, 0, true )
		end )
	end

	if main == [[multiplayer\mpmain.lua]] and GUI ~= nil and GUI.active_selection == -1 and IsControllerLocked() == false then
		local ftime, selection = UIButtons.GetSelection( GUI.carousel_id, 2 )
		if selection ~= UIEnums.RwMpBranch.Lan then
			return ServerStep( 0, "multiplayer: select LAN", function()
				UIButtons.SetSelection( GUI.carousel_id, UIEnums.RwMpBranch.Lan )
			end )
		end
		-- the LAN service connection opens the LAN screen once it is up
		return ServerStep( 0, "multiplayer: open LAN", function()
			ContextTable[ UIEnums.Context.Main ].MessageUpdate( UIEnums.Message.MenuNext, 0, true )
		end, 3 )
	end
end
