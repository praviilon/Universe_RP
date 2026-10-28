/*
===========================================================================
GalaxyRP: [Extras] what the Extras menus of the Galaxy RP menu share between the ui module (ui_main.c:
the lists, the prop preview, the menus' scripts) and cgame (cg_rpextras.c: asking the server for the
listings, the commands the menus send, the effect preview).

How the two work together:

- The menus keep their state in the ui_rpx_* cvars below: the folder and the selection of each list,
  the options of each menu. cgame puts them all back to these defaults when it starts (every map),
  so a menu remembers its folder, selection and options until the map changes.
- The ui asks cgame for a listing with "rpx list <kind>" (a cgame console command); cgame asks the
  server (/rpxlist, g_rplist.c), writes what it gets to one of rpxListFiles (named in ui_rpx_file)
  and raises ui_rpx_serial, and the ui reads the file then. ui_rpx_status says what is going on
  meanwhile. Several names, because the engine writes to one folder and reads from the first folder
  on its search path that has the file: a TaystJK client started without the launch script writes
  to taystjk/ but searches the mod folder first (GLX_U: the in-module write-folder switch is gone,
  see cg_main.c), so an old copy left there by a launcher session would hide the new one. cgame
  reads each file back after writing it and moves on to the next name when what it reads is not
  what it wrote (the serial in its first line).
- The action buttons run "rpx do <action>": cgame builds the server command from the cvars and
  sends it itself (never through the console, so no name can add a command of its own), after
  checking the selected name is one the server listed.
- The effect preview: cgame plays the effect as a sky-portal effect at RPX_FX_ORIGIN, far outside
  any map, and adds it to the scene after everything else it draws; the ui's preview box renders
  that scene without clearing it first. See CG_RpxFxPass() in cg_rpextras.c for what it shows and
  what it does not.
===========================================================================
*/

#ifndef RP_EXTRAS_H
#define RP_EXTRAS_H

// .dat: readable as a loose file even on a pure server. The first line is
// RPX_LIST_MAGIC <serial> <kind> <folder> <count> <cut short 0/1>, tab-separated; then a name a line.
static const char * const rpxListFiles[] = { "rpx/list.dat", "rpx/list1.dat", "rpx/list2.dat", "rpx/list3.dat" };
#define RPX_NUM_LIST_FILES	( (int)( sizeof( rpxListFiles ) / sizeof( rpxListFiles[0] ) ) )
#define RPX_LIST_MAGIC		"RPX1"
#define RPX_MAX_ENTRIES		4096
#define RPX_NAME_LEN		64				// MAX_QPATH: nothing longer could be loaded anyway

typedef enum {
	RPX_MODELS,
	RPX_EFFECTS,
	RPX_NPCS,
	RPX_VEHICLES,
	RPX_MUSIC,
	RPX_SOUNDS,
	RPX_GLMS,		// GLX_U: [Extras] the GLM Props menu's .glm models
	RPX_NUM_KINDS
} rpxKindNum_t;

// the kind names are also the /rpxlist words; root and default folder as in g_rplist.c's sources
static const struct {
	const char *name;
	const char *root;			// NULL: a list of types, no folders
	const char *defaultFolder;
	const char *ext;			// added to a file name to make its path; "" when it keeps its own
} rpxKinds[RPX_NUM_KINDS] = {
	{ "models",		"models",	"map_objects",	".md3" },
	{ "effects",	"effects",	"",				"" },
	{ "npcs",		NULL,		"",				"" },
	{ "vehicles",	NULL,		"",				"" },
	{ "music",		"music",	"",				"" },
	{ "sounds",		"sound",	"",				"" },
	{ "glms",		"models",	"players",		".glm" },
};

// the /playsound channels, in the order the Sounds menu lists them (Cmd_ZykSound_f in g_cmds.c)
static const char * const rpxSoundChannels[] = {
	"auto", "local", "weapon", "voice", "voice_attenuate", "item", "body", "ambient",
	"local_sound", "announcer", "less_attenuate", "menu1", "voice_global", "music"
};
#define RPX_NUM_SOUND_CHANNELS	( (int)( sizeof( rpxSoundChannels ) / sizeof( rpxSoundChannels[0] ) ) )

// the menus' cvars and their defaults. Folders and selections per list; the filter per menu.
#define RPX_CVAR_LIST( X ) \
	X( "ui_rpx_dir_models",		"map_objects" ) \
	X( "ui_rpx_dir_effects",	"" ) \
	X( "ui_rpx_dir_music",		"" ) \
	X( "ui_rpx_dir_sounds",		"" ) \
	X( "ui_rpx_sel_models",		"" ) \
	X( "ui_rpx_sel_effects",	"" ) \
	X( "ui_rpx_sel_npcs",		"" ) \
	X( "ui_rpx_sel_vehicles",	"" ) \
	X( "ui_rpx_sel_music",		"" ) \
	X( "ui_rpx_sel_sounds",		"" ) \
	X( "ui_rpx_filter_models",	"" ) \
	X( "ui_rpx_filter_effects",	"" ) \
	X( "ui_rpx_filter_npcs",	"" ) \
	X( "ui_rpx_filter_music",	"" ) \
	X( "ui_rpx_filter_sounds",	"" ) \
	X( "ui_rpx_dir_glms",		"players" )	/* GLX_U: GLM Props: its folder, selection and filter */ \
	X( "ui_rpx_sel_glms",		"" ) \
	X( "ui_rpx_filter_glms",	"" ) \
	X( "ui_rpx_npcmode",		"0" )		/* 0 NPCs, 1 vehicles */ \
	X( "ui_rpx_p_yaw",			"0" ) \
	X( "ui_rpx_p_pitch",		"0" ) \
	X( "ui_rpx_p_roll",			"0" ) \
	X( "ui_rpx_p_zoom",			"100" )		/* percent */ \
	X( "ui_rpx_p_scale",		"100" )		/* percent: 1 to 1023 (modelscale 0.01 to 10.23) */ \
	X( "ui_rpx_p_solid",		"1" ) \
	X( "ui_rpx_p_anim",			"0" ) \
	X( "ui_rpx_p_break",		"0" )		/* RPX_BREAK_*: 0 never, 1 by damage too, 2 only when used */ \
	X( "ui_rpx_p_health",		"100" ) \
	X( "ui_rpx_p_light",		"0" ) \
	X( "ui_rpx_p_lrad",			"200" ) \
	X( "ui_rpx_p_lr",			"255" )		/* the light's colour, 0 to 255 like the saber colour sliders */ \
	X( "ui_rpx_p_lg",			"255" ) \
	X( "ui_rpx_p_lb",			"255" ) \
	X( "ui_rpx_p_faceme",		"1" ) \
	X( "ui_rpx_p_lift",			"0" )		/* Props: units out from the surface aimed at (/entaddaim aimoffset) */ \
	X( "ui_rpx_p_sit",			"0" )		/* GLX_U: non-solid only: raised so the model stands on the surface aimed at */ \
	X( "ui_rpx_g_yaw",			"0" )		/* GLX_U: GLM Props: its own, as the ui_rpx_p_* of MD3 Props */ \
	X( "ui_rpx_g_pitch",		"0" ) \
	X( "ui_rpx_g_roll",			"0" ) \
	X( "ui_rpx_g_zoom",			"100" ) \
	X( "ui_rpx_g_scale",		"100" ) \
	X( "ui_rpx_g_solid",		"0" ) \
	X( "ui_rpx_g_light",		"0" ) \
	X( "ui_rpx_g_lrad",			"200" ) \
	X( "ui_rpx_g_lr",			"255" ) \
	X( "ui_rpx_g_lg",			"255" ) \
	X( "ui_rpx_g_lb",			"255" ) \
	X( "ui_rpx_g_faceme",		"1" ) \
	X( "ui_rpx_g_lift",			"0" ) \
	X( "ui_rpx_g_tab",			"0" )		/* GLX_U: GLM Props' tab: 0 Prop, 1 Animation */ \
	X( "ui_rpx_g_anim",			"0" )		/* plays frames ui_rpx_g_start to ui_rpx_g_end (startframe/endframe) */ \
	X( "ui_rpx_g_start",		"0" ) \
	X( "ui_rpx_g_end",			"0" ) \
	X( "ui_rpx_g_loop",			"0" )		/* spawnflags 2, LOOP */ \
	X( "ui_rpx_filter_anims",	"" )		/* the Animation tab's list of named animations */ \
	X( "ui_rpx_g_box",			"1" )		/* GLX_U: 1 its box fits the model as drawn, 0 the default 32 */ \
	X( "ui_rpx_g_sit",			"1" )		/* raised so its bottom sits on the surface aimed at */ \
	X( "ui_rpx_g_skin",			"" )		/* model_<skin>.skin of its folder; "" its default */ \
	X( "ui_rpx_e_dir",			"0" )		/* 0 up, 1 facing me, 2 facing me, tilted by ui_rpx_e_pitch */ \
	X( "ui_rpx_e_pitch",		"0" ) \
	X( "ui_rpx_e_delay",		"200" ) \
	X( "ui_rpx_e_random",		"0" ) \
	X( "ui_rpx_e_off",			"0" ) \
	X( "ui_rpx_e_once",			"0" ) \
	X( "ui_rpx_e_lift",			"0" )		/* Effects: units out from the surface aimed at (/entaddaim aimoffset) */ \
	X( "ui_rpx_e_cdist",		"160" ) \
	X( "ui_rpx_e_cang",			"0" ) \
	X( "ui_rpx_n_name",			"" ) \
	X( "ui_rpx_l_rad",			"300" )		/* Lights: the radius, on */ \
	X( "ui_rpx_l_r",			"255" )		/* the colour on, 0 to 255 */ \
	X( "ui_rpx_l_g",			"255" ) \
	X( "ui_rpx_l_b",			"255" ) \
	X( "ui_rpx_l_off",			"0" )		/* starts switched off */ \
	X( "ui_rpx_l_offrad",		"0" )		/* the radius when off: 0 is dark */ \
	X( "ui_rpx_l_offr",			"255" )		/* the colour when off */ \
	X( "ui_rpx_l_offg",			"255" ) \
	X( "ui_rpx_l_offb",			"255" ) \
	X( "ui_rpx_l_name",			"" )		/* its targetname, to switch it with */ \
	X( "ui_rpx_l_lift",			"32" )		/* units out from the surface aimed at (/entaddaim aimoffset) */ \
	X( "ui_rpx_sp_name",		"" )		/* Spawners: the spawner's targetname, required */ \
	X( "ui_rpx_sp_now",			"1" )		/* spawnnow: one at once as well, not counted */ \
	X( "ui_rpx_sp_count",		"1" )		/* how many times it can be fired, 1 to 999 */ \
	X( "ui_rpx_sp_unlim",		"0" )		/* no limit: count -1 */ \
	X( "ui_rpx_sp_delay",		"0" )		/* seconds after being fired, 0 to 3600 */ \
	X( "ui_rpx_sp_shy",			"0" )		/* spawnflags 2048 */ \
	X( "ui_rpx_sp_respawn",		"0" )		/* respawn: fired again when one it made dies */ \
	X( "ui_rpx_sp_face",		"0" )		/* 0 towards me, 1 the way I look */ \
	X( "ui_rpx_sp_npcname",		"" )		/* NPC_targetname: the NPC's or vehicle's own name */ \
	X( "ui_rpx_sp_health",		"0" )		/* NPCs: 0 is the type's own */ \
	X( "ui_rpx_sp_hbar",		"0" )		/* NPCs: showhealth */ \
	X( "ui_rpx_sp_noai",		"0" )		/* NPCs: spawnflags 32 (cinematic) */ \
	X( "ui_rpx_sp_frozen",		"0" )		/* NPCs: npcfreeze, spawned frozen as /npc freeze leaves them */ \
	X( "ui_rpx_sp_solid",		"1" )		/* NPCs: 0 is spawnflags 64 (not solid) */ \
	X( "ui_rpx_sp_effect",		"0" )		/* NPCs: npceffect, 1 holo, 2 ghost, 3 walkthrough */ \
	X( "ui_rpx_sp_team",		"0" )		/* both: npcteam, 1 player, 2 enemy, 3 neutral, 4 free; 0 its type's own */ \
	X( "ui_rpx_sp_scale",		"0" )		/* both: npcscale, the size in percent, 20 to 500; 0 its type's own */ \
	X( "ui_rpx_sp_hat",			"0" )		/* NPCs: npchat, an id in the shared list (BG_RpCosmeticName()), 0 none */ \
	X( "ui_rpx_sp_cape",		"0" )		/* NPCs: npccape, likewise */ \
	X( "ui_rpx_sp_credits",		"0" )		/* NPCs: npccredits, paid to the logged-in player who kills one, 0 to 100000 */ \
	X( "ui_rpx_sp_xp",			"0" )		/* NPCs: npcxp, likewise, 0 to 100 */ \
	X( "ui_rpx_sp_ondeath",		"" )		/* NPCs: NPC_target, fired when it dies */ \
	X( "ui_rpx_sp_vdie",		"0" )		/* vehicles: spawnflags 1, explodes once left by its rider */ \
	X( "ui_rpx_sp_vtime",		"10" )		/* vehicles: seconds the rider may stay away (dmg, in ms) */ \
	X( "ui_rpx_sp_vdist",		"512" )		/* vehicles: how far the rider may go first (speed) */ \
	X( "ui_rpx_sp_vdock",		"0" )		/* vehicles: spawnflags 2, fighters hang until boarded */ \
	X( "ui_rpx_sp_lift",		"0" )		/* units out from the surface aimed at (/entaddaim aimoffset) */ \
	X( "ui_rpx_s_chan",			"0" ) \
	X( "ui_rpx_status",			"" )		/* cgame: loading, or why a listing failed */ \
	X( "ui_rpx_serial",			"0" )		/* cgame: raised each time it writes the list file */ \
	X( "ui_rpx_file",			"rpx/list.dat" )	/* cgame: which of rpxListFiles it wrote */ \
	X( "ui_rpx_msg",			"" )		/* either: the result of the last button */ \
	X( "ui_rpx_info",			"" )		/* ui: the folder and how many entries */ \
	X( "ui_rpx_selname",		"" )		/* ui: the selected name in full, which the list may cut short */ \
	X( "ui_rpx_fxbox",			"0" )		/* ui: when it last drew the effect preview box (Milliseconds) */ \
	X( "ui_rpx_back",			"0" )		/* ui: the Back button asks the Galaxy RP menu for its Extras tab */ \
	X( "ui_rpx_sent",			"0" )		/* ui: when a menu last sent the server a command (Milliseconds) */ \
	X( "ui_rpx_fxtime",			"0" )		/* cgame: its time, for the effect preview's render */ \
	X( "ui_rpx_mapid",			"0" )		/* cgame: new at each cgame start; lists and prop previews from before are old */ \
	X( "ui_rpx_viewyaw",		"0" )		/* cgame: the player's view yaw (0-359) while a menu is open, for the prop preview */

// GLX_U: [Extras] MD3 Props' Breakable (ui_rpx_p_break), as the menu's list sets it (ingame_rpx_props.menu):
// never (spawnflag 64, USE_NOT_BREAK), by damage too (its Health), or only when used, as vanilla's
// misc_model_breakable without health -- a trigger or /enttrigger breaks it
#define RPX_BREAK_NO		0
#define RPX_BREAK_YES		1
#define RPX_BREAK_USED		2

// the effect preview: where the effect plays (far outside any map, so no map geometry or sky effect
// is near it), how long after a Preview click it is drawn, and the camera
#define RPX_FX_ORIGIN_X		0.0f
#define RPX_FX_ORIGIN_Y		0.0f
#define RPX_FX_ORIGIN_Z		60000.0f
#define RPX_FX_WINDOW		15000
#define RPX_FX_HEARTBEAT	300		// the box counts as on screen this long after the ui last drew it
#define RPX_FX_FOV			60.0f
#define RPX_FX_ELEVATION	20.0f
#define RPX_FX_SETTLE		250		// Direction or Tilt changed while the preview shows: replayed once they rest this long

// the most different models and effects one map's previews may load (each keeps a slot until then)
#define RPX_PREVIEW_MODELS	150
#define RPX_PREVIEW_EFFECTS	32
#define RPX_PREVIEW_GLMS	50		// GLX_U: each takes a model slot, and its .gla one too

// GLX_U: [Extras] a .glm model's shape as the game draws it, for the GLM Props preview and the spawned
// prop's radius, box and lift (RPX_GlmShape() below). Both files are read forward only, a piece at a time
// (the ui has no FS_Seek).
#define RPX_GLM_PIECE		16384
#define RPX_GLM_MAX_SURFS	1024
#define RPX_GLM_SURF_VERTS	4096		// in one surface (the renderer draws at most 1000 at a time)
#define RPX_GLM_MAX_COORD	65536.0f
#define RPX_GLM_MAX_BONES	256
#define RPX_GLM_MAX_REFS	32			// bone references of one surface: a vertex names them in 5 bits
#define RPX_GLM_MAX_FRAMES	16			// frames of an animation measured: its start, its end, 14 between

typedef struct {
	fileHandle_t	f;
	int				len;		// the file's length
	int				pos;		// the file offset of buf[0]
	int				have;		// bytes in buf
	byte			buf[RPX_GLM_PIECE];
} rpxGlmReader_t;

// n bytes at file offset ofs, or NULL: past the end, or behind what has been read
static QINLINE const byte *RPX_GlmAt( rpxGlmReader_t *r, int ofs, int n ) {
	if ( ofs < r->pos || n <= 0 || n > RPX_GLM_PIECE || ofs > r->len - n ) {
		return NULL;
	}
	while ( ofs + n > r->pos + r->have ) {
		int get;

		if ( ofs >= r->pos + r->have ) {
			r->pos += r->have;		// nothing read so far is wanted
			r->have = 0;
		} else if ( ofs > r->pos ) {
			int keep = r->pos + r->have - ofs;

			memmove( r->buf, r->buf + ( ofs - r->pos ), keep );
			r->pos = ofs;
			r->have = keep;
		}
		get = RPX_GLM_PIECE - r->have;
		if ( get > r->len - ( r->pos + r->have ) ) {
			get = r->len - ( r->pos + r->have );
		}
		if ( get <= 0 ) {
			return NULL;
		}
		get = trap->FS_Read( r->buf + r->have, get, r->f );
		if ( get <= 0 ) {
			return NULL;
		}
		r->have += get;
	}
	return r->buf + ( ofs - r->pos );
}

static QINLINE int RPX_GlmInt( const byte *p ) {
	int v;

	memcpy( &v, p, 4 );
	return LittleLong( v );
}

static QINLINE float RPX_GlmFloat( const byte *p ) {
	float v;

	memcpy( &v, p, 4 );
	return LittleFloat( v );
}

static QINLINE unsigned short RPX_GlmShort( const byte *p ) {
	return (unsigned short)( p[0] | ( p[1] << 8 ) );
}

static rpxGlmReader_t rpxGlmReader;	// one file at a time; not on the stack: 16 KB

static QINLINE qboolean RPX_GlmOpen( const char *path ) {
	rpxGlmReader.f = 0;
	rpxGlmReader.pos = rpxGlmReader.have = 0;
	rpxGlmReader.len = trap->FS_Open( path, &rpxGlmReader.f, FS_READ );
	if ( !rpxGlmReader.f ) {
		return qfalse;
	}
	if ( rpxGlmReader.len <= 0 ) {
		trap->FS_Close( rpxGlmReader.f );
		return qfalse;
	}
	return qtrue;
}

// a .glm's header: the animation file it names (without .gla; '\' read as '/', no leading '/'; "*default"
// is the game's own one-bone skeleton), where its first LOD is and how many surfaces it has
static QINLINE qboolean RPX_GlmHeader( char *gla, int glaSize, int *lod, int *numSurfs ) {
	static const int ident = ( 'M' << 24 ) + ( 'G' << 16 ) + ( 'L' << 8 ) + '2';	// MDXM_IDENT
	const byte *p = RPX_GlmAt( &rpxGlmReader, 0, 164 );	// mdxmHeader_t
	int n = 0, at = 72;

	if ( !p || RPX_GlmInt( p ) != ident || RPX_GlmInt( p + 4 ) != 6 || RPX_GlmInt( p + 144 ) < 1 ) {
		return qfalse;
	}
	while ( at < 136 && ( p[at] == '/' || p[at] == '\\' ) ) {	// animName, 64 characters
		at++;
	}
	for ( ; at < 136 && p[at] && n < glaSize - 1; at++ ) {
		gla[n++] = ( p[at] == '\\' ) ? '/' : (char)p[at];
	}
	gla[n] = '\0';
	*lod = RPX_GlmInt( p + 148 );
	*numSurfs = RPX_GlmInt( p + 152 );
	return ( *lod >= 164 && *numSurfs >= 1 && *numSurfs <= RPX_GLM_MAX_SURFS ) ? qtrue : qfalse;
}

// the drawn bones of some frames of an animation file: each bone's matrix in the frame times its parent's,
// which is what the renderer skins a vertex with (tr_ghoul2.cpp, G2_TransformBone(); the root matrix is
// none for these models). Returns the file's frame count, 0 when it does not read as one.
static float rpxGlmBones[RPX_GLM_MAX_FRAMES][RPX_GLM_MAX_BONES][3][4];
static int rpxGlmNumBones;

typedef struct {
	int		index;			// into the compressed bone pool
	short	frame, bone;	// whose
} rpxGlmPoolUse_t;

static int QDECL RPX_GlmPoolCompare( const void *a, const void *b ) {
	return ( (const rpxGlmPoolUse_t *)a )->index - ( (const rpxGlmPoolUse_t *)b )->index;
}

static int QDECL RPX_GlmIntCompare( const void *a, const void *b ) {
	return *(const int *)a - *(const int *)b;
}

// a compressed bone: quaternion and position (MC_UnCompressQuat(), matcomp.cpp)
static QINLINE void RPX_GlmUnCompress( const byte *comp, float m[3][4] ) {
	float w = RPX_GlmShort( comp ) / 16383.0f - 2.0f, x = RPX_GlmShort( comp + 2 ) / 16383.0f - 2.0f;
	float y = RPX_GlmShort( comp + 4 ) / 16383.0f - 2.0f, z = RPX_GlmShort( comp + 6 ) / 16383.0f - 2.0f;
	float tx = 2.0f * x, ty = 2.0f * y, tz = 2.0f * z;
	float twx = tx * w, twy = ty * w, twz = tz * w, txx = tx * x, txy = ty * x, txz = tz * x;
	float tyy = ty * y, tyz = tz * y, tzz = tz * z;

	m[0][0] = 1.0f - ( tyy + tzz );	m[0][1] = txy - twz;			m[0][2] = txz + twy;
	m[1][0] = txy + twz;			m[1][1] = 1.0f - ( txx + tzz );	m[1][2] = tyz - twx;
	m[2][0] = txz - twy;			m[2][1] = tyz + twx;			m[2][2] = 1.0f - ( txx + tyy );
	m[0][3] = RPX_GlmShort( comp + 8 ) / 64.0f - 512.0f;
	m[1][3] = RPX_GlmShort( comp + 10 ) / 64.0f - 512.0f;
	m[2][3] = RPX_GlmShort( comp + 12 ) / 64.0f - 512.0f;
}

static QINLINE void RPX_GlmMultiply( float out[3][4], float a[3][4], float b[3][4] ) {
	int i, j;

	for ( i = 0; i < 3; i++ ) {
		for ( j = 0; j < 4; j++ ) {
			out[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + ( j == 3 ? a[i][3] : 0.0f );
		}
	}
}

// frames: the ones wanted, put in order, past the file's last moved to it; *numFrames: how many are left
static QINLINE int RPX_GlaBones( const char *gla, int *frames, int *numFrames ) {
	static const int ident = ( 'A' << 24 ) + ( 'G' << 16 ) + ( 'L' << 8 ) + '2';	// MDXA_IDENT
	static float local[RPX_GLM_MAX_FRAMES][RPX_GLM_MAX_BONES][3][4];
	static rpxGlmPoolUse_t uses[RPX_GLM_MAX_FRAMES * RPX_GLM_MAX_BONES];
	static int parents[RPX_GLM_MAX_BONES], order[RPX_GLM_MAX_BONES], skelOfs[RPX_GLM_MAX_BONES];
	char path[MAX_QPATH];
	int fileFrames, ofsFrames, numBones, ofsPool, i, j, k, n, numUses;
	const byte *p;
	qboolean done[RPX_GLM_MAX_BONES];

	if ( strlen( gla ) + 4 >= sizeof( path ) ) {
		return 0;
	}
	Com_sprintf( path, sizeof( path ), "%s.gla", gla );
	if ( !RPX_GlmOpen( path ) ) {
		return 0;
	}
	// mdxaHeader_t: ident, version, name, fScale, numFrames, ofsFrames, numBones, ofsCompBonePool, ofsSkel
	p = RPX_GlmAt( &rpxGlmReader, 0, 100 );
	if ( !p || RPX_GlmInt( p ) != ident || RPX_GlmInt( p + 4 ) != 6 ) {
		trap->FS_Close( rpxGlmReader.f );
		return 0;
	}
	fileFrames = RPX_GlmInt( p + 76 );
	ofsFrames = RPX_GlmInt( p + 80 );
	numBones = RPX_GlmInt( p + 84 );
	ofsPool = RPX_GlmInt( p + 88 );
	if ( fileFrames < 1 || numBones < 1 || numBones > RPX_GLM_MAX_BONES || ofsFrames < 100 || ofsPool < ofsFrames ) {
		trap->FS_Close( rpxGlmReader.f );
		return 0;
	}

	// the frames: in order, each once, within the file
	for ( i = 0; i < *numFrames; i++ ) {
		frames[i] = (int)Com_Clamp( 0, fileFrames - 1, frames[i] );
	}
	qsort( frames, *numFrames, sizeof( frames[0] ), RPX_GlmIntCompare );
	for ( i = 1, n = 1; i < *numFrames; i++ ) {
		if ( frames[i] != frames[n - 1] ) {
			frames[n++] = frames[i];
		}
	}
	*numFrames = ( *numFrames > 0 ) ? n : 0;

	// the skeleton: each bone's parent (mdxaSkel_t: name, flags, parent, ...), in the file's order
	for ( i = 0; i < numBones; i++ ) {
		p = RPX_GlmAt( &rpxGlmReader, 100 + i * 4, 4 );
		if ( !p ) {
			trap->FS_Close( rpxGlmReader.f );
			return 0;
		}
		skelOfs[i] = 100 + RPX_GlmInt( p );
		order[i] = i;
	}
	for ( i = 1; i < numBones; i++ ) {
		int b = order[i];

		for ( j = i; j > 0 && skelOfs[order[j - 1]] > skelOfs[b]; j-- ) {
			order[j] = order[j - 1];
		}
		order[j] = b;
	}
	for ( i = 0; i < numBones; i++ ) {
		p = RPX_GlmAt( &rpxGlmReader, skelOfs[order[i]] + 68, 4 );
		if ( !p ) {
			trap->FS_Close( rpxGlmReader.f );
			return 0;
		}
		parents[order[i]] = RPX_GlmInt( p );
		if ( parents[order[i]] >= numBones ) {
			trap->FS_Close( rpxGlmReader.f );
			return 0;
		}
	}

	// the frames' bones: a 3-byte index into the pool each
	numUses = 0;
	for ( i = 0; i < *numFrames; i++ ) {
		p = RPX_GlmAt( &rpxGlmReader, ofsFrames + frames[i] * numBones * 3, numBones * 3 );
		if ( !p ) {
			trap->FS_Close( rpxGlmReader.f );
			return 0;
		}
		for ( j = 0; j < numBones; j++ ) {
			uses[numUses].index = p[j * 3] | ( p[j * 3 + 1] << 8 ) | ( p[j * 3 + 2] << 16 );
			uses[numUses].frame = (short)i;
			uses[numUses].bone = (short)j;
			numUses++;
		}
	}
	// and the pool, in order: 14 bytes a bone
	qsort( uses, numUses, sizeof( uses[0] ), RPX_GlmPoolCompare );
	for ( i = 0; i < numUses; i++ ) {
		p = RPX_GlmAt( &rpxGlmReader, ofsPool + uses[i].index * 14, 14 );
		if ( !p ) {
			trap->FS_Close( rpxGlmReader.f );
			return 0;
		}
		RPX_GlmUnCompress( p, local[uses[i].frame][uses[i].bone] );
	}
	trap->FS_Close( rpxGlmReader.f );

	// each bone times its parent's, the parent first
	for ( i = 0; i < *numFrames; i++ ) {
		for ( j = 0; j < numBones; j++ ) {
			done[j] = qfalse;
		}
		for ( k = 0; k < numBones; k++ ) {		// a pass a level of the tree at most
			qboolean progress = qfalse, left = qfalse;

			for ( j = 0; j < numBones; j++ ) {
				if ( done[j] ) {
					continue;
				}
				if ( parents[j] < 0 ) {
					memcpy( rpxGlmBones[i][j], local[i][j], sizeof( local[i][j] ) );
				} else if ( done[parents[j]] ) {
					RPX_GlmMultiply( rpxGlmBones[i][j], rpxGlmBones[i][parents[j]], local[i][j] );
				} else {
					left = qtrue;
					continue;
				}
				done[j] = qtrue;
				progress = qtrue;
			}
			if ( !left ) {
				break;
			}
			if ( !progress ) {
				return 0;	// a loop in the tree
			}
		}
	}
	rpxGlmNumBones = numBones;
	return fileFrames;
}

/*
==================
RPX_GlmShape

GLX_U: [Extras] the box round a .glm as the game draws it at some frames of its animation file, and the
farthest it reaches from its origin (the same however it is turned): every vertex of its most detailed
LOD, skinned with its bone weights (mdx_format.h) by the frames' bones (RPX_GlaBones()). A model whose
animation file is "*default", or that has none the client can read, is measured as the file stores it
(the drawn pose of a "*default" one). With gla, the animation file it names (without .gla); with
glaFrames, that file's frame count (0: none read). qfalse when the .glm does not read as one.
==================
*/
static QINLINE qboolean RPX_GlmShape( const char *path, const int *wanted, int numWanted, vec3_t mins, vec3_t maxs,
	float *reach, char *gla, int glaSize, int *glaFrames ) {
	static int surfOfs[RPX_GLM_MAX_SURFS];
	static float pos[RPX_GLM_SURF_VERTS][3];
	static unsigned int packed[RPX_GLM_SURF_VERTS];
	static byte weights[RPX_GLM_SURF_VERTS][4];
	char name[MAX_QPATH];
	int frames[RPX_GLM_MAX_FRAMES], numFrames = 0, fileFrames = 0;
	int lod, numSurfs, table, i, j, f, w, verts = 0;
	const byte *p;

	if ( !gla || glaSize <= 0 ) {
		gla = name;
		glaSize = sizeof( name );
	}
	gla[0] = '\0';
	if ( glaFrames ) {
		*glaFrames = 0;
	}
	if ( !RPX_GlmOpen( path ) ) {
		return qfalse;
	}
	if ( !RPX_GlmHeader( gla, glaSize, &lod, &numSurfs ) ) {
		trap->FS_Close( rpxGlmReader.f );
		return qfalse;
	}
	trap->FS_Close( rpxGlmReader.f );

	// the bones of the frames wanted (the animation file is read before the .glm's vertices)
	for ( i = 0; i < numWanted && i < RPX_GLM_MAX_FRAMES; i++ ) {
		frames[numFrames++] = wanted[i];
	}
	if ( gla[0] && gla[0] != '*' && numFrames ) {
		fileFrames = RPX_GlaBones( gla, frames, &numFrames );
	}
	if ( !fileFrames ) {
		numFrames = 0;		// as the file stores it
	}
	if ( glaFrames ) {
		*glaFrames = fileFrames;
	}

	if ( !RPX_GlmOpen( path ) || !RPX_GlmHeader( name, sizeof( name ), &lod, &numSurfs ) ) {
		if ( rpxGlmReader.f ) {
			trap->FS_Close( rpxGlmReader.f );
		}
		return qfalse;
	}

	// the first LOD: its mdxmLOD_t, then an offset a surface, from where that table starts; in file order
	table = lod + 4;
	for ( i = 0; i < numSurfs; i++ ) {
		p = RPX_GlmAt( &rpxGlmReader, table + i * 4, 4 );
		if ( !p ) {
			trap->FS_Close( rpxGlmReader.f );
			return qfalse;
		}
		surfOfs[i] = table + RPX_GlmInt( p );
		if ( surfOfs[i] < table + numSurfs * 4 || surfOfs[i] >= rpxGlmReader.len ) {
			trap->FS_Close( rpxGlmReader.f );
			return qfalse;
		}
	}
	qsort( surfOfs, numSurfs, sizeof( surfOfs[0] ), RPX_GlmIntCompare );

	VectorClear( mins );
	VectorClear( maxs );
	*reach = 0.0f;
	for ( i = 0; i < numSurfs; i++ ) {
		int refs[RPX_GLM_MAX_REFS], numVerts, ofsVerts, numRefs, ofsRefs, part;

		// mdxmSurface_t: ident, thisSurfaceIndex, ofsHeader, numVerts, ofsVerts, numTriangles, ofsTriangles,
		// numBoneReferences, ofsBoneReferences, ofsEnd
		p = RPX_GlmAt( &rpxGlmReader, surfOfs[i], 40 );
		if ( !p ) {
			trap->FS_Close( rpxGlmReader.f );
			return qfalse;
		}
		numVerts = RPX_GlmInt( p + 12 );
		ofsVerts = RPX_GlmInt( p + 16 );
		numRefs = RPX_GlmInt( p + 28 );
		ofsRefs = RPX_GlmInt( p + 32 );
		if ( numVerts < 0 || numVerts > RPX_GLM_SURF_VERTS || ofsVerts < 40 || numRefs < 0 || numRefs > RPX_GLM_MAX_REFS ||
			( numRefs && ofsRefs < 40 ) ) {
			trap->FS_Close( rpxGlmReader.f );
			return qfalse;
		}

		// the vertices (mdxmVertex_t, 32 bytes: normal, position, weight count and bone numbers, weights) and
		// the bone references, whichever comes first in the file
		for ( part = 0; part < 2; part++ ) {
			qboolean vertsNow = ( ( ofsVerts <= ofsRefs ) == ( part == 0 ) ) ? qtrue : qfalse;

			if ( vertsNow ) {
				for ( j = 0; j < numVerts; j++ ) {
					p = RPX_GlmAt( &rpxGlmReader, surfOfs[i] + ofsVerts + j * 32 + 12, 20 );
					if ( !p ) {
						trap->FS_Close( rpxGlmReader.f );
						return qfalse;
					}
					for ( w = 0; w < 3; w++ ) {
						pos[j][w] = RPX_GlmFloat( p + w * 4 );
						if ( !( pos[j][w] > -RPX_GLM_MAX_COORD && pos[j][w] < RPX_GLM_MAX_COORD ) ) {
							trap->FS_Close( rpxGlmReader.f );
							return qfalse;	// not a number, or no model's
						}
					}
					packed[j] = (unsigned int)RPX_GlmInt( p + 12 );
					memcpy( weights[j], p + 16, 4 );
				}
			} else {
				for ( j = 0; j < numRefs; j++ ) {
					p = RPX_GlmAt( &rpxGlmReader, surfOfs[i] + ofsRefs + j * 4, 4 );
					if ( !p ) {
						trap->FS_Close( rpxGlmReader.f );
						return qfalse;
					}
					refs[j] = RPX_GlmInt( p );
				}
			}
		}

		for ( j = 0; j < numVerts; j++ ) {
			for ( f = 0; f < ( numFrames ? numFrames : 1 ); f++ ) {
				vec3_t out;
				float lenSq;

				if ( !numFrames ) {
					VectorCopy( pos[j], out );
				} else {
					int count = ( packed[j] >> 30 ) + 1;
					float total = 0.0f;

					VectorClear( out );
					for ( w = 0; w < count; w++ ) {
						int ref = ( packed[j] >> ( 5 * w ) ) & 31;
						float weight, ( *m )[4];

						if ( w == count - 1 ) {
							weight = 1.0f - total;
						} else {
							weight = ( weights[j][w] | ( ( packed[j] >> ( 12 + w * 2 ) ) & 0x300 ) ) / 1023.0f;
							total += weight;
						}
						if ( ref >= numRefs || refs[ref] < 0 || refs[ref] >= rpxGlmNumBones ) {
							trap->FS_Close( rpxGlmReader.f );
							return qfalse;
						}
						m = rpxGlmBones[f][refs[ref]];
						out[0] += weight * ( m[0][0] * pos[j][0] + m[0][1] * pos[j][1] + m[0][2] * pos[j][2] + m[0][3] );
						out[1] += weight * ( m[1][0] * pos[j][0] + m[1][1] * pos[j][1] + m[1][2] * pos[j][2] + m[1][3] );
						out[2] += weight * ( m[2][0] * pos[j][0] + m[2][1] * pos[j][1] + m[2][2] * pos[j][2] + m[2][3] );
					}
				}
				for ( w = 0; w < 3; w++ ) {
					if ( !verts || out[w] < mins[w] ) mins[w] = out[w];
					if ( !verts || out[w] > maxs[w] ) maxs[w] = out[w];
				}
				lenSq = DotProduct( out, out );
				if ( lenSq > *reach ) {
					*reach = lenSq;
				}
				verts++;
			}
		}
	}
	trap->FS_Close( rpxGlmReader.f );
	*reach = sqrt( *reach );
	return verts ? qtrue : qfalse;
}

// GLX_U: [Extras] the frames GLM Props measures a model at: frame 0 when it is not animated (what the renderer
// draws a bone with no animation at), else its start, its end and up to 14 frames between
static QINLINE int RPX_GlmFrameList( qboolean animated, int start, int end, int *frames ) {
	int k, n = 0;

	if ( !animated || end < start ) {
		frames[0] = animated ? start : 0;
		return 1;
	}
	for ( k = 0; k < RPX_GLM_MAX_FRAMES; k++ ) {
		int f = start + (int)( (float)( end - start ) * k / ( RPX_GLM_MAX_FRAMES - 1 ) + 0.5f );

		if ( !n || f != frames[n - 1] ) {
			frames[n++] = f;
		}
	}
	return n;
}

// GLX_U: [Extras] a box turned by angles: the world-axis box round its eight turned corners (an entity's box
// does not turn with it)
static QINLINE void RPX_TurnedBox( const vec3_t mins, const vec3_t maxs, const vec3_t angles, vec3_t outMins, vec3_t outMaxs ) {
	vec3_t axis[3];
	int c, k;

	AnglesToAxis( angles, axis );
	for ( c = 0; c < 8; c++ ) {
		vec3_t corner, world;

		corner[0] = ( c & 1 ) ? maxs[0] : mins[0];
		corner[1] = ( c & 2 ) ? maxs[1] : mins[1];
		corner[2] = ( c & 4 ) ? maxs[2] : mins[2];
		for ( k = 0; k < 3; k++ ) {
			world[k] = corner[0] * axis[0][k] + corner[1] * axis[1][k] + corner[2] * axis[2][k];
		}
		for ( k = 0; k < 3; k++ ) {
			if ( !c || world[k] < outMins[k] ) outMins[k] = world[k];
			if ( !c || world[k] > outMaxs[k] ) outMaxs[k] = world[k];
		}
	}
}

// GLX_U: [Extras] what is wrong with GLM Props' frames, or NULL: the server's limits (SP_misc_model_ghoul's
// RP_GhoulFrames(), g_misc.c) and, when the model's animation file is known (numFrames > 0), its last frame.
// Only when animated: without, no frame keys are sent.
#define RPX_GLM_MAX_FRAME	65534

static QINLINE const char *RPX_GlmFramesProblem( qboolean animated, int start, int end, int numFrames ) {
	static char problem[96];

	if ( !animated ) {
		return NULL;
	}
	if ( start < 0 || start > RPX_GLM_MAX_FRAME || end < 0 || end > RPX_GLM_MAX_FRAME ) {
		Com_sprintf( problem, sizeof( problem ), "Frames go from 0 to %d.", RPX_GLM_MAX_FRAME );
		return problem;
	}
	if ( end < start ) {
		return "The end frame comes before the start frame.";
	}
	if ( numFrames > 0 && end >= numFrames ) {
		Com_sprintf( problem, sizeof( problem ), "Its animation file ends at frame %d.", numFrames - 1 );
		return problem;
	}
	return NULL;
}

// the camera of the effect preview, the same in both modules: it circles the look-at point at "angle"
// degrees round from +x, RPX_FX_ELEVATION up, "dist" away; for an effect going up it looks a little
// above where it starts
static QINLINE void RPX_FxCamera( float dist, float angle, int dirMode, vec3_t origin, vec3_t camOrg, vec3_t camAngles ) {
	vec3_t lookAt, dir;
	float yaw, pitch;

	if ( dist < 32.0f ) dist = 32.0f;
	if ( dist > 1024.0f ) dist = 1024.0f;

	VectorSet( origin, RPX_FX_ORIGIN_X, RPX_FX_ORIGIN_Y, RPX_FX_ORIGIN_Z );
	VectorCopy( origin, lookAt );
	if ( dirMode == 0 ) {
		lookAt[2] += dist * 0.3f;
	}

	yaw = DEG2RAD( angle );
	pitch = DEG2RAD( RPX_FX_ELEVATION );
	VectorSet( dir, cos( pitch ) * cos( yaw ), cos( pitch ) * sin( yaw ), sin( pitch ) );
	VectorMA( lookAt, dist, dir, camOrg );

	VectorSubtract( lookAt, camOrg, dir );
	vectoangles( dir, camAngles );
}

// the direction the preview effect is played in: up, or towards the camera (level, or tilted by
// pitch: -90 is straight up, as for an fx_runner's angles)
static QINLINE void RPX_FxDirection( int dirMode, float pitch, const vec3_t origin, const vec3_t camOrg, vec3_t fwd ) {
	vec3_t toCam, angles;

	if ( dirMode <= 0 || dirMode > 2 ) {
		VectorSet( fwd, 0, 0, 1 );
		return;
	}
	VectorSubtract( camOrg, origin, toCam );
	toCam[2] = 0;
	vectoangles( toCam, angles );
	angles[PITCH] = ( dirMode == 2 ) ? pitch : 0;
	angles[ROLL] = 0;
	AngleVectors( angles, fwd, NULL, NULL );
}

// what is wrong with a name (targetname) typed in the NPCs or Lights menu, or NULL: letters, digits, _ and -,
// at most 32 of them
static QINLINE const char *RPX_LabelProblem( const char *label ) {
	int i;

	for ( i = 0; label[i]; i++ ) {
		char c = label[i];

		if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_' || c == '-' ) ) {
			return "The name may only have letters, digits, _ and -.";
		}
	}
	if ( i > 32 ) {
		return "The name may be at most 32 characters long.";
	}
	return NULL;
}

// GalaxyRP: [Extras] what is wrong with the names typed in the Spawners menu, or NULL. The spawner's name is
// required -- triggers, buttons and /entuse fire it by it; the server refuses a spawner without one -- and
// all three are names as RPX_LabelProblem() takes them. The NPC's own name may not be the spawner's (firing
// the spawner would use its NPCs too: a vehicle used makes whoever used it board it), and an NPC's
// "On death fires" (npcs only) may not be the spawner's either: Respawn when killed is that, done once.
static QINLINE const char *RPX_SpawnerNamesProblem( const char *spawner, const char *npc, const char *ondeath, qboolean npcs ) {
	static char problem[128];
	const char *names[3], *labels[3];
	int i, n = npcs ? 3 : 2;

	if ( !spawner[0] ) {
		return "Give the spawner a name: triggers, buttons and /entuse fire it by it.";
	}
	names[0] = spawner;	labels[0] = "The spawner's name";
	names[1] = npc;		labels[1] = "Its name";
	names[2] = ondeath;	labels[2] = "On death fires";
	for ( i = 0; i < n; i++ ) {
		const char *p = RPX_LabelProblem( names[i] );

		if ( p ) {
			// "The name may ..." with the field's own label
			if ( strncmp( p, "The name", 8 ) ) {
				return p;
			}
			Com_sprintf( problem, sizeof( problem ), "%s%s", labels[i], p + 8 );
			return problem;
		}
	}
	if ( npc[0] && !Q_stricmp( npc, spawner ) ) {
		return "The spawner and what it spawns need different names.";
	}
	if ( npcs && ondeath[0] && !Q_stricmp( ondeath, spawner ) ) {
		return "On death fires cannot be the spawner's own name: use Respawn when killed.";
	}
	return NULL;
}

// a name from a listing that can be used safely: printable, none of " ; \ | and not "." or "..";
// a folder (folderOk) ends in its one "/"
static QINLINE qboolean RPX_NameOk( const char *name, qboolean folderOk ) {
	int i, len;

	if ( !name || !name[0] ) {
		return qfalse;
	}
	len = strlen( name );
	if ( len >= RPX_NAME_LEN ) {
		return qfalse;
	}
	for ( i = 0; i < len; i++ ) {
		unsigned char c = (unsigned char)name[i];

		// '%': a server command or a client command carries it as '.' (MSG_ReadString())
		if ( c < 32 || c == 127 || c == '"' || c == ';' || c == '\\' || c == '|' || c == '%' ) {
			return qfalse;
		}
		if ( c == '/' && !( folderOk && i == len - 1 && i > 0 ) ) {
			return qfalse;
		}
	}
	if ( !strcmp( name, "." ) || !strcmp( name, ".." ) || !strcmp( name, "./" ) || !strcmp( name, "../" ) ) {
		return qfalse;
	}
	return qtrue;
}

// a folder of a list, relative to its root ("" for the root): no "..", "//", or slash at either end,
// and only characters a name can have
static QINLINE qboolean RPX_FolderOk( const char *folder ) {
	int i, len = strlen( folder );

	if ( len >= RPX_NAME_LEN ) {
		return qfalse;
	}
	if ( !len ) {
		return qtrue;
	}
	if ( folder[0] == '/' || folder[len - 1] == '/' || strstr( folder, "//" ) || strstr( folder, ".." ) ) {
		return qfalse;
	}
	for ( i = 0; i < len; i++ ) {
		unsigned char c = (unsigned char)folder[i];

		if ( c < 32 || c == 127 || c == '"' || c == ';' || c == '\\' || c == '|' || c == ':' || c == '%' ) {
			return qfalse;
		}
	}
	return qtrue;
}

#endif // RP_EXTRAS_H
