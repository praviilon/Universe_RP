/*
===========================================================================
GLX_U: [Emotes Menu] the names /emote and /anim take, shared by the server (g_cmds.c: Cmd_Emote_f(), Cmd_Anim_f())
and the Emotes menu of the Extras (ui_rpemotes.c lists them, cg_rpextras.c checks a name against them before it
sends the command), so the menu lists exactly what the server takes. Moved here unchanged from g_cmds.c; static
(UNUSED_VAR: a file may use only some of them). Include after bg_public.h (the animation numbers, anims.h).
===========================================================================
*/

#ifndef BG_RPEMOTES_H
#define BG_RPEMOTES_H

#define MAX_WORDED_EMOTES 147	// GLX_U: [Anim] 13 of JA++'s emote animations and 2 style victories added
//alex: type for storing worde animations wo use with the emote system
typedef struct worded_animation_s {
	const char* animation_name;
	int			animation_code;
	const char* animation_category;
} worded_animation_t;

/*
alex: list of all worded emotes, ids, and categories, these are to be stored alphabetically
the animation code is to be exactly the same as the correcponding id in anims.h
*/
static const worded_animation_t animations[MAX_WORDED_EMOTES] UNUSED_VAR = {
	{"aim",				BOTH_STAND4TOATTACK2,		"Blaster"	},
	{"aim2",			BOTH_STAND5TOAIM,			"Blaster"	},
	{"aim3",			BOTH_ATTACK2,				"Blaster"	},
	{"aim4",			TORSO_WEAPONIDLE4,			"Blaster"	},
	{"aim5",			TORSO_WEAPONIDLE3,			"Blaster"	},
	{"anikata",			BOTH_ANAKINKATA,			"Saber"		},
	{"anikata2",		BOTH_ANAKINKATA2,			"Saber"		},
	{"anikata3",		BOTH_ANAKINKATA3,			"Saber"		},
	{"ataru",			BOTH_ATARU,					"Saber"		},
	{"beg",				BOTH_KNEES1,				"Body"		},
	{"beg2",			BOTH_DEATH14_SITUP,			"Body"		},
	{"beg3",			BOTH_CHOKE2,				"Body"		},
	{"beg4",			BOTH_KNEES2,				"Body"		},
	{"bow",				BOTH_BOW,					"Body"		},
	{"breakdance",		BOTH_FORCE_GETUP_B4,		"Body"		},
	{"breakdance2",		BOTH_FORCE_GETUP_B6,		"Body"		},
	{"bump",			BOTH_FISTBUMP,				"Body"		},
	{"carry",			BOTH_CARRY,					"Movement"	},
	{"choked",			BOTH_CHOKE3,				"Body"		},
	{"commlinkdown",	BOTH_TALKCOMM1STOP,			"Body"		},
	{"commlinkup",		BOTH_TALKCOMM1START,		"Body"		},
	{"cover",			BOTH_DODGE_HOLD_FL,			"Movement"	},
	{"cross",			BOTH_ARMSCROSSED,			"Body"		},
	{"cuffed",			BOTH_STAND4,				"Body"		},
	{"cufffront",		BOTH_CUFFEDFRONT,			"Body"		},
	{"cuffknees",		BOTH_CUFFEDKNEES,			"Body"		},
	{"cup",				BOTH_COFFEE_IDLE,			"Body"		},
	{"cupsip",			BOTH_COFFEE_SIP,			"Body"		},
	{"dance1",			BOTH_TURNSTAND1,			"Body"		},
	{"dance2",			BOTH_TURNSTAND4,			"Body"		},
	{"dance3",			BOTH_TURNSTAND5,			"Body"		},
	{"datapad",			BOTH_DATAPAD,				"Body"		},
	{"datapad2",		BOTH_DATAPAD2,				"Body"		},
	{"die",				BOTH_DEATH14_UNGRIP,		"Body"		},
	{"djemso",			BOTH_DJEMSO,				"Saber"		},
	{"djemso2",			BOTH_DJEMSO2,				"Saber"		},
	{"drainloop",		BOTH_FORCE_DRAIN_GRAB_HOLD,	"Force"		},
	{"drink",			BOTH_COFFEE_SIP,			"Body"		},
	{"facepalm",		BOTH_FACEPALM,				"Body"		},
	{"facepalm2",		BOTH_FACEPALM2,				"Body"		},
	{"fear",			BOTH_SONICPAIN_HOLD,		"Body"		},
	{"finishinghim",	BOTH_SABERKILLER1,			"Saber"		},
	{"flourish",		BOTH_SHOWOFF_FAST,			"Saber"		},
	{"flourish2",		BOTH_SHOWOFF_MEDIUM,		"Saber"		},
	{"flourish3",		BOTH_SHOWOFF_STRONG,		"Saber"		},
	{"flourish4",		BOTH_SHOWOFF_DUAL,			"Saber"		},
	{"flourish5",		BOTH_SHOWOFF_STAFF,			"Saber"		},
	{"force",			BOTH_USEFORCE,				"Force"		},
	{"forcecasual",		BOTH_FORCECASUAL,			"Force"		},
	{"forcechoke",		BOTH_FORCEGRIP3,			"Force"		},
	{"forcelightning",	BOTH_FORCELIGHTNING_HOLD,	"Force"		},
	{"guard",			BOTH_GUARD,					"Saber"		},
	{"gunspin1",		BOTH_GUNSPINB,				"Blaster"	},
	{"gunspin2",		BOTH_GUNSPINF,				"Blaster"	},
	{"gunspin3",		BOTH_GUNSPINS,				"Blaster"	},
	{"handsback",		BOTH_HANDSBACK,				"Body"		},
	{"handsfront",		BOTH_HANDSFRONT,			"Body"		},
	{"handstand",		BOTH_HANDSTAND,				"Body"		},
	{"handstand2",		BOTH_HANDSTAND2,			"Body"		},
	{"headhold",		BOTH_HANDSHEAD,				"Body"		},
	{"helpedup",		BOTH_HELPEDUP,				"Body"		},
	{"helpup",			BOTH_HELPUP,				"Body"		},
	{"heroic",			BOTH_STAND5TOSTAND8,		"Body"		},
	{"hips",			BOTH_HANDSHIPS,				"Body"		},
	{"hips2",			BOTH_HANDSHIPS2,			"Body"		},
	{"holddetonator",	TORSO_WEAPONREADY10,		"Body"		},
	{"holdobject",		BOTH_OBJECT,				"Body"		},
	{"hug",				BOTH_HUGGER1,				"Body"		},
	{"hurt",			BOTH_HURT,					"Body"		},
	{"hurt2",			BOTH_HURT2,					"Body"		},
	{"idle",			BOTH_SABERIDLE,				"Saber"		},
	{"jarkai",			BOTH_JARKAI,				"Saber"		},
	{"jarkai2",			BOTH_JARKAIREVERSE,			"Saber"		},
	{"juyo",			BOTH_JUYO,					"Saber"		},
	{"kneel",			BOTH_CROUCH3,				"Body"		},
	{"kneel2",			BOTH_KNEEL_TO_STAND,		"Body"		},	// GLX_U: [Anim] it was spreadlegs
	{"lean",			BOTH_STAND10,				"Body"		},
	{"leanback",		BOTH_LEANWALL,				"Body"		},
	{"leanfront",		BOTH_LEANFRONT,				"Body"		},
	{"leantable",		BOTH_STAND7TOSTAND8,		"Body"		},
	{"makashi",			BOTH_MAKASHI,				"Saber"		},
	{"meditate",		BOTH_STAND5TOSIT2,			"Body"		},
	{"meditate2",		BOTH_MEDITATION2,			"Force"		},
	{"meditate3",		BOTH_MEDITATEFORCE,			"Force"		},
	{"mindtrick",		BOTH_MINDTRICK1,			"Force"		},
	{"niman",			BOTH_NIMAN,					"Saber"		},
	{"pistol",			BOTH_PISTOLREADY,			"Blaster"	},
	{"point",			BOTH_STAND5TOAIM,			"Body"		},
	{"ponder",			BOTH_PONDER,				"Body"		},
	{"ponder2",			BOTH_PONDER2,				"Body"		},
	{"power",			BOTH_FORCE_GETUP_F2,		"Force"		},
	{"pressbutton",		BOTH_BUTTON_HOLD,			"Body"		},
	{"pushup",			BOTH_PUSHUP,				"Body"		},
	{"quickdraw",		BOTH_QUICKDRAW,				"Blaster"	},
	{"quickdraw2",		BOTH_QUICKDRAW2,			"Blaster"	},
	{"read",			BOTH_READ,					"Body"		},
	{"relax",			BOTH_LAYDOWN,				"Body"		},
	{"saberdraw1",		BOTH_SABERDRAW1,			"Saber"		},
	{"saberdraw2",		BOTH_SABERDRAW2,			"Saber"		},
	{"saberdraw3",		BOTH_SABERDRAW3,			"Saber"		},
	{"saberdraw4",		BOTH_SABERDRAW4,			"Saber"		},
	{"saberdraw5",		BOTH_SABERDRAW5,			"Saber"		},
	{"saberpoint",		BOTH_SABERPOINT,			"Saber"		},
	{"saberpoint2",		BOTH_SABERPOINT2,			"Saber"		},
	{"saberthrow",		BOTH_SABERTHROW1START,		"Saber"		},
	{"salute",			BOTH_SALUTE,				"Body"		},
	{"scratch",			BOTH_HEADSCRATCH,			"Body"		},
	{"shien",			BOTH_SHIEN,					"Saber"		},
	{"shien2",			BOTH_SHIEN2,				"Saber"		},
	{"shien3",			BOTH_SHIEN3,				"Saber"		},
	{"shiicho",			BOTH_SHIICHO,				"Saber"		},
	{"sit",				BOTH_SIT2,					"Body"		},
	{"sit2",			BOTH_SIT3,					"Body"		},
	{"sit3",			BOTH_SIT6,					"Body"		},
	{"sit4",			BOTH_SITARMS,				"Body"		},
	{"sit5",			BOTH_SITCROSS,				"Body"		},
	{"sit6",			BOTH_SITCROSS2,				"Body"		},
	{"sit7",			BOTH_SITLEAN,				"Body"		},
	{"sit8",			BOTH_SIT1,					"Body"		},
	{"sit9",			BOTH_SIT4,					"Body"		},
	{"sitfeet",			BOTH_SITFEET,				"Body"		},
	{"sitpalm",			BOTH_SITPALM,				"Body"		},
	{"sitpalm2",		BOTH_SITPALM2,				"Body"		},
	{"sitpilot",		BOTH_GUNSIT1,				"Body"		},
	{"situp",			BOTH_SITUP,					"Body"		},
	{"sleep",			BOTH_SLEEP1,				"Body"		},
	{"sneak",			BOTH_CROUCH1,				"Movement"	},
	{"soresu",			BOTH_SORESU,				"Saber"		},
	{"soresu2",			BOTH_SORESU2,				"Saber"		},
	{"stance",			BOTH_SABERSTANCE,			"Saber"		},
	{"stance2",			BOTH_SABERSTANCE2,			"Saber"		},
	{"stance3",			BOTH_SABERSTANCE3,			"Saber"		},
	{"stand",			BOTH_STAND8,				"Body"		},
	{"suggest",			BOTH_STAND1_TALK3,			"Body"		},
	{"surrender",		TORSO_SURRENDER_START,		"Body"		},
	{"tossleft",		BOTH_TOSS1,					"Force"		},
	{"tossright",		BOTH_TOSS2,					"Force"		},
	{"type",			BOTH_CONSOLE1,				"Body"		},
	{"victory",			BOTH_TAVION_SCEPTERGROUND,	"Saber"		},	// GLX_U: [Anim] JA++'s (it was BOTH_WINGS_CLOSE, which has no humanoid frames)
	{"victory2",		BOTH_VICTORY_FAST,			"Saber"		},	// GLX_U: [Anim] the style victories (it was Desann's death, still /anim die)
	{"victory3",		BOTH_VICTORY_MEDIUM,		"Saber"		},	// GLX_U: [Anim] (it was Tavion's sit-up, still /anim beg2)
	{"victory4",		BOTH_VICTORY_STAFF,			"Saber"		},
	{"victory5",		BOTH_VICTORY_STRONG,		"Saber"		},
	{"victory6",		BOTH_VICTORY_DUAL,			"Saber"		},
	{"wave",			BOTH_SILENCEGESTURE1,		"Body"		},
	{"windy",			BOTH_WIND,					"Movement"	},
	{"won",				TORSO_HANDSIGNAL1,			"Body"		},
};

#define MAX_EMOTE_CATEGORIES 5

static const char anim_headers[MAX_EMOTE_CATEGORIES][50] UNUSED_VAR = {
	"Body",
	"Movement",
	"Blaster",
	"Saber",
	"Force"
};

// GLX_U: [Emote] /emote's emotes: see Cmd_Emote_f() in g_cmds.c for what the flags do
#define RP_EMF_STATIC	1
#define RP_EMF_HOLD		2
#define RP_EMF_HOLSTER	4

typedef struct {
	const char	*name;
	int			anim, leave;	// the first animation and the ending
	int			flags;
} rp_emote_t;

// JA++'s values (the /am* command each comes from)
static const rp_emote_t rpEmotes[] UNUSED_VAR = {
	{ "hiltthrow",	BOTH_SABERTHROW1START,	BOTH_SABERTHROW1STOP,	0 },											// amhiltthrow1
	{ "hiltthrow2",	BOTH_SABERTHROW2START,	BOTH_SABERTHROW2STOP,	0 },											// amhiltthrow2
	{ "cower",		BOTH_COWER1_START,		BOTH_COWER1,			RP_EMF_HOLSTER },								// amcower
	{ "die",		BOTH_HIT1,				BOTH_FORCE_GETUP_F2,	RP_EMF_STATIC|RP_EMF_HOLD },					// amdie
	{ "die2",		BOTH_DEATH15,			BOTH_FORCE_GETUP_F2,	RP_EMF_STATIC|RP_EMF_HOLD },					// amdie2
	{ "heal",		BOTH_FORCEHEAL_START,	BOTH_FORCEHEAL_STOP,	RP_EMF_STATIC|RP_EMF_HOLD|RP_EMF_HOLSTER },		// amheal
	{ "heroic",		BOTH_STAND5TOSTAND8,	BOTH_STAND8TOSTAND5,	RP_EMF_STATIC|RP_EMF_HOLD|RP_EMF_HOLSTER },		// amhips (as /anim heroic)
	{ "kneel",		BOTH_CROUCH3,			BOTH_UNCROUCH3,			RP_EMF_STATIC|RP_EMF_HOLD|RP_EMF_HOLSTER },		// amkneel
	{ "kneel2",		BOTH_ROSH_PAIN,			BOTH_UNCROUCH3,			RP_EMF_STATIC|RP_EMF_HOLD|RP_EMF_HOLSTER },		// amkneel2
	{ "fear",		BOTH_SONICPAIN_HOLD,	BOTH_SONICPAIN_END,		RP_EMF_HOLD },									// amnoisy
	{ "commlink",	BOTH_TALKCOMM1START,	BOTH_TALKCOMM1STOP,		RP_EMF_HOLD|RP_EMF_HOLSTER },					// amradio
	{ "sleep",		BOTH_SLEEP1,			BOTH_SLEEP1GETUP,		RP_EMF_STATIC|RP_EMF_HOLD|RP_EMF_HOLSTER },		// amasleep
	{ "surrender",	TORSO_SURRENDER_START,	TORSO_SURRENDER_STOP,	RP_EMF_HOLD|RP_EMF_HOLSTER },					// amsurrender
	{ "lean",		BOTH_STAND10,			BOTH_STAND10TOSTAND1,	RP_EMF_STATIC|RP_EMF_HOLD|RP_EMF_HOLSTER },		// amwait
};
#define RP_NUM_EMOTES	( (int)ARRAY_LEN( rpEmotes ) )

#endif // BG_RPEMOTES_H
