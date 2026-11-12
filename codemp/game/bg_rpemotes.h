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

// GLX_U: [RP Anims] 208: the StoryForce and JKG animations' 60 names and cuffback added (cuffed is BOTH_CUFFS now)
#define MAX_WORDED_EMOTES 208	// GLX_U: [Anim] 13 of JA++'s emote animations and 2 style victories added
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
/*
GLX_U: [RP Anims] Body's names are split into Social, Sit, Work, Leisure and Hurt (Body keeps the kneels, leans
and stand). cuffed plays BOTH_CUFFS (cuffed, hands in front); its old BOTH_STAND4 (hands behind the back) is
cuffback. tests/gla/t_animnames.py checks the list against anims.h, animation.cfg and the pages below.
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
	{"bartend",			BOTH_BARTEND,				"Work"		},
	{"beckon",			BOTH_COMEHERE,				"Social"	},
	{"beckon2",			BOTH_SNEAKHERE,				"Social"	},
	{"beg",				BOTH_KNEES1,				"Hurt"		},
	{"beg2",			BOTH_DEATH14_SITUP,			"Hurt"		},
	{"beg3",			BOTH_CHOKE2,				"Hurt"		},
	{"beg4",			BOTH_KNEES2,				"Hurt"		},
	{"bow",				BOTH_BOW,					"Social"	},
	{"breakdance",		BOTH_FORCE_GETUP_B4,		"Leisure"	},
	{"breakdance2",		BOTH_FORCE_GETUP_B6,		"Leisure"	},
	{"bump",			BOTH_FISTBUMP,				"Social"	},
	{"carry",			BOTH_CARRY,					"Movement"	},
	{"choked",			BOTH_CHOKE3,				"Hurt"		},
	{"commlinkdown",	BOTH_TALKCOMM1STOP,			"Work"		},
	{"commlinkup",		BOTH_TALKCOMM1START,		"Work"		},
	{"console",			BOTH_CONSOLETHINK,			"Work"		},
	{"console2",		BOTH_CROUCH_CONSOLE,		"Work"		},
	{"cover",			BOTH_DODGE_HOLD_FL,			"Movement"	},
	{"cower",			BOTH_COWERFLOOR,			"Hurt"		},
	{"cross",			BOTH_ARMSCROSSED,			"Social"	},
	{"cuffback",		BOTH_STAND4,				"Hurt"		},
	{"cuffed",			BOTH_CUFFS,					"Hurt"		},
	{"cufffront",		BOTH_CUFFEDFRONT,			"Hurt"		},
	{"cuffknees",		BOTH_CUFFEDKNEES,			"Hurt"		},
	{"cup",				BOTH_COFFEE_IDLE,			"Leisure"	},
	{"cupsip",			BOTH_COFFEE_SIP,			"Leisure"	},
	{"dance1",			BOTH_TURNSTAND1,			"Leisure"	},
	{"dance2",			BOTH_TURNSTAND4,			"Leisure"	},
	{"dance3",			BOTH_TURNSTAND5,			"Leisure"	},
	{"dance4",			BOTH_DANCE1,				"Leisure"	},
	{"dance5",			BOTH_DANCE2,				"Leisure"	},
	{"dance6",			BOTH_DANCE5,				"Leisure"	},
	{"dance7",			BOTH_DANCE6,				"Leisure"	},
	{"datapad",			BOTH_DATAPAD,				"Work"		},
	{"datapad2",		BOTH_DATAPAD2,				"Work"		},
	{"datapad3",		BOTH_DATAPADREAD,			"Work"		},
	{"datapad4",		BOTH_DATAPADUSE,			"Work"		},
	{"die",				BOTH_DEATH14_UNGRIP,		"Hurt"		},
	{"djemso",			BOTH_DJEMSO,				"Saber"		},
	{"djemso2",			BOTH_DJEMSO2,				"Saber"		},
	{"drainloop",		BOTH_FORCE_DRAIN_GRAB_HOLD,	"Force"		},
	{"drink",			BOTH_COFFEE_SIP,			"Leisure"	},
	{"examine",			BOTH_EXAMINE,				"Work"		},
	{"examineend",		BOTH_EXAMINEEND,			"Work"		},
	{"examinestart",	BOTH_EXAMINESTART,			"Work"		},
	{"exercise",		BOTH_EXERCISE,				"Leisure"	},
	{"facepalm",		BOTH_FACEPALM,				"Social"	},
	{"facepalm2",		BOTH_FACEPALM2,				"Social"	},
	{"fear",			BOTH_SONICPAIN_HOLD,		"Social"	},
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
	{"gaming",			BOTH_GAMING,				"Leisure"	},
	{"guard",			BOTH_GUARD,					"Saber"		},
	{"gunspin1",		BOTH_GUNSPINB,				"Blaster"	},
	{"gunspin2",		BOTH_GUNSPINF,				"Blaster"	},
	{"gunspin3",		BOTH_GUNSPINS,				"Blaster"	},
	{"handsback",		BOTH_HANDSBACK,				"Hurt"		},
	{"handsfront",		BOTH_HANDSFRONT,			"Hurt"		},
	{"handstand",		BOTH_HANDSTAND,				"Leisure"	},
	{"handstand2",		BOTH_HANDSTAND2,			"Leisure"	},
	{"headhold",		BOTH_HANDSHEAD,				"Hurt"		},
	{"helpedup",		BOTH_HELPEDUP,				"Social"	},
	{"helpup",			BOTH_HELPUP,				"Social"	},
	{"heroic",			BOTH_STAND5TOSTAND8,		"Social"	},
	{"hips",			BOTH_HANDSHIPS,				"Social"	},
	{"hips2",			BOTH_HANDSHIPS2,			"Social"	},
	{"holddetonator",	TORSO_WEAPONREADY10,		"Work"		},
	{"holdobject",		BOTH_OBJECT,				"Work"		},
	{"holo",			BOTH_HOLOMESSAGEIDLE,		"Work"		},
	{"holodown",		BOTH_HOLOMESSAGEEND,		"Work"		},
	{"holoup",			BOTH_HOLOMESSAGE,			"Work"		},
	{"hug",				BOTH_HUGGER1,				"Social"	},
	{"hurt",			BOTH_HURT,					"Hurt"		},
	{"hurt2",			BOTH_HURT2,					"Hurt"		},
	{"hurtarm",			BOTH_HURTARM,				"Hurt"		},
	{"hurtfloor",		BOTH_HURTFLAT,				"Hurt"		},
	{"hurtleg",			BOTH_HURTLEG,				"Hurt"		},
	{"idle",			BOTH_SABERIDLE,				"Saber"		},
	{"injured",			BOTH_INJURED,				"Hurt"		},
	{"jarkai",			BOTH_JARKAI,				"Saber"		},
	{"jarkai2",			BOTH_JARKAIREVERSE,			"Saber"		},
	{"juyo",			BOTH_JUYO,					"Saber"		},
	{"kneel",			BOTH_CROUCH3,				"Body"		},
	{"kneel2",			BOTH_KNEEL_TO_STAND,		"Body"		},	// GLX_U: [Anim] it was spreadlegs
	{"kneel3",			BOTH_KNEEL4,				"Body"		},
	{"kneelreach",		BOTH_KNEELREACH,			"Body"		},
	{"kneeup",			BOTH_KNEEUP,				"Body"		},
	{"laugh",			BOTH_LAUGH,					"Social"	},
	{"lean",			BOTH_STAND10,				"Body"		},
	{"leanback",		BOTH_LEANWALL,				"Body"		},
	{"leanfront",		BOTH_LEANFRONT,				"Body"		},
	{"leantable",		BOTH_STAND7TOSTAND8,		"Body"		},
	{"lookaround",		BOTH_LOOKAROUND,			"Social"	},
	{"makashi",			BOTH_MAKASHI,				"Saber"		},
	{"meditate",		BOTH_STAND5TOSIT2,			"Sit"		},
	{"meditate2",		BOTH_MEDITATION2,			"Force"		},
	{"meditate3",		BOTH_MEDITATEFORCE,			"Force"		},
	{"medpac",			BOTH_USEMED,				"Work"		},
	{"mindtrick",		BOTH_MINDTRICK1,			"Force"		},
	{"music",			BOTH_MUSICLISTEN,			"Leisure"	},
	{"niman",			BOTH_NIMAN,					"Saber"		},
	{"opencrate",		BOTH_OPENCRATE,				"Work"		},
	{"piano",			BOTH_PIANO,					"Leisure"	},
	{"pistol",			BOTH_PISTOLREADY,			"Blaster"	},
	{"plantbomb",		BOTH_PLANTBOMBIDLE,			"Work"		},
	{"plantbombend",	BOTH_PLANTBOMBFINISH,		"Work"		},
	{"plantbombstart",	BOTH_PLANTBOMBSTART,		"Work"		},
	{"point",			BOTH_STAND5TOAIM,			"Social"	},
	{"ponder",			BOTH_PONDER,				"Social"	},
	{"ponder2",			BOTH_PONDER2,				"Social"	},
	{"power",			BOTH_FORCE_GETUP_F2,		"Force"		},
	{"pressbutton",		BOTH_BUTTON_HOLD,			"Work"		},
	{"punchbag",		BOTH_PUNCHBAG,				"Leisure"	},
	{"pushup",			BOTH_PUSHUP,				"Leisure"	},
	{"pushup2",			BOTH_PUSHUPS,				"Leisure"	},
	{"quickdraw",		BOTH_QUICKDRAW,				"Blaster"	},
	{"quickdraw2",		BOTH_QUICKDRAW2,			"Blaster"	},
	{"read",			BOTH_READ,					"Work"		},
	{"relax",			BOTH_LAYDOWN,				"Sit"		},
	{"rummage",			BOTH_RUMMAGE,				"Work"		},
	{"saberdraw1",		BOTH_SABERDRAW1,			"Saber"		},
	{"saberdraw2",		BOTH_SABERDRAW2,			"Saber"		},
	{"saberdraw3",		BOTH_SABERDRAW3,			"Saber"		},
	{"saberdraw4",		BOTH_SABERDRAW4,			"Saber"		},
	{"saberdraw5",		BOTH_SABERDRAW5,			"Saber"		},
	{"saberpoint",		BOTH_SABERPOINT,			"Saber"		},
	{"saberpoint2",		BOTH_SABERPOINT2,			"Saber"		},
	{"saberthrow",		BOTH_SABERTHROW1START,		"Saber"		},
	{"salute",			BOTH_SALUTE,				"Social"	},
	{"scared",			BOTH_SCARED,				"Social"	},
	{"scratch",			BOTH_HEADSCRATCH,			"Social"	},
	{"shien",			BOTH_SHIEN,					"Saber"		},
	{"shien2",			BOTH_SHIEN2,				"Saber"		},
	{"shien3",			BOTH_SHIEN3,				"Saber"		},
	{"shiicho",			BOTH_SHIICHO,				"Saber"		},
	{"shrug",			BOTH_SHRUG,					"Social"	},
	{"sit",				BOTH_SIT2,					"Sit"		},
	{"sit2",			BOTH_SIT3,					"Sit"		},
	{"sit3",			BOTH_SIT6,					"Sit"		},
	{"sit4",			BOTH_SITARMS,				"Sit"		},
	{"sit5",			BOTH_SITCROSS,				"Sit"		},
	{"sit6",			BOTH_SITCROSS2,				"Sit"		},
	{"sit7",			BOTH_SITLEAN,				"Sit"		},
	{"sit8",			BOTH_SIT1,					"Sit"		},
	{"sit9",			BOTH_SIT4,					"Sit"		},
	{"sitfeet",			BOTH_SITFEET,				"Sit"		},
	{"sitpalm",			BOTH_SITPALM,				"Sit"		},
	{"sitpalm2",		BOTH_SITPALM2,				"Sit"		},
	{"sitpilot",		BOTH_GUNSIT1,				"Sit"		},
	{"sitpilot2",		BOTH_COCKPIT,				"Sit"		},
	{"sitpilot3",		BOTH_COCKPIT2,				"Sit"		},
	{"situp",			BOTH_SITUP,					"Sit"		},
	{"sleep",			BOTH_SLEEP1,				"Sit"		},
	{"sneak",			BOTH_CROUCH1,				"Movement"	},
	{"soresu",			BOTH_SORESU,				"Saber"		},
	{"soresu2",			BOTH_SORESU2,				"Saber"		},
	{"sorry",			BOTH_SORRY,					"Social"	},
	{"stance",			BOTH_SABERSTANCE,			"Saber"		},
	{"stance2",			BOTH_SABERSTANCE2,			"Saber"		},
	{"stance3",			BOTH_SABERSTANCE3,			"Saber"		},
	{"stand",			BOTH_STAND8,				"Body"		},
	{"startle",			BOTH_STARTLE,				"Social"	},
	{"suggest",			BOTH_STAND1_TALK3,			"Social"	},
	{"surrender",		TORSO_SURRENDER_START,		"Hurt"		},
	{"talk",			BOTH_TALK4,					"Social"	},
	{"talk2",			BOTH_TALK5,					"Social"	},
	{"talk3",			BOTH_TALK6,					"Social"	},
	{"talkbig",			BOTH_TALKEXAGGERATED1,		"Social"	},
	{"talkbig2",		BOTH_TALKEXAGGERATED2,		"Social"	},
	{"torch",			BOTH_TORCH,					"Work"		},
	{"torchdown",		BOTH_TORCHINSPECTDOWN,		"Work"		},
	{"torchup",			BOTH_TORCHINSPECTUP,		"Work"		},
	{"tossitem",		BOTH_TOSSITEM,				"Work"		},
	{"tossleft",		BOTH_TOSS1,					"Force"		},
	{"tossright",		BOTH_TOSS2,					"Force"		},
	{"type",			BOTH_CONSOLE1,				"Work"		},
	{"type2",			BOTH_CODE,					"Work"		},
	{"victory",			BOTH_TAVION_SCEPTERGROUND,	"Saber"		},	// GLX_U: [Anim] JA++'s (it was BOTH_WINGS_CLOSE, which has no humanoid frames)
	{"victory2",		BOTH_VICTORY_FAST,			"Saber"		},	// GLX_U: [Anim] the style victories (it was Desann's death, still /anim die)
	{"victory3",		BOTH_VICTORY_MEDIUM,		"Saber"		},	// GLX_U: [Anim] (it was Tavion's sit-up, still /anim beg2)
	{"victory4",		BOTH_VICTORY_STAFF,			"Saber"		},
	{"victory5",		BOTH_VICTORY_STRONG,		"Saber"		},
	{"victory6",		BOTH_VICTORY_DUAL,			"Saber"		},
	{"violin",			BOTH_VIOLIN,				"Leisure"	},
	{"wave",			BOTH_SILENCEGESTURE1,		"Social"	},
	{"whisper",			BOTH_WHISPER,				"Social"	},
	{"windy",			BOTH_WIND,					"Movement"	},
	{"won",				TORSO_HANDSIGNAL1,			"Social"	},
	{"wristcomm",		BOTH_WRISTCHATIDLE,			"Work"		},
	{"wristcommdown",	BOTH_WRISTCHATEND,			"Work"		},
	{"wristcommup",		BOTH_WRISTCHAT,				"Work"		},
};

#define MAX_EMOTE_CATEGORIES 10	// GLX_U: [RP Anims] Social, Sit, Work, Leisure and Hurt added

static const char anim_headers[MAX_EMOTE_CATEGORIES][50] UNUSED_VAR = {
	"Body",
	"Social",
	"Sit",
	"Work",
	"Leisure",
	"Hurt",
	"Movement",
	"Blaster",
	"Saber",
	"Force"
};

// GLX_U: [RP Anims] /anim list's pages: page n lists the categories anim_list_pages[n-1] to anim_list_pages[n]-1.
// Every row is a server command, and a client is dropped past 128 unacknowledged ones, so a page keeps to about
// 100 (rows, 3 per heading, the closing line and the hint; tests/gla/t_animnames.py checks it)
#define ANIM_LIST_PAGES 3

static const int anim_list_pages[ANIM_LIST_PAGES + 1] UNUSED_VAR = { 0, 3, 6, MAX_EMOTE_CATEGORIES };

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
