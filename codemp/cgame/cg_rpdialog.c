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
	int			walkAwayId;		// GLX_U fix: a dialogue a restart left open, to walk away from (CG_RpDialogFrame); 0 none
	int			walkAwayAt;		// trap->Milliseconds() when it may go; 0 until the first drawn frame
	char		walkAwaySid[16];	// the sv_serverid it was opened under
} rpdlg;

// GLX_U fix: [Dialogues] the server's sv_serverid (systeminfo): new at every map load and map_restart, and another
// server's is another. A dialogue id is only meaningful under the one it came with: the game numbers dialogues
// from 1 again on every map (g_dialog.c)
static const char *RP_DlgServerId( void )
{
	return Info_ValueForKey( CG_ConfigString( CS_SYSTEMINFO ), "sv_serverid" );
}

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
	trap->Cvar_Set( "ui_rpdlg_sid", RP_DlgServerId() );	// GLX_U fix: kept with the id, for a restart's walk-away (CG_RpDialogFrame)
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

// each frame (CG_DrawActiveFrame): a restart's walk-away, the time left, and a pending open
void CG_RpDialogFrame( void )
{
	if ( rpdlg.walkAwayId )
	{ // GLX_U fix: [Dialogues] the walk-away CG_RpDialogInit kept: "dialog 0 <id>", as Esc. Sent from init, it went in the
	  // first packet after the restart with the engine's own "vdr" and "cp", and the server's flood protection (one
	  // command per RP_FLOOD_PROTECT_MS, held by the game) ignored it: those count first. vid_restart sends nothing
	  // while it reloads, so the wait counts from the first drawn frame, as RPX_FloodGap (cg_rpextras.c) spaces the
	  // Extras list, and from the ui's own last send (ui_rpx_sent), which this then stamps. Once: if a command typed
	  // in those 150 ms still takes its place, the dialogue ends as before (its timeout, walking away, /dialog)
		int now = trap->Milliseconds();

		if ( !rpdlg.walkAwayAt )
		{ // the first drawn frame: the configstrings are here now. Another level (a map load, map_restart) or another
		  // server has no dialogue of ours open, and the id may be a new dialogue's there: nothing to walk away from
			if ( cg.demoPlayback || !rpdlg.walkAwaySid[0] || strcmp( rpdlg.walkAwaySid, RP_DlgServerId() ) )
				rpdlg.walkAwayId = 0;
			else
				rpdlg.walkAwayAt = now + RP_FLOOD_PROTECT_MS + 60;
		}
		else if ( now >= rpdlg.walkAwayAt )
		{
			char buf[16];
			int ui;

			trap->Cvar_VariableStringBuffer( "ui_rpx_sent", buf, sizeof( buf ) );
			ui = atoi( buf );
			if ( ui && now - ui >= 0 && now - ui < RP_FLOOD_PROTECT_MS + 60 )
				rpdlg.walkAwayAt = ui + RP_FLOOD_PROTECT_MS + 60;	// a menu sent one just now: after it
			else
			{
				trap->SendClientCommand( va( "dialog 0 %d", rpdlg.walkAwayId ) );
				trap->Cvar_Set( "ui_rpx_sent", va( "%d", now ) );
				rpdlg.walkAwayId = 0;
			}
		}
	}
	if ( !rpdlg.id )
		return;
	RP_DlgSetLeft();
	if ( rpdlg.pendingOpen && !RP_DlgTypingSomewhere() )
	{
		rpdlg.pendingOpen = qfalse;
		trap->OpenUIMenu( UIMENU_RPDIALOG );
	}
}

// CG_Init: a dialogue a restart left open kept to walk away from (CG_RpDialogFrame), the userinfo key the server looks for, the font the
// menu measures with, and clean cvars (a map change leaves nothing open: a menu still up can only send a "dialog 0"
// for an old id, which the server ignores)
void CG_RpDialogInit( void )
{
	int i;

	memset( &rpdlg, 0, sizeof( rpdlg ) );
	{ // GLX_U fix: [Dialogues] a restart (vid_restart, a cgame or ui restart) takes the menu away with a dialogue still
	  // open on the server, and nothing told it: the menu's onClose is a menu script, and a ui shutdown runs none. The
	  // dialogue then stayed open and unseen, and using its speaker again only refreshed it (the held-Use guard,
	  // g_dialog.c). ui_rpdlg_id and ui_rpdlg_sid outlive the restart: kept here, and CG_RpDialogFrame walks away from
	  // that dialogue, as Esc does ("dialog 0 <id>"), once the game is drawn again and only under the same sv_serverid.
	  // The server closes it if it is still the one open, and ignores a stale id without a word.
		char buf[16];
		int id;

		trap->Cvar_VariableStringBuffer( "ui_rpdlg_id", buf, sizeof( buf ) );
		id = atoi( buf );
		if ( id > 0 )
		{
			rpdlg.walkAwayId = id;
			trap->Cvar_VariableStringBuffer( "ui_rpdlg_sid", rpdlg.walkAwaySid, sizeof( rpdlg.walkAwaySid ) );
		}
	}

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
	trap->Cvar_Set( "ui_rpdlg_sid", "" );
	trap->Cvar_Set( "ui_rpdlg_left", "" );
}
