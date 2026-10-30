// TEST ONLY: cg_rpdialog.c against a stub engine: the rpdlg command (cvars, the line breaking with ergoec-like
// widths, colours carried over, options shortened, malformed commands ignored), the pending open while the
// console or the chat box is up, rpdlgclose with the current id and another, the time left, demo playback.
#include "../../codemp/cgame/cg_local.h"
#include "../../codemp/ui/ui_public.h"
#include <stdio.h>
#include <stdarg.h>

cg_t cg; cgs_t cgs; cgameImport_t *trap; static cgameImport_t imp;
static int fails;
#define CHECK( c, ... ) do { if ( !(c) ) { fails++; printf( "FAIL %s:%d: ", __FILE__, __LINE__ ); printf( __VA_ARGS__ ); printf( "\n" ); } } while ( 0 )

// the stub engine
static char t_cvars[64][2][300]; static int t_ncvars;
static void s_CvarSet( const char *name, const char *value ) { int i; for ( i = 0; i < t_ncvars; i++ ) if ( !strcmp( t_cvars[i][0], name ) ) { Q_strncpyz( t_cvars[i][1], value, 300 ); return; } Q_strncpyz( t_cvars[t_ncvars][0], name, 300 ); Q_strncpyz( t_cvars[t_ncvars][1], value, 300 ); t_ncvars++; }
static const char *cvar( const char *name ) { int i; for ( i = 0; i < t_ncvars; i++ ) if ( !strcmp( t_cvars[i][0], name ) ) return t_cvars[i][1]; return "(unset)"; }
static void s_CvarRegister( vmCvar_t *v, const char *name, const char *value, uint32_t flags ) { (void)v; (void)flags; s_CvarSet( name, value ); }
static void s_CvarString( const char *name, char *buf, int size ) { const char *v = cvar( name ); Q_strncpyz( buf, strcmp( v, "(unset)" ) ? v : "", size ); }
static char t_sentCmd[256]; static int t_nsentCmd; static void s_SendClientCommand( const char *c ) { Q_strncpyz( t_sentCmd, c, sizeof( t_sentCmd ) ); t_nsentCmd++; }
static int t_argc; static char t_argv[16][300];
static int s_Argc( void ) { return t_argc; }
static void s_Argv( int n, char *buf, int size ) { Q_strncpyz( buf, n < t_argc ? t_argv[n] : "", size ); }
static int t_catcher; static int s_GetCatcher( void ) { return t_catcher; }
static int t_opened[16], t_nopened; static void s_OpenUIMenu( int menu ) { if ( t_nopened < 16 ) t_opened[t_nopened++] = menu; }
static qhandle_t s_RegisterFont( const char *name ) { (void)name; return 7; }
// ergoec-like widths: W 15, M/m 14, digits 10, space 6, the rest 8; colour codes draw nothing
static int s_StrLen( const char *s, int font, float scale ) { float w = 0; (void)font; for ( ; *s; s++ ) { if ( *s == '^' && s[1] >= '0' && s[1] <= '9' ) { s++; continue; } w += ( *s == 'W' ) ? 15 : ( *s == 'M' || *s == 'm' ) ? 14 : ( *s >= '0' && *s <= '9' ) ? 10 : ( *s == ' ' ) ? 6 : 8; } return (int)( w * scale + 0.5f ); }
static char t_printed[4096]; static void QDECL s_Print( const char *fmt, ... ) { va_list a; char b[1024]; va_start( a, fmt ); Q_vsnprintf( b, sizeof( b ), fmt, a ); va_end( a ); Q_strcat( t_printed, sizeof( t_printed ), b ); }
static void QDECL s_Error( int l, const char *fmt, ... ) { (void)l; printf( "Error: %s\n", fmt ); exit( 1 ); }
qboolean t_chat; qboolean CG_RpChatActive( void ) { return t_chat; }
NORETURN_PTR void (*Com_Error)( int level, const char *error, ... ) = (void *)s_Error;
void (*Com_Printf)( const char *msg, ... ) = s_Print;

static void cmd( const char *line ) { char b[2048], *p = b; Q_strncpyz( b, line, sizeof( b ) ); t_argc = 0; while ( *p ) { while ( *p == ' ' ) p++; if ( !*p ) break; if ( *p == '"' ) { char *e = strchr( ++p, '"' ); if ( !e ) e = p + strlen( p ); *e = 0; Q_strncpyz( t_argv[t_argc++], p, 300 ); p = e + 1; } else { char *e = p; while ( *e && *e != ' ' ) e++; if ( *e ) *e++ = 0; Q_strncpyz( t_argv[t_argc++], p, 300 ); p = e; } } }
static int width( const char *s ) { return s_StrLen( s, 7, 0.58f ); }

int main( void )
{
	int i;
	trap = &imp; imp.Cvar_Set = s_CvarSet; imp.Cvar_Register = s_CvarRegister; imp.Cmd_Argc = s_Argc; imp.Cmd_Argv = s_Argv; imp.Key_GetCatcher = s_GetCatcher; imp.OpenUIMenu = s_OpenUIMenu; imp.R_RegisterFont = s_RegisterFont; imp.R_Font_StrLenPixels = s_StrLen; imp.Cvar_VariableStringBuffer = s_CvarString; imp.SendClientCommand = s_SendClientCommand; imp.Print = s_Print;
	setvbuf( stdout, NULL, _IONBF, 0 );
	CG_RpDialogInit();
	CHECK( t_nsentCmd == 0, "init with no dialogue id (unset): nothing sent" );
	CHECK( !strcmp( cvar( "rpdlg" ), "1" ) && !strcmp( cvar( "ui_rpdlg_id" ), "0" ) && !strcmp( cvar( "ui_rpdlg_opt1" ), "" ), "init: the userinfo key, clean cvars" );

	// a plain dialogue
	cg.time = 10000;
	cmd( "rpdlg 7 60 \"Merchant\" \"Hello wanderer, what do you want to do today in this ^3fine^7 market of the Outer Rim?\" \"Take me to the docks\" \"Buy a blaster (500 credits)\" \"Nothing\"" );
	CG_RpDialogCmd();
	CHECK( t_nopened == 1 && t_opened[0] == UIMENU_RPDIALOG, "opened the menu" );
	CHECK( !strcmp( cvar( "ui_rpdlg_title" ), "Merchant" ) && !strcmp( cvar( "ui_rpdlg_count" ), "3" ) && !strcmp( cvar( "ui_rpdlg_id" ), "7" ), "title, count, id" );
	CHECK( !strcmp( cvar( "ui_rpdlg_opt1" ), "^31.^7 Take me to the docks" ) && !strcmp( cvar( "ui_rpdlg_opt3" ), "^33.^7 Nothing" ) && !strcmp( cvar( "ui_rpdlg_opt4" ), "" ), "options numbered, unused empty: %s", cvar( "ui_rpdlg_opt4" ) );
	CHECK( !strcmp( cvar( "ui_rpdlg_left" ), "1:00 left" ), "time left: %s", cvar( "ui_rpdlg_left" ) );
	for ( i = 1; i <= 7; i++ ) { const char *l = cvar( va( "ui_rpdlg_line%d", i ) ); CHECK( width( l ) <= 572, "line %d within the column (%d)", i, width( l ) ); CHECK( !l[0] || ( l[0] == '^' && l[1] >= '0' && l[1] <= '9' ), "line %d starts with a colour: %s", i, l ); }
	CHECK( !strncmp( cvar( "ui_rpdlg_line1" ), "^7Hello wanderer", 16 ), "line 1: %s", cvar( "ui_rpdlg_line1" ) );
	{ // the colour carries over: find the line after the one holding ^3fine
	  int k; for ( k = 1; k <= 6; k++ ) { const char *l = cvar( va( "ui_rpdlg_line%d", k ) ); if ( strstr( l, "^3fine" ) && !strstr( l, "^7 market" ) ) { CHECK( !strncmp( cvar( va( "ui_rpdlg_line%d", k + 1 ) ), "^7", 2 ) || !strncmp( cvar( va( "ui_rpdlg_line%d", k + 1 ) ), "^3", 2 ), "the next line starts with the colour in effect" ); } } }
	// the worst case: 255 W's in words of 40, seven lines, the rest cut with ...
	{ char big[400] = ""; for ( i = 0; i < 6; i++ ) Q_strcat( big, sizeof( big ), "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW " ); big[255] = 0;
	  cmd( va( "rpdlg 8 30 \"T\" \"%s\" \"a\"", big ) ); CG_RpDialogCmd();
	  for ( i = 1; i <= 7; i++ ) CHECK( width( cvar( va( "ui_rpdlg_line%d", i ) ) ) <= 572, "worst case line %d within the column", i );
	  CHECK( cvar( "ui_rpdlg_line7" )[0] == 0 || strstr( cvar( "ui_rpdlg_line7" ), "..." ) || !strstr( big, cvar( "ui_rpdlg_line7" ) + 2 ), "what does not fit ends with ..." ); }
	// an over-long word is cut, a wide option is shortened, @ is covered
	cmd( "rpdlg 9 30 \"@title\" \"WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW end\" \"WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW\"" ); CG_RpDialogCmd();
	CHECK( !strcmp( cvar( "ui_rpdlg_title" ), "^7@title" ), "@ covered: %s", cvar( "ui_rpdlg_title" ) );
	CHECK( width( cvar( "ui_rpdlg_line1" ) ) <= 572 && strstr( cvar( "ui_rpdlg_line2" ), "end" ), "a word wider than the column is cut, the rest follows: %s | %s", cvar( "ui_rpdlg_line1" ), cvar( "ui_rpdlg_line2" ) );
	CHECK( width( cvar( "ui_rpdlg_opt1" ) ) <= 572 && strstr( cvar( "ui_rpdlg_opt1" ), "..." ), "a wide option shortened: %s", cvar( "ui_rpdlg_opt1" ) );
	// ; % and \ stay text
	cmd( "rpdlg 10 30 \"T\" \"say ;quit %s \\\\x\" \"a;b\"" ); CG_RpDialogCmd(); CHECK( strstr( cvar( "ui_rpdlg_line1" ), ";quit %s \\\\x" ) && strstr( cvar( "ui_rpdlg_opt1" ), "a;b" ), "special characters arrive unchanged: %s", cvar( "ui_rpdlg_line1" ) );
	// malformed
	t_nopened = 0; cmd( "rpdlg 0 30 \"T\" \"x\" \"a\"" ); CG_RpDialogCmd(); cmd( "rpdlg 11 700 \"T\" \"x\" \"a\"" ); CG_RpDialogCmd(); cmd( "rpdlg 11 30 \"T\" \"x\"" ); CG_RpDialogCmd(); cmd( "rpdlg 11 30 \"T\" \"x\" 1 2 3 4 5 6 7" ); CG_RpDialogCmd();
	CHECK( t_nopened == 0 && !strcmp( cvar( "ui_rpdlg_id" ), "10" ), "malformed commands ignored (opened %d)", t_nopened );
	// pending while the console is up, then opened; the chat box too
	t_nopened = 0; t_catcher = KEYCATCH_CONSOLE; cmd( "rpdlg 12 20 \"T\" \"x\" \"a\"" ); CG_RpDialogCmd(); CHECK( t_nopened == 0 && !strcmp( cvar( "ui_rpdlg_id" ), "12" ), "waits while the console is open" );
	CG_RpDialogFrame(); CHECK( t_nopened == 0, "still waiting" ); t_catcher = 0; CG_RpDialogFrame(); CHECK( t_nopened == 1 && t_opened[0] == UIMENU_RPDIALOG, "opens once the console closed" );
	t_nopened = 0; t_chat = qtrue; cmd( "rpdlg 13 20 \"T\" \"x\" \"a\"" ); CG_RpDialogCmd(); CHECK( t_nopened == 0, "waits for the chat box" ); t_chat = qfalse; CG_RpDialogFrame(); CHECK( t_nopened == 1, "opens after the chat box" );
	// the time counts down
	cg.time += 5500; CG_RpDialogFrame(); CHECK( !strcmp( cvar( "ui_rpdlg_left" ), "0:15 left" ), "time left after 5.5 s: %s", cvar( "ui_rpdlg_left" ) );
	// close: another id does nothing, the current one closes
	t_nopened = 0; cmd( "rpdlgclose 12" ); CG_RpDialogCloseCmd(); CHECK( t_nopened == 0 && !strcmp( cvar( "ui_rpdlg_id" ), "13" ), "another id: nothing" );
	cmd( "rpdlgclose 13" ); CG_RpDialogCloseCmd(); CHECK( t_nopened == 1 && t_opened[0] == UIMENU_RPDIALOG_CLOSE && !strcmp( cvar( "ui_rpdlg_id" ), "0" ), "the current id: closed" );
	cmd( "rpdlgclose 13" ); CG_RpDialogCloseCmd(); CHECK( t_nopened == 1, "a second close says nothing" );
	// a pending open that gets closed before it opened never opens
	t_nopened = 0; t_catcher = KEYCATCH_MESSAGE; cmd( "rpdlg 14 20 \"T\" \"x\" \"a\"" ); CG_RpDialogCmd(); cmd( "rpdlgclose 14" ); CG_RpDialogCloseCmd(); t_catcher = 0; CG_RpDialogFrame(); CHECK( t_nopened == 1 && t_opened[0] == UIMENU_RPDIALOG_CLOSE, "closed while pending: never opened" );
	// demo playback
	t_nopened = 0; cg.demoPlayback = qtrue; cmd( "rpdlg 15 20 \"T\" \"x\" \"a\"" ); CG_RpDialogCmd(); CHECK( t_nopened == 0 && !strcmp( cvar( "ui_rpdlg_id" ), "0" ), "demo: ignored" ); cg.demoPlayback = qfalse;
	// the menu file: the scale and the column the cgame measures against are the menu's
	{ FILE *f = fopen( "../../../assets/client/ui/jamp/ingame_rpdialog.menu", "r" ); char line[512]; int scale58 = 0, col572 = 0, opts = 0, lines = 0;
	  CHECK( f != NULL, "the menu file exists" );
	  if ( f ) { while ( fgets( line, sizeof( line ), f ) ) { if ( strstr( line, "textscale           0.58" ) ) scale58++; if ( strstr( line, "rect                14 " ) && strstr( line, " 572 " ) ) col572++; if ( strstr( line, "name                rpdlg_opt" ) ) opts++; if ( strstr( line, "name                rpdlg_line" ) ) lines++; } fclose( f );
	    CHECK( scale58 == 13 && col572 >= 13 && opts == 6 && lines == 7, "menu: 13 items at 0.58 (%d), 572 wide (%d), 6 options (%d), 7 lines (%d)", scale58, col572, opts, lines ); } }
	// AUDIT 3 H3: a restart (CG_Init) walks away from a dialogue its ui_rpdlg_id says was open, as Esc does
	t_nsentCmd = 0; s_CvarSet( "ui_rpdlg_id", "7" ); CG_RpDialogInit();
	CHECK( t_nsentCmd == 1 && !strcmp( t_sentCmd, "dialog 0 7" ) && !strcmp( cvar( "ui_rpdlg_id" ), "0" ), "init with id 7: \"dialog 0 7\" sent once, the cvar cleared (%d: %s)", t_nsentCmd, t_sentCmd );
	t_nsentCmd = 0; CG_RpDialogInit(); CHECK( t_nsentCmd == 0, "init again with id 0: nothing sent" );
	t_nsentCmd = 0; s_CvarSet( "ui_rpdlg_id", "junk" ); CG_RpDialogInit(); CHECK( t_nsentCmd == 0, "a non-number: nothing sent" );
	t_nsentCmd = 0; s_CvarSet( "ui_rpdlg_id", "-3" ); CG_RpDialogInit(); CHECK( t_nsentCmd == 0, "a negative id: nothing sent" );
	t_nsentCmd = 0; t_catcher = 0; cg.demoPlayback = qfalse; cmd( "rpdlg 9 30 \"T\" \"x\" \"a\"" ); CG_RpDialogCmd(); CG_RpDialogInit();
	CHECK( t_nsentCmd == 1 && !strcmp( t_sentCmd, "dialog 0 9" ), "a dialogue received, then a restart: \"dialog 0 9\" (%s)", t_sentCmd );
	printf( fails ? "%d FAILED\n" : "t_cgdlg: ALL OK\n", fails );
	return fails != 0;
}
