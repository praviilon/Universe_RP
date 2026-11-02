/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// cg_info.c -- display information while data is being loading

#include "cg_local.h"

#define MAX_LOADING_PLAYER_ICONS	16
#define MAX_LOADING_ITEM_ICONS		26

//static int			loadingPlayerIconCount;
//static qhandle_t	loadingPlayerIcons[MAX_LOADING_PLAYER_ICONS];

void CG_LoadBar(void);

/*
======================
CG_LoadingString

======================
*/
void CG_LoadingString( const char *s ) {
	Q_strncpyz( cg.infoScreenText, s, sizeof( cg.infoScreenText ) );

	trap->UpdateScreen();
}

/*
===================
CG_LoadingItem
===================
*/
void CG_LoadingItem( int itemNum ) {
	gitem_t		*item;
	char	upperKey[1024];

	item = &bg_itemlist[itemNum];

	if (!item->classname || !item->classname[0])
	{
	//	CG_LoadingString( "Unknown item" );
		return;
	}

	strcpy(upperKey, item->classname);
	CG_LoadingString( CG_GetStringEdString("SP_INGAME",Q_strupr(upperKey)) );
}

/*
===================
CG_LoadingClient
===================
*/
void CG_LoadingClient( int clientNum ) {
	const char		*info;
	char			personality[MAX_QPATH];

	info = CG_ConfigString( CS_PLAYERS + clientNum );

/*
	char			model[MAX_QPATH];
	char			iconName[MAX_QPATH];
	char			*skin;
	if ( loadingPlayerIconCount < MAX_LOADING_PLAYER_ICONS ) {
		Q_strncpyz( model, Info_ValueForKey( info, "model" ), sizeof( model ) );
		skin = Q_strrchr( model, '/' );
		if ( skin ) {
			*skin++ = '\0';
		} else {
			skin = "default";
		}

		Com_sprintf( iconName, MAX_QPATH, "models/players/%s/icon_%s.tga", model, skin );

		loadingPlayerIcons[loadingPlayerIconCount] = trap->R_RegisterShaderNoMip( iconName );
		if ( !loadingPlayerIcons[loadingPlayerIconCount] ) {
			Com_sprintf( iconName, MAX_QPATH, "models/players/characters/%s/icon_%s.tga", model, skin );
			loadingPlayerIcons[loadingPlayerIconCount] = trap->R_RegisterShaderNoMip( iconName );
		}
		if ( !loadingPlayerIcons[loadingPlayerIconCount] ) {
			Com_sprintf( iconName, MAX_QPATH, "models/players/%s/icon_%s.tga", DEFAULT_MODEL, "default" );
			loadingPlayerIcons[loadingPlayerIconCount] = trap->R_RegisterShaderNoMip( iconName );
		}
		if ( loadingPlayerIcons[loadingPlayerIconCount] ) {
			loadingPlayerIconCount++;
		}
	}
*/
	Q_strncpyz( personality, Info_ValueForKey( info, "n" ), sizeof(personality) );
//	Q_CleanStr( personality );

	/*
	if( cgs.gametype == GT_SINGLE_PLAYER ) {
		trap->S_RegisterSound( va( "sound/player/announce/%s.wav", personality ));
	}
	*/

	CG_LoadingString( personality );
}


/*
====================
CG_DrawInformation

Draw all the status / pacifier stuff during level loading
====================
overlays UI_DrawConnectScreen
*/
// GLX_U: [Loading] the levelshot and the load bar only (Alex cut the info text, this the last
// "Loading..." / "Awaiting snapshot..." line). cg.infoScreenText is still set by CG_LoadingString:
// it is the "still loading" flag CG_DrawActiveFrame checks.
//
// GLX_U fix: [Widescreen] the picture as TaystJK's cgame picks and places it. On a 16:9 or wider screen
// (cgs.widthRatioCoef at or under CG_WIDESCREEN_RATIO, the ui's own test for the connect screen) a map's
// levelshots_16_9 shot comes first, drawn at its 16:9 shape; then its levelshot, which on any screen narrower
// than 4:3's shape keeps its own shape -- the full width, a strip of the top and bottom cut off -- instead of
// being stretched sideways; then unknownmap_mp_16_9, the picture the connect screen shows there
// (connect_16_9.menu), so a map with no levelshot no longer switched from it to a stretched unknownmap_mp when
// the cgame took over; and unknownmap_mp last, over the whole screen as before. 4:3, 5:4 and cl_ratioFix 0
// (a coefficient of 1 or more) get exactly what they had.
#define CG_WIDESCREEN_RATIO		0.76f	// the ui's UI_WIDESCREEN_RATIO: 16:9 (0.75) and wider; 16:10 (0.83) is not

void CG_DrawInformation( void ) {
	const char	*s;
	const char	*info;
	const float	coef = cgs.widthRatioCoef;
	const qboolean wide = ( coef > 0.0f && coef <= CG_WIDESCREEN_RATIO ) ? qtrue : qfalse;
	qhandle_t	levelshot = 0;

	info = CG_ConfigString( CS_SERVERINFO );

	s = Info_ValueForKey( info, "mapname" );
	trap->R_SetColor( NULL );

	if ( wide ) {
		levelshot = trap->R_RegisterShaderNoMip( va( "levelshots_16_9/%s", s ) );
		if ( levelshot ) {	// a 16:9 picture: 480 * 0.75 / coef tall, centred (exact on 16:9)
			const float h = SCREEN_HEIGHT * ( 0.75f / coef );
			CG_DrawPic( 0, ( SCREEN_HEIGHT - h ) / 2, SCREEN_WIDTH, h, levelshot );
		}
	}

	if ( !levelshot ) {
		levelshot = trap->R_RegisterShaderNoMip( va( "levelshots/%s", s ) );
		if ( levelshot ) {
			if ( coef > 0.0f && coef < 1.0f ) {	// a 4:3 picture keeps its shape: 480 / coef tall, centred
				const float h = SCREEN_HEIGHT / coef;
				CG_DrawPic( 0, ( SCREEN_HEIGHT - h ) / 2, SCREEN_WIDTH, h, levelshot );
			} else {
				CG_DrawPic( 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, levelshot );
			}
		}
	}

	if ( !levelshot ) {
		if ( wide ) {
			levelshot = trap->R_RegisterShaderNoMip( "menu/art/unknownmap_mp_16_9" );
		}
		if ( !levelshot ) {
			levelshot = trap->R_RegisterShaderNoMip( "menu/art/unknownmap_mp" );
		}
		CG_DrawPic( 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, levelshot );
	}

	CG_LoadBar();

	// draw the icons of things as they are loaded
//	CG_DrawLoadingIcons();
}

/*
===================
CG_LoadBar
===================
*/
void CG_LoadBar(void)
{
	const int numticks = 9, tickwidth = 40, tickheight = 8;
	const int tickpadx = 20, tickpady = 12;
	const int capwidth = 8;
	const int barwidth = numticks*tickwidth+tickpadx*2+capwidth*2, barleft = ((640-barwidth)/2);
	const int barheight = tickheight + tickpady*2, bartop = 480-barheight;
	const int capleft = barleft+tickpadx, tickleft = capleft+capwidth, ticktop = bartop+tickpady;

	trap->R_SetColor( colorWhite );
	// Draw background
	CG_DrawPic(barleft, bartop, barwidth, barheight, cgs.media.loadBarLEDSurround);

	// Draw left cap (backwards)
	CG_DrawPic(tickleft, ticktop, -capwidth, tickheight, cgs.media.loadBarLEDCap);

	// Draw bar
	CG_DrawPic(tickleft, ticktop, tickwidth*cg.loadLCARSStage, tickheight, cgs.media.loadBarLED);

	// Draw right cap
	CG_DrawPic(tickleft+tickwidth*cg.loadLCARSStage, ticktop, capwidth, tickheight, cgs.media.loadBarLEDCap);
}

