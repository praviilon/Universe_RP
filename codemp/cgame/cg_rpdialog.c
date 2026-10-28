/*
===========================================================================
GLX_U: [Dialogues] the dialogue menu for our client (g_dialog.c on the server, ui_rpdialog.c and
ui/jamp/ingame_rpdialog.menu on this side).

The server sends  rpdlg <id> <seconds left> "<title>" "<text>" "<option 1>" ... "<option N>"  to a client
whose userinfo carries "rpdlg 1" (set here), and  rpdlgclose <id>  when that dialogue ends. The text goes
into cvars the menu draws from and never into a command line, so ';', '%' and '\' stay text. The cgame
breaks the text into lines itself: the menu's own wrapping can overrun its box by a word and drops the
colour at every line break (see the D2 design note). Answers go back from the ui as "cmd dialog <n> <id>".
===========================================================================
*/
#include "cg_local.h"
#include "ui/ui_public.h"

#define RPDLG_SCALE			0.58f	// the text and option items' textscale in ingame_rpdialog.menu
#define RPDLG_TITLE_SCALE	0.7f
#define RPDLG_COLUMN		572		// the text column's width there (rect 14 .. 586)
#define RPDLG_LINES			7
#define RPDLG_OPTIONS		6
#define RPDLG_TEXT_MAX		256
#define RPDLG_LINE_MAX		300

static struct {
	int			id;				// the dialogue shown, 0 for none
	int			endTime;		// cg.time when it times out
	int			shownLeft;		// the seconds last written to ui_rpdlg_left
	qboolean	pendingOpen;	// the menu waits for the console or the chat line to close
	qhandle_t	font;
} rpdlg;

static int RP_DlgWidth( const char *s, float scale )
{
	return trap->R_Font_StrLenPixels( s, rpdlg.font, scale );
}

// the colour code in effect at the end of s, "^7" when none
static void RP_DlgLastColour( const char *s, char out[3] )
{
	const char *p;

	out[0] = '^'; out[1] = '7'; out[2] = '\0';
	for ( p = s; *p; p++ )
	{
		if ( *p == '^' && p[1] >= '0' && p[1] <= '9' )
		{
			out[1] = p[1];
			p++;
		}
	}
}

/*
==================
RP_DlgBreakText

Breaks the text at spaces into at most RPDLG_LINES lines, each within the column at the menu's scale. Every
line starts with the colour in effect, so colours carry over and no line can start with '@' (a string-table
lookup). A word wider than the column on its own is cut; what is left after the last line ends it with "...".
Returns the number of lines.
==================
*/
static int RP_DlgBreakText( const char *text, char lines[RPDLG_LINES][RPDLG_LINE_MAX] )
{
	char colour[3] = "^7";
	char word[RPDLG_TEXT_MAX], line[RPDLG_LINE_MAX], tryLine[RPDLG_LINE_MAX];
	const char *p = text;
	int n = 0;

	Q_strncpyz( line, colour, sizeof( line ) );
	while ( *p )
	{
		int wl = 0;

		while ( *p == ' ' )
			p++;
		while ( *p && *p != ' ' && wl < (int)sizeof( word ) - 1 )
			word[wl++] = *p++;
		word[wl] = '\0';
		if ( !wl )
			break;
		// a word too wide for a line by itself: cut it (the server breaks runs over 40 characters, so this is a guard)
		while ( wl > 1 && RP_DlgWidth( va( "%s%s", colour, word ), RPDLG_SCALE ) > RPDLG_COLUMN )
			word[--wl] = '\0';
		Com_sprintf( tryLine, sizeof( tryLine ), "%s%s%s", line, strlen( line ) > 2 ? " " : "", word );
		if ( RP_DlgWidth( tryLine, RPDLG_SCALE ) <= RPDLG_COLUMN )
		{
			Q_strncpyz( line, tryLine, sizeof( line ) );
			continue;
		}
		// the line is full
		Q_strncpyz( lines[n++], line, RPDLG_LINE_MAX );
		RP_DlgLastColour( line, colour );
		if ( n == RPDLG_LINES )
		{ // no room for the rest: the last line ends with ...
			char *last = lines[n - 1];

			while ( strlen( last ) > 2 && RP_DlgWidth( va( "%s...", last ), RPDLG_SCALE ) > RPDLG_COLUMN )
				last[strlen( last ) - 1] = '\0';
			Q_strcat( last, RPDLG_LINE_MAX, "..." );
			return n;
		}
		Com_sprintf( line, sizeof( line ), "%s%s", colour, word );
	}
	if ( ( strlen( line ) > 2 || n == 0 ) && n < RPDLG_LINES )
		Q_strncpyz( lines[n++], line, RPDLG_LINE_MAX );
	return n;
}

// "^3n.^7 option", shortened with ... when wider than the column
static void RP_DlgOption( int n, const char *option, char *out, int outSize )
{
	Com_sprintf( out, outSize, "^3%d.^7 %s", n, option );
	if ( RP_DlgWidth( out, RPDLG_SCALE ) <= RPDLG_COLUMN )
		return;
	while ( strlen( out ) > 6 && RP_DlgWidth( va( "%s...", out ), RPDLG_SCALE ) > RPDLG_COLUMN )
		out[strlen( out ) - 1] = '\0';
	Q_strcat( out, outSize, "..." );
}

static void RP_DlgSetLeft( void )
{
	int left = ( rpdlg.endTime - cg.time + 999 ) / 1000;

	if ( left < 0 )
		left = 0;
	if ( left == rpdlg.shownLeft )
		return;
	rpdlg.shownLeft = left;
	trap->Cvar_Set( "ui_rpdlg_left", va( "%d:%02d left", left / 60, left % 60 ) );
}

static qboolean RP_DlgTypingSomewhere( void )
{ // the console, the stock chat line, or our chat box
	return ( ( trap->Key_GetCatcher() & ( KEYCATCH_CONSOLE | KEYCATCH_MESSAGE ) ) || CG_RpChatActive() ) ? qtrue : qfalse;
}

/*
==================
rpdlg <id> <seconds> "title" "text" "opt1" ... -- the server opened, replaced or re-sent a dialogue
==================
*/
void CG_RpDialogCmd( void )
{
	char arg[RPDLG_TEXT_MAX + 1], lines[RPDLG_LINES][RPDLG_LINE_MAX], opt[RPDLG_LINE_MAX];
	int id, seconds, count, i, n;

	if ( cg.demoPlayback )
		return;
	count = trap->Cmd_Argc() - 5;
	trap->Cmd_Argv( 1, arg, sizeof( arg ) );
	id = atoi( arg );
	trap->Cmd_Argv( 2, arg, sizeof( arg ) );
	seconds = atoi( arg );
	if ( id <= 0 || seconds < 1 || seconds > 600 || count < 1 || count > RPDLG_OPTIONS )
	{
		trap->Print( "rpdlg: malformed (id %d, %d s, %d options)\n", id, seconds, count );
		return;
	}
	trap->Cmd_Argv( 3, arg, sizeof( arg ) );
	trap->Cvar_Set( "ui_rpdlg_title", arg[0] == '@' ? va( "^7%s", arg ) : arg );
	trap->Cmd_Argv( 4, arg, sizeof( arg ) );
	n = RP_DlgBreakText( arg, lines );
	for ( i = 0; i < RPDLG_LINES; i++ )
		trap->Cvar_Set( va( "ui_rpdlg_line%d", i + 1 ), i < n ? lines[i] : "" );
	for ( i = 0; i < RPDLG_OPTIONS; i++ )
	{
		if ( i < count )
		{
			trap->Cmd_Argv( 5 + i, arg, sizeof( arg ) );
			RP_DlgOption( i + 1, arg, opt, sizeof( opt ) );
			trap->Cvar_Set( va( "ui_rpdlg_opt%d", i + 1 ), opt );
		}
		else
			trap->Cvar_Set( va( "ui_rpdlg_opt%d", i + 1 ), "" );
	}
	trap->Cvar_Set( "ui_rpdlg_count", va( "%d", count ) );
	trap->Cvar_Set( "ui_rpdlg_id", va( "%d", id ) );
	rpdlg.id = id;
	rpdlg.endTime = cg.time + seconds * 1000;
	rpdlg.shownLeft = -1;
	RP_DlgSetLeft();
	if ( RP_DlgTypingSomewhere() )
		rpdlg.pendingOpen = qtrue;	// opens once the console or the chat line closes; the timeout runs meanwhile
	else
	{
		rpdlg.pendingOpen = qfalse;
		trap->OpenUIMenu( UIMENU_RPDIALOG );
	}
}

// rpdlgclose <id> -- the dialogue ended on the server (answered, cancelled, timeout, away, died...)
void CG_RpDialogCloseCmd( void )
{
	char arg[16];

	if ( cg.demoPlayback )
		return;
	trap->Cmd_Argv( 1, arg, sizeof( arg ) );
	if ( !rpdlg.id || atoi( arg ) != rpdlg.id )
		return;	// not the one shown: a later one took its place already
	rpdlg.id = 0;
	rpdlg.pendingOpen = qfalse;
	trap->Cvar_Set( "ui_rpdlg_id", "0" );
	trap->OpenUIMenu( UIMENU_RPDIALOG_CLOSE );
}

// each frame (CG_DrawActiveFrame): the time left, and a pending open
void CG_RpDialogFrame( void )
{
	if ( !rpdlg.id )
		return;
	RP_DlgSetLeft();
	if ( rpdlg.pendingOpen && !RP_DlgTypingSomewhere() )
	{
		rpdlg.pendingOpen = qfalse;
		trap->OpenUIMenu( UIMENU_RPDIALOG );
	}
}

// CG_Init: the userinfo key the server looks for, the font the menu measures with, and clean cvars (a map
// change leaves nothing open: a menu still up can only send a "dialog 0" for an old id, which the server ignores)
void CG_RpDialogInit( void )
{
	int i;

	memset( &rpdlg, 0, sizeof( rpdlg ) );
	trap->Cvar_Register( NULL, "rpdlg", "1", CVAR_USERINFO | CVAR_ROM );
	trap->Cvar_Set( "rpdlg", "1" );
	rpdlg.font = trap->R_RegisterFont( "ergoec" );	// the menus' font 2; registered here so a HUD font change cannot change the measuring
	trap->Cvar_Set( "ui_rpdlg_title", "" );
	for ( i = 1; i <= RPDLG_LINES; i++ )
		trap->Cvar_Set( va( "ui_rpdlg_line%d", i ), "" );
	for ( i = 1; i <= RPDLG_OPTIONS; i++ )
		trap->Cvar_Set( va( "ui_rpdlg_opt%d", i ), "" );
	trap->Cvar_Set( "ui_rpdlg_count", "0" );
	trap->Cvar_Set( "ui_rpdlg_id", "0" );
	trap->Cvar_Set( "ui_rpdlg_left", "" );
}
