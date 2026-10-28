/*
===========================================================================
GLX_U: [Dialogues] the dialogue menu's ui side: opening and closing ingame_rpdialog on the cgame's word
(UIMENU_RPDIALOG / UIMENU_RPDIALOG_CLOSE), the menu's scripts (rpDialogChoose <n>, rpDialogClosed), and the
keys 1-6 while the menu is focused. The text is in the ui_rpdlg_* cvars the cgame set (cg_rpdialog.c); the
answer goes to the server as "cmd dialog <n> <id>", two numbers built here, so a text can never become a
command.
===========================================================================
*/
#include "ui_local.h"

#define RPDLG_MENU		"ingame_rpdialog"
#define RPDLG_OPTIONS	6

static int UI_RpDialogId( void )
{
	return (int)trap->Cvar_VariableValue( "ui_rpdlg_id" );
}

// UIMENU_RPDIALOG: the menu, on top of whatever is open; without the menu file, the console
void UI_RpDialogOpen( void )
{
	if ( !Menus_ActivateByName( RPDLG_MENU ) )
	{
		char buf[256];
		int i, count = (int)trap->Cvar_VariableValue( "ui_rpdlg_count" );

		trap->Cvar_VariableStringBuffer( "ui_rpdlg_title", buf, sizeof( buf ) );
		if ( buf[0] )
			Com_Printf( "^3%s\n", buf );
		for ( i = 1; i <= 7; i++ )
		{
			trap->Cvar_VariableStringBuffer( va( "ui_rpdlg_line%d", i ), buf, sizeof( buf ) );
			if ( buf[0] )
				Com_Printf( "%s\n", buf );
		}
		for ( i = 1; i <= count && i <= RPDLG_OPTIONS; i++ )
		{
			trap->Cvar_VariableStringBuffer( va( "ui_rpdlg_opt%d", i ), buf, sizeof( buf ) );
			Com_Printf( "%s\n", buf );
		}
		Com_Printf( "^9Answer with /dialog <number> (the dialogue menu file is missing from this client).\n" );
		return;
	}
	trap->Key_SetCatcher( trap->Key_GetCatcher() | KEYCATCH_UI );
}

// UIMENU_RPDIALOG_CLOSE: the server ended it; the keyboard goes back when no menu is left
void UI_RpDialogClose( void )
{
	trap->Cvar_Set( "ui_rpdlg_id", "0" );	// so the menu's onClose sends nothing
	Menus_CloseByName( RPDLG_MENU );
	if ( !Menus_FocusAnyVisible() )
	{
		trap->Key_SetCatcher( trap->Key_GetCatcher() & ~KEYCATCH_UI );
		trap->Key_ClearStates();
	}
}

static void UI_RpDialogSend( int n, int id )
{
	trap->Cmd_ExecuteText( EXEC_APPEND, va( "cmd dialog %d %d\n", n, id ) );
}

// the menu's scripts: qtrue when the name was one of ours
qboolean UI_RpDialogScript( const char *name, char **args )
{
	if ( !Q_stricmp( name, "rpDialogChoose" ) )
	{
		int n = 0, id = UI_RpDialogId();

		Int_Parse( args, &n );
		trap->Cvar_Set( "ui_rpdlg_id", "0" );	// the following close must not send a cancel
		if ( id > 0 && n >= 1 && n <= RPDLG_OPTIONS && n <= (int)trap->Cvar_VariableValue( "ui_rpdlg_count" ) )
			UI_RpDialogSend( n, id );
		return qtrue;
	}
	if ( !Q_stricmp( name, "rpDialogClosed" ) )
	{ // onClose: Esc, or any other way the menu went, while a dialogue is still open: walk away
		int id = UI_RpDialogId();

		if ( id > 0 )
		{
			trap->Cvar_Set( "ui_rpdlg_id", "0" );
			UI_RpDialogSend( 0, id );
		}
		return qtrue;
	}
	return qfalse;
}

// 1-6 and keypad 1-6 while the dialogue menu is focused run that option's item, as a click would;
// a number past the count does nothing. qtrue when the key was taken.
qboolean UI_RpDialogKey( menuDef_t *menu, int key )
{
	int n;
	itemDef_t *item;

	if ( !menu || !menu->window.name || Q_stricmp( menu->window.name, RPDLG_MENU ) )
		return qfalse;
	if ( key >= A_1 && key <= A_6 )
		n = key - A_1 + 1;
	else if ( key >= A_KP_1 && key <= A_KP_6 )
		n = key - A_KP_1 + 1;
	else
		return qfalse;
	if ( n > (int)trap->Cvar_VariableValue( "ui_rpdlg_count" ) )
		return qtrue;
	item = Menu_FindItemByName( menu, va( "rpdlg_opt%d", n ) );
	if ( item && item->action )
		Item_RunScript( item, item->action );
	return qtrue;
}
