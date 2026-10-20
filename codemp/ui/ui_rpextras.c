/*
===========================================================================
GalaxyRP: [Extras] the ui half of the Extras menus of the Galaxy RP menu: ingame_rpx_props,
ingame_rpx_effects, ingame_rpx_npcs, ingame_rpx_music, ingame_rpx_sounds, ingame_rpx_lights and
ingame_rpx_spawners (GLX_U: and ingame_rpx_glmprops). cgame's half is cg_rpextras.c; ui/rp_extras.h says how the two work together. Here:

- The list the open menu shows (FEEDER_RPX_LIST): read from the list file cgame writes, when
  ui_rpx_serial changes; filtered by the menu's filter field; folders open with a double click.
  The file is only taken when it is the one for this menu's kind and folder, from this map.
- The menus' scripts (uiScript rpx...): opening a menu, the NPCs/vehicles tabs, the buttons.
  A button that needs a selection only goes ahead with a file selected in the list; the ones that
  spawn or play something close the menus, as the Extras menus do after an action.
- The prop preview (UI_RPX_PREVIEW), drawn here like an ITEM_TYPE_MODEL: the model is only loaded
  when Preview is pressed -- each different model keeps a renderer slot until the map changes, so
  RPX_PREVIEW_MODELS different ones a map at most, and a model the player does not have is not
  asked for at all (a failed load keeps a slot too). The sliders then turn and zoom the loaded
  model at no cost.
- GLX_U: the GLM Props preview (UI_RPX_GPREVIEW): the same, for a .glm, through a Ghoul2 instance of
  its own. The instance is freed when the menu closes and made again from the model's path when it
  opens on the same map (the model keeps its slot); RPX_GlmShape() (rp_extras.h) gives its size as drawn.
  Its skin: one of the model_*.skin files of the model's folder (RPX_ReadSkins()).
  Its Animation tab: the frames of the model's own animation file (.gla) it plays, typed or taken from
  the named animations of the animation.cfg next to that file (FEEDER_RPX_ANIMS); the preview plays
  them as the spawned prop will (RPX_GlmAnimate()).
- The effect preview box (UI_RPX_FXBOX): renders the scene cgame left the effect in (without
  clearing it first, see CG_RpxFxPass()), and tells cgame it is on screen (ui_rpx_fxbox).
- The light colour swatch of the Props menu (UI_RPX_SWATCH), and the Lights menu's two (UI_RPX_LSWATCH,
  UI_RPX_LOFFSWATCH). The Lights menu has no list: rpxOpen lights opens it without one (RPX_OpenNoList).
- The Spawners menu shows the NPCs & Vehicles menu's lists, with its tabs, filter and selection: rpxOpen
  spawners is rpxOpen npcs.
===========================================================================
*/

#include "ui_local.h"
#include "ui/rp_extras.h"
#include "ghoul2/G2.h"	// GLX_U: [Extras] the bone animation flags, for the GLM preview

extern void Text_Paint( float x, float y, float scale, vec4_t color, const char *text, float adjust, int limit, int style, int iMenuFont );
extern int Text_Width( const char *text, float scale, int iMenuFont );
extern void Item_RunScript( itemDef_t *item, const char *s );
extern void Menu_ShowItemByName( menuDef_t *menu, const char *p, qboolean bShow );
extern int Item_ListBox_MaxScroll( itemDef_t *item );
extern void UI_DrawRect( float x, float y, float width, float height, const float *color );
extern void UI_InsertG2Pointer( void *ghoul2 );
extern void UI_ClearG2Pointer( void *ghoul2 );

typedef struct {
	char		name[RPX_NAME_LEN];
	qboolean	folder;
} uiRpxEntry_t;

static uiRpxEntry_t	rpxEntries[RPX_MAX_ENTRIES];
static int			rpxNum;
static qboolean		rpxTruncated;
static int			rpxView[RPX_MAX_ENTRIES + 1];	// index into rpxEntries; -1 for ".."
static int			rpxViewNum;
static char			rpxFile[RPX_MAX_ENTRIES * ( RPX_NAME_LEN + 1 ) + 256];

static int			rpxKind = -1;			// the list the open Extras menu shows
static char			rpxMenu[64];			// that menu's name
static qboolean		rpxLoaded;				// rpxEntries is rpxLoadedKind's rpxLoadedFolder, this map
static int			rpxLoadedKind = -1;
static char			rpxLoadedFolder[RPX_NAME_LEN];
static int			rpxSerialSeen;
static int			rpxMapId;
static char			rpxFilterSeen[64];
static int			rpxSyncTime = -1;
static int			rpxPendingNav = -2;		// a row to open at the next sync (-1 is ".."); -2 for none

static struct {
	char		path[MAX_QPATH];
	qhandle_t	model;
	vec3_t		mins, maxs;
	char		previewed[RPX_PREVIEW_MODELS][MAX_QPATH];
	int			numPreviewed;
} rpxProp;

// GLX_U: [Extras] the GLM Props preview
static struct {
	char		path[MAX_QPATH];	// the model the box shows
	qboolean	shown;				// the box shows path (a new selection or folder clears it)
	void		*ghoul2;			// its instance while the menu is open
	qboolean	sized;				// mins/maxs are the model's own, not the default box
	vec3_t		mins, maxs;
	char		gla[MAX_QPATH];		// its animation file, without .gla: "" or "*default" for none
	int			numFrames;			// that file's frames; 0 when there is none or it was not read
	int			sizedAt[3];			// the Animation tab (Animated, Start, End) mins/maxs were measured at
	int			changedAt;			// when it last changed from that; 0 when it has not
	char		skins[32][32];		// its folder's skins (model_<name>.skin), "default" first
	int			numSkins;
	char		previewed[RPX_PREVIEW_GLMS][MAX_QPATH];
	int			numPreviewed;
} rpxGlm;

#define RPX_GLM_RESIZE_SETTLE	300		// the Animation tab's frames typed: measured again once they rest this long

// GLX_U: [Extras] what the GLM preview's instance plays now (RPX_GlmAnimate())
static struct {
	qboolean	on;
	int			start, end;
	qboolean	loop;
	qboolean	replay;				// Replay pressed: from the start again
} rpxGlmAnim;

// GLX_U: [Extras] the named animations of the previewed model's animation file (its animation.cfg)
#define RPX_MAX_ANIMS		2048
#define RPX_ANIM_NAME		64
#define RPX_ANIMCFG_SIZE	0x20000		// the largest animation.cfg read (_humanoid's is some 53 KB)

typedef enum {
	RPX_ANIMS_NONE,			// nothing read: no model previewed, or it has no animation file
	RPX_ANIMS_OK,
	RPX_ANIMS_NOCFG,		// no animation.cfg (or none with a named animation in it)
	RPX_ANIMS_TOOBIG
} rpxAnimsState_t;

typedef struct {
	char		name[RPX_ANIM_NAME];
	int			first, num;
} uiRpxAnim_t;

static uiRpxAnim_t		rpxAnims[RPX_MAX_ANIMS];
static int				rpxNumAnims;
static rpxAnimsState_t	rpxAnimsState;
static char				rpxAnimsGla[MAX_QPATH];			// the animation file rpxAnims is from
static int				rpxAnimView[RPX_MAX_ANIMS];		// index into rpxAnims
static int				rpxAnimViewNum;
static char				rpxAnimFilterSeen[64];
static int				rpxAnimSel = -1;				// the selected one, index into rpxAnims
static char				rpxAnimText[RPX_ANIMCFG_SIZE + 1];

static int			rpxFxWindowEnd;
static int			rpxFxDir;
static int			rpxFxPitch;			// the Tilt the effect showing was played with (for Direction Tilted)
static int			rpxFxChangeAt;		// Direction or Tilt differ from the playing effect's since then; 0 if they do not
static int			rpxFxSeenDir, rpxFxSeenPitch;
static int			rpxFxBoxSet;

/*
===========================================================================
Helpers
===========================================================================
*/

static void RPX_Cvar( const char *name, char *buf, int size ) {
	trap->Cvar_VariableStringBuffer( name, buf, size );
}

static const char *RPX_CvarStr( const char *name ) {
	static char buf[4][256];
	static int n;

	n = ( n + 1 ) & 3;
	trap->Cvar_VariableStringBuffer( name, buf[n], sizeof( buf[n] ) );
	return buf[n];
}

static void RPX_Msg( const char *text ) {
	trap->Cvar_Set( "ui_rpx_msg", text );
}

// the menus' filter field of a kind: the NPC and vehicle tabs share theirs
static const char *RPX_FilterCvar( int kind ) {
	if ( kind == RPX_VEHICLES ) {
		kind = RPX_NPCS;
	}
	return va( "ui_rpx_filter_%s", rpxKinds[kind].name );
}

static void RPX_Folder( int kind, char *folder, int size ) {
	folder[0] = '\0';
	if ( kind >= 0 && rpxKinds[kind].root ) {
		RPX_Cvar( va( "ui_rpx_dir_%s", rpxKinds[kind].name ), folder, size );
	}
}

static qboolean RPX_Contains( const char *hay, const char *needle ) {
	int hl = strlen( hay ), nl = strlen( needle ), i;

	if ( !nl ) {
		return qtrue;
	}
	for ( i = 0; i + nl <= hl; i++ ) {
		if ( !Q_stricmpn( hay + i, needle, nl ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

// the list of the open Extras menu
// GLX_U: [Extras] or the open menu's list of that feeder (FEEDER_RPX_ANIMS: GLM Props' animations)
static itemDef_t *RPX_FeederItem( int feeder ) {
	menuDef_t *menu = rpxMenu[0] ? Menus_FindByName( rpxMenu ) : NULL;
	int i;

	if ( !menu ) {
		return NULL;
	}
	for ( i = 0; i < menu->itemCount; i++ ) {
		if ( menu->items[i]->type == ITEM_TYPE_LISTBOX && menu->items[i]->special == feeder ) {
			return menu->items[i];
		}
	}
	return NULL;
}

static itemDef_t *RPX_ListItem( void ) {
	return RPX_FeederItem( FEEDER_RPX_LIST );
}

// the selected row, as the list shows it
static int RPX_SelectedRow( void ) {
	itemDef_t *item = RPX_ListItem();

	if ( !item || item->cursorPos < 0 || item->cursorPos >= rpxViewNum ) {
		return -3;
	}
	return rpxView[item->cursorPos];
}

static void RPX_SetListCursor( int index ) {
	itemDef_t *item = RPX_ListItem();
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

// the selected name in full under the list: the list draws a name only as wide as its column
static void RPX_ShowSelected( const char *name ) {
	trap->Cvar_Set( "ui_rpx_selname", name[0] ? va( "Selected: ^7%s", name ) : "" );
}

static void RPX_UpdateInfo( void ) {
	char folder[RPX_NAME_LEN];
	const char *where;
	int files = 0, i;

	if ( rpxKind < 0 ) {
		return;
	}
	for ( i = 0; i < rpxNum; i++ ) {
		if ( !rpxEntries[i].folder ) {
			files++;
		}
	}
	RPX_Folder( rpxKind, folder, sizeof( folder ) );
	if ( !rpxKinds[rpxKind].root ) {
		where = ( rpxKind == RPX_VEHICLES ) ? "Vehicle types" : "NPC types";
	} else {
		where = va( "%s%s%s", rpxKinds[rpxKind].root, folder[0] ? "/" : "", folder );
	}

	if ( !rpxLoaded ) {
		trap->Cvar_Set( "ui_rpx_info", where );
	} else if ( rpxFilterSeen[0] ) {
		trap->Cvar_Set( "ui_rpx_info", va( "%s: %d of %d shown", where, rpxViewNum - ( ( rpxViewNum && rpxView[0] == -1 ) ? 1 : 0 ), rpxNum ) );
	} else {
		trap->Cvar_Set( "ui_rpx_info", va( "%s: %d %s", where, files, files == 1 ? "file" : "files" ) );
	}
}

// the rows the list shows: ".." first in a subfolder, then what the filter lets through; the
// selection kept on the selected name
static void RPX_BuildView( void ) {
	char folder[RPX_NAME_LEN], sel[RPX_NAME_LEN];
	int i, cursor = -1;

	rpxViewNum = 0;
	if ( rpxKind < 0 ) {
		return;
	}
	RPX_Folder( rpxKind, folder, sizeof( folder ) );
	if ( rpxLoaded && rpxKinds[rpxKind].root && folder[0] ) {
		rpxView[rpxViewNum++] = -1;
	}
	if ( rpxLoaded ) {
		for ( i = 0; i < rpxNum; i++ ) {
			if ( RPX_Contains( rpxEntries[i].name, rpxFilterSeen ) ) {
				rpxView[rpxViewNum++] = i;
			}
		}
	}

	RPX_Cvar( va( "ui_rpx_sel_%s", rpxKinds[rpxKind].name ), sel, sizeof( sel ) );
	if ( sel[0] ) {
		for ( i = 0; i < rpxViewNum; i++ ) {
			if ( rpxView[i] >= 0 && !rpxEntries[rpxView[i]].folder && !strcmp( rpxEntries[rpxView[i]].name, sel ) ) {
				cursor = i;
				break;
			}
		}
	}
	RPX_SetListCursor( cursor );
	RPX_ShowSelected( cursor >= 0 ? sel : "" );
	RPX_UpdateInfo();
}

// the list file, when it is the one for this menu (its kind and folder, and the serial cgame wrote)
static void RPX_LoadFile( int serial ) {
	char file[64], folder[RPX_NAME_LEN];
	fileHandle_t f = 0;
	char *p, *line, *fields[6];
	int len, i, nf;

	RPX_Cvar( "ui_rpx_file", file, sizeof( file ) );
	for ( i = 0; i < RPX_NUM_LIST_FILES; i++ ) {
		if ( !strcmp( file, rpxListFiles[i] ) ) {
			break;
		}
	}
	if ( i == RPX_NUM_LIST_FILES || rpxKind < 0 ) {
		return;
	}

	len = trap->FS_Open( file, &f, FS_READ );
	if ( !f ) {
		return;
	}
	if ( len <= 0 || len >= (int)sizeof( rpxFile ) ) {
		trap->FS_Close( f );
		return;
	}
	trap->FS_Read( rpxFile, len, f );
	trap->FS_Close( f );
	rpxFile[len] = '\0';

	// the header: magic, serial, kind, folder, count, cut short
	line = rpxFile;
	p = strchr( line, '\n' );
	if ( !p ) {
		return;
	}
	*p++ = '\0';
	nf = 0;
	fields[nf++] = line;
	while ( nf < 6 && ( line = strchr( line, '\t' ) ) ) {
		*line++ = '\0';
		fields[nf++] = line;
	}
	if ( nf != 6 || strcmp( fields[0], RPX_LIST_MAGIC ) || atoi( fields[1] ) != serial ) {
		return;
	}

	RPX_Folder( rpxKind, folder, sizeof( folder ) );
	if ( Q_stricmp( fields[2], rpxKinds[rpxKind].name ) || Q_stricmp( fields[3], folder ) ) {
		return;	// not what this menu shows now: the file for a list it has left
	}

	rpxNum = 0;
	rpxTruncated = atoi( fields[5] ) ? qtrue : qfalse;
	while ( *p ) {
		char *end = strchr( p, '\n' );

		if ( end ) {
			*end = '\0';
		}
		if ( rpxNum < RPX_MAX_ENTRIES && RPX_NameOk( p, rpxKinds[rpxKind].root ? qtrue : qfalse ) ) {
			int l = strlen( p );

			rpxEntries[rpxNum].folder = ( p[l - 1] == '/' ) ? qtrue : qfalse;
			Q_strncpyz( rpxEntries[rpxNum].name, p, sizeof( rpxEntries[0].name ) );
			rpxNum++;
		}
		if ( !end ) {
			break;
		}
		p = end + 1;
	}

	rpxLoaded = qtrue;
	rpxLoadedKind = rpxKind;
	Q_strncpyz( rpxLoadedFolder, folder, sizeof( rpxLoadedFolder ) );
	RPX_BuildView();
}

static void RPX_Request( void ) {
	rpxLoaded = qfalse;
	rpxNum = 0;
	RPX_BuildView();
	trap->Cmd_ExecuteText( EXEC_APPEND, va( "rpx list %s\n", rpxKinds[rpxKind].name ) );
}

static void RPX_StopFxPreview( void ) {
	rpxFxChangeAt = 0;
	if ( rpxFxWindowEnd ) {
		rpxFxWindowEnd = 0;
		trap->Cmd_ExecuteText( EXEC_APPEND, "rpx do fxstop\n" );
	}
}

// GLX_U: [Extras] the GLM preview's instance freed (the box keeps its model, made again when drawn)
static void RPX_GlmFree( void ) {
	if ( rpxGlm.ghoul2 ) {
		UI_ClearG2Pointer( rpxGlm.ghoul2 );
		trap->G2API_CleanGhoul2Models( &rpxGlm.ghoul2 );
		rpxGlm.ghoul2 = NULL;
	}
}

// GLX_U: [Extras] the GLM preview emptied: "Press Preview" again
static void RPX_GlmClear( void ) {
	RPX_GlmFree();
	rpxGlm.shown = qfalse;
}

// open a row: a folder, or ".." (-1)
static void RPX_Navigate( int row ) {
	char folder[RPX_NAME_LEN], next[RPX_NAME_LEN * 2];

	if ( rpxKind < 0 || !rpxKinds[rpxKind].root ) {
		return;
	}
	RPX_Folder( rpxKind, folder, sizeof( folder ) );

	if ( row == -1 ) {
		char *slash = strrchr( folder, '/' );

		if ( slash ) {
			*slash = '\0';
		} else {
			folder[0] = '\0';
		}
		Q_strncpyz( next, folder, sizeof( next ) );
	} else {
		char name[RPX_NAME_LEN];
		int l;

		if ( row < 0 || row >= rpxNum || !rpxEntries[row].folder ) {
			return;
		}
		Q_strncpyz( name, rpxEntries[row].name, sizeof( name ) );
		l = strlen( name );
		name[l - 1] = '\0';	// its "/"
		Com_sprintf( next, sizeof( next ), "%s%s%s", folder, folder[0] ? "/" : "", name );
	}

	if ( !RPX_FolderOk( next ) ) {
		RPX_Msg( "^3That folder's path is too long to open." );
		return;
	}

	trap->Cvar_Set( va( "ui_rpx_dir_%s", rpxKinds[rpxKind].name ), next );
	trap->Cvar_Set( va( "ui_rpx_sel_%s", rpxKinds[rpxKind].name ), "" );
	RPX_Msg( "" );
	if ( rpxKind == RPX_GLMS ) {
		RPX_GlmClear();
		trap->Cvar_Set( "ui_rpx_g_skin", "" );
	} else {
		rpxProp.model = 0;
	}
	RPX_StopFxPreview();
	RPX_Request();
}

// the map changed (cgame started again): nothing from before is ours any more
static void RPX_CheckMap( void ) {
	int mapId = atoi( RPX_CvarStr( "ui_rpx_mapid" ) );

	if ( mapId == rpxMapId ) {
		return;
	}
	rpxMapId = mapId;
	rpxLoaded = qfalse;
	rpxNum = 0;
	rpxViewNum = 0;
	rpxSerialSeen = -1;
	rpxProp.model = 0;
	rpxProp.numPreviewed = 0;
	RPX_GlmClear();
	rpxGlm.numPreviewed = 0;
	rpxGlm.numFrames = 0;
	rpxAnimsGla[0] = '\0';		// GLX_U: files can change between maps
	rpxAnimsState = RPX_ANIMS_NONE;
	rpxNumAnims = 0;
	rpxAnimViewNum = 0;
	rpxAnimSel = -1;
	rpxFxWindowEnd = 0;
}

// once a frame, from whatever asks first: a folder to open, a new list file, a new filter
static void RPX_Sync( void ) {
	char filter[64];
	int serial;

	if ( rpxSyncTime == uiInfo.uiDC.realTime ) {
		return;
	}
	rpxSyncTime = uiInfo.uiDC.realTime;
	if ( rpxKind < 0 ) {
		return;
	}

	RPX_CheckMap();

	if ( rpxPendingNav != -2 ) {
		int row = rpxPendingNav;

		rpxPendingNav = -2;
		RPX_Navigate( row );
	}

	serial = atoi( RPX_CvarStr( "ui_rpx_serial" ) );
	if ( serial != rpxSerialSeen ) {
		rpxSerialSeen = serial;
		RPX_LoadFile( serial );
	}

	RPX_Cvar( RPX_FilterCvar( rpxKind ), filter, sizeof( filter ) );
	if ( strcmp( filter, rpxFilterSeen ) ) {
		Q_strncpyz( rpxFilterSeen, filter, sizeof( rpxFilterSeen ) );
		RPX_BuildView();
	}
}

/*
===========================================================================
The list
===========================================================================
*/

int UI_RpxFeederCount( void ) {
	RPX_Sync();
	return rpxViewNum;
}

const char *UI_RpxFeederItemText( int index ) {
	int row;

	if ( index < 0 || index >= rpxViewNum ) {
		return "";
	}
	row = rpxView[index];
	if ( row == -1 ) {
		return "^5..  (up one folder)";
	}
	if ( rpxEntries[row].folder ) {
		return va( "^5%s", rpxEntries[row].name );
	}
	return rpxEntries[row].name;
}

// a row selected: a file becomes the selection; the previews show the old one no longer
qboolean UI_RpxFeederSelection( int index ) {
	const char *name = "";
	int row;

	if ( rpxKind < 0 || index < 0 || index >= rpxViewNum ) {
		return qtrue;
	}
	row = rpxView[index];
	if ( row >= 0 && !rpxEntries[row].folder ) {
		name = rpxEntries[row].name;
	}
	RPX_ShowSelected( name );
	if ( strcmp( name, RPX_CvarStr( va( "ui_rpx_sel_%s", rpxKinds[rpxKind].name ) ) ) ) {
		trap->Cvar_Set( va( "ui_rpx_sel_%s", rpxKinds[rpxKind].name ), name );
		RPX_Msg( "" );
		if ( rpxKind == RPX_MODELS ) {
			rpxProp.model = 0;
		} else if ( rpxKind == RPX_GLMS ) {
			RPX_GlmClear();
			trap->Cvar_Set( "ui_rpx_g_skin", "" );	// another model's skins
		} else if ( rpxKind == RPX_EFFECTS ) {
			RPX_StopFxPreview();
		}
	}
	return qtrue;
}

/*
===========================================================================
Scripts
===========================================================================
*/

static int RPX_KindForMenu( const char *word ) {
	if ( !Q_stricmp( word, "props" ) ) return RPX_MODELS;
	if ( !Q_stricmp( word, "glmprops" ) ) return RPX_GLMS;	// GLX_U: [Extras]
	if ( !Q_stricmp( word, "effects" ) ) return RPX_EFFECTS;
	// GalaxyRP: [Extras] the Spawners menu shows the same lists, with the same tab and selection
	if ( !Q_stricmp( word, "npcs" ) || !Q_stricmp( word, "spawners" ) ) return atoi( RPX_CvarStr( "ui_rpx_npcmode" ) ) ? RPX_VEHICLES : RPX_NPCS;
	if ( !Q_stricmp( word, "music" ) ) return RPX_MUSIC;
	if ( !Q_stricmp( word, "sounds" ) ) return RPX_SOUNDS;
	return -1;
}

// GalaxyRP: [Extras] a menu with no list (Lights): nothing to ask the server for; no list kind is open
static void RPX_OpenNoList( const char *menu ) {
	rpxKind = -1;
	rpxViewNum = 0;
	Q_strncpyz( rpxMenu, menu, sizeof( rpxMenu ) );
	rpxSyncTime = -1;
	RPX_CheckMap();
	RPX_Msg( "" );
	RPX_StopFxPreview();
}

static void RPX_Open( int kind, const char *menu ) {
	char folder[RPX_NAME_LEN];

	rpxKind = kind;
	Q_strncpyz( rpxMenu, menu, sizeof( rpxMenu ) );
	rpxSyncTime = -1;
	RPX_CheckMap();
	RPX_Cvar( RPX_FilterCvar( kind ), rpxFilterSeen, sizeof( rpxFilterSeen ) );
	RPX_Msg( "" );
	RPX_StopFxPreview();

	RPX_Folder( kind, folder, sizeof( folder ) );
	if ( rpxLoaded && rpxLoadedKind == kind && !Q_stricmp( rpxLoadedFolder, folder ) ) {
		// shown as it was, and asked for again all the same: cgame answers at once from what it keeps,
		// and a listing that failed last time (the server busy, or no answer) is tried again
		RPX_BuildView();
		trap->Cmd_ExecuteText( EXEC_APPEND, va( "rpx list %s\n", rpxKinds[kind].name ) );
		return;
	}
	RPX_Request();
}

// the selected row is a file of the list (what cgame will check again against the server's list)
static qboolean RPX_HaveSelection( void ) {
	int row = RPX_SelectedRow();

	if ( row < 0 || row >= rpxNum || rpxEntries[row].folder ) {
		RPX_Msg( "^3Select one from the list first." );
		return qfalse;
	}
	return qtrue;
}

static void RPX_CloseMenus( void ) {
	trap->Key_SetCatcher( trap->Key_GetCatcher() & ~KEYCATCH_UI );
	trap->Key_ClearStates();
	trap->Cvar_Set( "cl_paused", "0" );
	Menus_CloseAll();
}

static qboolean RPX_LocalFileExists( const char *path ) {
	fileHandle_t f = 0;
	int len = trap->FS_Open( path, &f, FS_READ );

	if ( f ) {
		trap->FS_Close( f );
	}
	return ( f && len > 0 ) ? qtrue : qfalse;
}

static void RPX_PreviewProp( void ) {
	char folder[RPX_NAME_LEN], path[MAX_QPATH * 2];
	int row = RPX_SelectedRow(), i;
	qhandle_t model;

	if ( rpxKind != RPX_MODELS || !RPX_HaveSelection() ) {
		return;
	}
	RPX_Folder( RPX_MODELS, folder, sizeof( folder ) );
	Com_sprintf( path, sizeof( path ), "models/%s%s%s.md3", folder, folder[0] ? "/" : "", rpxEntries[row].name );
	if ( strlen( path ) >= MAX_QPATH ) {
		RPX_Msg( "^3That model's path is too long for the game." );
		return;
	}

	for ( i = 0; i < rpxProp.numPreviewed; i++ ) {
		if ( !Q_stricmp( rpxProp.previewed[i], path ) ) {
			break;
		}
	}
	if ( i == rpxProp.numPreviewed ) {
		if ( rpxProp.numPreviewed >= RPX_PREVIEW_MODELS ) {
			RPX_Msg( "^3Preview limit reached until the next map." );
			return;
		}
		if ( !RPX_LocalFileExists( path ) ) {
			rpxProp.model = 0;
			RPX_Msg( "^3That model is not installed on your game (players without it will not see it either)." );
			return;
		}
	}

	model = trap->R_RegisterModel( path );
	if ( i == rpxProp.numPreviewed ) {
		Q_strncpyz( rpxProp.previewed[rpxProp.numPreviewed++], path, sizeof( rpxProp.previewed[0] ) );
	}
	if ( !model ) {
		rpxProp.model = 0;
		RPX_Msg( "^3That model could not be loaded." );
		return;
	}
	Q_strncpyz( rpxProp.path, path, sizeof( rpxProp.path ) );
	rpxProp.model = model;
	trap->R_ModelBounds( model, rpxProp.mins, rpxProp.maxs );
	RPX_Msg( "" );
}

static qboolean RPX_GlmMake( const char *path );
static void RPX_GlmMeasure( void );

// GLX_U: [Extras] an animation file (without .gla) that is one: not "" nor the game's own "*default"
static qboolean RPX_GlaNamed( const char *gla ) {
	return ( gla[0] && gla[0] != '*' ) ? qtrue : qfalse;
}

static int QDECL RPX_AnimCompare( const void *a, const void *b ) {
	return Q_stricmp( ( (const uiRpxAnim_t *)a )->name, ( (const uiRpxAnim_t *)b )->name );
}

// GLX_U: [Extras] the animation list as the filter lets it through, the selection kept on its animation
static void RPX_BuildAnimView( void ) {
	itemDef_t *item = RPX_FeederItem( FEEDER_RPX_ANIMS );
	int i, cursor = -1;

	rpxAnimViewNum = 0;
	for ( i = 0; i < rpxNumAnims; i++ ) {
		if ( RPX_Contains( rpxAnims[i].name, rpxAnimFilterSeen ) ) {
			if ( i == rpxAnimSel ) {
				cursor = rpxAnimViewNum;
			}
			rpxAnimView[rpxAnimViewNum++] = i;
		}
	}
	if ( item && item->typeData.listbox ) {
		listBoxDef_t *listPtr = item->typeData.listbox;

		item->cursorPos = cursor;
		listPtr->cursorPos = ( cursor >= 0 ) ? cursor : 0;
		listPtr->startPos = 0;
		if ( cursor > 3 ) {
			int max = Item_ListBox_MaxScroll( item );

			listPtr->startPos = cursor - 3;
			if ( listPtr->startPos > max ) {
				listPtr->startPos = max;
			}
		}
	}
}

// GLX_U: [Extras] the named animations of an animation file (gla, without .gla; "" for none): the
// animation.cfg next to it, "NAME firstFrame numFrames loopFrames fps" a line -- read here and not with
// UI_ParseAnimationFile(), which drops the game on a file over 60000 bytes and keeps what it reads in a
// table the player menus share. Read again only for another file.
static void RPX_ReadAnims( const char *gla ) {
	char path[MAX_QPATH];
	const char *slash, *p;
	fileHandle_t f = 0;
	int len;

	if ( !Q_stricmp( gla, rpxAnimsGla ) && ( rpxAnimsState != RPX_ANIMS_NONE || !gla[0] ) ) {
		return;
	}
	Q_strncpyz( rpxAnimsGla, gla, sizeof( rpxAnimsGla ) );
	rpxNumAnims = 0;
	rpxAnimSel = -1;
	rpxAnimsState = RPX_ANIMS_NONE;

	if ( RPX_GlaNamed( gla ) ) {
		slash = strrchr( gla, '/' );
		rpxAnimsState = RPX_ANIMS_NOCFG;
		if ( slash && (int)( slash - gla ) + 15 < (int)sizeof( path ) ) {
			Com_sprintf( path, sizeof( path ), "%.*s/animation.cfg", (int)( slash - gla ), gla );
			len = trap->FS_Open( path, &f, FS_READ );
			if ( f ) {
				if ( len > RPX_ANIMCFG_SIZE ) {
					rpxAnimsState = RPX_ANIMS_TOOBIG;
				} else if ( len > 0 ) {
					trap->FS_Read( rpxAnimText, len, f );
					rpxAnimText[len] = '\0';
					p = rpxAnimText;
					COM_BeginParseSession( "RPX_ReadAnims" );
					while ( rpxNumAnims < RPX_MAX_ANIMS ) {
						char name[RPX_ANIM_NAME];
						int nums[4], k, nameLen;
						const char *token = COM_Parse( &p );

						if ( !token || !token[0] ) {
							break;
						}
						nameLen = strlen( token );
						Q_strncpyz( name, token, sizeof( name ) );
						for ( k = 0; k < 4; k++ ) {
							token = COM_Parse( &p );
							if ( !token || !token[0] ) {
								break;
							}
							nums[k] = atoi( token );
						}
						if ( k < 4 ) {
							break;
						}
						// a frame to start at and some to play; a name too long for the list is left out
						if ( nums[0] >= 0 && nums[1] > 0 && nameLen < RPX_ANIM_NAME ) {
							Q_strncpyz( rpxAnims[rpxNumAnims].name, name, sizeof( rpxAnims[0].name ) );
							rpxAnims[rpxNumAnims].first = nums[0];
							rpxAnims[rpxNumAnims].num = nums[1];
							rpxNumAnims++;
						}
					}
					if ( rpxNumAnims ) {
						qsort( rpxAnims, rpxNumAnims, sizeof( rpxAnims[0] ), RPX_AnimCompare );
						rpxAnimsState = RPX_ANIMS_OK;
					}
				}
				trap->FS_Close( f );
			}
		}
	}
	RPX_BuildAnimView();
}

// GLX_U: [Extras] the GLM preview playing what the Animation tab says, as the spawned prop will (CG_General's
// EF_G2ANIMATING routine: the frames on model_root at 20 a second, then held, or looped); its base pose
// when not animated. Again whenever the frames, Loop or Animated change, or Replay is pressed.
static void RPX_GlmAnimate( void ) {
	qboolean on = ( atoi( RPX_CvarStr( "ui_rpx_g_anim" ) ) && rpxGlm.numFrames > 0 ) ? qtrue : qfalse;
	qboolean loop = atoi( RPX_CvarStr( "ui_rpx_g_loop" ) ) ? qtrue : qfalse;
	int start = (int)atof( RPX_CvarStr( "ui_rpx_g_start" ) ), end = (int)atof( RPX_CvarStr( "ui_rpx_g_end" ) );

	if ( on ) {
		start = (int)Com_Clamp( 0, rpxGlm.numFrames - 1, start );
		end = (int)Com_Clamp( start, rpxGlm.numFrames - 1, end );
		loop = ( loop && end > start ) ? qtrue : qfalse;
	}
	if ( !rpxGlmAnim.replay && on == rpxGlmAnim.on &&
		( !on || ( start == rpxGlmAnim.start && end == rpxGlmAnim.end && loop == rpxGlmAnim.loop ) ) ) {
		return;
	}
	rpxGlmAnim.replay = qfalse;

	if ( !on ) {
		if ( !RPX_GlmMake( rpxGlm.path ) ) {	// the base pose: an instance not animated
			rpxGlm.shown = qfalse;
		}
		return;
	}
	trap->G2API_SetBoneAnim( rpxGlm.ghoul2, 0, "model_root", start, end + 1,
		loop ? BONE_ANIM_OVERRIDE_LOOP : BONE_ANIM_OVERRIDE_FREEZE, 1.0f, uiInfo.uiDC.realTime, -1, 0 );
	rpxGlmAnim.on = qtrue;
	rpxGlmAnim.start = start;
	rpxGlmAnim.end = end;
	rpxGlmAnim.loop = loop;
}

// GLX_U: [Extras] the animation list's filter changed: shown again
static void RPX_AnimSync( void ) {
	char filter[64];

	RPX_Cvar( "ui_rpx_filter_anims", filter, sizeof( filter ) );
	if ( strcmp( filter, rpxAnimFilterSeen ) ) {
		Q_strncpyz( rpxAnimFilterSeen, filter, sizeof( rpxAnimFilterSeen ) );
		RPX_BuildAnimView();
	}
}

// the list shows the previewed model's: none while the box waits for Preview
int UI_RpxAnimFeederCount( void ) {
	RPX_AnimSync();
	return rpxGlm.shown ? rpxAnimViewNum : 0;
}

const char *UI_RpxAnimFeederItemText( int index ) {
	if ( index < 0 || index >= rpxAnimViewNum ) {
		return "";
	}
	return rpxAnims[rpxAnimView[index]].name;
}

qboolean UI_RpxAnimFeederSelection( int index ) {
	rpxAnimSel = ( index >= 0 && index < rpxAnimViewNum ) ? rpxAnimView[index] : -1;
	return qtrue;
}

// GLX_U: [Extras] Set start / Set end / Use (which: 1 start, 2 end, 3 both): the selected animation's first
// frame, last frame or both, and Animated on
static void RPX_AnimSet( int which ) {
	const uiRpxAnim_t *a;

	if ( !rpxGlm.shown || rpxAnimSel < 0 || rpxAnimSel >= rpxNumAnims ) {
		RPX_Msg( "^3Select an animation from the list first." );
		return;
	}
	a = &rpxAnims[rpxAnimSel];
	if ( which & 1 ) {
		trap->Cvar_Set( "ui_rpx_g_start", va( "%d", a->first ) );
	}
	if ( which & 2 ) {
		trap->Cvar_Set( "ui_rpx_g_end", va( "%d", a->first + a->num - 1 ) );
	}
	trap->Cvar_Set( "ui_rpx_g_anim", "1" );
	RPX_GlmMeasure();	// at once: a button, not typing
	RPX_Msg( "" );
}

// GLX_U: [Extras] the named animation that starts at frame (start) or ends at it, or NULL
static const char *RPX_AnimAt( int frame, qboolean start ) {
	int i;

	if ( !rpxGlm.shown ) {
		return NULL;	// the animations are the previewed model's
	}
	if ( rpxAnimSel >= 0 && rpxAnimSel < rpxNumAnims ) {
		const uiRpxAnim_t *a = &rpxAnims[rpxAnimSel];

		if ( ( start ? a->first : a->first + a->num - 1 ) == frame ) {
			return a->name;		// the selected one first: animations can share frames
		}
	}
	for ( i = 0; i < rpxNumAnims; i++ ) {
		if ( ( start ? rpxAnims[i].first : rpxAnims[i].first + rpxAnims[i].num - 1 ) == frame ) {
			return rpxAnims[i].name;
		}
	}
	return NULL;
}

// GLX_U: [Extras] the frames GLM Props will be spawned with, or why it cannot be (RPX_GlmFramesProblem());
// the model's frame count only when the preview shows the selected model
static const char *RPX_GlmSpawnFramesProblem( void ) {
	return RPX_GlmFramesProblem( atoi( RPX_CvarStr( "ui_rpx_g_anim" ) ) ? qtrue : qfalse,
		(int)atof( RPX_CvarStr( "ui_rpx_g_start" ) ), (int)atof( RPX_CvarStr( "ui_rpx_g_end" ) ),
		rpxGlm.shown ? rpxGlm.numFrames : 0 );
}

// GLX_U: [Extras] the GLM preview's instance of path, with the skin the game shows a misc_model_ghoul
// without one: model_default.skin from its folder, when there is one (CG_General in cg_ents.c)
static qboolean RPX_GlmSetSkin( const char *file ) {
	qhandle_t h;

	if ( !RPX_LocalFileExists( file ) || !( h = trap->R_RegisterSkin( file ) ) ) {
		return qfalse;
	}
	trap->G2API_SetSkin( rpxGlm.ghoul2, 0, h, h );
	return qtrue;
}

static qboolean RPX_GlmMake( const char *path ) {
	char skin[MAX_QPATH];
	char *slash;

	RPX_GlmFree();
	rpxGlmAnim.on = qfalse;	// a new instance stands in its base pose
	if ( trap->G2API_InitGhoul2Model( &rpxGlm.ghoul2, path, 0, 0, 0, 0, 0 ) < 0 || !rpxGlm.ghoul2 ||
		!trap->G2_HaveWeGhoul2Models( rpxGlm.ghoul2 ) ) {
		if ( rpxGlm.ghoul2 ) {
			trap->G2API_CleanGhoul2Models( &rpxGlm.ghoul2 );
		}
		rpxGlm.ghoul2 = NULL;
		return qfalse;
	}
	UI_InsertG2Pointer( rpxGlm.ghoul2 );

	// GLX_U: [Extras] or the one Skin chooses (the server's misc_model_ghoul "skin": model_<skin>.skin)
	Q_strncpyz( skin, path, sizeof( skin ) );
	slash = strrchr( skin, '/' );
	if ( slash ) {
		const char *chosen = RPX_CvarStr( "ui_rpx_g_skin" );
		qboolean set = qfalse;

		if ( chosen[0] && Q_stricmp( chosen, "default" ) && !RPX_LabelProblem( chosen ) &&
			(int)( slash + 1 - skin ) + 11 + (int)strlen( chosen ) < (int)sizeof( skin ) ) {
			Com_sprintf( slash + 1, sizeof( skin ) - (int)( slash + 1 - skin ), "model_%s.skin", chosen );
			set = RPX_GlmSetSkin( skin );
		}
		if ( !set && (int)( slash + 1 - skin ) + 18 < (int)sizeof( skin ) ) {
			strcpy( slash + 1, "model_default.skin" );
			RPX_GlmSetSkin( skin );
		}
	}
	return qtrue;
}

// GLX_U: [Extras] the GLM preview's model measured as drawn at the Animation tab's frames (RPX_GlmShape());
// its animation file and that file's frame count with it
static void RPX_GlmMeasure( void ) {
	int frames[RPX_GLM_MAX_FRAMES], n;
	float reach;

	rpxGlm.sizedAt[0] = atoi( RPX_CvarStr( "ui_rpx_g_anim" ) );
	rpxGlm.sizedAt[1] = (int)atof( RPX_CvarStr( "ui_rpx_g_start" ) );
	rpxGlm.sizedAt[2] = (int)atof( RPX_CvarStr( "ui_rpx_g_end" ) );
	rpxGlm.changedAt = 0;
	n = RPX_GlmFrameList( rpxGlm.sizedAt[0] ? qtrue : qfalse, rpxGlm.sizedAt[1], rpxGlm.sizedAt[2], frames );
	rpxGlm.sized = RPX_GlmShape( rpxGlm.path, frames, n, rpxGlm.mins, rpxGlm.maxs, &reach, rpxGlm.gla, sizeof( rpxGlm.gla ), &rpxGlm.numFrames );
	if ( !rpxGlm.sized ) {
		// the box the ui frames a menu's Ghoul2 model in when it has none (Item_Model_Paint)
		VectorSet( rpxGlm.mins, -16, -16, -24 );
		VectorSet( rpxGlm.maxs, 16, 16, 32 );
	}
}

// GLX_U: [Extras] measured again when the Animation tab's frames have changed and rest (typed ones change at
// each key; measuring reads most of a big animation file)
static void RPX_GlmResizeCheck( void ) {
	int now = uiInfo.uiDC.realTime;
	int at[3];

	at[0] = atoi( RPX_CvarStr( "ui_rpx_g_anim" ) );
	at[1] = (int)atof( RPX_CvarStr( "ui_rpx_g_start" ) );
	at[2] = (int)atof( RPX_CvarStr( "ui_rpx_g_end" ) );
	if ( !memcmp( at, rpxGlm.sizedAt, sizeof( at ) ) ) {
		rpxGlm.changedAt = 0;
		return;
	}
	if ( !rpxGlm.changedAt ) {
		rpxGlm.changedAt = now ? now : 1;
	} else if ( now - rpxGlm.changedAt >= RPX_GLM_RESIZE_SETTLE ) {
		RPX_GlmMeasure();
	}
}

// GLX_U: [Extras] the skins of the previewed model's folder on this client: its model_<name>.skin files, with
// a name the server takes (letters, digits, _ and -); "default" first
static void RPX_ReadSkins( void ) {
	static char list[8192];
	char folder[MAX_QPATH];
	const char *slash = strrchr( rpxGlm.path, '/' ), *name;
	int count, i, k;

	rpxGlm.numSkins = 0;
	if ( !slash ) {
		return;
	}
	Q_strncpyz( folder, rpxGlm.path, (int)( slash - rpxGlm.path ) + 1 );
	count = trap->FS_GetFileList( folder, ".skin", list, sizeof( list ) );
	for ( i = 0, name = list; i < count && name < list + sizeof( list ) && *name; i++, name += strlen( name ) + 1 ) {
		char skin[32];
		int len = strlen( name );

		if ( len <= 11 || len - 11 >= (int)sizeof( skin ) || Q_stricmpn( name, "model_", 6 ) || Q_stricmp( name + len - 5, ".skin" ) ) {
			continue;
		}
		Q_strncpyz( skin, name + 6, len - 10 );
		if ( RPX_LabelProblem( skin ) || rpxGlm.numSkins >= (int)ARRAY_LEN( rpxGlm.skins ) ) {
			continue;
		}
		for ( k = 0; k < rpxGlm.numSkins && Q_stricmp( rpxGlm.skins[k], skin ); k++ ) {
		}
		if ( k < rpxGlm.numSkins ) {
			continue;	// in two places
		}
		if ( !Q_stricmp( skin, "default" ) && rpxGlm.numSkins ) {
			memmove( rpxGlm.skins[1], rpxGlm.skins[0], sizeof( rpxGlm.skins[0] ) * rpxGlm.numSkins );
			Q_strncpyz( rpxGlm.skins[0], skin, sizeof( rpxGlm.skins[0] ) );
		} else {
			Q_strncpyz( rpxGlm.skins[rpxGlm.numSkins], skin, sizeof( rpxGlm.skins[0] ) );
		}
		rpxGlm.numSkins++;
	}
}

// GLX_U: [Extras] where the chosen skin is in the previewed model's list, or -1
static int RPX_GlmSkinIndex( void ) {
	const char *chosen = RPX_CvarStr( "ui_rpx_g_skin" );
	int i;

	for ( i = 0; i < rpxGlm.numSkins; i++ ) {
		if ( !Q_stricmp( rpxGlm.skins[i], chosen[0] ? chosen : "default" ) ) {
			return i;
		}
	}
	return -1;
}

// GLX_U: [Extras] Skin's < and >: the previewed model's skins in turn
static void RPX_GlmSkinStep( int step ) {
	int i;

	if ( !rpxGlm.shown ) {
		RPX_Msg( "^3Press Preview first: the skins are the previewed model's." );
		return;
	}
	if ( !rpxGlm.numSkins ) {
		RPX_Msg( "^3This model has no skin files of its own." );
		return;
	}
	i = RPX_GlmSkinIndex();
	i = ( i < 0 ) ? ( step > 0 ? 0 : rpxGlm.numSkins - 1 ) : ( i + step + rpxGlm.numSkins ) % rpxGlm.numSkins;
	trap->Cvar_Set( "ui_rpx_g_skin", Q_stricmp( rpxGlm.skins[i], "default" ) ? rpxGlm.skins[i] : "" );
	if ( !RPX_GlmMake( rpxGlm.path ) ) {	// shown with it
		rpxGlm.shown = qfalse;
	}
	RPX_Msg( "" );
}

// GLX_U: [Extras] Preview in GLM Props: as RPX_PreviewProp(), for the selected .glm
static void RPX_PreviewGlm( void ) {
	char folder[RPX_NAME_LEN], path[MAX_QPATH * 2];
	int row = RPX_SelectedRow(), i;

	if ( rpxKind != RPX_GLMS || !RPX_HaveSelection() ) {
		return;
	}
	RPX_Folder( RPX_GLMS, folder, sizeof( folder ) );
	Com_sprintf( path, sizeof( path ), "models/%s%s%s.glm", folder, folder[0] ? "/" : "", rpxEntries[row].name );
	if ( strlen( path ) >= MAX_QPATH ) {
		RPX_Msg( "^3That model's path is too long for the game." );
		return;
	}

	for ( i = 0; i < rpxGlm.numPreviewed; i++ ) {
		if ( !Q_stricmp( rpxGlm.previewed[i], path ) ) {
			break;
		}
	}
	if ( i == rpxGlm.numPreviewed ) {
		if ( rpxGlm.numPreviewed >= RPX_PREVIEW_GLMS ) {
			RPX_Msg( "^3Preview limit reached until the next map." );
			return;
		}
		if ( !RPX_LocalFileExists( path ) ) {
			RPX_GlmClear();
			RPX_Msg( "^3That model is not installed on your game (players without it will not see it either)." );
			return;
		}
		Q_strncpyz( rpxGlm.previewed[rpxGlm.numPreviewed++], path, sizeof( rpxGlm.previewed[0] ) );
	}

	Q_strncpyz( rpxGlm.path, path, sizeof( rpxGlm.path ) );
	RPX_ReadSkins();
	if ( RPX_GlmSkinIndex() < 0 ) {
		trap->Cvar_Set( "ui_rpx_g_skin", "" );	// not one of this model's
	}
	if ( !RPX_GlmMake( path ) ) {
		RPX_GlmClear();
		RPX_Msg( "^3That model could not be loaded." );
		return;
	}
	rpxGlm.shown = qtrue;
	RPX_GlmMeasure();
	RPX_ReadAnims( rpxGlm.numFrames > 0 ? rpxGlm.gla : "" );
	RPX_Msg( "" );
}

// the Tilt as cgame plays it (RPX_FxPreview: clamped to -90..90), in whole degrees
static int RPX_FxTilt( void ) {
	return (int)Com_Clamp( -90.0f, 90.0f, (float)(int)atof( RPX_CvarStr( "ui_rpx_e_pitch" ) ) );
}

static void RPX_PreviewEffect( void ) {
	if ( rpxKind != RPX_EFFECTS || !RPX_HaveSelection() ) {
		return;
	}
	rpxFxDir = atoi( RPX_CvarStr( "ui_rpx_e_dir" ) );
	rpxFxPitch = RPX_FxTilt();
	rpxFxChangeAt = 0;
	rpxFxWindowEnd = trap->Milliseconds() + RPX_FX_WINDOW;
	trap->Cmd_ExecuteText( EXEC_APPEND, "rpx do fxpreview\n" );
}

// the Galaxy RP menu opening (its onOpen): opened from an Extras menu's Back button, its Extras tab,
// as the Extras button leaves it (the same lines as that button's action)
static void RPX_BackTab( void ) {
	menuDef_t *menu = Menus_FindByName( "ingame_galaxyrp" );

	// the Galaxy RP menu sends "zykmod" as it opens: cgame's own commands keep the flood gap from it
	trap->Cvar_Set( "ui_rpx_sent", va( "%d", trap->Milliseconds() ) );

	if ( !menu || menu->itemCount <= 0 || atoi( RPX_CvarStr( "ui_rpx_back" ) ) != 1 ) {
		return;
	}
	trap->Cvar_Set( "ui_rpx_back", "0" );
	Menu_ShowItemByName( menu, "listControls", qfalse );
	Menu_ShowItemByName( menu, "extrasControls", qtrue );
	Item_RunScript( menu->items[0], "setitemcolor mainButtons forecolor 1 .682 0 1 ; setitemcolor extrasButton forecolor 1 1 1 1 ; setitemcolor extrasControls forecolor 1 .682 0 1" );
}

/*
==================
UI_RpxScript

The Extras menus' uiScripts; qfalse when name is not one of them.

	rpxOpen <props|glmprops|effects|npcs|music|sounds|lights|spawners> <menu>	in each menu's onOpen
	rpxNpcMode <0|1>									the NPCs and Vehicles tabs (NPCs & Vehicles, Spawners)
	rpxEnter											the list's double click: open a folder
	rpxPreview											Props, GLM Props and Effects
	rpxAnimSet <start|end|both>, rpxAnimEnter, rpxAnimReplay	GLM Props' Animation tab
	rpxSkin <prev|next>									GLM Props' Skin
	rpxDo <action>										a button: see CG_Rpx_f() in cg_rpextras.c
	rpxReset <prop|angles|cam>							the preview's sliders back, or the prop's angles
	rpxLightSame										Lights: the colour when off is the colour when on
	rpxClose											in each menu's onClose
	rpxBackTab											in the Galaxy RP menu's onOpen
==================
*/
qboolean UI_RpxScript( const char *name, char **args ) {
	const char *arg = NULL, *arg2 = NULL;

	if ( Q_stricmpn( name, "rpx", 3 ) ) {
		return qfalse;
	}

	if ( !Q_stricmp( name, "rpxOpen" ) ) {
		if ( String_Parse( args, &arg ) && String_Parse( args, &arg2 ) ) {
			int kind = RPX_KindForMenu( arg );

			if ( kind >= 0 ) {
				RPX_Open( kind, arg2 );
			} else if ( !Q_stricmp( arg, "lights" ) ) {
				RPX_OpenNoList( arg2 );
			}
		}
	} else if ( !Q_stricmp( name, "rpxNpcMode" ) ) {
		if ( String_Parse( args, &arg ) && ( rpxKind == RPX_NPCS || rpxKind == RPX_VEHICLES ) ) {
			int kind = atoi( arg ) ? RPX_VEHICLES : RPX_NPCS;

			trap->Cvar_Set( "ui_rpx_npcmode", ( kind == RPX_VEHICLES ) ? "1" : "0" );
			if ( kind != rpxKind ) {
				RPX_Open( kind, rpxMenu );
			}
		}
	} else if ( !Q_stricmp( name, "rpxEnter" ) ) {
		itemDef_t *item = RPX_ListItem();
		int row = RPX_SelectedRow();

		// the list runs this before it selects the row just clicked (Item_ListBox_HandleKey()), so it is a
		// double click on one row only when the row under the mouse is the one already selected -- not a
		// click on one row and then quickly on another. And after it the list goes on to select the row
		// under the mouse in the list as it is now, so the folder is opened at the next frame.
		if ( !item || !item->typeData.listbox || item->typeData.listbox->cursorPos != item->cursorPos ) {
			return qtrue;
		}
		if ( row == -1 || ( row >= 0 && row < rpxNum && rpxEntries[row].folder ) ) {
			rpxPendingNav = row;
		}
	} else if ( !Q_stricmp( name, "rpxPreview" ) ) {
		if ( rpxKind == RPX_MODELS ) {
			RPX_PreviewProp();
		} else if ( rpxKind == RPX_GLMS ) {
			RPX_PreviewGlm();
		} else if ( rpxKind == RPX_EFFECTS ) {
			RPX_PreviewEffect();
		}
	} else if ( !Q_stricmp( name, "rpxDo" ) ) {
		if ( String_Parse( args, &arg ) ) {
			// DAJ_RP: [Extras] grabprop and grabfx: the Grab buttons under Spawn, as Spawn
			// GLX_U: [Extras] spawnglm and grabglm: GLM Props' Spawn and Grab
			static const char *closing[] = { "spawnprop", "spawnfx", "grabprop", "grabfx", "spawnglm", "grabglm", "musicme", "musicall", "soundall" };
			int i;

			if ( !Q_stricmp( arg, "musicstop" ) || !Q_stricmp( arg, "musicmap" ) || !Q_stricmp( arg, "undo" ) ) {
				trap->Cmd_ExecuteText( EXEC_APPEND, va( "rpx do %s\n", arg ) );
				return qtrue;
			}
			// GalaxyRP: [Extras] the Lights menu's Spawn: nothing to select, only its name to check first,
			// so a name it cannot use is said in the menu, which stays open
			// DAJ_RP: [Extras] and its Grab (grablight), the same
			if ( !Q_stricmp( arg, "spawnlight" ) || !Q_stricmp( arg, "grablight" ) ) {
				const char *problem = RPX_LabelProblem( RPX_CvarStr( "ui_rpx_l_name" ) );

				if ( problem ) {
					RPX_Msg( va( "^3%s", problem ) );
					return qtrue;
				}
				trap->Cmd_ExecuteText( EXEC_APPEND, !Q_stricmp( arg, "grablight" ) ? "rpx do grablight\n" : "rpx do spawnlight\n" );
				RPX_CloseMenus();
				return qtrue;
			}
			// GalaxyRP: [Extras] the NPCs & Vehicles menu's Spawn: a selection, and its optional name as the
			// Lights menu checks one -- a problem is said in the menu, which stays open (cgame checks it again)
			if ( !Q_stricmp( arg, "spawnnpc" ) ) {
				const char *problem;

				if ( !RPX_HaveSelection() ) {
					return qtrue;
				}
				problem = RPX_LabelProblem( RPX_CvarStr( "ui_rpx_n_name" ) );
				if ( problem ) {
					RPX_Msg( va( "^3%s", problem ) );
					return qtrue;
				}
				trap->Cmd_ExecuteText( EXEC_APPEND, "rpx do spawnnpc\n" );
				RPX_CloseMenus();
				return qtrue;
			}
			// GalaxyRP: [Extras] the Spawners menu's Spawn: a selection, and the names it needs and may have
			// (RPX_SpawnerNamesProblem()) -- a problem is said in the menu, which stays open
			// DAJ_RP: [Extras] and its Grab (grabspawner), the same
			if ( !Q_stricmp( arg, "spawnspawner" ) || !Q_stricmp( arg, "grabspawner" ) ) {
				const char *problem;

				if ( !RPX_HaveSelection() ) {
					return qtrue;
				}
				problem = RPX_SpawnerNamesProblem( RPX_CvarStr( "ui_rpx_sp_name" ), RPX_CvarStr( "ui_rpx_sp_npcname" ),
					RPX_CvarStr( "ui_rpx_sp_ondeath" ), rpxKind != RPX_VEHICLES );
				if ( problem ) {
					RPX_Msg( va( "^3%s", problem ) );
					return qtrue;
				}
				trap->Cmd_ExecuteText( EXEC_APPEND, !Q_stricmp( arg, "grabspawner" ) ? "rpx do grabspawner\n" : "rpx do spawnspawner\n" );
				RPX_CloseMenus();
				return qtrue;
			}
			// GLX_U: [Extras] GLM Props' Spawn and Grab: frames the server would refuse are said in the menu,
			// which stays open (cgame checks them again)
			if ( !Q_stricmp( arg, "spawnglm" ) || !Q_stricmp( arg, "grabglm" ) ) {
				const char *problem = RPX_GlmSpawnFramesProblem();

				if ( problem ) {
					RPX_Msg( va( "^3%s", problem ) );
					return qtrue;
				}
			}
			for ( i = 0; i < (int)ARRAY_LEN( closing ); i++ ) {
				if ( !Q_stricmp( arg, closing[i] ) ) {
					if ( RPX_HaveSelection() ) {
						trap->Cmd_ExecuteText( EXEC_APPEND, va( "rpx do %s\n", closing[i] ) );
						RPX_StopFxPreview();
						RPX_CloseMenus();
					}
					break;
				}
			}
		}
	} else if ( !Q_stricmp( name, "rpxAnimSet" ) ) {
		// GLX_U: [Extras] GLM Props' Set start / Set end / Use
		if ( String_Parse( args, &arg ) ) {
			RPX_AnimSet( !Q_stricmp( arg, "start" ) ? 1 : !Q_stricmp( arg, "end" ) ? 2 : 3 );
		}
	} else if ( !Q_stricmp( name, "rpxAnimEnter" ) ) {
		// GLX_U: [Extras] a double click in the animation list: Use -- on the row already selected only, as
		// rpxEnter (the list runs this before it selects the row clicked)
		itemDef_t *item = RPX_FeederItem( FEEDER_RPX_ANIMS );

		if ( item && item->typeData.listbox && item->typeData.listbox->cursorPos == item->cursorPos ) {
			RPX_AnimSet( 3 );
		}
	} else if ( !Q_stricmp( name, "rpxSkin" ) ) {
		// GLX_U: [Extras] GLM Props' Skin < and >
		if ( String_Parse( args, &arg ) ) {
			RPX_GlmSkinStep( !Q_stricmp( arg, "prev" ) ? -1 : 1 );
		}
	} else if ( !Q_stricmp( name, "rpxAnimReplay" ) ) {
		rpxGlmAnim.replay = qtrue;	// GLX_U: [Extras] RPX_GlmAnimate() plays them again
	} else if ( !Q_stricmp( name, "rpxReset" ) ) {
		if ( String_Parse( args, &arg ) ) {
			// GalaxyRP: [Extras] "prop" is the Props preview's Reset view: only what the preview alone
			// uses. The prop's own angles, which it is spawned with, have their own button ("angles").
			// GLX_U: in GLM Props, its own (ui_rpx_g_*)
			const char *pfx = ( rpxKind == RPX_GLMS ) ? "g" : "p";

			if ( !Q_stricmp( arg, "prop" ) ) {
				trap->Cvar_Set( va( "ui_rpx_%s_zoom", pfx ), "100" );
			} else if ( !Q_stricmp( arg, "angles" ) ) {
				trap->Cvar_Set( va( "ui_rpx_%s_yaw", pfx ), "0" );
				trap->Cvar_Set( va( "ui_rpx_%s_pitch", pfx ), "0" );
				trap->Cvar_Set( va( "ui_rpx_%s_roll", pfx ), "0" );
			} else if ( !Q_stricmp( arg, "cam" ) ) {
				trap->Cvar_Set( "ui_rpx_e_cdist", "160" );
				trap->Cvar_Set( "ui_rpx_e_cang", "0" );
			}
		}
	} else if ( !Q_stricmp( name, "rpxLightSame" ) ) {
		// GalaxyRP: [Extras] the Lights menu's "Same as on": its colour when off is the colour when on
		trap->Cvar_Set( "ui_rpx_l_offr", RPX_CvarStr( "ui_rpx_l_r" ) );
		trap->Cvar_Set( "ui_rpx_l_offg", RPX_CvarStr( "ui_rpx_l_g" ) );
		trap->Cvar_Set( "ui_rpx_l_offb", RPX_CvarStr( "ui_rpx_l_b" ) );
	} else if ( !Q_stricmp( name, "rpxClose" ) ) {
		RPX_StopFxPreview();
		RPX_GlmFree();	// GLX_U: [Extras]
		trap->Cmd_ExecuteText( EXEC_APPEND, "rpx do stop\n" );
	} else if ( !Q_stricmp( name, "rpxBackTab" ) ) {
		RPX_BackTab();
	} else {
		return qfalse;
	}
	return qtrue;
}

/*
===========================================================================
Owner draws
===========================================================================
*/

static void RPX_CenterText( rectDef_t *rect, float scale, vec4_t color, const char *text, int iMenuFont ) {
	int w = Text_Width( text, scale, iMenuFont );

	Text_Paint( rect->x + ( rect->w - w ) / 2, rect->y + rect->h / 2 - 8, scale, color, text, 0, 0, ITEM_TEXTSTYLE_NORMAL, iMenuFont );
}

static void RPX_SceneRect( rectDef_t *rect, refdef_t *refdef, float fovX ) {
	float x = rect->x + 1, y = rect->y + 1, w = rect->w - 2, h = rect->h - 2;

	memset( refdef, 0, sizeof( *refdef ) );
	refdef->rdflags = RDF_NOWORLDMODEL;
	AxisClear( refdef->viewaxis );
	refdef->x = x * uiInfo.uiDC.xscale;
	refdef->y = y * uiInfo.uiDC.yscale;
	refdef->width = w * uiInfo.uiDC.xscale;
	refdef->height = h * uiInfo.uiDC.yscale;
	refdef->fov_x = fovX;
	refdef->fov_y = atan2( refdef->height, refdef->width / tan( fovX / 360 * M_PI ) ) * ( 360 / M_PI );
	refdef->time = uiInfo.uiDC.realTime;
}

// the yaw the Props preview turns the model by, in the camera's frame (the camera looks along +x, as
// the player looks at what they spawn): the spawned prop's yaw less the player's view yaw
static float RPX_PropPreviewYaw( qboolean faceMe, int yaw, int viewYaw ) {
	if ( faceMe ) {
		return 180.0f + yaw;
	}
	return (float)( yaw - viewYaw );
}

// GLX_U: [Extras] the MD3 and GLM previews' box: the model (an md3's handle, or a Ghoul2 instance) with
// the box round it (mins/maxs; sized qfalse: a stand-in), turned, zoomed and lit by its menu's cvars
// (ui_rpx_<pfx>_*: p for MD3 Props, g for GLM Props)
static void RPX_DrawModelPreview( rectDef_t *rect, float scale, vec4_t color, int iMenuFont, const char *pfx,
	qhandle_t hModel, void *ghoul2, const vec3_t mins, const vec3_t maxs, qboolean sized ) {
	refdef_t refdef;
	refEntity_t ent;
	vec3_t center, size, angles, rc;
	float radius, dist, zoom, propScale, fov;
	char text[128];
	int k;

	fov = 40.0f;
	RPX_SceneRect( rect, &refdef, fov );
	if ( refdef.fov_y < fov ) {
		fov = refdef.fov_y;
	}

	VectorAdd( mins, maxs, center );
	VectorScale( center, 0.5f, center );
	VectorSubtract( maxs, mins, size );
	radius = 0.5f * VectorLength( size );
	if ( radius < 1.0f ) {
		radius = 1.0f;
	}
	zoom = atof( RPX_CvarStr( va( "ui_rpx_%s_zoom", pfx ) ) );
	if ( zoom < 25.0f ) zoom = 25.0f;
	if ( zoom > 400.0f ) zoom = 400.0f;
	dist = radius / sin( DEG2RAD( fov * 0.5f ) ) * ( 100.0f / zoom );

	// GalaxyRP: [Extras] the prop as it will stand in front of the player, the camera standing for
	// them: the spawn's yaw (cg_rpextras.c, RPX_SpawnProp) less their view yaw. With Face me that is
	// 180 + Yaw whatever way they look (its front towards them, then Yaw); without it, Yaw is a map
	// direction, so how it turns to them depends on which way they look (ui_rpx_viewyaw, from cgame).
	angles[PITCH] = (int)atof( RPX_CvarStr( va( "ui_rpx_%s_pitch", pfx ) ) );
	angles[YAW] = RPX_PropPreviewYaw( atoi( RPX_CvarStr( va( "ui_rpx_%s_faceme", pfx ) ) ) ? qtrue : qfalse,
		(int)atof( RPX_CvarStr( va( "ui_rpx_%s_yaw", pfx ) ) ), atoi( RPX_CvarStr( "ui_rpx_viewyaw" ) ) );
	angles[ROLL] = (int)atof( RPX_CvarStr( va( "ui_rpx_%s_roll", pfx ) ) );

	memset( &ent, 0, sizeof( ent ) );
	AnglesToAxis( angles, ent.axis );
	// the middle of the model's box, turned with it, at the point the camera looks at
	for ( k = 0; k < 3; k++ ) {
		rc[k] = center[0] * ent.axis[0][k] + center[1] * ent.axis[1][k] + center[2] * ent.axis[2][k];
	}
	VectorSet( ent.origin, dist, 0, 0 );
	VectorSubtract( ent.origin, rc, ent.origin );
	VectorCopy( ent.origin, ent.oldorigin );
	VectorCopy( ent.origin, ent.lightingOrigin );
	if ( ghoul2 ) {
		ent.ghoul2 = ghoul2;
		ent.radius = 1000;	// as the ui's own menu models (Item_Model_Paint)
	} else {
		ent.hModel = hModel;
	}
	ent.renderfx = RF_LIGHTING_ORIGIN | RF_NOSHADOW;

	propScale = atof( RPX_CvarStr( va( "ui_rpx_%s_scale", pfx ) ) ) / 100.0f;	// a percent
	if ( propScale < 0.01f ) propScale = 0.01f;
	if ( propScale > 10.23f ) propScale = 10.23f;

	trap->R_ClearScene();
	trap->R_AddRefEntityToScene( &ent );
	if ( atoi( RPX_CvarStr( va( "ui_rpx_%s_light", pfx ) ) ) ) {
		// the prop's light, near the side of the model facing the camera, at the size it has next to
		// the model at this scale (kept big enough to be seen)
		vec3_t lightOrg;
		float intensity = atof( RPX_CvarStr( va( "ui_rpx_%s_lrad", pfx ) ) ) / propScale;

		if ( intensity < radius * 0.5f ) intensity = radius * 0.5f;
		if ( intensity > radius * 6.0f ) intensity = radius * 6.0f;
		VectorSet( lightOrg, dist - radius * 1.2f, radius * 0.4f, radius * 0.6f );
		trap->R_AddLightToScene( lightOrg, intensity,
			Com_Clamp( 0, 1, atof( RPX_CvarStr( va( "ui_rpx_%s_lr", pfx ) ) ) / 255.0f ),
			Com_Clamp( 0, 1, atof( RPX_CvarStr( va( "ui_rpx_%s_lg", pfx ) ) ) / 255.0f ),
			Com_Clamp( 0, 1, atof( RPX_CvarStr( va( "ui_rpx_%s_lb", pfx ) ) ) / 255.0f ) );
	}
	trap->R_RenderScene( &refdef );

	if ( sized ) {
		Com_sprintf( text, sizeof( text ), "%.0f x %.0f x %.0f units", size[0] * propScale, size[1] * propScale, size[2] * propScale );
	} else {
		Q_strncpyz( text, "size unknown", sizeof( text ) );
	}
	Text_Paint( rect->x + 4, rect->y + rect->h - 18, scale * 0.8f, color, text, 0, 0, ITEM_TEXTSTYLE_NORMAL, iMenuFont );
}

static vec4_t rpxPreviewBack = { 0.02f, 0.03f, 0.06f, 1.0f };

static void RPX_DrawPropPreview( rectDef_t *rect, float scale, vec4_t color, int iMenuFont ) {
	RPX_CheckMap();
	UI_FillRect( rect->x, rect->y, rect->w, rect->h, rpxPreviewBack );

	if ( !rpxProp.model ) {
		RPX_CenterText( rect, scale, color, "Press Preview to see it", iMenuFont );
		return;
	}
	RPX_DrawModelPreview( rect, scale, color, iMenuFont, "p", rpxProp.model, NULL, rpxProp.mins, rpxProp.maxs, qtrue );
}

// GLX_U: [Extras] GLM Props' preview: its instance made again first when the menu has been opened again
static void RPX_DrawGlmPreview( rectDef_t *rect, float scale, vec4_t color, int iMenuFont ) {
	RPX_CheckMap();
	UI_FillRect( rect->x, rect->y, rect->w, rect->h, rpxPreviewBack );

	if ( rpxGlm.shown && !rpxGlm.ghoul2 && !RPX_GlmMake( rpxGlm.path ) ) {
		rpxGlm.shown = qfalse;
	}
	if ( rpxGlm.shown ) {
		RPX_GlmAnimate();	// GLX_U: the Animation tab's frames (may make the instance again)
		RPX_GlmResizeCheck();
	}
	if ( !rpxGlm.shown || !rpxGlm.ghoul2 ) {
		rpxGlm.shown = qfalse;
		RPX_CenterText( rect, scale, color, "Press Preview to see it", iMenuFont );
		return;
	}
	RPX_DrawModelPreview( rect, scale, color, iMenuFont, "g", 0, rpxGlm.ghoul2, rpxGlm.mins, rpxGlm.maxs, rpxGlm.sized );
}

static void RPX_DrawFxBox( rectDef_t *rect, float scale, vec4_t color, int iMenuFont ) {
	static vec4_t black = { 0, 0, 0, 1 };
	int now = trap->Milliseconds();

	RPX_CheckMap();
	UI_FillRect( rect->x, rect->y, rect->w, rect->h, black );

	// tell cgame the box is on screen, so it adds the preview's effect to the scene
	if ( !rpxFxBoxSet || now - rpxFxBoxSet >= 100 || now - rpxFxBoxSet < 0 ) {
		rpxFxBoxSet = now ? now : 1;
		trap->Cvar_Set( "ui_rpx_fxbox", va( "%d", rpxFxBoxSet ) );
	}

	if ( rpxFxWindowEnd && now - rpxFxWindowEnd < 0 ) {
		refdef_t refdef;
		vec3_t origin, camOrg, camAngles;
		int dir = atoi( RPX_CvarStr( "ui_rpx_e_dir" ) ), tilt = RPX_FxTilt();

		// GalaxyRP: [Extras] Direction or Tilt changed while the effect shows: it is played again the
		// new way once they have rested RPX_FX_SETTLE (a dragged slider changes it every frame). Tilt
		// only counts for Direction Tilted. A replay of the same effect takes no new preview slot.
		if ( dir != rpxFxDir || ( dir == 2 && tilt != rpxFxPitch ) ) {
			if ( !rpxFxChangeAt || dir != rpxFxSeenDir || tilt != rpxFxSeenPitch ) {
				rpxFxChangeAt = now ? now : 1;
				rpxFxSeenDir = dir;
				rpxFxSeenPitch = tilt;
			} else if ( now - rpxFxChangeAt >= RPX_FX_SETTLE ) {
				RPX_PreviewEffect();
			}
		} else {
			rpxFxChangeAt = 0;
		}

		RPX_SceneRect( rect, &refdef, RPX_FX_FOV );
		refdef.time = atoi( RPX_CvarStr( "ui_rpx_fxtime" ) );	// the effects' shader times are on cgame's clock
		RPX_FxCamera( atof( RPX_CvarStr( "ui_rpx_e_cdist" ) ), atof( RPX_CvarStr( "ui_rpx_e_cang" ) ), rpxFxDir, origin, camOrg, camAngles );
		VectorCopy( camOrg, refdef.vieworg );
		VectorCopy( camAngles, refdef.viewangles );
		AnglesToAxis( camAngles, refdef.viewaxis );
		// no R_ClearScene: the scene holds what cgame's CG_RpxFxPass() added, the preview's effect
		trap->R_RenderScene( &refdef );
		return;
	}
	rpxFxWindowEnd = 0;
	rpxFxChangeAt = 0;
	RPX_CenterText( rect, scale, color, "Press Preview to play it", iMenuFont );
}

// a light's colour from three 0..255 cvars; dark (black) when "lit" says it gives no light
static void RPX_DrawSwatchOf( rectDef_t *rect, const char *r, const char *g, const char *b, qboolean lit ) {
	static vec4_t edge = { 1, 0.682f, 0, 1 };
	vec4_t c;

	c[0] = lit ? Com_Clamp( 0, 1, atof( RPX_CvarStr( r ) ) / 255.0f ) : 0;
	c[1] = lit ? Com_Clamp( 0, 1, atof( RPX_CvarStr( g ) ) / 255.0f ) : 0;
	c[2] = lit ? Com_Clamp( 0, 1, atof( RPX_CvarStr( b ) ) / 255.0f ) : 0;
	c[3] = 1;
	UI_FillRect( rect->x, rect->y, rect->w, rect->h, c );
	UI_DrawRect( rect->x, rect->y, rect->w, rect->h, edge );
}

static void RPX_DrawSwatch( rectDef_t *rect ) {
	RPX_DrawSwatchOf( rect, "ui_rpx_p_lr", "ui_rpx_p_lg", "ui_rpx_p_lb", qtrue );
}

// GLX_U: [Extras] GLM Props' animation file line: the file and its frames, or why there are none
static void RPX_DrawAnimFile( rectDef_t *rect, float scale, vec4_t color, int iMenuFont ) {
	char text[128];
	const char *file;

	if ( !rpxGlm.shown ) {
		Q_strncpyz( text, "Press Preview to read its animations", sizeof( text ) );
	} else if ( !RPX_GlaNamed( rpxGlm.gla ) ) {
		Q_strncpyz( text, "No animation file: base pose only", sizeof( text ) );
	} else {
		file = strrchr( rpxGlm.gla, '/' );
		file = file ? file + 1 : rpxGlm.gla;
		if ( !rpxGlm.numFrames ) {
			Com_sprintf( text, sizeof( text ), "^3%s.gla not found", file );
		} else {
			Com_sprintf( text, sizeof( text ), "%s.gla: %d frames%s", file, rpxGlm.numFrames,
				rpxAnimsState == RPX_ANIMS_TOOBIG ? ", ^3animation.cfg too big" :
				rpxAnimsState == RPX_ANIMS_OK ? "" : ", no named ones" );
		}
	}
	Text_Paint( rect->x, rect->y, scale, color, text, 0, 0, ITEM_TEXTSTYLE_NORMAL, iMenuFont );
}

// GLX_U: [Extras] GLM Props' frames: what will be played and for how long, the animations they start and end
// with, or what is wrong with them
static void RPX_DrawAnimRange( rectDef_t *rect, float scale, vec4_t color, int iMenuFont ) {
	const char *problem = RPX_GlmSpawnFramesProblem();
	int start = (int)atof( RPX_CvarStr( "ui_rpx_g_start" ) ), end = (int)atof( RPX_CvarStr( "ui_rpx_g_end" ) );
	char text[128];

	if ( !atoi( RPX_CvarStr( "ui_rpx_g_anim" ) ) ) {
		Q_strncpyz( text, "Not animated: its base pose", sizeof( text ) );
	} else if ( problem ) {
		Com_sprintf( text, sizeof( text ), "^3%s", problem );
	} else {
		int n = end - start + 1;
		const char *from = RPX_AnimAt( start, qtrue ), *to = RPX_AnimAt( end, qfalse );

		if ( from || to ) {
			if ( from && to && !strcmp( from, to ) ) {
				Q_strncpyz( text, from, sizeof( text ) );
			} else {
				Com_sprintf( text, sizeof( text ), "%s > %s", from ? from : "...", to ? to : "..." );
			}
			Text_Paint( rect->x, rect->y, scale, color, text, 0, 0, ITEM_TEXTSTYLE_NORMAL, iMenuFont );
		}
		Com_sprintf( text, sizeof( text ), "Frames %d-%d: %d, %.1f s", start, end, n, n / 20.0f );
	}
	Text_Paint( rect->x, rect->y + rect->h / 2, scale, color, text, 0, 0, ITEM_TEXTSTYLE_NORMAL, iMenuFont );
}

// GLX_U: [Extras] GLM Props' Skin line: the chosen one, and which of the previewed model's it is
static void RPX_DrawSkin( rectDef_t *rect, float scale, vec4_t color, int iMenuFont ) {
	const char *chosen = RPX_CvarStr( "ui_rpx_g_skin" );
	char text[96];
	int i = rpxGlm.shown ? RPX_GlmSkinIndex() : -1;

	if ( i >= 0 ) {
		Com_sprintf( text, sizeof( text ), "Skin: %s (%d of %d)", rpxGlm.skins[i], i + 1, rpxGlm.numSkins );
	} else {
		Com_sprintf( text, sizeof( text ), "Skin: %s", chosen[0] ? chosen : "default" );
	}
	Text_Paint( rect->x, rect->y, scale, color, text, 0, 0, ITEM_TEXTSTYLE_NORMAL, iMenuFont );
}

qboolean UI_RpxOwnerDraw( int ownerDraw, rectDef_t *rect, float scale, vec4_t color, int iMenuFont ) {
	switch ( ownerDraw ) {
	case UI_RPX_PREVIEW:
		RPX_DrawPropPreview( rect, scale, color, iMenuFont );
		return qtrue;
	case UI_RPX_FXBOX:
		RPX_DrawFxBox( rect, scale, color, iMenuFont );
		return qtrue;
	case UI_RPX_LSWATCH:
		RPX_DrawSwatchOf( rect, "ui_rpx_l_r", "ui_rpx_l_g", "ui_rpx_l_b", qtrue );
		return qtrue;
	case UI_RPX_LOFFSWATCH:	// black while it is dark when off (Off radius 0)
		RPX_DrawSwatchOf( rect, "ui_rpx_l_offr", "ui_rpx_l_offg", "ui_rpx_l_offb", atoi( RPX_CvarStr( "ui_rpx_l_offrad" ) ) > 0 ? qtrue : qfalse );
		return qtrue;
	case UI_RPX_SWATCH:
		RPX_DrawSwatch( rect );
		return qtrue;
	case UI_RPX_GPREVIEW:	// GLX_U: [Extras] GLM Props
		RPX_DrawGlmPreview( rect, scale, color, iMenuFont );
		return qtrue;
	case UI_RPX_GSWATCH:
		RPX_DrawSwatchOf( rect, "ui_rpx_g_lr", "ui_rpx_g_lg", "ui_rpx_g_lb", qtrue );
		return qtrue;
	case UI_RPX_GANIMFILE:
		RPX_DrawAnimFile( rect, scale, color, iMenuFont );
		return qtrue;
	case UI_RPX_GANIMRANGE:
		RPX_DrawAnimRange( rect, scale, color, iMenuFont );
		return qtrue;
	case UI_RPX_GSKIN:
		RPX_DrawSkin( rect, scale, color, iMenuFont );
		return qtrue;
	default:
		return qfalse;
	}
}

// with the ui's own cvars, so they exist, with their defaults, before cgame first starts
void UI_RpxRegisterCvars( void ) {
#define RPX_REGISTER( name, def ) trap->Cvar_Register( NULL, name, def, CVAR_TEMP );
	RPX_CVAR_LIST( RPX_REGISTER )
#undef RPX_REGISTER
}
