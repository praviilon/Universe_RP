/*
===========================================================================
GLX_U: [Shader Remaps Menu] the Shader Remaps menu of the Extras (ingame_rpx_shaders.menu) and its preview strip
(ingame_rpx_shprev.menu): the shader remap commands (/remap, /remapremove, /remapreset) in a menu.

- Left list (FEEDER_RPX_SHMAP): the shaders the map's drawn surfaces use, read here from the map file's shader
  and surface parts, so a shader this game could not load is listed too (in red: neither an image nor a script
  of that name), or -- ui_rpx_sh_view 1 -- the remaps in force, from the CS_SHADERSTATE configstring.
- Right list (FEEDER_RPX_SHNEW): what a shader can be remapped to, from this game's own files (each client draws a
  remap from its own files): the image files and the shaders named in shaders/*.shader, as one folder tree. A
  script shader with fog, nodraw, a portal or a sky is left out: it breaks a surface it is put on.
- "Pick from world": cgame finds the shader under the aim (rpx do pickshader, cg_shaderaim.c) and it is selected
  in the left list.
- Old and New (ui_rpx_sh_old, ui_rpx_sh_new) with a thumbnail each (UI_RPX_SHOLD, UI_RPX_SHNEW). A shader is never
  registered under its real name here: see RPX_ShaderThumb() in cg_rpextras.c, which registers the image under
  another spelling; it is drawn on a flat model (RPSH_QUAD_MODEL), which lights it like any model.
- Preview: the remap on this game only (R_RemapShader, nothing sent), with the menu closed and the strip open,
  so the real surfaces show it with the engine's real lighting. Closing the strip takes it back: the shader
  remapped to itself, then to what the server says, as /remapreset does.
- Apply, Remove, Reset all: cgame builds and sends the commands (rpx do remap / remapremove / remapreset).

What the engine does with a remap is its own (R_RemapShader, tr_shader.cpp): every copy of the old shader is
pointed at one copy of the new one, so how the surface is lit can differ from the original.
===========================================================================
*/

#include "ui_local.h"
#include "ui/rp_extras.h"
#include "qcommon/qfiles.h"

extern void Text_Paint( float x, float y, float scale, vec4_t color, const char *text, float adjust, int limit, int style, int iMenuFont );
extern int Text_Width( const char *text, float scale, int iMenuFont );
extern int Item_ListBox_MaxScroll( itemDef_t *item );

#define RPSH_MENU			"ingame_rpx_shaders"
#define RPSH_STRIP			"ingame_rpx_shprev"
#define RPSH_QUAD_MODEL		"models/rp_ui/quad.md3"

#define RPSH_MAX_MAP		2048		// shaders of one map listed
#define RPSH_MAX_BSP_SHADERS 4096		// shader records of a map file read (the map compiler stops at 1024)
#define RPSH_MAX_REMAPS		128			// MAX_SHADER_REMAPS
#define RPSH_MAX_NAMES		24576		// shaders named in the scripts
#define RPSH_ARENA			0x100000
#define RPSH_MAX_RIGHT		4096		// entries of one folder shown
#define RPSH_RAW_RIGHT		( RPSH_MAX_RIGHT * 3 )
#define RPSH_LISTBUF		0x20000
#define RPSH_CHUNK			16384
#define RPSH_THUMB_SETTLE	250			// a name rests this long before its thumbnail is asked for
#define RPSH_RESET_CONFIRM	4000

#define RPSH_DIR			1
#define RPSH_IMAGE			2
#define RPSH_SCRIPT			4
#define RPSH_UNSAFE			8

typedef struct {
	char	name[MAX_QPATH];
	byte	flags;
} rpshEntry_t;

typedef struct {
	char	oldName[MAX_QPATH];
	char	newName[MAX_QPATH];
	char	time[16];
} rpshRemap_t;

// the map's shaders
static char			rpshMap[RPSH_MAX_MAP][MAX_QPATH];
static byte			rpshMapMissing[RPSH_MAX_MAP];
static int			rpshMapNum;
static qboolean		rpshMapTried, rpshMapOk;

// the remaps in force
static rpshRemap_t	rpshRemaps[RPSH_MAX_REMAPS];
static int			rpshRemapNum;
static char			rpshCsSeen[MAX_STRING_CHARS * 4 + 16];

// the left list as shown
static int			rpshLeft[RPSH_MAX_MAP];
static int			rpshLeftNum;
static int			rpshLeftMode = -1;
static char			rpshLeftFilter[64];

// the shaders named in the scripts, sorted
static char			rpshArena[RPSH_ARENA];
static int			rpshArenaUsed;
static int			rpshNameOfs[RPSH_MAX_NAMES];
static byte			rpshNameUnsafe[RPSH_MAX_NAMES];
static int			rpshNumNames;
static qboolean		rpshScriptsRead, rpshScriptsCut;

// the right list
static rpshEntry_t	rpshRaw[RPSH_RAW_RIGHT];
static int			rpshRawNum;
static rpshEntry_t	rpshRight[RPSH_MAX_RIGHT];
static int			rpshRightNum;
static qboolean		rpshRightCut;
static char			rpshRightDir[MAX_QPATH];
static qboolean		rpshRightLoaded;
static int			rpshRightView[RPSH_MAX_RIGHT + 1];	// index into rpshRight; -1 for ".."
static int			rpshRightViewNum;
static char			rpshRightFilter[64];
static int			rpshPendingEnter = -2;		// a row of the right list to open at the next sync; -2 for none

static char			rpshListBuf[RPSH_LISTBUF];

static int			rpshMapId;
static int			rpshSyncTime = -1;
static int			rpshPickSeen;
static char			rpshOldSeen[MAX_QPATH], rpshNewSeen[MAX_QPATH];
static int			rpshResetAsked;

// the preview in force: the shader remapped on this game only; "" for none
static char			rpshPreviewOld[MAX_QPATH];

typedef enum { RPSH_TH_NONE, RPSH_TH_PENDING, RPSH_TH_ASKED, RPSH_TH_READY, RPSH_TH_NOIMAGE, RPSH_TH_FULL } rpshThumbState_t;

static struct {
	char				name[MAX_QPATH];
	rpshThumbState_t	state;
	int					changedAt;
	qhandle_t			handle;
} rpshThumb[2];

static qhandle_t	rpshQuad;

/*
===========================================================================
Helpers
===========================================================================
*/

static void RPSH_Cvar( const char *name, char *buf, int size ) {
	trap->Cvar_VariableStringBuffer( name, buf, size );
}

static int RPSH_CvarInt( const char *name ) {
	char buf[32];

	trap->Cvar_VariableStringBuffer( name, buf, sizeof( buf ) );
	return atoi( buf );
}

static void RPSH_Msg( const char *text ) {
	trap->Cvar_Set( "ui_rpx_msg", text );
}

static qboolean RPSH_Contains( const char *hay, const char *needle ) {
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

// a name /remap takes and that can stand in a command: as RPX_ShaderNameOk() in cg_rpextras.c, and no '$'
static qboolean RPSH_NameOk( const char *name ) {
	int i;

	for ( i = 0; name[i]; i++ ) {
		unsigned char c = (unsigned char)name[i];

		if ( c <= ' ' || c > 126 || strchr( "=:@\";%\\^$", c ) ) {
			return qfalse;
		}
	}
	return ( i >= 1 && i < MAX_QPATH ) ? qtrue : qfalse;
}

static qboolean RPSH_FileExists( const char *path ) {
	fileHandle_t f = 0;

	trap->FS_Open( path, &f, FS_READ );
	if ( f ) {
		trap->FS_Close( f );
		return qtrue;
	}
	return qfalse;
}

// an image file of that shader name, as the renderer looks for one
static qboolean RPSH_ImageExists( const char *name ) {
	char base[MAX_QPATH];

	COM_StripExtension( name, base, sizeof( base ) );
	if ( !base[0] || strlen( base ) > MAX_QPATH - 5 ) {
		return qfalse;
	}
	return ( RPSH_FileExists( va( "%s.jpg", base ) ) || RPSH_FileExists( va( "%s.tga", base ) ) || RPSH_FileExists( va( "%s.png", base ) ) ) ? qtrue : qfalse;
}

static itemDef_t *RPSH_List( int feeder ) {
	menuDef_t *menu = Menus_FindByName( RPSH_MENU );
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

// the selected row of a list, scrolled into view; -1 for none (as RPX_SetListCursor(), ui_rpextras.c)
static void RPSH_SetCursor( int feeder, int index ) {
	itemDef_t *item = RPSH_List( feeder );
	listBoxDef_t *listPtr;

	if ( !item || !item->typeData.listbox ) {
		return;
	}
	listPtr = item->typeData.listbox;
	item->cursorPos = index;
	listPtr->cursorPos = ( index >= 0 ) ? index : 0;
	if ( index < 0 ) {
		listPtr->startPos = 0;
	} else if ( !RP_ListRowInView( item, index ) ) {
		int max = Item_ListBox_MaxScroll( item );

		listPtr->startPos = ( index > 3 ) ? index - 3 : 0;
		if ( listPtr->startPos > max ) {
			listPtr->startPos = max;
		}
	}
}

// GLX_U: [Shader Remaps Menu] a list's highlight removed, the list left where it is scrolled
static void RPSH_Unselect( int feeder ) {
	itemDef_t *item = RPSH_List( feeder );

	if ( !item || !item->typeData.listbox ) {
		return;
	}
	item->cursorPos = -1;
	item->typeData.listbox->cursorPos = 0;
}

/*
===========================================================================
The shaders named in the scripts
===========================================================================
*/

static int QDECL RPSH_NameCompare( const void *a, const void *b ) {
	return strcmp( rpshArena + *(const int *)a, rpshArena + *(const int *)b );
}

// the index of a script shader of that name, -1 for none
static int RPSH_FindScript( const char *name ) {
	char lower[MAX_QPATH];
	int lo = 0, hi = rpshNumNames - 1, i;

	for ( i = 0; name[i] && i < MAX_QPATH - 1; i++ ) {
		lower[i] = ( name[i] == '\\' ) ? '/' : tolower( (unsigned char)name[i] );
	}
	lower[i] = '\0';
	if ( name[i] ) {
		return -1;
	}
	while ( lo <= hi ) {
		int mid = ( lo + hi ) / 2;
		int c = strcmp( lower, rpshArena + rpshNameOfs[mid] );

		if ( !c ) {
			return mid;
		}
		if ( c < 0 ) {
			hi = mid - 1;
		} else {
			lo = mid + 1;
		}
	}
	return -1;
}

// one script file, read forward a piece at a time: the names outside any braces are its shaders; inside one, the
// words that make it unfit to remap to
typedef struct {
	int			depth;
	char		tok[80];
	int			tokLen;
	qboolean	tokLong;
	char		prev[16];
	int			comment;	// 0 none, 1 to the end of the line, 2 to the closing star-slash
	char		last;
	int			cur;		// the shader being read, -1 for none
	qboolean	quote;
} rpshParse_t;

static void RPSH_ParseToken( rpshParse_t *ps ) {
	int i;

	if ( !ps->tokLen && !ps->tokLong ) {
		return;
	}
	ps->tok[ps->tokLen] = '\0';
	if ( ps->depth == 0 ) {
		ps->cur = -1;
		if ( !ps->tokLong && ps->tokLen < MAX_QPATH ) {
			if ( rpshNumNames >= RPSH_MAX_NAMES || rpshArenaUsed + ps->tokLen + 1 > RPSH_ARENA ) {
				rpshScriptsCut = qtrue;
			} else {
				for ( i = 0; i < ps->tokLen; i++ ) {
					rpshArena[rpshArenaUsed + i] = ( ps->tok[i] == '\\' ) ? '/' : tolower( (unsigned char)ps->tok[i] );
				}
				rpshArena[rpshArenaUsed + ps->tokLen] = '\0';
				ps->cur = rpshNumNames;
				rpshNameOfs[rpshNumNames] = rpshArenaUsed;
				rpshNameUnsafe[rpshNumNames] = 0;
				rpshNumNames++;
				rpshArenaUsed += ps->tokLen + 1;
			}
		}
	} else if ( !ps->tokLong ) {
		qboolean unsafe = qfalse;

		if ( !Q_stricmp( ps->tok, "fogparms" ) || !Q_stricmp( ps->tok, "skyparms" ) ) {
			unsafe = qtrue;
		} else if ( !Q_stricmp( ps->prev, "surfaceparm" ) && ( !Q_stricmp( ps->tok, "nodraw" ) || !Q_stricmp( ps->tok, "fog" ) || !Q_stricmp( ps->tok, "sky" ) ) ) {
			unsafe = qtrue;
		} else if ( !Q_stricmp( ps->tok, "portal" ) && Q_stricmp( ps->prev, "alphagen" ) ) {
			unsafe = qtrue;		// "portal" or "sort portal"; "alphaGen portal" is only a fade
		}
		if ( unsafe && ps->cur >= 0 ) {
			rpshNameUnsafe[ps->cur] = 1;
		}
		Q_strncpyz( ps->prev, ps->tok, sizeof( ps->prev ) );
	} else {
		ps->prev[0] = '\0';
	}
	ps->tokLen = 0;
	ps->tokLong = qfalse;
}

static void RPSH_ParseChar( rpshParse_t *ps, char c ) {
	char last = ps->last;

	ps->last = c;
	if ( ps->comment == 1 ) {
		if ( c == '\n' ) {
			ps->comment = 0;
		}
		return;
	}
	if ( ps->comment == 2 ) {
		if ( last == '*' && c == '/' ) {
			ps->comment = 0;
			ps->last = '\0';
		}
		return;
	}
	if ( ps->quote ) {
		if ( c == '"' || c == '\n' ) {
			ps->quote = qfalse;
			RPSH_ParseToken( ps );
			return;
		}
	} else {
		if ( last == '/' && ( c == '/' || c == '*' ) ) {
			// the '/' before it was taken as part of a word: take it back
			if ( ps->tokLen > 0 ) {
				ps->tokLen--;
			}
			RPSH_ParseToken( ps );
			ps->comment = ( c == '/' ) ? 1 : 2;
			ps->last = '\0';
			return;
		}
		if ( c == '"' ) {
			RPSH_ParseToken( ps );
			ps->quote = qtrue;
			return;
		}
		if ( (unsigned char)c <= ' ' ) {
			RPSH_ParseToken( ps );
			return;
		}
		if ( c == '{' ) {
			RPSH_ParseToken( ps );
			ps->depth++;
			ps->prev[0] = '\0';
			return;
		}
		if ( c == '}' ) {
			RPSH_ParseToken( ps );
			if ( ps->depth > 0 ) {
				ps->depth--;
			}
			ps->prev[0] = '\0';
			return;
		}
	}
	if ( ps->tokLen < (int)sizeof( ps->tok ) - 1 ) {
		ps->tok[ps->tokLen++] = c;
	} else {
		ps->tokLong = qtrue;
	}
}

static void RPSH_ReadScriptFile( const char *path ) {
	static char chunk[RPSH_CHUNK];
	rpshParse_t ps;
	fileHandle_t f = 0;
	int len, pos, i;

	len = trap->FS_Open( path, &f, FS_READ );
	if ( !f ) {
		return;
	}
	memset( &ps, 0, sizeof( ps ) );
	ps.cur = -1;
	for ( pos = 0; pos < len; ) {
		int n = len - pos > RPSH_CHUNK ? RPSH_CHUNK : len - pos;

		if ( trap->FS_Read( chunk, n, f ) != n ) {
			break;
		}
		for ( i = 0; i < n; i++ ) {
			RPSH_ParseChar( &ps, chunk[i] );
		}
		pos += n;
	}
	RPSH_ParseChar( &ps, '\n' );
	trap->FS_Close( f );
}

static void RPSH_ReadScripts( void ) {
	static int order[RPSH_MAX_NAMES];
	static int sortedOfs[RPSH_MAX_NAMES];
	static byte sortedUnsafe[RPSH_MAX_NAMES];
	int numFiles, i, n;
	char *p;

	rpshScriptsRead = qtrue;
	rpshScriptsCut = qfalse;
	rpshNumNames = 0;
	rpshArenaUsed = 0;

	numFiles = trap->FS_GetFileList( "shaders", ".shader", rpshListBuf, sizeof( rpshListBuf ) );
	for ( i = 0, p = rpshListBuf; i < numFiles; i++ ) {
		int l = strlen( p );

		if ( l > 0 && l < MAX_QPATH - 9 ) {
			RPSH_ReadScriptFile( va( "shaders/%s", p ) );
		}
		p += l + 1;
		if ( p >= rpshListBuf + sizeof( rpshListBuf ) ) {
			break;
		}
	}

	// sorted, each name once: one made unfit by any of its definitions stays so
	for ( i = 0; i < rpshNumNames; i++ ) {
		order[i] = rpshNameOfs[i];
	}
	qsort( order, rpshNumNames, sizeof( order[0] ), RPSH_NameCompare );
	n = 0;
	for ( i = 0; i < rpshNumNames; i++ ) {
		// the entry this offset came from: offsets were handed out in rising order, so a binary search finds it
		int lo = 0, hi = rpshNumNames - 1, src = -1;

		while ( lo <= hi ) {
			int mid = ( lo + hi ) / 2;

			if ( rpshNameOfs[mid] == order[i] ) {
				src = mid;
				break;
			}
			if ( rpshNameOfs[mid] < order[i] ) {
				lo = mid + 1;
			} else {
				hi = mid - 1;
			}
		}
		if ( src < 0 ) {
			continue;
		}
		if ( n > 0 && !strcmp( rpshArena + sortedOfs[n - 1], rpshArena + order[i] ) ) {
			sortedUnsafe[n - 1] |= rpshNameUnsafe[src];
			continue;
		}
		sortedOfs[n] = order[i];
		sortedUnsafe[n] = rpshNameUnsafe[src];
		n++;
	}
	memcpy( rpshNameOfs, sortedOfs, n * sizeof( rpshNameOfs[0] ) );
	memcpy( rpshNameUnsafe, sortedUnsafe, n * sizeof( rpshNameUnsafe[0] ) );
	rpshNumNames = n;

	if ( rpshScriptsCut ) {
		Com_Printf( S_COLOR_YELLOW "Shader Remaps: too many shaders in the scripts, some are not listed.\n" );
	}
}

/*
===========================================================================
The map's shaders
===========================================================================
*/

static int QDECL RPSH_MapCompare( const void *a, const void *b ) {
	return Q_stricmp( (const char *)a, (const char *)b );
}

// reads forward to the file position 'to', keeping nothing; qfalse when the file ends first
static qboolean RPSH_SkipTo( fileHandle_t f, int *pos, int to ) {
	static char scratch[4096];

	while ( *pos < to ) {
		int n = to - *pos > (int)sizeof( scratch ) ? (int)sizeof( scratch ) : to - *pos;

		if ( trap->FS_Read( scratch, n, f ) != n ) {
			return qfalse;
		}
		*pos += n;
	}
	return qtrue;
}

/*
The names of the shaders the map's drawn surfaces use (flat faces, curves, meshes), from maps/<map>.bsp: its
shader records and its surface records, whichever comes first in the file. Every number is checked: a damaged
file gives no list, never a read outside a buffer.
*/
static qboolean RPSH_ReadMapFile( const char *path ) {
	static dshader_t shaders[256];
	static char names[RPSH_MAX_BSP_SHADERS][MAX_QPATH];
	static dsurface_t surfs[64];
	static byte usedIndex[RPSH_MAX_BSP_SHADERS];
	dheader_t header;
	fileHandle_t f = 0;
	int len, pos, pass, i;
	int shOfs, shLen, sfOfs, sfLen, numShaders, numSurfs;
	qboolean ok = qfalse, haveShaders = qfalse;

	rpshMapNum = 0;
	len = trap->FS_Open( path, &f, FS_READ );
	if ( !f ) {
		return qfalse;
	}
	if ( len < (int)sizeof( header ) || trap->FS_Read( &header, sizeof( header ), f ) != (int)sizeof( header ) ) {
		goto done;
	}
	pos = sizeof( header );
	if ( LittleLong( header.ident ) != BSP_IDENT || LittleLong( header.version ) != BSP_VERSION ) {
		goto done;
	}
	shOfs = LittleLong( header.lumps[LUMP_SHADERS].fileofs );
	shLen = LittleLong( header.lumps[LUMP_SHADERS].filelen );
	sfOfs = LittleLong( header.lumps[LUMP_SURFACES].fileofs );
	sfLen = LittleLong( header.lumps[LUMP_SURFACES].filelen );
	if ( shLen < 0 || shOfs < (int)sizeof( header ) || shOfs > len || shLen > len - shOfs || shLen % (int)sizeof( dshader_t ) ) {
		goto done;
	}
	if ( sfLen < 0 || sfOfs < (int)sizeof( header ) || sfOfs > len || sfLen > len - sfOfs || sfLen % (int)sizeof( dsurface_t ) ) {
		goto done;
	}
	numShaders = shLen / (int)sizeof( dshader_t );
	numSurfs = sfLen / (int)sizeof( dsurface_t );
	if ( numShaders < 1 || numShaders > RPSH_MAX_BSP_SHADERS ) {
		goto done;
	}
	// the two parts must not overlap, to be read one after the other
	if ( !( shOfs + shLen <= sfOfs || sfOfs + sfLen <= shOfs ) ) {
		goto done;
	}
	memset( usedIndex, 0, numShaders );

	// two passes, in file order: 0 the part that comes first, 1 the other
	for ( pass = 0; pass < 2; pass++ ) {
		qboolean doShaders = ( ( shOfs < sfOfs ) == ( pass == 0 ) ) ? qtrue : qfalse;

		if ( doShaders ) {
			int done = 0;

			if ( !RPSH_SkipTo( f, &pos, shOfs ) ) {
				goto done;
			}
			while ( done < numShaders ) {
				int n = numShaders - done > (int)ARRAY_LEN( shaders ) ? (int)ARRAY_LEN( shaders ) : numShaders - done;
				int bytes = n * (int)sizeof( dshader_t );

				if ( trap->FS_Read( shaders, bytes, f ) != bytes ) {
					goto done;
				}
				pos += bytes;
				for ( i = 0; i < n; i++ ) {
					memcpy( names[done + i], shaders[i].shader, MAX_QPATH );
					names[done + i][MAX_QPATH - 1] = '\0';
				}
				done += n;
			}
			haveShaders = qtrue;
		} else {
			int done = 0;

			if ( !RPSH_SkipTo( f, &pos, sfOfs ) ) {
				goto done;
			}
			while ( done < numSurfs ) {
				int n = numSurfs - done > (int)ARRAY_LEN( surfs ) ? (int)ARRAY_LEN( surfs ) : numSurfs - done;
				int bytes = n * (int)sizeof( dsurface_t );

				if ( trap->FS_Read( surfs, bytes, f ) != bytes ) {
					goto done;
				}
				pos += bytes;
				for ( i = 0; i < n; i++ ) {
					int type = LittleLong( surfs[i].surfaceType );
					int sh = LittleLong( surfs[i].shaderNum );

					if ( type != MST_PLANAR && type != MST_PATCH && type != MST_TRIANGLE_SOUP ) {
						continue;
					}
					if ( sh < 0 || sh >= numShaders ) {
						goto done;
					}
					usedIndex[sh] = 1;
				}
				done += n;
			}
		}
	}
	if ( !haveShaders ) {
		goto done;
	}

	for ( i = 0; i < numShaders; i++ ) {
		int k;
		char *name = names[i];

		if ( !usedIndex[i] || !name[0] ) {
			continue;
		}
		// printable, as a name is: anything else is not a name the menu could send
		for ( k = 0; name[k]; k++ ) {
			if ( (unsigned char)name[k] <= ' ' || (unsigned char)name[k] > 126 ) {
				break;
			}
			if ( name[k] == '\\' ) {
				name[k] = '/';
			}
		}
		if ( name[k] ) {
			continue;
		}
		if ( rpshMapNum >= RPSH_MAX_MAP ) {
			break;
		}
		Q_strncpyz( rpshMap[rpshMapNum++], name, MAX_QPATH );
	}
	ok = qtrue;

	// sorted, each name once (a map can hold a name twice, with other surface flags)
	qsort( rpshMap, rpshMapNum, sizeof( rpshMap[0] ), RPSH_MapCompare );
	{
		int n = 0;

		for ( i = 0; i < rpshMapNum; i++ ) {
			if ( n > 0 && !Q_stricmp( rpshMap[n - 1], rpshMap[i] ) ) {
				continue;
			}
			if ( n != i ) {
				memcpy( rpshMap[n], rpshMap[i], MAX_QPATH );
			}
			n++;
		}
		rpshMapNum = n;
	}

done:
	trap->FS_Close( f );
	if ( !ok ) {
		rpshMapNum = 0;
	}
	return ok;
}

static void RPSH_ReadMap( void ) {
	char info[MAX_INFO_STRING];
	const char *mapname;
	int i;

	rpshMapTried = qtrue;
	rpshMapOk = qfalse;
	rpshMapNum = 0;
	trap->GetConfigString( CS_SERVERINFO, info, sizeof( info ) );
	mapname = Info_ValueForKey( info, "mapname" );
	if ( !mapname[0] || strlen( mapname ) > MAX_QPATH - 10 ) {
		return;
	}
	rpshMapOk = RPSH_ReadMapFile( va( "maps/%s.bsp", mapname ) );

	// missing on this game: neither a script nor an image of that name
	for ( i = 0; i < rpshMapNum; i++ ) {
		rpshMapMissing[i] = ( strchr( rpshMap[i], '/' ) && RPSH_FindScript( rpshMap[i] ) < 0 && !RPSH_ImageExists( rpshMap[i] ) ) ? 1 : 0;
	}
}

static int RPSH_FindMapShader( const char *name ) {
	int i;

	for ( i = 0; i < rpshMapNum; i++ ) {
		if ( !Q_stricmp( rpshMap[i], name ) ) {
			return i;
		}
	}
	return -1;
}

/*
===========================================================================
The remaps in force
===========================================================================
*/

// "old=new:time@..." (BuildShaderStateConfig(), g_utils.c), as CG_ShaderStateChanged() reads it. An entry that
// points at itself (what /remap X X leaves) is no remap and is not kept.
static void RPSH_ParseRemaps( const char *cs ) {
	const char *p = cs;

	rpshRemapNum = 0;
	while ( *p && rpshRemapNum < RPSH_MAX_REMAPS ) {
		const char *eq = strchr( p, '=' );
		const char *colon = eq ? strchr( eq, ':' ) : NULL;
		const char *at = colon ? strchr( colon, '@' ) : NULL;
		rpshRemap_t *r = &rpshRemaps[rpshRemapNum];
		int ol, nl, tl;

		if ( !at ) {
			break;
		}
		ol = eq - p;
		nl = colon - ( eq + 1 );
		tl = at - ( colon + 1 );
		if ( ol > 0 && ol < MAX_QPATH && nl > 0 && nl < MAX_QPATH && tl >= 0 && tl < (int)sizeof( r->time ) ) {
			memcpy( r->oldName, p, ol );
			r->oldName[ol] = '\0';
			memcpy( r->newName, eq + 1, nl );
			r->newName[nl] = '\0';
			memcpy( r->time, colon + 1, tl );
			r->time[tl] = '\0';
			if ( Q_stricmp( r->oldName, r->newName ) ) {
				rpshRemapNum++;
			}
		}
		p = at + 1;
	}
}

static int RPSH_FindRemap( const char *oldName ) {
	int i;

	for ( i = 0; i < rpshRemapNum; i++ ) {
		if ( !Q_stricmp( rpshRemaps[i].oldName, oldName ) ) {
			return i;
		}
	}
	return -1;
}

/*
===========================================================================
The left list
===========================================================================
*/

static void RPSH_BuildLeft( void ) {
	int i;

	rpshLeftNum = 0;
	if ( rpshLeftMode == 1 ) {
		for ( i = 0; i < rpshRemapNum; i++ ) {
			if ( RPSH_Contains( rpshRemaps[i].oldName, rpshLeftFilter ) || RPSH_Contains( rpshRemaps[i].newName, rpshLeftFilter ) ) {
				rpshLeft[rpshLeftNum++] = i;
			}
		}
		trap->Cvar_Set( "ui_rpx_sh_leftinfo", va( "%d remap%s in force (of %d at most)", rpshRemapNum, rpshRemapNum == 1 ? "" : "s", RPSH_MAX_REMAPS ) );
	} else {
		for ( i = 0; i < rpshMapNum; i++ ) {
			if ( RPSH_Contains( rpshMap[i], rpshLeftFilter ) ) {
				rpshLeft[rpshLeftNum++] = i;
			}
		}
		if ( !rpshMapOk ) {
			trap->Cvar_Set( "ui_rpx_sh_leftinfo", "^3Could not read this map's shaders. Pick from world still works." );
		} else {
			trap->Cvar_Set( "ui_rpx_sh_leftinfo", va( "%d shaders on this map%s", rpshMapNum, rpshMapNum >= RPSH_MAX_MAP ? " (the list is full)" : "" ) );
		}
	}
	RPSH_SetCursor( FEEDER_RPX_SHMAP, -1 );
}

// the row of the left list that is the Old name, selected again (after the list was made anew)
static void RPSH_ReselectLeft( void ) {
	char oldName[MAX_QPATH];
	int i;

	RPSH_Cvar( "ui_rpx_sh_old", oldName, sizeof( oldName ) );
	if ( !oldName[0] ) {
		return;
	}
	for ( i = 0; i < rpshLeftNum; i++ ) {
		const char *name = ( rpshLeftMode == 1 ) ? rpshRemaps[rpshLeft[i]].oldName : rpshMap[rpshLeft[i]];

		if ( !Q_stricmp( name, oldName ) ) {
			RPSH_SetCursor( FEEDER_RPX_SHMAP, i );
			return;
		}
	}
}

/*
===========================================================================
The right list
===========================================================================
*/

static int QDECL RPSH_EntryCompare( const void *a, const void *b ) {
	const rpshEntry_t *ea = (const rpshEntry_t *)a, *eb = (const rpshEntry_t *)b;
	int da = ( ea->flags & RPSH_DIR ) ? 0 : 1, db = ( eb->flags & RPSH_DIR ) ? 0 : 1;

	if ( da != db ) {
		return da - db;
	}
	return strcmp( ea->name, eb->name );
}

static void RPSH_AddRaw( const char *name, int len, int flags ) {
	rpshEntry_t *e;
	int i;

	if ( len <= 0 || len >= MAX_QPATH ) {
		return;
	}
	if ( rpshRawNum >= RPSH_RAW_RIGHT ) {
		rpshRightCut = qtrue;
		return;
	}
	e = &rpshRaw[rpshRawNum];
	for ( i = 0; i < len; i++ ) {
		unsigned char c = (unsigned char)name[i];

		if ( c <= ' ' || c > 126 ) {
			return;		// not a name a remap could carry
		}
		e->name[i] = tolower( c );
	}
	e->name[len] = '\0';
	e->flags = flags;
	rpshRawNum++;
}

// the files of the folder with that ending, without it
static void RPSH_AddImages( const char *dir, const char *ext ) {
	int num = trap->FS_GetFileList( dir, ext, rpshListBuf, sizeof( rpshListBuf ) );
	int extLen = strlen( ext ), i;
	char *p = rpshListBuf;

	for ( i = 0; i < num && p < rpshListBuf + sizeof( rpshListBuf ); i++ ) {
		int l = strlen( p );

		if ( l > extLen && !Q_stricmp( p + l - extLen, ext ) && !strchr( p, '/' ) && !strchr( p, '\\' ) ) {
			RPSH_AddRaw( p, l - extLen, RPSH_IMAGE );
		}
		p += l + 1;
	}
}

static void RPSH_LoadRight( const char *dir ) {
	int dirLen = strlen( dir ), i, n;
	char *p;

	Q_strncpyz( rpshRightDir, dir, sizeof( rpshRightDir ) );
	rpshRightLoaded = qtrue;
	rpshRightCut = qfalse;
	rpshRawNum = 0;
	rpshRightNum = 0;

	if ( !dirLen ) {
		// the top: the folders shaders live in
		RPSH_AddRaw( "textures", 8, RPSH_DIR );
		RPSH_AddRaw( "models", 6, RPSH_DIR );
		RPSH_AddRaw( "gfx", 3, RPSH_DIR );
	} else {
		int num = trap->FS_GetFileList( dir, "/", rpshListBuf, sizeof( rpshListBuf ) );

		for ( i = 0, p = rpshListBuf; i < num && p < rpshListBuf + sizeof( rpshListBuf ); i++ ) {
			int l = strlen( p ), nl = l;

			while ( nl > 0 && ( p[nl - 1] == '/' || p[nl - 1] == '\\' ) ) {
				nl--;
			}
			if ( nl > 0 && p[0] != '.' && !memchr( p, '/', nl ) && !memchr( p, '\\', nl ) ) {
				RPSH_AddRaw( p, nl, RPSH_DIR );
			}
			p += l + 1;
		}
		RPSH_AddImages( dir, ".jpg" );
		RPSH_AddImages( dir, ".tga" );
		RPSH_AddImages( dir, ".png" );
	}

	// the script shaders of this folder, and the folders the deeper ones are in
	for ( i = 0; i < rpshNumNames; i++ ) {
		const char *name = rpshArena + rpshNameOfs[i];
		const char *rest, *slash;

		if ( dirLen ) {
			if ( Q_stricmpn( name, dir, dirLen ) || name[dirLen] != '/' ) {
				continue;
			}
			rest = name + dirLen + 1;
		} else {
			rest = name;
		}
		slash = strchr( rest, '/' );
		if ( slash ) {
			RPSH_AddRaw( rest, slash - rest, RPSH_DIR );
		} else if ( dirLen ) {		// at the top, only folders
			RPSH_AddRaw( rest, strlen( rest ), RPSH_SCRIPT | ( rpshNameUnsafe[i] ? RPSH_UNSAFE : 0 ) );
		}
	}

	// sorted, each name once with all it is; a script shader unfit to remap to is left out, with the image
	// of its name (the name would give the script)
	qsort( rpshRaw, rpshRawNum, sizeof( rpshRaw[0] ), RPSH_EntryCompare );
	for ( i = 0; i < rpshRawNum; i = n ) {
		int flags = rpshRaw[i].flags;

		for ( n = i + 1; n < rpshRawNum && !strcmp( rpshRaw[n].name, rpshRaw[i].name ) && ( ( rpshRaw[n].flags ^ rpshRaw[i].flags ) & RPSH_DIR ) == 0; n++ ) {
			flags |= rpshRaw[n].flags;
		}
		if ( flags & RPSH_UNSAFE ) {
			continue;
		}
		if ( dirLen + 1 + (int)strlen( rpshRaw[i].name ) >= MAX_QPATH ) {
			continue;	// too long a path to be a shader name
		}
		if ( rpshRightNum >= RPSH_MAX_RIGHT ) {
			rpshRightCut = qtrue;
			break;
		}
		rpshRight[rpshRightNum] = rpshRaw[i];
		rpshRight[rpshRightNum++].flags = flags;
	}
}

static void RPSH_BuildRight( void ) {
	int i, files = 0;

	rpshRightViewNum = 0;
	if ( rpshRightDir[0] ) {
		rpshRightView[rpshRightViewNum++] = -1;
	}
	for ( i = 0; i < rpshRightNum; i++ ) {
		if ( !( rpshRight[i].flags & RPSH_DIR ) ) {
			files++;
		}
		if ( RPSH_Contains( rpshRight[i].name, rpshRightFilter ) ) {
			rpshRightView[rpshRightViewNum++] = i;
		}
	}
	trap->Cvar_Set( "ui_rpx_sh_path", va( "Remap to:  ^7%s%s  (%d)%s", rpshRightDir[0] ? rpshRightDir : "top", rpshRightDir[0] ? "/" : "", files, rpshRightCut ? " ^3the list is full" : "" ) );
	RPSH_SetCursor( FEEDER_RPX_SHNEW, -1 );
}

static void RPSH_EnterRight( int row ) {
	char dir[MAX_QPATH];

	Q_strncpyz( dir, rpshRightDir, sizeof( dir ) );
	if ( row == -1 ) {
		char *slash = strrchr( dir, '/' );

		if ( slash ) {
			*slash = '\0';
		} else {
			dir[0] = '\0';
		}
	} else if ( row >= 0 && row < rpshRightNum && ( rpshRight[row].flags & RPSH_DIR ) ) {
		if ( strlen( dir ) + 1 + strlen( rpshRight[row].name ) >= sizeof( dir ) ) {
			return;
		}
		if ( dir[0] ) {
			Q_strcat( dir, sizeof( dir ), "/" );
		}
		Q_strcat( dir, sizeof( dir ), rpshRight[row].name );
	} else {
		return;
	}
	trap->Cvar_Set( "ui_rpx_sh_dir", dir );
	trap->Cvar_Set( "ui_rpx_filter_shnew", "" );
}

/*
GLX_U: [Shader Remaps Menu] the right list's highlight follows New: the row that is New when New is a file of the folder
shown and the filter leaves it in the list (scrolled into view if it is out of view), none otherwise (the list stays
where it is). Run when the list is made again (the menu opened -- so a file chosen before Preview is shown again --,
another folder, the filter) and when New changes (a click there, a remap selected in Current remaps, Pick from world,
typing); the folder, the filter, Old and New are never changed by it.
*/
static void RPSH_ReselectRight( void ) {
	char newName[MAX_QPATH];
	int dirLen = strlen( rpshRightDir ), i;

	RPSH_Cvar( "ui_rpx_sh_new", newName, sizeof( newName ) );
	if ( dirLen && !Q_stricmpn( newName, rpshRightDir, dirLen ) && newName[dirLen] == '/' ) {
		const char *leaf = newName + dirLen + 1;

		for ( i = 0; i < rpshRightViewNum; i++ ) {
			int row = rpshRightView[i];

			if ( row >= 0 && !( rpshRight[row].flags & RPSH_DIR ) && !Q_stricmp( rpshRight[row].name, leaf ) ) {
				RPSH_SetCursor( FEEDER_RPX_SHNEW, i );
				return;
			}
		}
	}
	RPSH_Unselect( FEEDER_RPX_SHNEW );
}

/*
===========================================================================
Old and New, the preview
===========================================================================
*/

// the line under the Old name: what it is remapped to now, or that this game does not have it
static void RPSH_UpdateOldInfo( void ) {
	char oldName[MAX_QPATH];
	int i;

	RPSH_Cvar( "ui_rpx_sh_old", oldName, sizeof( oldName ) );
	if ( !oldName[0] ) {
		trap->Cvar_Set( "ui_rpx_sh_oldinfo", "" );
		return;
	}
	i = RPSH_FindRemap( oldName );
	if ( i >= 0 ) {
		trap->Cvar_Set( "ui_rpx_sh_oldinfo", va( "Remapped to: ^7%s", rpshRemaps[i].newName ) );
		return;
	}
	i = RPSH_FindMapShader( oldName );
	trap->Cvar_Set( "ui_rpx_sh_oldinfo", ( i >= 0 && rpshMapMissing[i] ) ? "^1Missing on your game" : "" );
}

// the preview taken back: the shader remapped to itself (the engine clears a remap then), then to what the
// server says it is remapped to, if anything
static void RPSH_EndPreview( void ) {
	int i;

	if ( !rpshPreviewOld[0] ) {
		return;
	}
	trap->R_RemapShader( rpshPreviewOld, rpshPreviewOld, "0" );
	i = RPSH_FindRemap( rpshPreviewOld );
	if ( i >= 0 ) {
		trap->R_RemapShader( rpshRemaps[i].oldName, rpshRemaps[i].newName, rpshRemaps[i].time );
	}
	rpshPreviewOld[0] = '\0';
}

// both names there, fit to send, and not the same; says why not
static qboolean RPSH_NamesOk( char *oldName, char *newName, int size ) {
	RPSH_Cvar( "ui_rpx_sh_old", oldName, size );
	RPSH_Cvar( "ui_rpx_sh_new", newName, size );
	if ( !oldName[0] ) {
		RPSH_Msg( "^3Select the shader to remap in the left list first." );
		return qfalse;
	}
	if ( !newName[0] ) {
		RPSH_Msg( "^3Select or type the shader to remap it to." );
		return qfalse;
	}
	if ( !RPSH_NameOk( oldName ) || !RPSH_NameOk( newName ) ) {
		RPSH_Msg( "^3A shader name is 1 to 63 characters, with no spaces or special signs." );
		return qfalse;
	}
	if ( !Q_stricmp( oldName, newName ) ) {
		RPSH_Msg( "^3A shader can't be remapped to itself. Remove takes a remap back." );
		return qfalse;
	}
	return qtrue;
}

static void RPSH_StartPreview( void ) {
	char oldName[MAX_QPATH * 2], newName[MAX_QPATH * 2];

	if ( !RPSH_NamesOk( oldName, newName, sizeof( oldName ) ) ) {
		return;
	}
	RPSH_EndPreview();
	trap->R_RemapShader( oldName, newName, "0" );
	Q_strncpyz( rpshPreviewOld, oldName, sizeof( rpshPreviewOld ) );
	trap->Cvar_Set( "ui_rpx_sh_prevtext", va( "Preview, on your screen only:  ^7%s  ^3->  ^7%s", oldName, newName ) );
	RPSH_Msg( "" );
	Menus_CloseByName( RPSH_MENU );
	Menus_ActivateByName( RPSH_STRIP );
}

/*
===========================================================================
Once a frame
===========================================================================
*/

static void RPSH_CheckMap( void ) {
	int mapId = RPSH_CvarInt( "ui_rpx_mapid" );

	if ( mapId == rpshMapId ) {
		return;
	}
	rpshMapId = mapId;
	rpshMapTried = qfalse;
	rpshMapNum = 0;
	rpshScriptsRead = qfalse;	// files can change between maps
	rpshNumNames = 0;
	rpshRightLoaded = qfalse;
	rpshRightNum = 0;
	rpshRightViewNum = 0;
	rpshLeftNum = 0;
	rpshLeftMode = -1;
	rpshCsSeen[0] = '\1';		// not a configstring: read again
	rpshCsSeen[1] = '\0';
	rpshPreviewOld[0] = '\0';	// the renderer's shaders are new with the map
	memset( rpshThumb, 0, sizeof( rpshThumb ) );
	rpshQuad = 0;
	rpshPickSeen = RPSH_CvarInt( "ui_rpx_sh_pickn" );
	rpshOldSeen[0] = rpshNewSeen[0] = '\1';
	rpshOldSeen[1] = rpshNewSeen[1] = '\0';
}

static void RPSH_Sync( void ) {
	static char cs[MAX_STRING_CHARS * 4 + 16];
	char buf[MAX_QPATH * 2];
	qboolean leftChanged = qfalse, infoChanged = qfalse, rightChanged = qfalse;
	int mode, pickN;

	if ( rpshSyncTime == uiInfo.uiDC.realTime ) {
		return;
	}
	rpshSyncTime = uiInfo.uiDC.realTime;

	RPSH_CheckMap();

	if ( !rpshScriptsRead ) {
		RPSH_ReadScripts();
	}
	if ( !rpshMapTried ) {
		RPSH_ReadMap();
		leftChanged = infoChanged = qtrue;
	}

	// the remaps in force
	cs[0] = '\0';
	trap->GetConfigString( CS_SHADERSTATE, cs, sizeof( cs ) );
	if ( strcmp( cs, rpshCsSeen ) ) {
		Q_strncpyz( rpshCsSeen, cs, sizeof( rpshCsSeen ) );
		RPSH_ParseRemaps( cs );
		infoChanged = qtrue;
		if ( rpshLeftMode == 1 ) {
			leftChanged = qtrue;
		}
	}

	// the left list: which one, and its filter
	mode = RPSH_CvarInt( "ui_rpx_sh_view" ) ? 1 : 0;
	RPSH_Cvar( "ui_rpx_filter_shmap", buf, sizeof( rpshLeftFilter ) );
	if ( mode != rpshLeftMode || strcmp( buf, rpshLeftFilter ) ) {
		rpshLeftMode = mode;
		Q_strncpyz( rpshLeftFilter, buf, sizeof( rpshLeftFilter ) );
		leftChanged = qtrue;
	}
	if ( leftChanged ) {
		RPSH_BuildLeft();
		RPSH_ReselectLeft();
	}

	// "Pick from world": cgame found a shader
	pickN = RPSH_CvarInt( "ui_rpx_sh_pickn" );
	if ( pickN != rpshPickSeen ) {
		char pick[MAX_QPATH];

		rpshPickSeen = pickN;
		RPSH_Cvar( "ui_rpx_sh_pick", pick, sizeof( pick ) );
		if ( pick[0] ) {
			int r = RPSH_FindRemap( pick );

			trap->Cvar_Set( "ui_rpx_sh_old", pick );
			// GLX_U: [Shader Remaps Menu] remapped: New is what it is remapped to, as selecting it in Current remaps
			// gives; not remapped, New stays as the player had it (an X=X entry is not kept: no remap)
			if ( r >= 0 ) {
				trap->Cvar_Set( "ui_rpx_sh_new", rpshRemaps[r].newName );
			}
			if ( rpshLeftMode != 0 || rpshLeftFilter[0] ) {
				// shown in the map's list, unfiltered, so it can be selected there
				trap->Cvar_Set( "ui_rpx_sh_view", "0" );
				trap->Cvar_Set( "ui_rpx_filter_shmap", "" );
				rpshLeftMode = 0;
				rpshLeftFilter[0] = '\0';
				RPSH_BuildLeft();
			}
			RPSH_ReselectLeft();
			if ( RPSH_FindMapShader( pick ) < 0 ) {
				RPSH_Msg( va( "Picked %s", pick ) );
			}
		}
	}

	// the right list: its folder, its filter
	RPSH_Cvar( "ui_rpx_sh_dir", buf, MAX_QPATH );
	if ( rpshPendingEnter != -2 ) {
		int row = rpshPendingEnter;

		rpshPendingEnter = -2;
		RPSH_EnterRight( row );
		RPSH_Cvar( "ui_rpx_sh_dir", buf, MAX_QPATH );
	}
	if ( !rpshRightLoaded || strcmp( buf, rpshRightDir ) ) {
		RPSH_LoadRight( buf );
		RPSH_Cvar( "ui_rpx_filter_shnew", rpshRightFilter, sizeof( rpshRightFilter ) );
		RPSH_BuildRight();
		rightChanged = qtrue;
	} else {
		RPSH_Cvar( "ui_rpx_filter_shnew", buf, sizeof( rpshRightFilter ) );
		if ( strcmp( buf, rpshRightFilter ) ) {
			Q_strncpyz( rpshRightFilter, buf, sizeof( rpshRightFilter ) );
			RPSH_BuildRight();
			rightChanged = qtrue;
		}
	}

	// GLX_U: [Shader Remaps Menu] the right list's highlight on New (RPSH_ReselectRight())
	RPSH_Cvar( "ui_rpx_sh_new", buf, MAX_QPATH );
	if ( rightChanged || strcmp( buf, rpshNewSeen ) ) {
		Q_strncpyz( rpshNewSeen, buf, sizeof( rpshNewSeen ) );
		RPSH_ReselectRight();
	}

	// the line under Old
	RPSH_Cvar( "ui_rpx_sh_old", buf, MAX_QPATH );
	if ( strcmp( buf, rpshOldSeen ) ) {
		Q_strncpyz( rpshOldSeen, buf, sizeof( rpshOldSeen ) );
		infoChanged = qtrue;
	}
	if ( infoChanged ) {
		RPSH_UpdateOldInfo();
	}
}

/*
===========================================================================
The feeders
===========================================================================
*/

int UI_RpShLeftCount( void ) {
	RPSH_Sync();
	return rpshLeftNum;
}

const char *UI_RpShLeftText( int index ) {
	if ( index < 0 || index >= rpshLeftNum ) {
		return "";
	}
	if ( rpshLeftMode == 1 ) {
		const rpshRemap_t *r = &rpshRemaps[rpshLeft[index]];

		return va( "%s ^3-> ^7%s", r->oldName, r->newName );
	}
	return va( "%s%s", rpshMapMissing[rpshLeft[index]] ? "^1" : "", rpshMap[rpshLeft[index]] );
}

qboolean UI_RpShLeftSelection( int index ) {
	if ( index < 0 || index >= rpshLeftNum ) {
		return qtrue;
	}
	if ( rpshLeftMode == 1 ) {
		const rpshRemap_t *r = &rpshRemaps[rpshLeft[index]];

		trap->Cvar_Set( "ui_rpx_sh_old", r->oldName );
		trap->Cvar_Set( "ui_rpx_sh_new", r->newName );
	} else {
		trap->Cvar_Set( "ui_rpx_sh_old", rpshMap[rpshLeft[index]] );
	}
	RPSH_Msg( "" );
	return qtrue;
}

int UI_RpShRightCount( void ) {
	RPSH_Sync();
	return rpshRightViewNum;
}

const char *UI_RpShRightText( int index ) {
	const rpshEntry_t *e;

	if ( index < 0 || index >= rpshRightViewNum ) {
		return "";
	}
	if ( rpshRightView[index] == -1 ) {
		return "^5..  (up one folder)";
	}
	e = &rpshRight[rpshRightView[index]];
	if ( e->flags & RPSH_DIR ) {
		return va( "^5%s", e->name );
	}
	return va( "%s%s", ( e->flags & RPSH_SCRIPT ) ? "^3" : "", e->name );
}

qboolean UI_RpShRightSelection( int index ) {
	const rpshEntry_t *e;

	if ( index < 0 || index >= rpshRightViewNum || rpshRightView[index] < 0 ) {
		return qtrue;
	}
	e = &rpshRight[rpshRightView[index]];
	if ( !( e->flags & RPSH_DIR ) ) {
		trap->Cvar_Set( "ui_rpx_sh_new", va( "%s/%s", rpshRightDir, e->name ) );
		RPSH_Msg( "" );
	}
	return qtrue;
}

/*
===========================================================================
Scripts
===========================================================================

	rpxShOpen			the menu's onOpen
	rpxShEnter			a double click in the right list: a folder is opened
	rpxShView <0|1>		the left list's tabs: the map's shaders, or the remaps in force
	rpxShPick			Pick from world
	rpxShPreview		Preview: the remap on this game only, the strip in the menu's place
	rpxShPreviewEnd		the strip's onClose: the preview taken back
	rpxShApply			Apply: /remap
	rpxShRemove			Remove: /remapremove of the selected remap
	rpxShReset			Reset all: /remapreset, pressed twice
*/
qboolean UI_RpShScript( const char *name, char **args ) {
	const char *arg = NULL;
	char oldName[MAX_QPATH * 2], newName[MAX_QPATH * 2];

	if ( Q_stricmpn( name, "rpxSh", 5 ) ) {
		return qfalse;
	}

	if ( !Q_stricmp( name, "rpxShOpen" ) ) {
		rpshSyncTime = -1;
		RPSH_CheckMap();
		RPSH_EndPreview();		// should the strip have gone without its onClose
		rpshResetAsked = 0;
		RPSH_Msg( "" );
		// the lists as they were: made again at the next sync, with the selection
		rpshLeftMode = -1;
		rpshRightLoaded = qfalse;
	} else if ( !Q_stricmp( name, "rpxShEnter" ) ) {
		itemDef_t *item = RPSH_List( FEEDER_RPX_SHNEW );

		// as rpxEnter (ui_rpextras.c): a double click on one row, not a click on one and then another
		if ( item && item->typeData.listbox && item->typeData.listbox->cursorPos == item->cursorPos
			&& item->cursorPos >= 0 && item->cursorPos < rpshRightViewNum ) {
			int row = rpshRightView[item->cursorPos];

			if ( row == -1 || ( rpshRight[row].flags & RPSH_DIR ) ) {
				rpshPendingEnter = row;
			}
		}
	} else if ( !Q_stricmp( name, "rpxShView" ) ) {
		// the two tabs; the one already open does nothing, so a filter typed there stays
		if ( String_Parse( args, &arg ) && ( atoi( arg ) ? 1 : 0 ) != ( RPSH_CvarInt( "ui_rpx_sh_view" ) ? 1 : 0 ) ) {
			trap->Cvar_Set( "ui_rpx_sh_view", atoi( arg ) ? "1" : "0" );
			trap->Cvar_Set( "ui_rpx_filter_shmap", "" );
			rpshResetAsked = 0;
			RPSH_Msg( "" );
		}
	} else if ( !Q_stricmp( name, "rpxShPick" ) ) {
		trap->Cmd_ExecuteText( EXEC_APPEND, "rpx do pickshader\n" );
	} else if ( !Q_stricmp( name, "rpxShPreview" ) ) {
		RPSH_StartPreview();
	} else if ( !Q_stricmp( name, "rpxShPreviewEnd" ) ) {
		RPSH_EndPreview();
	} else if ( !Q_stricmp( name, "rpxShApply" ) ) {
		if ( RPSH_NamesOk( oldName, newName, sizeof( oldName ) ) ) {
			trap->Cmd_ExecuteText( EXEC_APPEND, "rpx do remap\n" );
		}
	} else if ( !Q_stricmp( name, "rpxShRemove" ) ) {
		RPSH_Cvar( "ui_rpx_sh_old", oldName, sizeof( oldName ) );
		if ( !oldName[0] || RPSH_FindRemap( oldName ) < 0 ) {
			RPSH_Msg( "^3Select a remap in the Current remaps list first." );
		} else {
			trap->Cmd_ExecuteText( EXEC_APPEND, "rpx do remapremove\n" );
		}
	} else if ( !Q_stricmp( name, "rpxShReset" ) ) {
		if ( !rpshRemapNum ) {
			RPSH_Msg( "There are no shader remaps to clear." );
		} else if ( rpshResetAsked && uiInfo.uiDC.realTime - rpshResetAsked < RPSH_RESET_CONFIRM ) {
			rpshResetAsked = 0;
			trap->Cmd_ExecuteText( EXEC_APPEND, "rpx do remapreset\n" );
		} else {
			rpshResetAsked = uiInfo.uiDC.realTime ? uiInfo.uiDC.realTime : 1;
			RPSH_Msg( va( "^3Press Reset all again to clear all %d remaps.", rpshRemapNum ) );
		}
	}
	return qtrue;
}

/*
===========================================================================
The thumbnails
===========================================================================
*/

static void RPSH_ThumbText( rectDef_t *rect, float scale, vec4_t color, int iMenuFont, const char *line1, const char *line2 ) {
	float s = scale * 0.7f;

	Text_Paint( rect->x + ( rect->w - Text_Width( line1, s, iMenuFont ) ) / 2, rect->y + rect->h / 2 - 14, s, color, line1, 0, 0, ITEM_TEXTSTYLE_NORMAL, iMenuFont );
	if ( line2 ) {
		Text_Paint( rect->x + ( rect->w - Text_Width( line2, s, iMenuFont ) ) / 2, rect->y + rect->h / 2 - 2, s, color, line2, 0, 0, ITEM_TEXTSTYLE_NORMAL, iMenuFont );
	}
}

static void RPSH_DrawThumb( int slot, rectDef_t *rect, float scale, vec4_t color, int iMenuFont ) {
	char name[MAX_QPATH * 2];
	refdef_t refdef;
	refEntity_t ent;
	int now = uiInfo.uiDC.realTime;

	RPSH_Sync();
	RPSH_Cvar( slot ? "ui_rpx_sh_new" : "ui_rpx_sh_old", name, sizeof( name ) );
	if ( strlen( name ) >= MAX_QPATH ) {
		name[0] = '\0';
	}
	if ( strcmp( name, rpshThumb[slot].name ) ) {
		Q_strncpyz( rpshThumb[slot].name, name, sizeof( rpshThumb[slot].name ) );
		rpshThumb[slot].state = name[0] ? RPSH_TH_PENDING : RPSH_TH_NONE;
		rpshThumb[slot].changedAt = now;
		rpshThumb[slot].handle = 0;
	}

	if ( rpshThumb[slot].state == RPSH_TH_PENDING && now - rpshThumb[slot].changedAt >= RPSH_THUMB_SETTLE ) {
		if ( RPSH_NameOk( name ) && strchr( name, '/' ) && RPSH_ImageExists( name ) ) {
			trap->Cvar_Set( slot ? "ui_rpx_sh_thumb1" : "ui_rpx_sh_thumb0", "" );
			trap->Cmd_ExecuteText( EXEC_APPEND, va( "rpx shthumb %d \"%s\"\n", slot, name ) );
			rpshThumb[slot].state = RPSH_TH_ASKED;
		} else {
			rpshThumb[slot].state = RPSH_TH_NOIMAGE;
		}
	}
	if ( rpshThumb[slot].state == RPSH_TH_ASKED ) {
		char answer[MAX_QPATH * 2];
		char *space;

		// cgame's answer: "<handle> <name>"
		RPSH_Cvar( slot ? "ui_rpx_sh_thumb1" : "ui_rpx_sh_thumb0", answer, sizeof( answer ) );
		space = strchr( answer, ' ' );
		if ( space && !strcmp( space + 1, name ) ) {
			int handle = atoi( answer );

			rpshThumb[slot].handle = handle > 0 ? handle : 0;
			rpshThumb[slot].state = handle > 0 ? RPSH_TH_READY : ( handle < 0 ? RPSH_TH_FULL : RPSH_TH_NOIMAGE );
		}
	}

	switch ( rpshThumb[slot].state ) {
	case RPSH_TH_NOIMAGE:
		RPSH_ThumbText( rect, scale, color, iMenuFont, "No thumbnail:", "use Preview" );
		return;
	case RPSH_TH_FULL:
		RPSH_ThumbText( rect, scale, color, iMenuFont, "No more", "on this map" );
		return;
	case RPSH_TH_READY:
		break;
	default:
		return;
	}

	if ( !rpshQuad ) {
		rpshQuad = trap->R_RegisterModel( RPSH_QUAD_MODEL );
		if ( !rpshQuad ) {
			rpshThumb[slot].state = RPSH_TH_NOIMAGE;
			return;
		}
	}

	// the flat model, 64 units square, filling the box: 64 units away, seen under 2 * atan( 32 / 64 )
	memset( &refdef, 0, sizeof( refdef ) );
	refdef.rdflags = RDF_NOWORLDMODEL;
	AxisClear( refdef.viewaxis );
	refdef.x = ( rect->x + 1 ) * uiInfo.uiDC.xscale;
	refdef.y = ( rect->y + 1 ) * uiInfo.uiDC.yscale;
	refdef.width = ( rect->w - 2 ) * uiInfo.uiDC.xscale;
	refdef.height = ( rect->h - 2 ) * uiInfo.uiDC.yscale;
	refdef.fov_x = refdef.fov_y = 53.13f;
	refdef.time = now;

	memset( &ent, 0, sizeof( ent ) );
	ent.reType = RT_MODEL;
	ent.hModel = rpshQuad;
	ent.customShader = rpshThumb[slot].handle;
	VectorSet( ent.origin, 64, 0, 0 );
	VectorCopy( ent.origin, ent.oldorigin );
	VectorCopy( ent.origin, ent.lightingOrigin );
	AxisClear( ent.axis );
	ent.renderfx = RF_NOSHADOW | RF_LIGHTING_ORIGIN;
	ent.shaderRGBA[0] = ent.shaderRGBA[1] = ent.shaderRGBA[2] = ent.shaderRGBA[3] = 255;

	trap->R_ClearScene();
	trap->R_AddRefEntityToScene( &ent );
	trap->R_RenderScene( &refdef );
}

qboolean UI_RpShOwnerDraw( int ownerDraw, rectDef_t *rect, float scale, vec4_t color, int iMenuFont ) {
	switch ( ownerDraw ) {
	case UI_RPX_SHOLD:
		RPSH_DrawThumb( 0, rect, scale, color, iMenuFont );
		return qtrue;
	case UI_RPX_SHNEW:
		RPSH_DrawThumb( 1, rect, scale, color, iMenuFont );
		return qtrue;
	default:
		return qfalse;
	}
}
