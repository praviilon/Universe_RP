/*
=========================== GalaxyRP Mod ============================
Project based on OpenJK and Zyk Mod. Work copyrighted (C) 2020 - 2022
=====================================================================
[Description]: Public header
=====================================================================
*/

#ifndef __RP_PUBLIC_H__
#define __RP_PUBLIC_H__

/*
=====================================================================
Global definitions
=====================================================================
*/

// Utilities
#define VALIDSTRINGCVAR(a)		((a != NULL) && (a[0] != '\0') && (*a != '0'))
#define ARRAY_CLEAR(a)			memset(a, 0, sizeof(a))

// GLX_U: [Flood Protection] the gap, in milliseconds, the engine's sv_floodProtect makes between two commands
// from one client. The game holds sv_floodProtect at this value (RP_CVU_floodProtect, g_cvar.c) and the
// client paces its own command runs by it (RPX_FloodGap, cg_rpextras.c), so the two always agree: change
// it here and both follow. 150 lets every menu answer through (a click is one command, and nobody clicks
// twice within 150 ms) while still holding one client to about seven commands a second.
#define RP_FLOOD_PROTECT_MS		150

/*
=====================================================================
Player / world information
=====================================================================
*/

#endif //__RP_PUBLIC_H__
