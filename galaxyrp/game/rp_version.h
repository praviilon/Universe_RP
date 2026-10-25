/*
=========================== GalaxyRP Mod ============================
Project based on OpenJK and Zyk Mod. Work copyrighted (C) 2020 - 2022
=====================================================================
[Description]: Main version header
=====================================================================
*/

#ifndef __RP_VERSION_H__
#define __RP_VERSION_H__

#define VAL(x) #x
#define STR(x) VAL(x)

#ifdef _DEBUG
    #define VER(x) "(debug)" x
#else
    #define VER(x) x
#endif

// Current version of the multi player game
#define VERSION_MAJOR_RELEASE		3
#define VERSION_MINOR_RELEASE		8
#define VERSION_BUGFIX_RELEASE		7
#define VERSION_EXTERNAL_BUILD		0
#define VERSION_INTERNAL_BUILD		0

#define VERSION_STRING STR(VERSION_MAJOR_RELEASE) ", " STR(VERSION_MINOR_RELEASE) // "a, b"
#define VERSION_STRING_DOTTED STR(VERSION_MAJOR_RELEASE) "." STR(VERSION_MINOR_RELEASE) "." STR(VERSION_BUGFIX_RELEASE) // "a.b.c"

// GLX_U: [Branding] the mod's name, folder and gamename tag -- the only place they are written.
#define RP_MOD_NAME		"Universe RP"		// player-facing name; plain text (no colour codes: /enthelp text forbids '^')
#define RP_MOD_FOLDER	"UniverseRP"		// the mod folder (fs_game) and the root of the server's data files; CMake reads it too
#define RP_MOD_TAG		"UniverseRP Mod"	// how JK_VERSION / the serverinfo gamename starts; cgame's mod check looks for it

// Set mod version definitions
#define	JK_VERSION                  VER(RP_MOD_TAG " v" VERSION_STRING_DOTTED)
#define JK_VERSION_OLD              VER("JAmp: v" VERSION_STRING_DOTTED)
// DAJ_RP: the address shown in two centre-prints ("Incomplete mod detected", the plugin mismatch) whose
// lines break every 50 characters and only at spaces -- so short, and without "https://", to stay on one line
// GLX_U: [Website] the website, whose download page picks the right files for the player's system
#define JK_URL                      "praviilon.github.io/Universe_RP"

// Public version
#define GAMEVERSION			        JK_VERSION

#endif //__RP_VERSION_H__
