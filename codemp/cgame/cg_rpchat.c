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
- Not here (yet): paste, a history of sent messages, the Insert key's overwrite mode; and turning
  the view with the mouse while typing (with the keyboard caught, the mouse goes to the cgame).
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
#define RPCHAT_WIDTH		550				// and its line width (CHATBOX_CUTOFF_LEN)
#define RPCHAT_SCALE		0.65f			// and font size
#define RPCHAT_LINE_HEIGHT	( 20 * RPCHAT_SCALE )	// CHATBOX_FONT_HEIGHT at that size
#define RPCHAT_MAX_LINES	64

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

	// the layout, redone after every change
	qboolean		dirty;
	int				promptWidth;
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
		RpChat_Close();	// empty text

	rpChat.active = qtrue;
	rpChat.mode = (rpChatMode_t)mode;
	rpChat.target = target;
	rpChat.dirty = qtrue;

	trap->Key_SetCatcher( trap->Key_GetCatcher() | KEYCATCH_CGAME );
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
	rpChat.dirty = qtrue;
}

static void RpChat_Delete( int at )
{
	if ( at < 0 || at >= rpChat.len )
		return;

	rpChat.sentLen -= ( rpChat.text[at] == '"' ) ? 2 : 1;
	memmove( rpChat.text + at, rpChat.text + at + 1, rpChat.len - at );
	rpChat.len--;
	rpChat.dirty = qtrue;
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

/*
================
CG_RpChatKey

From CG_KeyEvent while the box is open. Characters as Field_CharEvent takes them, keys as
Field_KeyDownEvent and Message_Key do.
================
*/
void CG_RpChatKey( int key, qboolean down )
{
	if ( !down )
		return;

	if ( key & K_CHAR_FLAG )
	{
		int ch = key & ~K_CHAR_FLAG;

		if ( ch == 'h' - 'a' + 1 )
		{ // backspace
			if ( rpChat.cursor > 0 )
			{
				rpChat.cursor--;
				RpChat_Delete( rpChat.cursor );
			}
		}
		else if ( ch == 'a' - 'a' + 1 )
			rpChat.cursor = 0;
		else if ( ch == 'e' - 'a' + 1 )
			rpChat.cursor = rpChat.len;
		else if ( ch >= 32 && ch != 127 && ch <= 255 )
			RpChat_Insert( ch );
		return;
	}

	switch ( key )
	{
	case A_ENTER:
	case A_KP_ENTER:
		RpChat_Send();
		RpChat_Close();
		trap->Key_SetCatcher( trap->Key_GetCatcher() & ~KEYCATCH_CGAME );
		break;
	case A_DELETE:
		RpChat_Delete( rpChat.cursor );
		break;
	case A_CURSOR_LEFT:
		if ( rpChat.cursor > 0 )
			rpChat.cursor--;
		break;
	case A_CURSOR_RIGHT:
		if ( rpChat.cursor < rpChat.len )
			rpChat.cursor++;
		break;
	case A_HOME:
		rpChat.cursor = 0;
		break;
	case A_END:
		rpChat.cursor = rpChat.len;
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
RpChat_Layout

Where each line starts. A line is RPCHAT_WIDTH wide, the first less the prompt, and is broken after
its last space; a word longer than a line is broken where it runs out, but never inside a colour code.
================
*/
static void RpChat_Layout( void )
{
	int start = 0, i = 0;
	int avail = RPCHAT_WIDTH - rpChat.promptWidth;

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
			avail = RPCHAT_WIDTH;
			i = start;
			continue;
		}
		i++;
	}

	rpChat.lineStart[rpChat.numLines] = rpChat.len;
	rpChat.dirty = qfalse;
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

	rpChat.promptWidth = CG_Text_Width( rpChat.prompt, RPCHAT_SCALE, FONT_SMALL );
	if ( rpChat.dirty )
		RpChat_Layout();

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
}
