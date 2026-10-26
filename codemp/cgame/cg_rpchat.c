/*
===========================================================================
GLX_U: [Chat] the chat input box.

The engine's own chat box (Con_MessageMode*_f and Message_Key, cl_console.cpp / cl_keys.cpp) is one
line of big text at the top of the screen that scrolls sideways as the player types, and the cgame
can neither restyle it nor read it. This is the cgame's replacement: the chat keys open a box drawn
in the received chat's own font and size (CG_ChatBox_DrawStrings, cg_draw.c), at the top left,
that goes on to the next line when a line is full, so the whole message stays in view.

- Opening. Every command run from a key binding or typed in the console reaches the cgame first
  (CG_IncomingConsoleCommand, cg_main.c), and may be swapped for another. With cg_chatInput 1,
  CG_RpChatSwap() turns messagemode, messagemode2, messagemode3 and messagemode4 into
  "chatmode say|team|tell|attacker". That one runs from the command buffer a moment later, after the
  chat key's own character has gone by -- so it is not typed into the box, as with the engine's.
  /chatmode ooc and /chatmode ally give OOC and ally chat keys of their own. With cg_chatInput 0
  nothing is swapped (the engine's box opens, as before) and /chatmode only says the box is off.
- Keys. The box takes the keyboard (KEYCATCH_CGAME): the engine then sends CG_KeyEvent every key,
  and every typed character already shifted and in the player's keyboard layout (K_CHAR_FLAG).
  Bindings do not run, so the player stands still, as in the engine's box. Editing follows the
  engine's field (Field_KeyDownEvent/Field_CharEvent): Backspace, Ctrl+A and Ctrl+E come as
  characters, Delete, the arrows, Home, End and Enter as keys. Escape is the engine's: it takes the
  keyboard back, and the box closes on the next frame (it closes whenever it no longer has the
  keyboard). The talk bubble shows, the engine setting BUTTON_TALK under any key catcher.
- Sending. Enter sends the same command the engine's box would (say, say_team, tell) or /ooc,
  /allychat, so the server treats every message exactly as before: RP modifiers, OOC, ally and team
  chat, distances. A long message goes in quoted parts, which the server joins (RpChat_Send). A '"' goes out as '' (which the server colours as a quote, format_text_in_quotes),
  so it counts as two of the RPCHAT_MAX_SENT characters.
- Mouse. With the keyboard caught, the engine sends the mouse to the cgame too (CL_MouseEvent), so the
  view can't turn while typing. The box shows the menus' cursor instead, starting in the top-left
  corner: a click places the text cursor, a drag selects (and a double-click a word, Shift extends),
  and typing, Backspace or Delete replace or remove the selection.
- Not here (yet): copy, cut and paste, a history of sent messages, the Insert key's overwrite mode.
===========================================================================
*/

#include "cg_local.h"
#include "ui/ui_shared.h"	// keycodes, ITEM_TEXTSTYLE_*

// GLX_U: [Chat] the longest message the box sends, counted as sent. Every client receives a chat line into
// MAX_SAY_TEXT (CG_Chat_f, cg_servercmds.c) together with what the server puts before the message: the
// name (up to MAX_NETNAME), the format of an RP modifier or OOC (up to about 30) and 4 per quoted span.
#define RPCHAT_MAX_SENT		480
#define RPCHAT_ARG_MAX		250				// a quoted part's longest (RpChat_Send)

#define RPCHAT_X			30				// the received chat's left edge (CG_ChatBox_DrawStrings)
#define RPCHAT_Y			16
#define RPCHAT_RADAR_LEFT	520				// the radar's left edge before RATIO_RX (RADAR_X in cg_draw.c)
#define RPCHAT_GAP			5				// lines end this far before the radar (RpChat_LineWidth)
#define RPCHAT_MAX_RIGHT	560				// and never past this: the powerup icons start at ~570 on every screen
#define RPCHAT_SCALE		0.65f			// the received chat's font size
#define RPCHAT_LINE_HEIGHT	( 20 * RPCHAT_SCALE )	// CHATBOX_FONT_HEIGHT at that size
#define RPCHAT_MAX_LINES	64
#define RPCHAT_CURSOR_SIZE	40				// the mouse cursor, as the ui draws it (width ratio-adjusted)
#define RPCHAT_DOUBLE_CLICK	400				// ms between two clicks on one spot that select a word

typedef enum {
	RPCHAT_SAY,
	RPCHAT_TEAM,
	RPCHAT_OOC,
	RPCHAT_ALLY,
	RPCHAT_TELL,
	RPCHAT_ATTACKER,
	RPCHAT_NUM_MODES
} rpChatMode_t;

static const struct {
	const char	*name;		// /chatmode's argument
	const char	*command;	// what is sent
	char		colour;		// the prompt's, the channel's colour in chat (G_Say)
} rpChatModes[RPCHAT_NUM_MODES] = {
	{ "say",		"say",		COLOR_GREEN },
	{ "team",		"say_team",	COLOR_CYAN },
	{ "ooc",		"ooc",		COLOR_RED },
	{ "ally",		"allychat",	COLOR_WHITE },
	{ "tell",		"tell",		COLOR_MAGENTA },
	{ "attacker",	"tell",		COLOR_MAGENTA },
};

static struct {
	qboolean		active;
	rpChatMode_t	mode;
	int				target;					// tell's client
	char			prompt[MAX_QPATH + 32];
	char			text[RPCHAT_MAX_SENT + 1];	// as typed; never longer than RPCHAT_MAX_SENT
	int				len;
	int				cursor;
	int				sentLen;				// len, with every '"' counted twice
	int				anchor;					// the selection's other end, -1 for none (RpChat_HasSel)

	// the mouse
	float			mouseX, mouseY;			// the cursor's tip, on the 640x480 screen
	qboolean		dragging;				// mouse 1 held since a click in the box
	int				lastClickTime;			// for a double-click
	int				lastClickPos;

	// the layout, redone after every change
	qboolean		dirty;
	int				promptWidth;
	int				lineWidth;				// RpChat_LineWidth, as last laid out
	int				numLines;
	int				lineStart[RPCHAT_MAX_LINES + 1];	// lineStart[numLines] == len
} rpChat;

qboolean CG_RpChatActive( void )
{
	return rpChat.active;
}

static void RpChat_Close( void )
{
	rpChat.active = qfalse;
	rpChat.len = rpChat.cursor = rpChat.sentLen = 0;
	rpChat.text[0] = '\0';
	rpChat.anchor = -1;
	rpChat.dragging = qfalse;
	rpChat.lastClickTime = 0;
}

static qboolean RpChat_HasSel( void )
{
	return (qboolean)( rpChat.anchor >= 0 && rpChat.anchor != rpChat.cursor );
}

/*
================
CG_RpChatSwap

From CG_IncomingConsoleCommand: a bound or console-typed messagemode[2-4] becomes /chatmode, with
cg_chatInput 1. Only the whole command: anything else in it (a console line holding several) leaves it
as it is. qtrue if conCommand was swapped.
================
*/
qboolean CG_RpChatSwap( char *conCommand, int size )
{
	static const char *modeCmds[] = { "messagemode", "messagemode2", "messagemode3", "messagemode4" };
	static const char *modeArgs[] = { "say", "team", "tell", "attacker" };
	char token[32];
	const char *p = conCommand;
	int i, n = 0;

	if ( !cg_chatInput.integer || cg.demoPlayback )
		return qfalse;

	while ( *p == ' ' || *p == '\t' || *p == '/' || *p == '\\' )
		p++;

	while ( *p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' )
	{
		if ( n >= (int)sizeof( token ) - 1 )
			return qfalse;
		token[n++] = *p++;
	}
	token[n] = '\0';

	while ( *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' )
		p++;
	if ( *p )
		return qfalse;

	for ( i = 0; i < (int)ARRAY_LEN( modeCmds ); i++ )
	{
		if ( !Q_stricmp( token, modeCmds[i] ) )
		{
			Q_strncpyz( conCommand, va( "chatmode %s", modeArgs[i] ), size );
			return qtrue;
		}
	}
	return qfalse;
}

/*
================
CG_ChatMode_f

/chatmode say|team|ooc|ally|tell|attacker: opens the box. tell is to the crosshair's player and
attacker to the last one who hit you (messagemode3/4); with neither, nothing opens, as with the
engine's. Opened again while open, it changes the channel and keeps the text.
================
*/
void CG_ChatMode_f( void )
{
	const char *arg = CG_Argv( 1 );
	char label[64] = { 0 };
	int mode, target = -1;

	if ( !cg_chatInput.integer )
	{
		trap->Print( "The new chat box is off (cg_chatInput 0).\n" );
		return;
	}

	if ( cg.demoPlayback || !cg.snap )
		return;

	for ( mode = 0; mode < RPCHAT_NUM_MODES; mode++ )
	{
		if ( !Q_stricmp( arg, rpChatModes[mode].name ) )
			break;
	}
	if ( mode == RPCHAT_NUM_MODES )
	{
		trap->Print( "Usage: chatmode <say|team|ooc|ally|tell|attacker>\n" );
		return;
	}

	if ( mode == RPCHAT_TELL || mode == RPCHAT_ATTACKER )
	{
		target = ( mode == RPCHAT_TELL ) ? CG_CrosshairPlayer() : CG_LastAttacker();
		if ( target < 0 || target >= MAX_CLIENTS || !cgs.clientinfo[target].infoValid )
			return;
	}

	switch ( mode )
	{
	case RPCHAT_SAY:
		trap->SE_GetStringTextString( "MP_SVGAME_SAY", label, sizeof( label ) );
		if ( !label[0] )
			Q_strncpyz( label, "Say:", sizeof( label ) );
		break;
	case RPCHAT_TEAM:
		trap->SE_GetStringTextString( "MP_SVGAME_SAY_TEAM", label, sizeof( label ) );
		if ( !label[0] )
			Q_strncpyz( label, "Say Team:", sizeof( label ) );
		break;
	case RPCHAT_OOC:
		Q_strncpyz( label, "OOC:", sizeof( label ) );
		break;
	case RPCHAT_ALLY:
		Q_strncpyz( label, "Ally:", sizeof( label ) );
		break;
	default:
		Q_strncpyz( label, "Tell", sizeof( label ) );
		break;
	}

	if ( target >= 0 )
		Com_sprintf( rpChat.prompt, sizeof( rpChat.prompt ), "^%c%s %s^%c: ", rpChatModes[mode].colour, label,
			cgs.clientinfo[target].name, rpChatModes[mode].colour );
	else
		Com_sprintf( rpChat.prompt, sizeof( rpChat.prompt ), "^%c%s ", rpChatModes[mode].colour, label );

	if ( !rpChat.active )
	{
		RpChat_Close();	// empty text
		rpChat.mouseX = rpChat.mouseY = 0;	// the mouse cursor starts in the top-left corner, off the view and the text
	}

	rpChat.active = qtrue;
	rpChat.mode = (rpChatMode_t)mode;
	rpChat.target = target;
	rpChat.dirty = qtrue;

	trap->Key_SetCatcher( trap->Key_GetCatcher() | KEYCATCH_CGAME );

	// typed in the console: close it, so the player types straight into the box. The cgame can't
	// drop the console's keyboard itself (the engine keeps KEYCATCH_CONSOLE), so the console key's own
	// command does it. Binds never get here with the console up: none run while it's open.
	if ( trap->Key_GetCatcher() & KEYCATCH_CONSOLE )
		trap->SendConsoleCommand( "toggleconsole\n" );
}

static void RpChat_Insert( int ch )
{
	int cost = ( ch == '"' ) ? 2 : 1;

	if ( rpChat.sentLen + cost > RPCHAT_MAX_SENT )
		return;

	memmove( rpChat.text + rpChat.cursor + 1, rpChat.text + rpChat.cursor, rpChat.len - rpChat.cursor + 1 );
	rpChat.text[rpChat.cursor] = (char)ch;
	rpChat.cursor++;
	rpChat.len++;
	rpChat.sentLen += cost;
	rpChat.anchor = -1;
	rpChat.dirty = qtrue;
}

static void RpChat_Delete( int at )
{
	if ( at < 0 || at >= rpChat.len )
		return;

	rpChat.sentLen -= ( rpChat.text[at] == '"' ) ? 2 : 1;
	memmove( rpChat.text + at, rpChat.text + at + 1, rpChat.len - at );
	rpChat.len--;
	rpChat.anchor = -1;
	rpChat.dirty = qtrue;
}

// the selection removed, the text cursor where it began. An end between '^' and its colour character
// takes the whole code, so no stray half of one is left.
static void RpChat_DeleteSel( void )
{
	int a = Q_min( rpChat.anchor, rpChat.cursor ), b = Q_max( rpChat.anchor, rpChat.cursor ), i;

	if ( a > 0 && a < rpChat.len && Q_IsColorString( &rpChat.text[a - 1] ) )
		a--;
	if ( b > 0 && b < rpChat.len && Q_IsColorString( &rpChat.text[b - 1] ) )
		b++;

	for ( i = a; i < b; i++ )
		rpChat.sentLen -= ( rpChat.text[i] == '"' ) ? 2 : 1;
	memmove( rpChat.text + a, rpChat.text + b, rpChat.len - b + 1 );
	rpChat.len -= b - a;
	rpChat.cursor = a;
	rpChat.anchor = -1;
	rpChat.dirty = qtrue;
}

// the text cursor to pos; with select, the selection runs from where it was (or from its anchor)
static void RpChat_MoveTo( int pos, qboolean select )
{
	if ( select )
	{
		if ( !RpChat_HasSel() )
			rpChat.anchor = rpChat.cursor;
	}
	else
		rpChat.anchor = -1;
	rpChat.cursor = pos;
}

/*
================
RpChat_Send

As the engine's box: say "message" -- but in parts. The server engine cuts each argument of a client
command to MAX_CVAR_VALUE_STRING-1 (255) characters, when it filters commands (sv_filterCommands, on
by default; Cmd_Args_Sanitize in SV_ExecuteClientCommand), so one quoted message is cut there. Every
chat command reads the message as all its arguments joined with a space (ConcatArgs), so the message
goes as several quoted ones, each split where it has a space: say "first part" "second part".
================
*/
static void RpChat_Send( void )
{
	char out[RPCHAT_MAX_SENT + 1];
	char cmd[RPCHAT_MAX_SENT * 2 + 64];
	int i, o = 0;

	for ( i = 0; i < rpChat.len && o < RPCHAT_MAX_SENT; i++ )
	{
		if ( rpChat.text[i] == '"' )
		{ // as the engines send it, and what the server colours as a quote
			if ( o + 2 > RPCHAT_MAX_SENT )
				break;
			out[o++] = '\'';
			out[o++] = '\'';
		}
		else
			out[o++] = rpChat.text[i];
	}
	out[o] = '\0';

	if ( !out[0] )
		return;

	if ( rpChat.mode == RPCHAT_TELL || rpChat.mode == RPCHAT_ATTACKER )
		Com_sprintf( cmd, sizeof( cmd ), "tell %i", rpChat.target );
	else
		Q_strncpyz( cmd, rpChatModes[rpChat.mode].command, sizeof( cmd ) );

	// in quoted parts of at most RPCHAT_ARG_MAX, split at a space, which the server's ConcatArgs puts back
	for ( i = 0; i < o; )
	{
		int end = o;

		if ( o - i > RPCHAT_ARG_MAX )
		{
			for ( end = i + RPCHAT_ARG_MAX; end > i; end-- )
			{
				if ( out[end] == ' ' )
					break;
			}
			if ( end == i )
				end = i + RPCHAT_ARG_MAX;	// one word longer than a part: it gets a space where it is cut
		}

		Q_strcat( cmd, sizeof( cmd ), va( " \"%.*s\"", end - i, out + i ) );
		i = ( end < o && out[end] == ' ' ) ? end + 1 : end;
	}

	trap->SendClientCommand( cmd );
}

static int RpChat_PosAt( float x, float y );

// mouse 1 down: place the text cursor, extend the selection with Shift, select a word on a double-click
static void RpChat_Click( qboolean shift )
{
	int pos = RpChat_PosAt( rpChat.mouseX, rpChat.mouseY );
	int now = trap->Milliseconds();

	if ( shift )
	{
		RpChat_MoveTo( pos, qtrue );
		rpChat.dragging = qtrue;
		rpChat.lastClickTime = 0;
		return;
	}

	if ( rpChat.lastClickTime && now - rpChat.lastClickTime < RPCHAT_DOUBLE_CLICK && pos == rpChat.lastClickPos )
	{ // the word around it: the run of characters between spaces
		int a = pos, b = pos;

		while ( a > 0 && rpChat.text[a - 1] != ' ' )
			a--;
		while ( b < rpChat.len && rpChat.text[b] != ' ' )
			b++;

		rpChat.anchor = a;
		rpChat.cursor = b;	// on a space, a == b: just placed
		rpChat.dragging = qfalse;
		rpChat.lastClickTime = 0;
		return;
	}

	rpChat.anchor = rpChat.cursor = pos;
	rpChat.dragging = qtrue;
	rpChat.lastClickTime = now ? now : 1;
	rpChat.lastClickPos = pos;
}

/*
================
CG_RpChatKey

From CG_KeyEvent while the box is open. Characters as Field_CharEvent takes them, keys as
Field_KeyDownEvent and Message_Key do. With a selection, typing replaces it, Backspace and Delete remove
it, and the arrows, Home and End drop it -- or, with Shift held, extend it.
================
*/
void CG_RpChatKey( int key, qboolean down )
{
	qboolean shift;

	if ( !down )
	{
		if ( key == A_MOUSE1 )
			rpChat.dragging = qfalse;
		return;
	}

	if ( key & K_CHAR_FLAG )
	{
		int ch = key & ~K_CHAR_FLAG;

		if ( ch == 'h' - 'a' + 1 )
		{ // backspace
			if ( RpChat_HasSel() )
				RpChat_DeleteSel();
			else if ( rpChat.cursor > 0 )
			{
				rpChat.cursor--;
				RpChat_Delete( rpChat.cursor );
			}
		}
		else if ( ch == 'a' - 'a' + 1 )
			RpChat_MoveTo( 0, qfalse );
		else if ( ch == 'e' - 'a' + 1 )
			RpChat_MoveTo( rpChat.len, qfalse );
		else if ( ch >= 32 && ch != 127 && ch <= 255 )
		{
			if ( RpChat_HasSel() )
				RpChat_DeleteSel();
			RpChat_Insert( ch );
		}
		return;
	}

	shift = (qboolean)( trap->Key_IsDown( A_SHIFT ) || trap->Key_IsDown( A_SHIFT2 ) );

	switch ( key )
	{
	case A_ENTER:
	case A_KP_ENTER:
		RpChat_Send();
		RpChat_Close();
		trap->Key_SetCatcher( trap->Key_GetCatcher() & ~KEYCATCH_CGAME );
		break;
	case A_DELETE:
		if ( RpChat_HasSel() )
			RpChat_DeleteSel();
		else
			RpChat_Delete( rpChat.cursor );
		break;
	case A_CURSOR_LEFT:
		if ( !shift && RpChat_HasSel() )
			RpChat_MoveTo( Q_min( rpChat.anchor, rpChat.cursor ), qfalse );
		else
			RpChat_MoveTo( rpChat.cursor > 0 ? rpChat.cursor - 1 : 0, shift );
		break;
	case A_CURSOR_RIGHT:
		if ( !shift && RpChat_HasSel() )
			RpChat_MoveTo( Q_max( rpChat.anchor, rpChat.cursor ), qfalse );
		else
			RpChat_MoveTo( rpChat.cursor < rpChat.len ? rpChat.cursor + 1 : rpChat.len, shift );
		break;
	case A_HOME:
		RpChat_MoveTo( 0, shift );
		break;
	case A_END:
		RpChat_MoveTo( rpChat.len, shift );
		break;
	case A_MOUSE1:
		RpChat_Click( shift );
		break;
	default:
		break;
	}
}

// the width of text[from..to) as drawn: colour codes take none
static int RpChat_Width( int from, int to )
{
	char seg[RPCHAT_MAX_SENT + 1];
	int n = to - from;

	if ( n <= 0 )
		return 0;

	memcpy( seg, rpChat.text + from, n );
	seg[n] = '\0';
	return CG_Text_Width( seg, RPCHAT_SCALE, FONT_SMALL );
}

/*
================
RpChat_LineWidth

A line's width: up to just before the radar, which the widescreen HUD pins to the right edge
(RATIO_RX, by cl_ratioFix and the screen's shape), so 4:3 gets shorter lines than 16:9. Capped where
the powerup icons start, which aren't moved, for 21:9 and wider.
================
*/
static int RpChat_LineWidth( void )
{
	float right = RATIO_RX( RPCHAT_RADAR_LEFT ) - RPCHAT_GAP;

	if ( right > RPCHAT_MAX_RIGHT )
		right = RPCHAT_MAX_RIGHT;
	return (int)right - RPCHAT_X;
}

/*
================
RpChat_Layout

Where each line starts. A line is rpChat.lineWidth wide, the first less the prompt, and is broken after
its last space; a word longer than a line is broken where it runs out, but never inside a colour code.
================
*/
static void RpChat_Layout( void )
{
	int start = 0, i = 0;
	int avail = rpChat.lineWidth - rpChat.promptWidth;

	rpChat.numLines = 1;
	rpChat.lineStart[0] = 0;

	while ( i < rpChat.len )
	{
		if ( i > start && RpChat_Width( start, i + 1 ) > avail )
		{
			int brk, next;

			if ( rpChat.numLines >= RPCHAT_MAX_LINES )
				break;	// cannot happen at RPCHAT_MAX_SENT; the rest stays on the last line

			for ( brk = i; brk > start; brk-- )
			{
				if ( rpChat.text[brk] == ' ' )
					break;
			}

			if ( brk > start )
				next = brk + 1;	// the space ends the line
			else
			{
				next = i;
				if ( next - 1 > start && Q_IsColorString( &rpChat.text[next - 1] ) )
					next--;	// keep ^N together
			}

			rpChat.lineStart[rpChat.numLines++] = next;
			start = next;
			avail = rpChat.lineWidth;
			i = start;
			continue;
		}
		i++;
	}

	rpChat.lineStart[rpChat.numLines] = rpChat.len;
	rpChat.dirty = qfalse;
}

// the prompt's width, the line width and the layout, up to date
static void RpChat_Refresh( void )
{
	int width = RpChat_LineWidth();

	rpChat.promptWidth = CG_Text_Width( rpChat.prompt, RPCHAT_SCALE, FONT_SMALL );
	if ( width != rpChat.lineWidth )
	{ // first frame, or cl_ratioFix changed: wrap again
		rpChat.lineWidth = width;
		rpChat.dirty = qtrue;
	}
	if ( rpChat.dirty )
		RpChat_Layout();
}

/*
================
RpChat_PosAt

The text position nearest a point on the screen: the line under it (the first above the box, the last
below it), then the gap between two characters nearest across. A line broken after a space ends before
that space, so its end stays on it. Never between '^' and its colour character.
================
*/
static int RpChat_PosAt( float x, float y )
{
	int l, from, to, last, p, best;
	float xs, bestDist = 1e9f;

	RpChat_Refresh();

	l = (int)floorf( ( y - RPCHAT_Y ) / RPCHAT_LINE_HEIGHT );
	if ( l < 0 )
		l = 0;
	else if ( l > rpChat.numLines - 1 )
		l = rpChat.numLines - 1;

	from = rpChat.lineStart[l];
	to = rpChat.lineStart[l + 1];
	last = to;
	if ( l < rpChat.numLines - 1 && to > from && rpChat.text[to - 1] == ' ' )
		last = to - 1;

	xs = RPCHAT_X + ( l ? 0 : rpChat.promptWidth );
	best = from;
	for ( p = from; p <= last; p++ )
	{
		float dist = fabsf( xs + RpChat_Width( from, p ) - x );

		if ( dist <= bestDist )
		{ // on a tie (a colour code takes no width), the later: after the code, in its colour
			bestDist = dist;
			best = p;
		}
	}

	if ( best > 0 && best < rpChat.len && Q_IsColorString( &rpChat.text[best - 1] ) )
		best--;
	return best;
}

/*
================
CG_RpChatMouse

From CG_MouseEvent while the box is open: moves its cursor by as much as the menus' would, and with
mouse 1 held since a click, the text cursor with it (the selection).
================
*/
void CG_RpChatMouse( int dx, int dy )
{
	if ( !rpChat.active )
		return;

	rpChat.mouseX = Com_Clamp( 0, SCREEN_WIDTH, rpChat.mouseX + dx );
	rpChat.mouseY = Com_Clamp( 0, SCREEN_HEIGHT, rpChat.mouseY + dy );

	if ( rpChat.dragging )
	{
		if ( !trap->Key_IsDown( A_MOUSE1 ) )
			rpChat.dragging = qfalse;	// its release went elsewhere
		else
			rpChat.cursor = RpChat_PosAt( rpChat.mouseX, rpChat.mouseY );
	}
}

// the colour in force where a line starts: the channel's, as the message will show in chat (G_SayTo
// puts it before the message), or the last colour code before it
static char RpChat_ColourAt( int pos )
{
	char colour = rpChatModes[rpChat.mode].colour;
	int i;

	for ( i = 0; i + 1 < pos; i++ )
	{
		if ( Q_IsColorString( &rpChat.text[i] ) )
		{
			colour = rpChat.text[i + 1];
			i++;
		}
	}
	return colour;
}

/*
================
CG_RpChatDraw

Every frame, last (CG_DrawActive): drawn over the HUD and the scoreboard, and also with cg_draw2D 0.
================
*/
void CG_RpChatDraw( void )
{
	char line[RPCHAT_MAX_SENT + 3];
	float y = RPCHAT_Y;
	int l, caretLine;

	if ( !rpChat.active )
		return;

	if ( !( trap->Key_GetCatcher() & KEYCATCH_CGAME ) )
	{ // Escape, or a menu that took the keyboard
		RpChat_Close();
		return;
	}

	RpChat_Refresh();

	if ( RpChat_HasSel() )
	{ // the selection's highlight, behind the text, line by line
		static const vec4_t selColour = { 0.25f, 0.45f, 0.9f, 0.45f };
		int a = Q_min( rpChat.anchor, rpChat.cursor ), b = Q_max( rpChat.anchor, rpChat.cursor );

		for ( l = 0; l < rpChat.numLines; l++ )
		{
			int from = rpChat.lineStart[l], to = rpChat.lineStart[l + 1];
			float x = RPCHAT_X + ( l ? 0 : rpChat.promptWidth );
			int x1, x2;

			if ( b <= from || a >= to )
				continue;

			x1 = RpChat_Width( from, Q_max( a, from ) );
			x2 = RpChat_Width( from, Q_min( b, to ) );
			if ( x2 > x1 )
				CG_FillRect( x + x1, y + l * RPCHAT_LINE_HEIGHT, x2 - x1, RPCHAT_LINE_HEIGHT, selColour );
		}
	}

	CG_Text_Paint( RPCHAT_X, y, RPCHAT_SCALE, colorWhite, rpChat.prompt, 0, 0, ITEM_TEXTSTYLE_OUTLINED, FONT_SMALL );

	caretLine = rpChat.numLines - 1;
	for ( l = 0; l < rpChat.numLines; l++ )
	{
		int from = rpChat.lineStart[l], to = rpChat.lineStart[l + 1];
		float x = RPCHAT_X + ( l ? 0 : rpChat.promptWidth );

		if ( to > from )
		{
			line[0] = Q_COLOR_ESCAPE;
			line[1] = RpChat_ColourAt( from );
			memcpy( line + 2, rpChat.text + from, to - from );
			line[2 + to - from] = '\0';
			CG_Text_Paint( x, y + l * RPCHAT_LINE_HEIGHT, RPCHAT_SCALE, colorWhite, line, 0, 0, ITEM_TEXTSTYLE_OUTLINED, FONT_SMALL );
		}

		if ( rpChat.cursor >= from && rpChat.cursor < to && caretLine == rpChat.numLines - 1 )
			caretLine = l;
	}

	if ( ( trap->Milliseconds() >> 8 ) & 1 )
	{
		int from = rpChat.lineStart[caretLine];
		float x = RPCHAT_X + ( caretLine ? 0 : rpChat.promptWidth ) + RpChat_Width( from, rpChat.cursor );

		CG_Text_Paint( x, y + caretLine * RPCHAT_LINE_HEIGHT, RPCHAT_SCALE, colorWhite, "_", 0, 0, ITEM_TEXTSTYLE_OUTLINED, FONT_SMALL );
	}

	// the mouse cursor, last: its tip is the point
	trap->R_SetColor( NULL );
	CG_DrawPic( rpChat.mouseX, rpChat.mouseY, RATIO_W( RPCHAT_CURSOR_SIZE ), RPCHAT_CURSOR_SIZE, cgs.media.rpChatCursor );
}
