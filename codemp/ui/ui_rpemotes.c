/*
===========================================================================
GLX_U: [Emotes Menu] the Emotes menu of the Extras (ingame_rpx_emotes.menu): /emote, /anim, /wehug and /wekiss in a
menu, with a preview of the player's own character.

- Three tabs (ui_rpx_em_tab) over one list (FEEDER_RPX_EMOTES), each sorted by name:
  0 Emotes: /emote's emotes (rpEmotes[], bg_rpemotes.h, which the server uses too);
  1 Animations: /anim's named animations (animations[]), with their category, which the filter matches too;
  2 All animations: every animation the character's animation file has frames for, by its own name
    (BOTH_STAND1 ...); /anim takes it as its number.
  Each tab keeps its selected row (ui_rpx_em_sel0..2, a name, so it is found again under any filter); the filter
  (ui_rpx_filter_emotes) is cleared when the tab changes. In red: the character's animation file lacks it.
- The preview (UI_RPX_EMPREVIEW): the character the server has for this player (its CS_PLAYERS model; the model
  cvar while there is none), turned and zoomed by ui_rpx_em_yaw and ui_rpx_em_zoom, playing the selected row as
  the game plays it for a player standing still, on the whole body (bg_pmove.c, HANDEXTEND_TAUNT and _DODGE):
  an emote's first animation, held a moment when it is a held one, then its ending, again and again; an
  animation at its own speed, looped when its file says so, else held a moment and played again. The frames
  are read here from the animation.cfg next to the model's .gla -- not with UI_ParseAnimationFile(), which
  drops the game on a file over 60000 bytes. No saber, weapon, colour tint, or other player for Hug and Kiss.
- Play (and a double click on the selected row), Hug and Kiss: cgame builds the command (rpx do emote | anim |
  animnum | wehug | wekiss, cg_rpextras.c) and the menus close, so the player sees it and can move. Why the
  server refuses one (emotes off, the player's state) is its own message.
===========================================================================
*/

#include "ui_local.h"
#include "ui/rp_extras.h"
#include "game/bg_rpemotes.h"
#include "ghoul2/G2.h"	// the bone animation flags

extern void Text_Paint( float x, float y, float scale, vec4_t color, const char *text, float adjust, int limit, int style, int iMenuFont );
extern int Text_Width( const char *text, float scale, int iMenuFont );
extern int Item_ListBox_MaxScroll( itemDef_t *item );
extern void UI_InsertG2Pointer( void *ghoul2 );
extern void UI_ClearG2Pointer( void *ghoul2 );
extern stringID_table_t animTable[MAX_ANIMATIONS+1];

#define RPEM_MENU			"ingame_rpx_emotes"
#define RPEM_CFG_SIZE		0x20000		// the largest animation.cfg read (_humanoid's is some 56 KB)
#define RPEM_HOLD_SHOW		1500		// a held emote's first animation, or an animation's last frame, shown this long
#define RPEM_END_PAUSE		1200		// after an emote's ending, before it plays again
#define RPEM_BLEND			150
#define RPEM_FOV			40.0f

typedef enum {
	RPEM_EMOTES,
	RPEM_ANIMS,
	RPEM_ALL,
	RPEM_NUM_TABS
} rpemTab_t;

static const char * const rpemSelCvars[RPEM_NUM_TABS] = { "ui_rpx_em_sel0", "ui_rpx_em_sel1", "ui_rpx_em_sel2" };

typedef enum {
	RPEM_CFG_NONE,		// no character, or not read yet
	RPEM_CFG_OK,
	RPEM_CFG_MISSING,	// no animation.cfg next to its .gla, or none with an animation in it
	RPEM_CFG_TOOBIG
} rpemCfgState_t;

// the character previewed
static struct {
	char			model[MAX_QPATH];		// what it is made from: "name/skin" as the server or cvar has it
	qboolean		made;					// tried for that model
	void			*ghoul2;
	char			cfg[MAX_QPATH];			// its animation.cfg
	rpemCfgState_t	cfgState;
	animation_t		anims[MAX_ANIMATIONS];	// by animation number; numFrames 0: not in the file
	int				numAnims;				// with frames
} rpemChar;

static char			rpemCfgText[RPEM_CFG_SIZE + 1];

// the animations' own names, by number (from animTable)
static const char	*rpemAnimNames[MAX_ANIMATIONS];
static qboolean		rpemNamesMade;

// each tab's rows, sorted: an index into rpEmotes, into animations, or an animation number
static int			rpemSorted[RPEM_NUM_TABS][MAX_ANIMATIONS];
static int			rpemSortedNum[RPEM_NUM_TABS];

// the list as shown: indices into rpemSorted[tab]'s items (the items themselves)
static int			rpemView[MAX_ANIMATIONS];
static int			rpemViewNum;
static int			rpemTabSeen = -1;
static char			rpemFilterSeen[64];
static qboolean		rpemListStale = qtrue;

static int			rpemSyncTime = -1;
static int			rpemMapId;

// what the preview plays
static struct {
	int			tab;
	int			item;		// -1: none, the character stands
	int			stage;		// 0 the first animation, 1 an emote's ending
	int			stageEnd;	// when the stage is over (realTime); 0 never
	int			anim;		// playing now; -1 none
	qboolean	missing;	// the row's animation is not in the character's file
} rpemPlay = { 0, -1, 0, 0, -1, qfalse };

/*
===========================================================================
Helpers
===========================================================================
*/

static void RPEM_Cvar( const char *name, char *buf, int size ) {
	trap->Cvar_VariableStringBuffer( name, buf, size );
}

static int RPEM_CvarInt( const char *name ) {
	char buf[32];

	trap->Cvar_VariableStringBuffer( name, buf, sizeof( buf ) );
	return atoi( buf );
}

static void RPEM_Msg( const char *text ) {
	trap->Cvar_Set( "ui_rpx_msg", text );
}

static int RPEM_Tab( void ) {
	int tab = RPEM_CvarInt( "ui_rpx_em_tab" );

	return ( tab >= 0 && tab < RPEM_NUM_TABS ) ? tab : RPEM_EMOTES;
}

// case-insensitive "needle is in hay"; an empty needle is in everything
static qboolean RPEM_Contains( const char *hay, const char *needle ) {
	int n = strlen( needle ), i;

	if ( !n ) {
		return qtrue;
	}
	for ( i = 0; hay[i]; i++ ) {
		if ( !Q_stricmpn( hay + i, needle, n ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

static void RPEM_MakeNames( void ) {
	int i;

	if ( rpemNamesMade ) {
		return;
	}
	rpemNamesMade = qtrue;
	for ( i = 0; animTable[i].name; i++ ) {
		if ( animTable[i].id >= 0 && animTable[i].id < MAX_ANIMATIONS ) {
			rpemAnimNames[animTable[i].id] = animTable[i].name;
		}
	}
}

// a row's name (what its tab's selection cvar holds)
static const char *RPEM_ItemName( int tab, int item ) {
	switch ( tab ) {
	case RPEM_EMOTES:
		return ( item >= 0 && item < RP_NUM_EMOTES ) ? rpEmotes[item].name : "";
	case RPEM_ANIMS:
		return ( item >= 0 && item < MAX_WORDED_EMOTES && animations[item].animation_name ) ? animations[item].animation_name : "";
	default:
		return ( item >= 0 && item < MAX_ANIMATIONS && rpemAnimNames[item] ) ? rpemAnimNames[item] : "";
	}
}

// a name's row on a tab, or -1
static int RPEM_FindItem( int tab, const char *name ) {
	int i;

	if ( !name[0] ) {
		return -1;
	}
	if ( tab == RPEM_ALL ) {
		i = GetIDForString( animTable, name );
		return ( i >= 0 && i < MAX_ANIMATIONS ) ? i : -1;
	}
	for ( i = 0; i < ( tab == RPEM_EMOTES ? RP_NUM_EMOTES : MAX_WORDED_EMOTES ); i++ ) {
		if ( !Q_stricmp( RPEM_ItemName( tab, i ), name ) ) {
			return i;
		}
	}
	return -1;
}

// the character's file has this animation; qtrue while the file is not known, so nothing is marked then
static qboolean RPEM_HasAnim( int anim ) {
	if ( rpemChar.cfgState != RPEM_CFG_OK ) {
		return qtrue;
	}
	return ( anim >= 0 && anim < MAX_ANIMATIONS && rpemChar.anims[anim].numFrames > 0 ) ? qtrue : qfalse;
}

static qboolean RPEM_ItemInFile( int tab, int item ) {
	switch ( tab ) {
	case RPEM_EMOTES:
		return ( RPEM_HasAnim( rpEmotes[item].anim ) && RPEM_HasAnim( rpEmotes[item].leave ) ) ? qtrue : qfalse;
	case RPEM_ANIMS:
		return RPEM_HasAnim( animations[item].animation_code );
	default:
		return RPEM_HasAnim( item );
	}
}

static itemDef_t *RPEM_List( void ) {
	menuDef_t *menu = Menus_FindByName( RPEM_MENU );
	int i;

	if ( !menu ) {
		return NULL;
	}
	for ( i = 0; i < menu->itemCount; i++ ) {
		if ( menu->items[i]->type == ITEM_TYPE_LISTBOX && menu->items[i]->special == FEEDER_RPX_EMOTES ) {
			return menu->items[i];
		}
	}
	return NULL;
}

// the selected row, scrolled into view; -1 for none (as RPSH_SetCursor(), ui_rpshaders.c)
static void RPEM_SetCursor( int index ) {
	itemDef_t *item = RPEM_List();
	listBoxDef_t *listPtr;

	if ( !item || !item->typeData.listbox ) {
		return;
	}
	listPtr = item->typeData.listbox;
	item->cursorPos = index;
	listPtr->cursorPos = ( index >= 0 ) ? index : 0;
	if ( index < 0 ) {
		listPtr->startPos = 0;
	} else if ( index < listPtr->startPos || listPtr->endPos <= 0 || index > listPtr->endPos ) {
		int max = Item_ListBox_MaxScroll( item );

		listPtr->startPos = ( index > 3 ) ? index - 3 : 0;
		if ( listPtr->startPos > max ) {
			listPtr->startPos = max;
		}
	}
}

static void RPEM_CloseMenus( void ) {
	trap->Key_SetCatcher( trap->Key_GetCatcher() & ~KEYCATCH_UI );
	trap->Key_ClearStates();
	trap->Cvar_Set( "cl_paused", "0" );
	Menus_CloseAll();
}

/*
===========================================================================
The character
===========================================================================
*/

static void RPEM_FreeChar( void ) {
	if ( rpemChar.ghoul2 ) {
		UI_ClearG2Pointer( rpemChar.ghoul2 );
		trap->G2API_CleanGhoul2Models( &rpemChar.ghoul2 );
		rpemChar.ghoul2 = NULL;
	}
	rpemPlay.anim = -1;
}

static qboolean RPEM_FileExists( const char *path ) {
	fileHandle_t f = 0;
	int len = trap->FS_Open( path, &f, FS_READ );

	if ( f ) {
		trap->FS_Close( f );
	}
	return ( f && len > 0 ) ? qtrue : qfalse;
}

// a model folder or skin name that stays a file name inside models/players/
static qboolean RPEM_PathPartOk( const char *s, qboolean skin ) {
	int i;

	if ( !s[0] || strstr( s, ".." ) ) {
		return qfalse;
	}
	for ( i = 0; s[i]; i++ ) {
		if ( s[i] == '/' || s[i] == '\\' || s[i] == ':' || s[i] == '"' || (unsigned char)s[i] < ' ' || ( s[i] == '|' && !skin ) ) {
			return qfalse;
		}
	}
	return qtrue;
}

// the player's model, "name/skin", as the server has it for this player; the model cvar until it has one
static void RPEM_ModelString( char *out, int size ) {
	uiClientState_t cs;
	char info[MAX_INFO_STRING];

	out[0] = '\0';
	memset( &cs, 0, sizeof( cs ) );
	trap->GetClientState( &cs );
	if ( cs.connState == CA_ACTIVE && cs.clientNum >= 0 && cs.clientNum < MAX_CLIENTS ) {
		info[0] = '\0';
		trap->GetConfigString( CS_PLAYERS + cs.clientNum, info, sizeof( info ) );
		Q_strncpyz( out, Info_ValueForKey( info, "model" ), size );
	}
	if ( !out[0] ) {
		RPEM_Cvar( "model", out, size );
	}
}

/*
The animation.cfg of the character's .gla: "NAME firstFrame numFrames loopFrames fps" a line, read as the game reads
it (BG_ParseAnimationFile(): fps 0 is 1, a negative one plays backwards). A name the game does not know takes its
line with it; an animation with no frames is left out (not in the file).
*/
static void RPEM_ReadAnims( const char *path ) {
	fileHandle_t f = 0;
	int len, i;
	const char *p;

	if ( !Q_stricmp( path, rpemChar.cfg ) && rpemChar.cfgState != RPEM_CFG_NONE ) {
		return;
	}
	Q_strncpyz( rpemChar.cfg, path, sizeof( rpemChar.cfg ) );
	memset( rpemChar.anims, 0, sizeof( rpemChar.anims ) );
	rpemChar.numAnims = 0;
	rpemChar.cfgState = RPEM_CFG_MISSING;
	rpemListStale = qtrue;

	if ( !path[0] ) {
		return;
	}
	len = trap->FS_Open( path, &f, FS_READ );
	if ( !f ) {
		return;
	}
	if ( len > RPEM_CFG_SIZE ) {
		trap->FS_Close( f );
		rpemChar.cfgState = RPEM_CFG_TOOBIG;
		return;
	}
	if ( len <= 0 ) {
		trap->FS_Close( f );
		return;
	}
	trap->FS_Read( rpemCfgText, len, f );
	trap->FS_Close( f );
	rpemCfgText[len] = '\0';

	p = rpemCfgText;
	COM_BeginParseSession( "RPEM_ReadAnims" );
	while ( 1 ) {
		char name[MAX_QPATH];
		int nums[3], anim, k;
		float fps = 0.0f;
		const char *token = COM_Parse( &p );

		if ( !token || !token[0] ) {
			break;
		}
		Q_strncpyz( name, token, sizeof( name ) );
		for ( k = 0; k < 4; k++ ) {
			token = COM_Parse( &p );
			if ( !token || !token[0] ) {
				break;
			}
			if ( k < 3 ) {
				nums[k] = atoi( token );
			} else {
				fps = atof( token );
			}
		}
		if ( k < 4 ) {
			break;
		}
		anim = GetIDForString( animTable, name );
		if ( anim < 0 || anim >= MAX_ANIMATIONS || nums[0] < 0 || nums[1] <= 0 ) {
			continue;
		}
		if ( fps == 0.0f ) {
			fps = 1.0f;
		}
		rpemChar.anims[anim].firstFrame = nums[0];
		rpemChar.anims[anim].numFrames = nums[1];
		rpemChar.anims[anim].loopFrames = nums[2];
		rpemChar.anims[anim].frameLerp = ( fps < 0.0f ) ? floor( 1000.0f / fps ) : ceil( 1000.0f / fps );
	}
	for ( i = 0; i < MAX_ANIMATIONS; i++ ) {
		if ( rpemChar.anims[i].numFrames > 0 ) {
			if ( !rpemChar.anims[i].frameLerp ) {	// fps over 1000 backwards: floor() of a value above -1
				rpemChar.anims[i].frameLerp = -1;
			}
			rpemChar.numAnims++;
		}
	}
	if ( rpemChar.numAnims ) {
		rpemChar.cfgState = RPEM_CFG_OK;
	}
}

static void RPEM_PlayStage( qboolean blend );

// the character made from the model, its skin as cgame sets it (CG_RegisterClientModelname()); kyle when the
// model cannot be loaded, as the game does
static void RPEM_MakeChar( const char *modelString ) {
	char model[MAX_QPATH], skinName[MAX_QPATH], path[MAX_QPATH * 2], gla[256];
	char *slash;
	qhandle_t skin = 0;
	int attempt;

	RPEM_FreeChar();
	Q_strncpyz( rpemChar.model, modelString, sizeof( rpemChar.model ) );
	rpemChar.made = qtrue;

	Q_strncpyz( model, modelString, sizeof( model ) );
	slash = strchr( model, '/' );
	if ( slash ) {
		*slash = '\0';
		Q_strncpyz( skinName, slash + 1, sizeof( skinName ) );
	} else {
		skinName[0] = '\0';
	}
	if ( !skinName[0] ) {
		Q_strncpyz( skinName, "default", sizeof( skinName ) );
	}

	for ( attempt = 0; attempt < 2 && !rpemChar.ghoul2; attempt++ ) {
		if ( attempt ) {
			Q_strncpyz( model, DEFAULT_MODEL, sizeof( model ) );
			Q_strncpyz( skinName, "default", sizeof( skinName ) );
		}
		if ( !RPEM_PathPartOk( model, qfalse ) || !RPEM_PathPartOk( skinName, qtrue ) ) {
			continue;
		}
		Com_sprintf( path, sizeof( path ), "models/players/%s/model.glm", model );
		if ( strlen( path ) >= MAX_QPATH ) {
			continue;
		}
		if ( trap->G2API_InitGhoul2Model( &rpemChar.ghoul2, path, 0, 0, 0, 0, 0 ) < 0 || !rpemChar.ghoul2 ||
			!trap->G2_HaveWeGhoul2Models( rpemChar.ghoul2 ) ) {
			if ( rpemChar.ghoul2 ) {
				trap->G2API_CleanGhoul2Models( &rpemChar.ghoul2 );
			}
			rpemChar.ghoul2 = NULL;
		}
	}
	if ( !rpemChar.ghoul2 ) {
		RPEM_ReadAnims( "" );
		return;
	}
	UI_InsertG2Pointer( rpemChar.ghoul2 );

	// the skin: a three-part one (head|torso|lower) as it is, else model_<skin>.skin, else the default
	if ( strchr( skinName, '|' ) && strstr( skinName, "head" ) && strstr( skinName, "torso" ) && strstr( skinName, "lower" ) ) {
		Com_sprintf( path, sizeof( path ), "models/players/%s/|%s", model, skinName );
		if ( strlen( path ) < MAX_QPATH ) {
			skin = trap->R_RegisterSkin( path );
		}
	} else if ( !strchr( skinName, '|' ) ) {
		Com_sprintf( path, sizeof( path ), "models/players/%s/model_%s.skin", model, skinName );
		if ( strlen( path ) < MAX_QPATH && RPEM_FileExists( path ) ) {
			skin = trap->R_RegisterSkin( path );
		}
	}
	if ( !skin ) {
		Com_sprintf( path, sizeof( path ), "models/players/%s/model_default.skin", model );
		if ( strlen( path ) < MAX_QPATH && RPEM_FileExists( path ) ) {
			skin = trap->R_RegisterSkin( path );
		}
	}
	if ( skin ) {
		trap->G2API_SetSkin( rpemChar.ghoul2, 0, skin, skin );
	}

	// its animation file: animation.cfg in its .gla's folder (as Item_Model_Paint() finds it)
	memset( gla, 0, sizeof( gla ) );
	trap->G2API_GetGLAName( rpemChar.ghoul2, 0, gla );
	gla[sizeof( gla ) - 1] = '\0';
	slash = strrchr( gla, '/' );
	if ( slash && (int)( slash - gla ) + 15 < MAX_QPATH ) {
		strcpy( slash, "/animation.cfg" );
		RPEM_ReadAnims( gla );
	} else {
		RPEM_ReadAnims( "" );
	}
	RPEM_PlayStage( qfalse );
}

// made again when the server gives the player another model, and after a map change
static void RPEM_CheckChar( void ) {
	char model[MAX_QPATH];
	int mapId = RPEM_CvarInt( "ui_rpx_mapid" );

	if ( mapId != rpemMapId ) {
		rpemMapId = mapId;
		RPEM_FreeChar();
		rpemChar.made = qfalse;
		rpemChar.cfgState = RPEM_CFG_NONE;	// files can change between maps
	}
	RPEM_ModelString( model, sizeof( model ) );
	if ( !rpemChar.made || Q_stricmp( model, rpemChar.model ) ) {
		RPEM_MakeChar( model );
	}
}

/*
===========================================================================
The preview's animation
===========================================================================
*/

static int RPEM_AnimLength( int anim ) {
	const animation_t *a = &rpemChar.anims[anim];

	return a->numFrames * abs( a->frameLerp );
}

// an animation on the whole body (model_root), as cgame sets it (CG_SetLerpFrameAnimation())
static void RPEM_SetAnim( int anim, qboolean blend ) {
	const animation_t *a;
	float speed;
	int first, last, flags;

	if ( !rpemChar.ghoul2 || anim < 0 || anim >= MAX_ANIMATIONS || rpemChar.anims[anim].numFrames <= 0 ) {
		return;
	}
	a = &rpemChar.anims[anim];
	speed = 50.0f / a->frameLerp;
	if ( speed < 0 ) {
		first = a->firstFrame + a->numFrames;
		last = a->firstFrame;
	} else {
		first = a->firstFrame;
		last = a->firstFrame + a->numFrames;
	}
	flags = ( a->loopFrames != -1 ) ? BONE_ANIM_OVERRIDE_LOOP : BONE_ANIM_OVERRIDE_FREEZE;
	if ( blend ) {
		flags |= BONE_ANIM_BLEND;
	}
	trap->G2API_SetBoneAnim( rpemChar.ghoul2, 0, "model_root", first, last, flags, speed, uiInfo.uiDC.realTime, -1, blend ? RPEM_BLEND : 0 );
	rpemPlay.anim = anim;
}

// the character standing, as when nothing is selected
static void RPEM_Stand( qboolean blend ) {
	if ( RPEM_HasAnim( BOTH_STAND1 ) && rpemChar.cfgState == RPEM_CFG_OK ) {
		RPEM_SetAnim( BOTH_STAND1, blend );
	} else if ( rpemPlay.anim != -1 ) {
		rpemChar.made = qfalse;	// a file without it: the model's base pose, made again at the next sync
	}
	rpemPlay.stageEnd = 0;
}

// the stage rpemPlay is at, from its start
static void RPEM_PlayStage( qboolean blend ) {
	int anim, end;

	rpemPlay.missing = qfalse;
	rpemPlay.stageEnd = 0;
	if ( !rpemChar.ghoul2 || rpemChar.cfgState != RPEM_CFG_OK ) {
		return;
	}
	if ( rpemPlay.item < 0 ) {
		RPEM_Stand( blend );
		return;
	}
	if ( !RPEM_ItemInFile( rpemPlay.tab, rpemPlay.item ) ) {
		rpemPlay.missing = qtrue;
		RPEM_Stand( blend );
		return;
	}

	if ( rpemPlay.tab == RPEM_EMOTES ) {
		const rp_emote_t *e = &rpEmotes[rpemPlay.item];

		anim = rpemPlay.stage ? e->leave : e->anim;
		end = RPEM_AnimLength( anim ) + ( rpemPlay.stage ? RPEM_END_PAUSE : ( ( e->flags & RP_EMF_HOLD ) ? RPEM_HOLD_SHOW : 0 ) );
	} else {
		anim = ( rpemPlay.tab == RPEM_ANIMS ) ? animations[rpemPlay.item].animation_code : rpemPlay.item;
		end = ( rpemChar.anims[anim].loopFrames != -1 ) ? 0 : RPEM_AnimLength( anim ) + RPEM_HOLD_SHOW;
	}
	RPEM_SetAnim( anim, blend );
	if ( end ) {
		rpemPlay.stageEnd = uiInfo.uiDC.realTime + ( end > 100 ? end : 100 );
		if ( !rpemPlay.stageEnd ) {
			rpemPlay.stageEnd = 1;
		}
	}
}

// a row's preview from its start; -1: none
static void RPEM_PlayStart( int tab, int item ) {
	rpemPlay.tab = tab;
	rpemPlay.item = item;
	rpemPlay.stage = 0;
	RPEM_PlayStage( qtrue );
}

// the next stage when this one is over: an emote's ending after its first animation, and the other way round
static void RPEM_PlayRun( void ) {
	if ( !rpemPlay.stageEnd || uiInfo.uiDC.realTime - rpemPlay.stageEnd < 0 ) {
		return;
	}
	rpemPlay.stage = ( rpemPlay.tab == RPEM_EMOTES && rpemPlay.item >= 0 ) ? !rpemPlay.stage : 0;
	RPEM_PlayStage( qtrue );
}

/*
===========================================================================
The list
===========================================================================
*/

static int rpemSortTab;

static int QDECL RPEM_ItemCompare( const void *a, const void *b ) {
	return Q_stricmp( RPEM_ItemName( rpemSortTab, *(const int *)a ), RPEM_ItemName( rpemSortTab, *(const int *)b ) );
}

// each tab's rows, sorted (the All animations tab's whenever the character's file changes)
static void RPEM_SortTabs( void ) {
	int tab, i;

	RPEM_MakeNames();
	for ( tab = 0; tab < RPEM_NUM_TABS; tab++ ) {
		int n = 0;

		if ( tab == RPEM_EMOTES ) {
			for ( i = 0; i < RP_NUM_EMOTES; i++ ) {
				rpemSorted[tab][n++] = i;
			}
		} else if ( tab == RPEM_ANIMS ) {
			for ( i = 0; i < MAX_WORDED_EMOTES; i++ ) {
				if ( animations[i].animation_name ) {
					rpemSorted[tab][n++] = i;
				}
			}
		} else if ( rpemChar.cfgState == RPEM_CFG_OK ) {
			for ( i = 0; i < MAX_ANIMATIONS; i++ ) {
				if ( rpemChar.anims[i].numFrames > 0 && rpemAnimNames[i] ) {
					rpemSorted[tab][n++] = i;
				}
			}
		}
		rpemSortTab = tab;
		qsort( rpemSorted[tab], n, sizeof( int ), RPEM_ItemCompare );
		rpemSortedNum[tab] = n;
	}
}

static void RPEM_BuildView( int tab, const char *filter ) {
	int i;

	rpemViewNum = 0;
	for ( i = 0; i < rpemSortedNum[tab]; i++ ) {
		int item = rpemSorted[tab][i];

		if ( RPEM_Contains( RPEM_ItemName( tab, item ), filter ) ||
			( tab == RPEM_ANIMS && animations[item].animation_category && RPEM_Contains( animations[item].animation_category, filter ) ) ) {
			rpemView[rpemViewNum++] = item;
		}
	}

	if ( tab == RPEM_ALL && rpemChar.cfgState != RPEM_CFG_OK ) {
		trap->Cvar_Set( "ui_rpx_em_info", !rpemChar.ghoul2 ? "^3Your character could not be loaded." :
			( rpemChar.cfgState == RPEM_CFG_TOOBIG ) ? "^3Your character's animation file is too big to read here." :
			"^3Your character's animation file could not be read." );
		return;
	}
	if ( rpemViewNum == rpemSortedNum[tab] ) {
		trap->Cvar_Set( "ui_rpx_em_info", va( "%d %s", rpemViewNum,
			tab == RPEM_EMOTES ? "emotes" : tab == RPEM_ANIMS ? "named animations" : "animations in your character's file" ) );
	} else {
		trap->Cvar_Set( "ui_rpx_em_info", va( "%d of %d %s", rpemViewNum, rpemSortedNum[tab],
			tab == RPEM_EMOTES ? "emotes" : tab == RPEM_ANIMS ? "named animations" : "animations in your character's file" ) );
	}
}

// the tab's selected row in the list as shown, or -1
static int RPEM_SelectedRow( int tab ) {
	char name[MAX_QPATH];
	int item, i;

	RPEM_Cvar( rpemSelCvars[tab], name, sizeof( name ) );
	item = RPEM_FindItem( tab, name );
	for ( i = 0; item >= 0 && i < rpemViewNum; i++ ) {
		if ( rpemView[i] == item ) {
			return i;
		}
	}
	return -1;
}

// the tab's selection, previewed (a name the tab does not have, or none: the character stands)
static void RPEM_PreviewSelection( int tab ) {
	char name[MAX_QPATH];
	int item;

	RPEM_Cvar( rpemSelCvars[tab], name, sizeof( name ) );
	item = RPEM_FindItem( tab, name );
	RPEM_PlayStart( tab, item );
}

static void RPEM_Sync( void ) {
	char filter[64];
	int tab;
	qboolean tabChanged;

	if ( rpemSyncTime == uiInfo.uiDC.realTime ) {
		return;
	}
	rpemSyncTime = uiInfo.uiDC.realTime;

	RPEM_CheckChar();

	tab = RPEM_Tab();
	RPEM_Cvar( "ui_rpx_filter_emotes", filter, sizeof( filter ) );
	tabChanged = ( tab != rpemTabSeen ) ? qtrue : qfalse;
	if ( !rpemListStale && !tabChanged && !strcmp( filter, rpemFilterSeen ) ) {
		return;
	}
	if ( rpemListStale ) {
		RPEM_SortTabs();
	}
	rpemTabSeen = tab;
	Q_strncpyz( rpemFilterSeen, filter, sizeof( rpemFilterSeen ) );
	RPEM_BuildView( tab, filter );
	RPEM_SetCursor( RPEM_SelectedRow( tab ) );
	if ( tabChanged || rpemListStale ) {
		RPEM_PreviewSelection( tab );
	}
	rpemListStale = qfalse;
}

/*
===========================================================================
The feeder
===========================================================================
*/

int UI_RpEmCount( void ) {
	RPEM_Sync();
	return rpemViewNum;
}

const char *UI_RpEmText( int index ) {
	int tab = rpemTabSeen, item;
	const char *red;

	if ( index < 0 || index >= rpemViewNum || tab < 0 ) {
		return "";
	}
	item = rpemView[index];
	red = RPEM_ItemInFile( tab, item ) ? "" : "^1";
	if ( tab == RPEM_ANIMS ) {
		return va( "%s%s ^3(%s)", red, animations[item].animation_name, animations[item].animation_category ? animations[item].animation_category : "" );
	}
	return va( "%s%s", red, RPEM_ItemName( tab, item ) );
}

qboolean UI_RpEmSelection( int index ) {
	int tab = rpemTabSeen;

	if ( index < 0 || index >= rpemViewNum || tab < 0 ) {
		return qtrue;
	}
	trap->Cvar_Set( rpemSelCvars[tab], RPEM_ItemName( tab, rpemView[index] ) );
	RPEM_Msg( "" );
	RPEM_PlayStart( tab, rpemView[index] );
	return qtrue;
}

/*
===========================================================================
Scripts
===========================================================================

	rpxEmOpen			the menu's onOpen
	rpxEmClose			its onClose: the character freed
	rpxEmTab <0|1|2>	the tabs: Emotes, Animations, All animations
	rpxEmDouble			the list's double click: Play, on the selected row only
	rpxEmPlay			Play: /emote or /anim of the tab's selected row
	rpxEmHug, rpxEmKiss	/wehug, /wekiss
	rpxEmReplay			the preview from its start
	rpxEmView			the preview turned and zoomed back
*/

static void RPEM_Play( void ) {
	static const char * const actions[RPEM_NUM_TABS] = { "emote", "anim", "animnum" };
	char name[MAX_QPATH];
	int tab = RPEM_Tab();

	RPEM_Cvar( rpemSelCvars[tab], name, sizeof( name ) );
	if ( RPEM_FindItem( tab, name ) < 0 ) {
		RPEM_Msg( "^3Select one in the list first." );
		return;
	}
	trap->Cmd_ExecuteText( EXEC_APPEND, va( "rpx do %s\n", actions[tab] ) );
	RPEM_CloseMenus();
}

qboolean UI_RpEmScript( const char *name, char **args ) {
	const char *arg = NULL;

	if ( Q_stricmpn( name, "rpxEm", 5 ) ) {
		return qfalse;
	}

	if ( !Q_stricmp( name, "rpxEmOpen" ) ) {
		RPEM_Msg( "" );
		rpemSyncTime = -1;
		rpemTabSeen = -1;		// the list and the preview made again at the next sync, with the selection
		rpemChar.made = qfalse;	// and the character
	} else if ( !Q_stricmp( name, "rpxEmClose" ) ) {
		RPEM_FreeChar();
		rpemChar.made = qfalse;
	} else if ( !Q_stricmp( name, "rpxEmTab" ) ) {
		// the one already open does nothing, so a filter typed there stays
		if ( String_Parse( args, &arg ) ) {
			int tab = atoi( arg );

			if ( tab >= 0 && tab < RPEM_NUM_TABS && tab != RPEM_Tab() ) {
				trap->Cvar_Set( "ui_rpx_em_tab", va( "%d", tab ) );
				trap->Cvar_Set( "ui_rpx_filter_emotes", "" );
				RPEM_Msg( "" );
			}
		}
	} else if ( !Q_stricmp( name, "rpxEmDouble" ) ) {
		itemDef_t *item = RPEM_List();

		// as rpxEnter (ui_rpextras.c): the list runs this before it selects the row clicked, so it is a double
		// click on the selected row only when the row under the mouse is that one
		if ( item && item->typeData.listbox && item->typeData.listbox->cursorPos == item->cursorPos
			&& item->cursorPos >= 0 && item->cursorPos < rpemViewNum ) {
			RPEM_Play();
		}
	} else if ( !Q_stricmp( name, "rpxEmPlay" ) ) {
		RPEM_Play();
	} else if ( !Q_stricmp( name, "rpxEmHug" ) || !Q_stricmp( name, "rpxEmKiss" ) ) {
		trap->Cmd_ExecuteText( EXEC_APPEND, !Q_stricmp( name, "rpxEmHug" ) ? "rpx do wehug\n" : "rpx do wekiss\n" );
		RPEM_CloseMenus();
	} else if ( !Q_stricmp( name, "rpxEmReplay" ) ) {
		RPEM_PlayStart( rpemPlay.tab, rpemPlay.item );
	} else if ( !Q_stricmp( name, "rpxEmView" ) ) {
		trap->Cvar_Set( "ui_rpx_em_yaw", "0" );
		trap->Cvar_Set( "ui_rpx_em_zoom", "100" );
	}
	return qtrue;
}

/*
===========================================================================
The preview
===========================================================================
*/

static void RPEM_CenterText( rectDef_t *rect, float scale, vec4_t color, const char *text, int iMenuFont ) {
	int w = Text_Width( text, scale, iMenuFont );

	Text_Paint( rect->x + ( rect->w - w ) / 2, rect->y + rect->h / 2 - 8, scale, color, text, 0, 0, ITEM_TEXTSTYLE_NORMAL, iMenuFont );
}

// what plays, under the character
static const char *RPEM_Caption( void ) {
	if ( rpemChar.cfgState != RPEM_CFG_OK ) {
		return "^3Your character's animation file could not be read";
	}
	if ( rpemPlay.missing ) {
		return "^3Your character's animation file does not have it";
	}
	if ( rpemPlay.item < 0 ) {
		return "Select one in the list";
	}
	if ( rpemPlay.tab == RPEM_EMOTES ) {
		const rp_emote_t *e = &rpEmotes[rpemPlay.item];

		return va( "%s: %s, then %s", e->name, RPEM_ItemName( RPEM_ALL, e->anim ), RPEM_ItemName( RPEM_ALL, e->leave ) );
	}
	if ( rpemPlay.tab == RPEM_ANIMS ) {
		return va( "%s: %s", animations[rpemPlay.item].animation_name, RPEM_ItemName( RPEM_ALL, animations[rpemPlay.item].animation_code ) );
	}
	return va( "%s (/anim %d)", RPEM_ItemName( RPEM_ALL, rpemPlay.item ), rpemPlay.item );
}

static void RPEM_DrawPreview( rectDef_t *rect, float scale, vec4_t color, int iMenuFont ) {
	static const vec3_t mins = { -16, -16, -24 }, maxs = { 16, 16, 40 };	// a player's box, standing
	refdef_t refdef;
	refEntity_t ent;
	vec3_t center, size, angles;
	float radius, dist, zoom, fov = RPEM_FOV;

	RPEM_Sync();
	if ( !rpemChar.ghoul2 ) {
		RPEM_CenterText( rect, scale, color, "Your character could not be loaded", iMenuFont );
		return;
	}
	RPEM_PlayRun();

	memset( &refdef, 0, sizeof( refdef ) );
	refdef.rdflags = RDF_NOWORLDMODEL;
	AxisClear( refdef.viewaxis );
	refdef.x = ( rect->x + 1 ) * uiInfo.uiDC.xscale;
	refdef.y = ( rect->y + 1 ) * uiInfo.uiDC.yscale;
	refdef.width = ( rect->w - 2 ) * uiInfo.uiDC.xscale;
	refdef.height = ( rect->h - 2 ) * uiInfo.uiDC.yscale;
	refdef.fov_x = fov;
	refdef.fov_y = atan2( refdef.height, refdef.width / tan( fov / 360 * M_PI ) ) * ( 360 / M_PI );
	refdef.time = uiInfo.uiDC.realTime;
	if ( refdef.fov_y < fov ) {
		fov = refdef.fov_y;
	}

	VectorAdd( mins, maxs, center );
	VectorScale( center, 0.5f, center );
	VectorSubtract( maxs, mins, size );
	radius = 0.5f * VectorLength( size );
	zoom = atof( UI_Cvar_VariableString( "ui_rpx_em_zoom" ) );
	if ( zoom < 25.0f ) zoom = 25.0f;
	if ( zoom > 400.0f ) zoom = 400.0f;
	dist = radius / sin( DEG2RAD( fov * 0.5f ) ) * ( 100.0f / zoom );

	// facing the camera (which looks along +x), turned by the slider; the middle of the box, on the
	// up axis it turns about, at the point the camera looks at
	VectorSet( angles, 0, 180.0f + (int)atof( UI_Cvar_VariableString( "ui_rpx_em_yaw" ) ), 0 );
	memset( &ent, 0, sizeof( ent ) );
	AnglesToAxis( angles, ent.axis );
	VectorSet( ent.origin, dist, 0, -center[2] );
	VectorCopy( ent.origin, ent.oldorigin );
	VectorCopy( ent.origin, ent.lightingOrigin );
	ent.ghoul2 = rpemChar.ghoul2;
	ent.radius = 1000;	// as the ui's own menu models (Item_Model_Paint)
	ent.shaderRGBA[0] = ent.shaderRGBA[1] = ent.shaderRGBA[2] = ent.shaderRGBA[3] = 255;
	ent.renderfx = RF_LIGHTING_ORIGIN | RF_NOSHADOW;

	trap->R_ClearScene();
	trap->R_AddRefEntityToScene( &ent );
	trap->R_RenderScene( &refdef );

	Text_Paint( rect->x + 4, rect->y + rect->h - 18, scale * 0.8f, color, RPEM_Caption(), 0, 0, ITEM_TEXTSTYLE_NORMAL, iMenuFont );
}

qboolean UI_RpEmOwnerDraw( int ownerDraw, rectDef_t *rect, float scale, vec4_t color, int iMenuFont ) {
	if ( ownerDraw != UI_RPX_EMPREVIEW ) {
		return qfalse;
	}
	RPEM_DrawPreview( rect, scale, color, iMenuFont );
	return qtrue;
}
