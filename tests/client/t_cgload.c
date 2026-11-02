// TEST ONLY: cg_info.c's loading screen (CG_DrawInformation) against a stub engine: which picture it picks and
// where it draws it on 16:9, 21:9, 16:10, 4:3 and 5:4 screens and with cl_ratioFix 0 (a coefficient of 1), for a
// map with a 16:9 levelshot, a 4:3 one or none, and with the 16:9 unknown-map art missing; the load bar after it.
#include "../../codemp/cgame/cg_local.h"
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

cg_t cg; cgs_t cgs; cgameImport_t *trap; static cgameImport_t imp;
gitem_t bg_itemlist[1];
static int fails;
#define CHECK( c, ... ) do { if ( !(c) ) { fails++; printf( "FAIL %s:%d: ", __FILE__, __LINE__ ); printf( __VA_ARGS__ ); printf( "\n" ); } } while ( 0 )

// the stub engine: the pictures that "exist", each with its handle (its index + 1)
static const char *t_files[8]; static int t_nfiles;
static char t_asked[16][MAX_QPATH]; static int t_nasked;
static qhandle_t s_RegisterShaderNoMip( const char *name )
{
	int i;
	if ( t_nasked < 16 ) Q_strncpyz( t_asked[t_nasked++], name, MAX_QPATH );
	for ( i = 0; i < t_nfiles; i++ ) if ( !Q_stricmp( t_files[i], name ) ) return i + 1;
	return 0;
}
static void s_SetColor( const float *rgba ) { (void)rgba; }
static int asked( const char *name ) { int i; for ( i = 0; i < t_nasked; i++ ) if ( !Q_stricmp( t_asked[i], name ) ) return 1; return 0; }

// the draws, in order
typedef struct { float x, y, w, h; qhandle_t pic; } draw_t;
static draw_t t_draws[16]; static int t_ndraws;
void CG_DrawPic( float x, float y, float w, float h, qhandle_t pic ) { if ( t_ndraws < 16 ) { draw_t d = { x, y, w, h, pic }; t_draws[t_ndraws++] = d; } }

static char t_serverinfo[256];
const char *CG_ConfigString( int index ) { return index == CS_SERVERINFO ? t_serverinfo : ""; }
const char *CG_GetStringEdString( char *refSection, char *refName ) { (void)refSection; (void)refName; return ""; }
static void QDECL s_Error( int l, const char *fmt, ... ) { (void)l; printf( "Error: %s\n", fmt ); exit( 1 ); }
static void QDECL s_Printf( const char *fmt, ... ) { (void)fmt; }
NORETURN_PTR void (*Com_Error)( int level, const char *error, ... ) = (void *)s_Error;
void (*Com_Printf)( const char *msg, ... ) = s_Printf;

#define BARPIC	100	// cgs.media.loadBarLEDSurround, the load bar's first draw

static int near( float a, float b ) { return fabsf( a - b ) < 0.01f; }
static int is( const draw_t *d, float x, float y, float w, float h, qhandle_t pic ) { return near( d->x, x ) && near( d->y, y ) && near( d->w, w ) && near( d->h, h ) && d->pic == pic; }

// one loading-screen draw: the map, the screen coefficient, the pictures that exist
static const draw_t *run( const char *map, float coef, int nfiles, const char **files )
{
	int i;
	t_nfiles = nfiles; for ( i = 0; i < nfiles; i++ ) t_files[i] = files[i];
	t_nasked = 0; t_ndraws = 0;
	Com_sprintf( t_serverinfo, sizeof( t_serverinfo ), "\\mapname\\%s\\sv_hostname\\test", map );
	cgs.widthRatioCoef = coef;
	CG_DrawInformation();
	return &t_draws[0];
}

int main( void )
{
	const float c169 = 0.75f, c219 = 640.0f * 1080 / ( 480.0f * 2560 ), c1610 = 640.0f * 1050 / ( 480.0f * 1680 ), c54 = 640.0f * 1024 / ( 480.0f * 1280 );
	const char *all[] = { "levelshots_16_9/mp/ffa1", "levelshots/mp/ffa1", "menu/art/unknownmap_mp_16_9", "menu/art/unknownmap_mp" };
	const char *shot43[] = { "levelshots/mp/ffa1", "menu/art/unknownmap_mp_16_9", "menu/art/unknownmap_mp" };
	const char *none[] = { "menu/art/unknownmap_mp_16_9", "menu/art/unknownmap_mp" };
	const char *only43[] = { "menu/art/unknownmap_mp" };
	const draw_t *d;

	trap = &imp; imp.R_RegisterShaderNoMip = s_RegisterShaderNoMip; imp.R_SetColor = s_SetColor;
	cgs.media.loadBarLEDSurround = BARPIC;
	setvbuf( stdout, NULL, _IONBF, 0 );

	// 16:9
	d = run( "mp/ffa1", c169, 4, all );
	CHECK( is( d, 0, 0, 640, 480, 1 ), "16:9, a 16:9 levelshot: drawn over the screen (%g %g %g %g pic %d)", d->x, d->y, d->w, d->h, d->pic );
	CHECK( t_ndraws >= 2 && t_draws[1].pic == BARPIC, "16:9: the load bar after the picture" );
	CHECK( !asked( "levelshots/mp/ffa1" ), "16:9: the 4:3 levelshot not asked for once the 16:9 one is there" );
	d = run( "mp/ffa1", c169, 3, shot43 );
	CHECK( is( d, 0, -80, 640, 640, 1 ), "16:9, a 4:3 levelshot: its shape kept, top and bottom cut (%g %g %g %g)", d->x, d->y, d->w, d->h );
	CHECK( asked( "levelshots_16_9/mp/ffa1" ), "16:9: the 16:9 levelshot asked for first" );
	d = run( "mp/ffa1", c169, 2, none );
	CHECK( is( d, 0, 0, 640, 480, 1 ), "16:9, no levelshot: unknownmap_mp_16_9 over the screen (pic %d)", d->pic );
	CHECK( t_ndraws == 5 && t_draws[1].pic == BARPIC, "16:9: one picture, then the load bar's four draws (%d draws)", t_ndraws );
	d = run( "mp/ffa1", c169, 1, only43 );
	CHECK( is( d, 0, 0, 640, 480, 1 ), "16:9, no levelshot and no 16:9 art: unknownmap_mp over the screen (pic %d)", d->pic );

	// 21:9 (2560x1080)
	d = run( "mp/ffa1", c219, 4, all );
	CHECK( near( d->w, 640 ) && near( d->h, 480 * 0.75f / c219 ) && near( d->y, ( 480 - d->h ) / 2 ) && d->h > 480 && d->pic == 1,
		"21:9, a 16:9 levelshot: taller than the screen, centred (%g %g %g)", d->y, d->w, d->h );
	d = run( "mp/ffa1", c219, 3, shot43 );
	CHECK( near( d->h, 480 / c219 ) && near( d->y, ( 480 - d->h ) / 2 ) && d->pic == 1, "21:9, a 4:3 levelshot: its shape kept (%g %g)", d->y, d->h );
	d = run( "mp/ffa1", c219, 2, none );
	CHECK( is( d, 0, 0, 640, 480, 1 ), "21:9, no levelshot: unknownmap_mp_16_9 over the screen" );

	// 16:10 (1680x1050): not wide -- no 16:9 art asked for; a levelshot keeps its shape
	d = run( "mp/ffa1", c1610, 4, all );
	CHECK( !asked( "levelshots_16_9/mp/ffa1" ) && d->pic == 2, "16:10: the 16:9 levelshot not used (pic %d)", d->pic );
	CHECK( near( d->h, 480 / c1610 ) && near( d->y, ( 480 - d->h ) / 2 ), "16:10, a 4:3 levelshot: its shape kept (%g %g)", d->y, d->h );
	d = run( "mp/ffa1", c1610, 2, none );
	CHECK( is( d, 0, 0, 640, 480, 2 ) && !asked( "menu/art/unknownmap_mp_16_9" ), "16:10, no levelshot: unknownmap_mp over the screen (pic %d)", d->pic );

	// 4:3, 5:4, cl_ratioFix 0 (1): exactly as before
	d = run( "mp/ffa1", 1.0f, 4, all );
	CHECK( is( d, 0, 0, 640, 480, 2 ) && !asked( "levelshots_16_9/mp/ffa1" ), "4:3: the levelshot over the screen (pic %d)", d->pic );
	d = run( "mp/ffa1", 1.0f, 2, none );
	CHECK( is( d, 0, 0, 640, 480, 2 ) && !asked( "menu/art/unknownmap_mp_16_9" ), "4:3, no levelshot: unknownmap_mp (pic %d)", d->pic );
	d = run( "mp/ffa1", c54, 4, all );
	CHECK( is( d, 0, 0, 640, 480, 2 ), "5:4: the levelshot over the screen, as before (%g %g %g %g pic %d)", d->x, d->y, d->w, d->h, d->pic );
	d = run( "mp/ffa1", c54, 2, none );
	CHECK( is( d, 0, 0, 640, 480, 2 ), "5:4, no levelshot: unknownmap_mp" );

	// nothing at all exists: still one draw over the screen (handle 0, the engine's default) and the bar
	d = run( "mp/ffa1", c169, 0, NULL );
	CHECK( is( d, 0, 0, 640, 480, 0 ) && t_ndraws == 5 && t_draws[1].pic == BARPIC, "no art at all: one full-screen draw and the bar" );

	printf( fails ? "t_cgload: %d FAILED\n" : "t_cgload: ALL OK\n", fails );
	return fails != 0;
}
