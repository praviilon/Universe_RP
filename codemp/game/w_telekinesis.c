/*
===========================================================================
GLX_U: [Telekinesis] the Grip skill's second power for logged-in players

A logged-in player with the Grip/Telekinesis skill picks one of its two powers with /settings 7 (player_settings
bit RP_SETTING_TELEKINESIS, Cmd_Settings_f in g_cmds.c). Grip is the default and is the stock power, untouched.
Telekinesis takes Grip's slot, key, start cost and 3 s cooldown, and holds its targets in the air without harm:
no damage, no choke, no crack, no grip sound, no hand effect. Logged-out players, NPCs and bots always grip.

  Level 1   lifts one target 64 units; drops it when the caster looks away (InFront, as grip below level 3),
            when it is out of range or out of sight.
  Level 2   as 1, and the target keeps its place relative to the caster, so walking carries it; turning
            does not swing it (and turning away drops it).
  Level 3   the target follows the aim in every direction, at the distance it was grabbed (64-224 units);
            only range and line of sight drop it. Push throws it (RP_TelekinesisThrow).
  Level 4   as 3, reaching 320 units (held at 64-288), and lifts what grip refuses: vehicles, Galak's mech,
            the AT-ST and the Rancor (RP_TelekinesisLiftsHeavy, asked by ForcePowerUsableOn).
  Level 5   as 4, reaching 450 units (held at 64-418), for up to RP_TK_MAX_TARGETS targets within 35 degrees
            of the aim; they keep their places around the aimed one and move as a group. Each breaks free on
            its own.

The reach to start (from the eyes) is 256/256/256/320/450 by level, and a target is dropped beyond
320/320/320/450/550 (feet to feet): rpTkReach, rpTkDrop. At level 3+ the hold point is that reach less 32
(rpTkHoldMax), so a held target stays well inside its drop distance.

Items too: weapons, ammo, powerups and holdables -- any item Force Push and Pull may move that is there to take
right now, but no team item (the flags). With no player, NPC or vehicle on the aim, the item nearest the aim line
(within RP_TK_ITEM_BAND of it, in reach and in sight) is taken; at level 5 items share the group with the others.
Every level holds one as it holds a player, steering it in a straight line (TR_LINEAR) to the hold point every
100 ms; let go or thrown, it falls, bounces and settles as a pushed one does (G_RunItem, which leaves a held item
alone: gentity_t::rpTkItemHeldUntil). Its own clock keeps running: a map item gets the push's return
(G_ReturnPushedItem) when taken and goes home when that runs out, a dropped one vanishes at its lifetime, and a
touch picks it up as usual -- each time the hold just lets it go, untouched. Another player's Push or Pull gives it
a jolt, as it gives a held player, and the next steer brings it back.

A hold lasts while the key is held and force lasts: the hand timer caps it at 10/20/30/40/60 s by level, and
it drains 1 force every 100/100/125/200/200 ms (RP_TelekinesisDrainInterval, in WP_ForcePowerRun). Being
gripped or lifted yourself ends it. Everything else is grip's: the busy hand (HANDEXTEND_FORCE_HOLD, so no
attack, no Push, Pull, Lightning, Drain or Mind Trick), the 0.4x walking speed, breaking free with Push or
Pull at a level at least the caster's (ForceThrow), Absorb's conversion, the target rules of ForcePowerUsableOn.

The hooks in w_force.c: ForceGrip() calls RP_TelekinesisGrab() after its own checks; WP_ForcePowerRun(FP_GRIP)
calls RP_TelekinesisUpdate() in place of DoGripAction(); WP_ForcePowerStop(FP_GRIP) calls RP_TelekinesisStopped()
first; ForceThrow() goes to RP_TelekinesisThrow() while holding, and its grip-escape also asks
RP_TelekinesisHolds(). ClientThink_real() (g_active.c) sets the STAT_RP_SELF_FX bits (bg_public.h) the shared
animation code reads, and plays the landing (RP_TelekinesisLanding). The targets' and the caster's animations
are the JKG set, 1534-1541 (anims.h); the caster's push and release are played on the torso after the power
stops, so the hand is cleared straight to HANDEXTEND_NONE there rather than through the weapon-raise.
===========================================================================
*/

#include "g_local.h"
#include "w_saber.h"

extern int WP_AbsorbConversion( gentity_t *attacked, int atdAbsLevel, gentity_t *attacker, int atPower, int atPowerLevel, int atForceSpent );
extern qboolean WP_ForcePowerAvailable( gentity_t *self, forcePowers_t forcePower, int overrideAmt );
extern qboolean WP_ForcePowerUsable( gentity_t *self, forcePowers_t forcePower );

#define RP_TK_LIFT_HEIGHT		64		// levels 1-2: how far above the grab point the target is held
#define RP_TK_MIN_DIST			64		// level 3+: the hold point is at least this far along the aim (at most rpTkHoldMax)
#define RP_TK_CONE				0.819f	// level 5: the extra targets are picked within 35 degrees of the aim (its cosine)
#define RP_TK_STEER_MS			100		// the targets' velocity is set this often
#define RP_TK_STEER_GAIN		6.0f	// velocity = distance to the hold point * this, per second
#define RP_TK_STEER_MAX			600.0f
#define RP_TK_STEER_DEAD		8.0f	// within this of the hold point the target is stopped
#define RP_TK_LIFT_ANIM_MS		1500	// the caster's BOTH_FORCELEVITATE, before BOTH_FORCELEVITATEIDLE
#define RP_TK_THROW_DELAY_MS	850		// Push: the targets fly this long into BOTH_FORCELEVITATEWAVE -- its forward thrust
										// (the hands swing back 0-0.6 s, thrust 0.7-0.9 s, fastest at 0.8)
#define RP_TK_LAND_WINDOW_MS	3000	// a released target landing within this plays BOTH_FORCELEVITATEDLAND
#define RP_TK_HELD_MS			200		// a target counts as held this long after its last update
#define RP_TK_ITEM_BAND			32.0f	// with nothing else on the aim, an item is taken within this of the aim line

static const int rpTkHoldCapMs[NUM_FORCE_POWER_LEVELS] = { 0, 10000, 20000, 30000, 40000, 60000 };
static const int rpTkDrainMs[NUM_FORCE_POWER_LEVELS] = { 100, 100, 100, 125, 200, 200 };
// by level: the reach to start, from the eyes (level 5's extra targets too; grip's is MAX_GRIP_DISTANCE, 256);
// a held target further than the drop distance from the caster (feet to feet) is let go; at level 3+ the hold
// point is at most rpTkHoldMax along the aim, the reach less 32 (levels 1-2 are not held along the aim)
static const int rpTkReach[NUM_FORCE_POWER_LEVELS] = { 256, 256, 256, 256, 320, 450 };
static const int rpTkDrop[NUM_FORCE_POWER_LEVELS] = { 320, 320, 320, 320, 450, 550 };
static const int rpTkHoldMax[NUM_FORCE_POWER_LEVELS] = { 224, 224, 224, 224, 288, 418 };

static int RP_TK_Level( const gentity_t *ent )
{
	int lvl = ent->client->ps.fd.forcePowerLevel[FP_GRIP];

	if ( lvl < FORCE_LEVEL_1 )
	{
		return FORCE_LEVEL_1;
	}
	if ( lvl >= NUM_FORCE_POWER_LEVELS )
	{
		return NUM_FORCE_POWER_LEVELS - 1;
	}
	return lvl;
}

// a logged-in player whose Grip skill is set to Telekinesis
qboolean RP_TelekinesisMode( const gentity_t *ent )
{
	if ( !ent || !ent->client || ent->s.number >= MAX_CLIENTS || (ent->r.svFlags & SVF_BOT) )
	{
		return qfalse;
	}
	if ( ent->client->sess.amrpgmode != 2 )
	{
		return qfalse;
	}
	if ( !(ent->client->pers.player_settings & (1 << RP_SETTING_TELEKINESIS)) )
	{
		return qfalse;
	}
	return ent->client->ps.fd.forcePowerLevel[FP_GRIP] >= FORCE_LEVEL_1;
}

// level 4+: may take hold of what RP_GripRefused() refuses to grip
qboolean RP_TelekinesisLiftsHeavy( const gentity_t *ent )
{
	return RP_TelekinesisMode( ent ) && ent->client->ps.fd.forcePowerLevel[FP_GRIP] >= FORCE_LEVEL_4;
}

qboolean RP_TelekinesisHolding( const gentity_t *ent )
{
	return ent && ent->client && ent->client->tk.num > 0;
}

qboolean RP_TelekinesisHolds( const gentity_t *holder, const gentity_t *ent )
{
	int i;

	if ( !holder || !holder->client || !ent )
	{
		return qfalse;
	}
	for ( i = 0; i < holder->client->tk.num; i++ )
	{
		if ( holder->client->tk.ents[i] == ent->s.number )
		{
			return qtrue;
		}
	}
	return qfalse;
}

// milliseconds per force point while the power runs: grip's 100, or the telekinesis level's
int RP_TelekinesisDrainInterval( const gentity_t *ent )
{
	if ( !RP_TelekinesisHolding( ent ) )
	{
		return 100;
	}
	return rpTkDrainMs[RP_TK_Level( ent )];
}

static qboolean RP_TK_IsVehicle( const gentity_t *ent )
{
	return ent->s.eType == ET_NPC && ent->s.NPC_class == CLASS_VEHICLE;
}

// where a target is: a client's origin, an item's (the bottom of its box)
static void RP_TK_Origin( const gentity_t *ent, vec3_t out )
{
	if ( ent->client )
	{
		VectorCopy( ent->client->ps.origin, out );
	}
	else
	{
		VectorCopy( ent->r.currentOrigin, out );
	}
}

// the point a target is aimed at and seen at: a client's origin, an item's middle
static void RP_TK_SeenPoint( const gentity_t *ent, vec3_t out )
{
	if ( ent->client )
	{
		VectorCopy( ent->client->ps.origin, out );
		return;
	}
	VectorAdd( ent->r.mins, ent->r.maxs, out );
	VectorMA( ent->r.currentOrigin, 0.5f, out, out );
}

// an item under a telekinesis hold right now: G_RunItem leaves its motion to the hold
qboolean RP_TelekinesisItemHeld( const gentity_t *ent )
{
	return ( ent->rpTkItemHolder && ent->rpTkItemHeldUntil > level.time ) ? qtrue : qfalse;
}

// an item self may take (starting) or keep holding: one Push and Pull may move (w_force.c) -- an item there to take
// right now, not a flag -- that an admin isn't holding (/entcopy, /entcut: rpHeldBy), and, starting, that no hold
// has; kept, that it is still ours. A freed or reused slot is zeroed, so it never passes for ours.
static qboolean RP_TK_ItemMovable( const gentity_t *self, const gentity_t *ent, qboolean starting )
{
	if ( !ent->inuse || ent->client || ent->s.eType != ET_ITEM || !ent->item || ent->item->giType == IT_TEAM )
	{
		return qfalse;
	}
	if ( (ent->s.eFlags & (EF_NODRAW | EF_ITEMPLACEHOLDER)) || !(ent->r.contents & CONTENTS_TRIGGER) || !ent->r.linked || ent->rpHeldBy )
	{	// taken and waiting on RespawnItem, still spawning, or an admin's
		return qfalse;
	}
	if ( starting )
	{
		return RP_TelekinesisItemHeld( ent ) ? qfalse : qtrue;
	}
	return ( ent->rpTkItemHolder == self->s.number + 1 ) ? qtrue : qfalse;
}

// a held item stays in the hold while it is movable and ours, and, for a map item, until the return it got when taken
// has run (G_ReturnPushedItem clears its think time and puts it home: it is then left where that put it)
static qboolean RP_TK_ItemKept( const gentity_t *self, const gentity_t *ent )
{
	if ( !RP_TK_ItemMovable( self, ent, qfalse ) )
	{
		return qfalse;
	}
	if ( !(ent->flags & FL_DROPPED_ITEM) && ( ent->think != G_ReturnPushedItem || !ent->nextthink ) )
	{
		return qfalse;
	}
	return qtrue;
}

// the hold mark off an item, if it is ours
static void RP_TK_ItemUnmark( const gentity_t *self, gentity_t *ent )
{
	if ( ent->rpTkItemHolder == self->s.number + 1 )
	{
		ent->rpTkItemHolder = 0;
		ent->rpTkItemHeldUntil = 0;
	}
}

// an item is taken: marked as ours, and a map item gets the return a push gives it, so it goes home in time
// whatever happens to it (G_ReturnPushedItem, G_ItemPushReturnTime); a dropped one keeps its lifetime
static void RP_TK_ItemTake( gentity_t *self, gentity_t *ent )
{
	ent->rpTkItemHolder = self->s.number + 1;
	ent->rpTkItemHeldUntil = level.time + RP_TK_HELD_MS;
	ent->physicsObject = qtrue;		// as a pushed item
	if ( !(ent->flags & FL_DROPPED_ITEM) )
	{
		ent->think = G_ReturnPushedItem;
		ent->nextthink = level.time + G_ItemPushReturnTime( ent ) * 1000;
	}
}

// steers a held item to point: a straight line from where it is, at the speed a held player gets
static void RP_TK_ItemSteer( gentity_t *ent, const vec3_t point )
{
	vec3_t a;
	float len;

	VectorSubtract( point, ent->r.currentOrigin, a );
	len = VectorLength( a );
	if ( len < RP_TK_STEER_DEAD )
	{
		VectorClear( a );
	}
	else
	{
		float v = len * RP_TK_STEER_GAIN;

		if ( v > RP_TK_STEER_MAX )
		{
			v = RP_TK_STEER_MAX;
		}
		VectorScale( a, v / len, a );
	}
	VectorCopy( ent->r.currentOrigin, ent->s.pos.trBase );
	VectorCopy( a, ent->s.pos.trDelta );
	ent->s.pos.trType = TR_LINEAR;
	ent->s.pos.trTime = level.time;
	ent->s.groundEntityNum = ENTITYNUM_NONE;
}

// lets a held item go from where it is, with the motion it has (or vel): it falls, bounces and settles as a pushed
// one does (G_RunItem). One that is no longer ours to hold (taken, gone, or sent home: RP_TK_ItemKept) only loses
// the mark, if it is still ours, and is otherwise left as it is.
static void RP_TK_ItemRelease( gentity_t *self, gentity_t *ent, const vec3_t vel )
{
	vec3_t v;

	if ( !RP_TK_ItemKept( self, ent ) )
	{
		RP_TK_ItemUnmark( self, ent );
		return;
	}
	RP_TK_ItemUnmark( self, ent );
	if ( vel )
	{
		VectorCopy( vel, v );
	}
	else
	{
		BG_EvaluateTrajectoryDelta( &ent->s.pos, level.time, v );
	}
	VectorCopy( ent->r.currentOrigin, ent->s.pos.trBase );
	VectorCopy( v, ent->s.pos.trDelta );
	ent->s.pos.trType = TR_GRAVITY;
	ent->s.pos.trTime = level.time;
	ent->s.groundEntityNum = ENTITYNUM_NONE;
	trap->LinkEntity( (sharedEntity_t *)ent );
}

// the player-on-a-vehicle rules, as Push's: a speeder or animal rider aimed at is thrown off and lifted
// alone; a walker or fighter pilot can't be lifted on his own -- at level 4+ his vehicle is lifted instead,
// rider aboard, below that nothing is. Returns what to lift, or NULL.
static gentity_t *RP_TK_Resolve( gentity_t *self, gentity_t *cand, int lvl )
{
	gentity_t *veh;

	if ( !cand->client->ps.m_iVehicleNum )
	{
		return cand;
	}
	veh = &g_entities[cand->client->ps.m_iVehicleNum];
	if ( !veh->inuse || !veh->client || !veh->m_pVehicle )
	{
		return cand;
	}
	if ( veh->m_pVehicle->m_pVehicleInfo->type == VH_SPEEDER || veh->m_pVehicle->m_pVehicleInfo->type == VH_ANIMAL )
	{
		veh->m_pVehicle->m_pVehicleInfo->Eject( veh->m_pVehicle, (bgEntity_t *)cand, qfalse );
		return cand;
	}
	return lvl >= FORCE_LEVEL_4 ? veh : NULL;
}

// may self take (or keep) hold of ent: grip's own rules, with the level 4+ exception made in ForcePowerUsableOn.
// A rider aimed at is resolved (RP_TK_Resolve) after this says he may be taken at all.
static qboolean RP_TK_CanHold( gentity_t *self, gentity_t *ent, qboolean starting )
{
	if ( !ent || !ent->inuse || !ent->client || ent == self || ent->health < 1 )
	{
		return qfalse;
	}
	if ( starting )
	{
		if ( ent->client->ps.fd.forceGripCripple || ent->client->ps.fd.forceGripBeingGripped >= level.time )
		{	// already held by someone (or by us): one hold at a time, as grip
			return qfalse;
		}
		if ( ent->client->ps.fd.forceGripEntityNum == self->s.number && (ent->client->ps.fd.forcePowersActive & (1 << FP_GRIP)) )
		{	// nor the one gripping us
			return qfalse;
		}
		if ( RP_TelekinesisHolds( ent, self ) )
		{
			return qfalse;
		}
	}
	if ( !ForcePowerUsableOn( self, ent, FP_GRIP ) )
	{
		return qfalse;
	}
	if ( !g_friendlyFire.integer && OnSameTeam( self, ent ) )
	{
		return qfalse;
	}
	return qtrue;
}

static void RP_TK_Eye( const gentity_t *self, vec3_t eye, vec3_t fwd )
{
	VectorCopy( self->client->ps.origin, eye );
	eye[2] += self->client->ps.viewheight;
	AngleVectors( self->client->ps.viewangles, fwd, NULL, NULL );
}

// can self see ent's origin (an item's middle) from his own: the world and other players block it, the held targets
// don't (an item never does: it is not solid)
static qboolean RP_TK_Visible( gentity_t *self, gentity_t *ent )
{
	trace_t tr;
	vec3_t p;

	RP_TK_SeenPoint( ent, p );
	trap->Trace( &tr, self->client->ps.origin, NULL, NULL, p, self->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
	if ( tr.fraction == 1.0f || tr.entityNum == ent->s.number )
	{
		return qtrue;
	}
	return tr.entityNum < ENTITYNUM_WORLD && RP_TelekinesisHolds( self, &g_entities[tr.entityNum] );
}

// where the aimed target (the first) is held: the lift above the grab at levels 1-2, along the aim at 3+
static void RP_TK_HoldPoint( const gentity_t *self, int lvl, vec3_t out )
{
	const rpTelekinesis_t *tk = &self->client->tk;

	if ( lvl == FORCE_LEVEL_1 )
	{
		VectorCopy( tk->offsets[0], out );
	}
	else if ( lvl == FORCE_LEVEL_2 )
	{
		VectorAdd( self->client->ps.origin, tk->offsets[0], out );
	}
	else
	{
		vec3_t eye, fwd;

		RP_TK_Eye( self, eye, fwd );
		VectorMA( eye, tk->dist, fwd, out );
	}
}

static void RP_TK_Add( gentity_t *self, gentity_t *ent )
{
	rpTelekinesis_t *tk = &self->client->tk;

	tk->item[tk->num] = ent->client ? qfalse : qtrue;
	tk->ents[tk->num++] = ent->s.number;
	if ( !ent->client )
	{
		RP_TK_ItemTake( self, ent );
		return;
	}
	ent->client->ps.fd.forceGripStarted = level.time;
	ent->client->tkHeldUntil = level.time + RP_TK_HELD_MS;
	ent->client->tkLandUntil = 0;
}

// lets ent go: back to normal movement, and a landing animation if it is a plain drop
static void RP_TK_ReleaseOne( gentity_t *ent, qboolean landing )
{
	if ( !ent->inuse || !ent->client )
	{
		return;
	}
	ent->client->ps.forceGripChangeMovetype = PM_NORMAL;
	ent->client->tkHeldUntil = 0;
	// let go is let go: no "being gripped" mark left to run out (grip leaves a second of it), which kept him crippled,
	// slowed and hearing grip's choking sound through his fall (WP_ForcePowersUpdate); the throw clears it the same way
	ent->client->ps.fd.forceGripBeingGripped = 0;
	ent->client->tkLandUntil = ( landing && ent->health > 0 && !RP_TK_IsVehicle( ent ) && ent->client->ps.groundEntityNum == ENTITYNUM_NONE )
		? level.time + RP_TK_LAND_WINDOW_MS : 0;	// a landing only for one that was in the air
}

// lets the i-th target go: a player, NPC or vehicle as RP_TK_ReleaseOne, an item as RP_TK_ItemRelease
static void RP_TK_ReleaseAt( gentity_t *self, int i, qboolean landing )
{
	rpTelekinesis_t *tk = &self->client->tk;
	gentity_t *ent = &g_entities[tk->ents[i]];

	if ( tk->item[i] )
	{
		RP_TK_ItemRelease( self, ent, NULL );
	}
	else
	{
		RP_TK_ReleaseOne( ent, landing );
	}
}

static void RP_TK_Remove( gentity_t *self, int i, qboolean landing )
{
	rpTelekinesis_t *tk = &self->client->tk;
	int num = tk->ents[i];		// not ent->s.number: a freed item's is cleared

	RP_TK_ReleaseAt( self, i, landing );
	for ( ; i < tk->num - 1; i++ )
	{
		tk->ents[i] = tk->ents[i + 1];
		tk->item[i] = tk->item[i + 1];
		VectorCopy( tk->offsets[i + 1], tk->offsets[i] );
	}
	tk->num--;
	if ( self->client->ps.fd.forceGripEntityNum == num )
	{	// the stock code's one target: keep it one we hold
		self->client->ps.fd.forceGripEntityNum = tk->num ? tk->ents[0] : ENTITYNUM_NONE;
	}
}

// with no player, NPC or vehicle on the aim: the item nearest the aim line, within RP_TK_ITEM_BAND of it, in front,
// in reach (the eyes to its middle) and in sight; of two as near the line, the nearer one. NULL if there is none.
static gentity_t *RP_TK_PickItem( gentity_t *self, int lvl, const vec3_t eye, const vec3_t fwd )
{
	gentity_t *best = NULL;
	float bestOff = 0, bestAlong = 0;
	int i;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ )
	{
		gentity_t *ent = &g_entities[i];
		vec3_t p, to;
		float along, off2, off;

		if ( !RP_TK_ItemMovable( self, ent, qtrue ) )
		{
			continue;
		}
		RP_TK_SeenPoint( ent, p );
		VectorSubtract( p, eye, to );
		along = DotProduct( to, fwd );
		if ( along <= 0 || VectorLength( to ) > rpTkReach[lvl] )
		{
			continue;
		}
		off2 = VectorLengthSquared( to ) - along * along;
		off = off2 > 0 ? sqrt( off2 ) : 0;
		if ( off > RP_TK_ITEM_BAND )
		{
			continue;
		}
		if ( best && ( off > bestOff || ( off == bestOff && along >= bestAlong ) ) )
		{
			continue;
		}
		if ( !RP_TK_Visible( self, ent ) )
		{
			continue;
		}
		best = ent;
		bestOff = off;
		bestAlong = along;
	}
	return best;
}

// Called by ForceGrip() in place of its own target code once the caster may grip at all. Picks the targets
// and sets up the hold; WP_DoSpecificPower() then starts the power and charges the start cost as for grip.
// Returns qfalse (and starts nothing) when nothing was in reach.
qboolean RP_TelekinesisGrab( gentity_t *self )
{
	rpTelekinesis_t *tk = &self->client->tk;
	int lvl = RP_TK_Level( self );
	vec3_t eye, fwd, end, hold;
	trace_t tr;
	gentity_t *first = NULL;
	int i;

	if ( tk->num )
	{	// never left holding: a hold always ends through WP_ForcePowerStop, but let nothing stay afloat if one did not
		RP_TelekinesisStopped( self );
	}
	memset( tk, 0, sizeof( *tk ) );

	RP_TK_Eye( self, eye, fwd );
	VectorMA( eye, rpTkReach[lvl], fwd, end );
	trap->Trace( &tr, eye, NULL, NULL, end, self->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );

	if ( tr.fraction != 1.0f && tr.entityNum < ENTITYNUM_WORLD && g_entities[tr.entityNum].client )
	{
		gentity_t *cand = &g_entities[tr.entityNum];

		if ( RP_TK_CanHold( self, cand, qtrue ) )
		{
			first = RP_TK_Resolve( self, cand, lvl );
			if ( first && first != cand && !RP_TK_CanHold( self, first, qtrue ) )
			{
				first = NULL;
			}
		}
	}

	if ( !first )
	{	// no one on the aim: an item near it
		first = RP_TK_PickItem( self, lvl, eye, fwd );
	}
	if ( first )
	{
		RP_TK_Add( self, first );
	}

	if ( lvl >= FORCE_LEVEL_5 )
	{	// the others in the cone, nearest the aim first
		float dots[RP_TK_MAX_TARGETS];
		int n = tk->num;

		for ( i = 0; i < level.num_entities; i++ )
		{
			gentity_t *ent = &g_entities[i];
			vec3_t p, to;
			float dist, dot;
			int j, k;

			if ( !ent->inuse || ent == self || ent == first )
			{
				continue;
			}
			if ( ent->client )
			{
				if ( ent->client->ps.m_iVehicleNum )
				{	// a rider goes with his vehicle; only the aimed one is thrown off (RP_TK_Resolve)
					continue;
				}
				if ( !RP_TK_CanHold( self, ent, qtrue ) )
				{
					continue;
				}
			}
			else if ( !RP_TK_ItemMovable( self, ent, qtrue ) )
			{	// nor an item to take
				continue;
			}
			RP_TK_SeenPoint( ent, p );
			VectorSubtract( p, eye, to );
			dist = VectorNormalize( to );
			if ( dist > rpTkReach[lvl] )
			{
				continue;
			}
			dot = DotProduct( fwd, to );
			if ( dot < RP_TK_CONE )
			{
				continue;
			}
			if ( !RP_TK_Visible( self, ent ) )
			{
				continue;
			}
			// insert by dot, the aimed one staying first
			for ( j = first ? 1 : 0; j < n && dots[j] >= dot; j++ )
			{
			}
			if ( j >= RP_TK_MAX_TARGETS )
			{
				continue;
			}
			if ( n < RP_TK_MAX_TARGETS )
			{
				n++;
			}
			for ( k = n - 1; k > j; k-- )
			{
				tk->ents[k] = tk->ents[k - 1];
				dots[k] = dots[k - 1];
			}
			tk->ents[j] = ent->s.number;
			dots[j] = dot;
		}
		for ( i = tk->num; i < n; i++ )
		{
			gentity_t *ent = &g_entities[tk->ents[i]];

			tk->num = i;		// RP_TK_Add appends at num
			RP_TK_Add( self, ent );
		}
		tk->num = n;
		if ( !first && n )
		{
			first = &g_entities[tk->ents[0]];
		}
	}

	if ( !tk->num )
	{
		return qfalse;
	}

	// the hold point and each target's place around it
	if ( lvl <= FORCE_LEVEL_2 )
	{
		RP_TK_Origin( first, hold );
		hold[2] += RP_TK_LIFT_HEIGHT;
		if ( lvl == FORCE_LEVEL_1 )
		{
			VectorCopy( hold, tk->offsets[0] );
		}
		else
		{
			VectorSubtract( hold, self->client->ps.origin, tk->offsets[0] );
		}
	}
	else
	{
		vec3_t to;

		RP_TK_Origin( first, to );
		VectorSubtract( to, eye, to );
		tk->dist = VectorLength( to );
		if ( tk->dist < RP_TK_MIN_DIST )
		{
			tk->dist = RP_TK_MIN_DIST;
		}
		else if ( tk->dist > rpTkHoldMax[lvl] )
		{
			tk->dist = rpTkHoldMax[lvl];
		}
		VectorMA( eye, tk->dist, fwd, hold );
		VectorClear( tk->offsets[0] );
		for ( i = 1; i < tk->num; i++ )
		{
			RP_TK_Origin( &g_entities[tk->ents[i]], tk->offsets[i] );
			VectorSubtract( tk->offsets[i], hold, tk->offsets[i] );
		}
	}

	tk->startTime = level.time;
	self->client->ps.fd.forceGripEntityNum = tk->ents[0];
	self->client->ps.fd.forceGripDamageDebounceTime = 0;
	self->client->ps.forceHandExtend = HANDEXTEND_FORCE_HOLD;
	self->client->ps.forceHandExtendTime = level.time + rpTkHoldCapMs[lvl];

	// the lift sound, once, as Push and Pull play theirs (the file is jumpbuild.mp3; the engine finds it)
	G_Sound( self, CHAN_BODY, G_SoundIndex( "sound/weapons/force/jumpbuild.wav" ) );
	return qtrue;
}

// the targets are let fly: Push's throw, without its resistance or distance falloff (they are held), knockdown
// at Push 3+ as Push's, and the kill credit; a vehicle just gets the velocity
static void RP_TK_DoThrow( gentity_t *self )
{
	rpTelekinesis_t *tk = &self->client->tk;
	vec3_t eye, fwd;
	float speed = 256.0f * tk->throwLevel;
	int i;

	// the push sound with the push itself (the targets flying), not on the key: the animation's thrust comes later
	G_Sound( self, CHAN_BODY, G_SoundIndex( "sound/weapons/force/push.wav" ) );

	RP_TK_Eye( self, eye, fwd );
	for ( i = 0; i < tk->num; i++ )
	{
		gentity_t *ent = &g_entities[tk->ents[i]];

		if ( tk->item[i] )
		{	// an item flies the same way, and falls as a pushed one
			vec3_t vel;

			VectorScale( fwd, speed, vel );
			if ( vel[2] < 128 )
			{
				vel[2] = 128;
			}
			RP_TK_ItemRelease( self, ent, vel );
			continue;
		}
		if ( !ent->inuse || !ent->client )
		{
			continue;
		}
		RP_TK_ReleaseOne( ent, qfalse );
		ent->client->ps.fd.forceGripBeingGripped = 0;
		VectorScale( fwd, speed, ent->client->ps.velocity );
		if ( ent->client->ps.velocity[2] < 128 )
		{
			ent->client->ps.velocity[2] = 128;
		}
		ent->client->ps.otherKiller = self->s.number;
		ent->client->ps.otherKillerTime = level.time + 5000;
		ent->client->ps.otherKillerDebounceTime = level.time + 100;
		if ( RP_TK_IsVehicle( ent ) )
		{
			continue;
		}
		ent->client->pushEffectTime = level.time + 600;
		if ( tk->throwLevel >= FORCE_LEVEL_3 && ent->client->ps.forceHandExtend != HANDEXTEND_KNOCKDOWN && BG_KnockDownable( &ent->client->ps ) )
		{
			ent->client->ps.forceHandExtend = HANDEXTEND_KNOCKDOWN;
			ent->client->ps.forceHandExtendTime = level.time + 700;
			ent->client->ps.forceDodgeAnim = 0;
			ent->client->ps.quickerGetup = qtrue;
		}
	}
	tk->num = 0;
	tk->throwTime = 0;
	tk->thrown = qtrue;
}

// Called by WP_ForcePowerRun(FP_GRIP) every frame the power runs, in place of DoGripAction()
void RP_TelekinesisUpdate( gentity_t *self )
{
	rpTelekinesis_t *tk = &self->client->tk;
	int lvl = RP_TK_Level( self );
	vec3_t hold;
	qboolean steer;
	int i;

	if ( tk->throwTime && level.time >= tk->throwTime )
	{
		RP_TK_DoThrow( self );
		WP_ForcePowerStop( self, FP_GRIP );
		return;
	}

	if ( self->client->ps.fd.forceGripBeingGripped > level.time || !RP_TelekinesisMode( self ) )
	{	// gripped or lifted yourself, or logged out: you let go
		WP_ForcePowerStop( self, FP_GRIP );
		return;
	}

	// as DoGripAction: using it is an attack, so no spawn invulnerability meanwhile
	self->client->dangerTime = level.time;
	self->client->ps.eFlags &= ~EF_INVULNERABLE;
	self->client->invulnerableTimer = 0;

	RP_TK_HoldPoint( self, lvl, hold );
	steer = tk->moveTime <= level.time;
	if ( steer )
	{
		tk->moveTime = level.time + RP_TK_STEER_MS;
	}

	for ( i = 0; i < tk->num; i++ )
	{
		gentity_t *ent = &g_entities[tk->ents[i]];
		vec3_t a, point;
		int useLvl;

		if ( tk->item[i] )
		{	// an item: let go untouched when it isn't ours to hold any more (taken, gone, or sent home); let fall
			// when out of range or sight, or, below level 3, when the caster looks away; else held and steered
			vec3_t org;

			if ( !RP_TK_ItemKept( self, ent ) )
			{	// RP_TK_ItemRelease, through the remove, only takes the mark off
				RP_TK_Remove( self, i--, qfalse );
				continue;
			}
			RP_TK_Origin( ent, org );
			VectorSubtract( org, self->client->ps.origin, a );
			if ( VectorLength( a ) > rpTkDrop[lvl] || !RP_TK_Visible( self, ent ) ||
				( lvl < FORCE_LEVEL_3 && !InFront( org, self->client->ps.origin, self->client->ps.viewangles, 0.9f ) ) )
			{
				RP_TK_Remove( self, i--, qtrue );
				continue;
			}
			ent->rpTkItemHeldUntil = level.time + RP_TK_HELD_MS;
			if ( steer )
			{
				if ( lvl == FORCE_LEVEL_1 || lvl == FORCE_LEVEL_2 )
				{
					VectorCopy( hold, point );
				}
				else
				{
					VectorAdd( hold, tk->offsets[i], point );
				}
				RP_TK_ItemSteer( ent, point );
			}
			continue;
		}

		if ( !RP_TK_CanHold( self, ent, qfalse ) )
		{
			RP_TK_Remove( self, i--, qtrue );
			continue;
		}
		VectorSubtract( ent->client->ps.origin, self->client->ps.origin, a );
		if ( VectorLength( a ) > rpTkDrop[lvl] || !RP_TK_Visible( self, ent ) )
		{
			RP_TK_Remove( self, i--, qtrue );
			continue;
		}
		if ( lvl < FORCE_LEVEL_3 && !InFront( ent->client->ps.origin, self->client->ps.origin, self->client->ps.viewangles, 0.9f ) )
		{
			RP_TK_Remove( self, i--, qtrue );
			continue;
		}
		useLvl = WP_AbsorbConversion( ent, ent->client->ps.fd.forcePowerLevel[FP_ABSORB], self, FP_GRIP, lvl, forcePowerNeeded[lvl][FP_GRIP] );
		if ( !useLvl )
		{	// absorbed away
			RP_TK_Remove( self, i--, qtrue );
			continue;
		}

		// held: crippled and floating, as grip's victim (bg_pmove.c, g_active.c), with the telekinesis animations
		ent->client->ps.fd.forceGripBeingGripped = level.time + 1000;
		ent->client->ps.forceGripChangeMovetype = PM_FLOAT;
		ent->client->tkHeldUntil = level.time + RP_TK_HELD_MS;
		ent->client->ps.otherKiller = self->s.number;
		ent->client->ps.otherKillerTime = level.time + 5000;
		ent->client->ps.otherKillerDebounceTime = level.time + 100;
		Jetpack_Off( ent );
		RP_LedgeLetGo( ent );
		RP_PairEmoteEnd( ent );

		if ( steer )
		{
			float len;

			if ( lvl == FORCE_LEVEL_1 || lvl == FORCE_LEVEL_2 )
			{
				VectorCopy( hold, point );
			}
			else
			{
				VectorAdd( hold, tk->offsets[i], point );
			}
			VectorSubtract( point, ent->client->ps.origin, a );
			len = VectorLength( a );
			if ( len < RP_TK_STEER_DEAD )
			{
				VectorClear( ent->client->ps.velocity );
			}
			else
			{
				float v = len * RP_TK_STEER_GAIN;

				if ( v > RP_TK_STEER_MAX )
				{
					v = RP_TK_STEER_MAX;
				}
				VectorScale( a, v / len, ent->client->ps.velocity );
			}
		}
	}

	if ( !tk->num )
	{
		WP_ForcePowerStop( self, FP_GRIP );
	}
}

// Called first thing by WP_ForcePowerStop(FP_GRIP) whenever a telekinesis hold or throw is in progress: lets the
// targets go (or throws them now if a push was pending), clears the hand, and plays the caster's release --
// or keeps his push animation running after the hand is gone.
void RP_TelekinesisStopped( gentity_t *self )
{
	rpTelekinesis_t *tk = &self->client->tk;
	qboolean handFree = self->client->ps.forceHandExtend == HANDEXTEND_FORCE_HOLD || self->client->ps.forceHandExtend == HANDEXTEND_WEAPONREADY;
	int i;

	if ( tk->throwTime && tk->num )
	{	// the key was released, or the force or the time ran out, in the half second before the throw
		RP_TK_DoThrow( self );
	}
	for ( i = 0; i < tk->num; i++ )
	{
		RP_TK_ReleaseAt( self, i, qtrue );
	}

	if ( handFree )
	{	// straight to free, not through the weapon-raise, so the torso animation below is not replaced
		self->client->ps.forceHandExtend = HANDEXTEND_NONE;
		self->client->ps.forceHandExtendTime = 0;
	}

	if ( self->health > 0 && handFree )
	{
		if ( tk->thrown )
		{
			if ( self->client->ps.torsoAnim == BOTH_FORCELEVITATEWAVE )
			{	// the hold forced its timer to 1 every frame: give it what is left of the animation
				int left = BG_AnimLength( self->localAnimIndex, BOTH_FORCELEVITATEWAVE ) - RP_TK_THROW_DELAY_MS;

				self->client->ps.torsoTimer = left > 1 ? left : 1;
			}
		}
		else if ( tk->num || tk->startTime )
		{
			G_SetAnim( self, &self->client->pers.cmd, SETANIM_TORSO, BOTH_FORCELEVITATEDONE, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD, 0 );
		}
		if ( self->client->ps.weapon == WP_MELEE && self->client->ps.weaponTime < self->client->ps.torsoTimer )
		{	// unarmed, PM_Weapon puts the torso back in its stance as soon as the weapon is free, whatever the torso
			// timer says (guns and sabers wait for it): keep the fists busy for the animation
			self->client->ps.weaponTime = self->client->ps.torsoTimer;
		}
	}

	memset( tk, 0, sizeof( *tk ) );
}

// victim broke free with Push or Pull (ForceThrow): only he is let go; the hold goes on for the others
void RP_TelekinesisEscape( gentity_t *holder, gentity_t *victim )
{
	int i;

	for ( i = 0; i < holder->client->tk.num; i++ )
	{
		if ( holder->client->tk.ents[i] == victim->s.number )
		{
			RP_TK_Remove( holder, i, qtrue );
			break;
		}
	}
	if ( !holder->client->tk.num )
	{
		WP_ForcePowerStop( holder, FP_GRIP );
	}
}

// The push key while holding (ForceThrow, which does nothing else while the hand is busy). At level 3+, with a
// Push skill and the force for it at that level, the force is paid now and the targets are thrown, with the push
// sound, RP_TK_THROW_DELAY_MS into the push animation, at its thrust (RP_TK_DoThrow). Below level 3, or without
// Push, the key does nothing.
void RP_TelekinesisThrow( gentity_t *self )
{
	rpTelekinesis_t *tk = &self->client->tk;
	int pushLevel = self->client->ps.fd.forcePowerLevel[FP_PUSH];

	if ( tk->throwTime || RP_TK_Level( self ) < FORCE_LEVEL_3 )
	{
		return;
	}
	if ( pushLevel < FORCE_LEVEL_1 || self->health <= 0 || !WP_ForcePowerUsable( self, FP_PUSH ) )
	{	// no Push, or none now (dead, a ysalamiri, a duel's rules, not known)
		return;
	}
	if ( !WP_ForcePowerAvailable( self, FP_PUSH, 0 ) )
	{
		return;
	}
	BG_ForcePowerDrain( &self->client->ps, FP_PUSH, 0 );	// paid now; the sound comes with the throw (RP_TK_DoThrow)
	tk->throwTime = level.time + RP_TK_THROW_DELAY_MS;
	tk->throwLevel = pushLevel;
}

// ClientThink_real(), before the move: the private bits the shared animation code and the HUD read
void RP_TelekinesisStatBits( gentity_t *ent )
{
	int *st = &ent->client->ps.stats[STAT_RP_SELF_FX];
	const rpTelekinesis_t *tk = &ent->client->tk;

	*st &= ~(RP_SELF_FX_TK_MODE | RP_SELF_FX_TK_LIFT | RP_SELF_FX_TK_THROW | RP_SELF_FX_TK_HELD);
	if ( RP_TelekinesisMode( ent ) )
	{
		*st |= RP_SELF_FX_TK_MODE;
	}
	if ( tk->num )
	{
		if ( level.time - tk->startTime < RP_TK_LIFT_ANIM_MS )
		{
			*st |= RP_SELF_FX_TK_LIFT;
		}
		if ( tk->throwTime )
		{
			*st |= RP_SELF_FX_TK_THROW;
		}
	}
	if ( ent->client->tkHeldUntil > level.time )
	{
		*st |= RP_SELF_FX_TK_HELD;
	}
}

// ClientThink_real(): a target let go lands on BOTH_FORCELEVITATEDLAND, a knockdown for its length, then the
// usual get-up. Only once, only on the ground within the window, and not on top of a knockdown or a downed state.
void RP_TelekinesisLanding( gentity_t *ent )
{
	int len;

	if ( !ent->client->tkLandUntil )
	{
		return;
	}
	if ( ent->client->tkLandUntil < level.time || ent->health <= 0 || ent->client->ps.m_iVehicleNum || RP_TK_IsVehicle( ent ) ||
		ent->client->ps.forceHandExtend == HANDEXTEND_KNOCKDOWN || G_PlayerIsDowned( ent ) || ent->client->tkHeldUntil > level.time )
	{
		ent->client->tkLandUntil = 0;
		return;
	}
	if ( ent->client->ps.groundEntityNum == ENTITYNUM_NONE )
	{
		return;
	}
	ent->client->tkLandUntil = 0;
	len = BG_AnimLength( ent->localAnimIndex, BOTH_FORCELEVITATEDLAND );
	if ( len < 1 )
	{	// not a humanoid: no such animation
		return;
	}
	G_SetAnim( ent, &ent->client->pers.cmd, SETANIM_BOTH, BOTH_FORCELEVITATEDLAND, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD, 0 );
	ent->client->ps.forceHandExtend = HANDEXTEND_KNOCKDOWN;
	ent->client->ps.forceHandExtendTime = level.time + len;
	ent->client->ps.forceDodgeAnim = 0;
	ent->client->ps.quickerGetup = qfalse;
}
