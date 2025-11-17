/*
 * Hovering Kigrax sentry.  The original binary exposed a bespoke hover
 * AI with multiple strafing and scouting tables.  This reimplementation
 * keeps the same behavioural contract using a light-weight state machine
 * that leverages the stock Quake II flying helpers.
 */

#include "g_local.h"

#ifndef ARRAY_LEN
#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))
#endif

enum
{
	KIGRAX_FRAME_IDLE_START = 0,
	KIGRAX_FRAME_IDLE_END = 27,
	KIGRAX_FRAME_SCAN_START = 28,
	KIGRAX_FRAME_SCAN_END = 48,
	KIGRAX_FRAME_PATROL_CCW_START = 61,
	KIGRAX_FRAME_PATROL_CCW_END = 82,
	KIGRAX_FRAME_PATROL_CW_START = 83,
	KIGRAX_FRAME_PATROL_CW_END = 104,
	KIGRAX_FRAME_STRAFE_LONG_START = 105,
	KIGRAX_FRAME_STRAFE_LONG_END = 121,
	KIGRAX_FRAME_STRAFE_DASH_START = 122,
	KIGRAX_FRAME_STRAFE_DASH_END = 138,
	KIGRAX_FRAME_ATTACK_PREP_START = 139,
	KIGRAX_FRAME_ATTACK_PREP_END = 149,
	KIGRAX_FRAME_ATTACK_START = 150,
	KIGRAX_FRAME_ATTACK_FIRE = 163,
	KIGRAX_FRAME_ATTACK_END = 168,
	KIGRAX_FRAME_PAIN_START = 9,
	KIGRAX_FRAME_PAIN_END = 10,
	KIGRAX_FRAME_DEATH_START = 11,
	KIGRAX_FRAME_DEATH_END = 14
};

static int sound_sight;
static int sound_search;
static int sound_idle;
static int sound_pain;
static int sound_death;
static int sound_attack;

static vec3_t kigrax_flash_offset = {12.0f, 0.0f, -6.0f};

static void kigrax_idle_select (edict_t *self);
static void kigrax_walk_select (edict_t *self);
static void kigrax_run_select (edict_t *self);
static void kigrax_attack_execute (edict_t *self);
static void kigrax_fire (edict_t *self);

static mframe_t kigrax_frames_hover[KIGRAX_FRAME_IDLE_END - KIGRAX_FRAME_IDLE_START + 1];
static mframe_t kigrax_frames_scan[KIGRAX_FRAME_SCAN_END - KIGRAX_FRAME_SCAN_START + 1];
static mframe_t kigrax_frames_patrol_ccw[KIGRAX_FRAME_PATROL_CCW_END - KIGRAX_FRAME_PATROL_CCW_START + 1];
static mframe_t kigrax_frames_patrol_cw[KIGRAX_FRAME_PATROL_CW_END - KIGRAX_FRAME_PATROL_CW_START + 1];
static mframe_t kigrax_frames_strafe_long[KIGRAX_FRAME_STRAFE_LONG_END - KIGRAX_FRAME_STRAFE_LONG_START + 1];
static mframe_t kigrax_frames_strafe_dash[KIGRAX_FRAME_STRAFE_DASH_END - KIGRAX_FRAME_STRAFE_DASH_START + 1];
static mframe_t kigrax_frames_attack_prep[KIGRAX_FRAME_ATTACK_PREP_END - KIGRAX_FRAME_ATTACK_PREP_START + 1];
static mframe_t kigrax_frames_attack[KIGRAX_FRAME_ATTACK_END - KIGRAX_FRAME_ATTACK_START + 1];

static mmove_t kigrax_move_hover = {
	KIGRAX_FRAME_IDLE_START,
	KIGRAX_FRAME_IDLE_END,
	kigrax_frames_hover,
	kigrax_idle_select
};
static mmove_t kigrax_move_scan = {
	KIGRAX_FRAME_SCAN_START,
	KIGRAX_FRAME_SCAN_END,
	kigrax_frames_scan,
	kigrax_idle_select
};
static mmove_t kigrax_move_patrol_ccw = {
	KIGRAX_FRAME_PATROL_CCW_START,
	KIGRAX_FRAME_PATROL_CCW_END,
	kigrax_frames_patrol_ccw,
	kigrax_walk_select
};
static mmove_t kigrax_move_patrol_cw = {
	KIGRAX_FRAME_PATROL_CW_START,
	KIGRAX_FRAME_PATROL_CW_END,
	kigrax_frames_patrol_cw,
	kigrax_walk_select
};
static mmove_t kigrax_move_strafe_long = {
	KIGRAX_FRAME_STRAFE_LONG_START,
	KIGRAX_FRAME_STRAFE_LONG_END,
	kigrax_frames_strafe_long,
	kigrax_run_select
};
static mmove_t kigrax_move_strafe_dash = {
	KIGRAX_FRAME_STRAFE_DASH_START,
	KIGRAX_FRAME_STRAFE_DASH_END,
	kigrax_frames_strafe_dash,
	kigrax_run_select
};
static mmove_t kigrax_move_attack_prep = {
	KIGRAX_FRAME_ATTACK_PREP_START,
	KIGRAX_FRAME_ATTACK_PREP_END,
	kigrax_frames_attack_prep,
	kigrax_attack_execute
};
static mmove_t kigrax_move_attack = {
	KIGRAX_FRAME_ATTACK_START,
	KIGRAX_FRAME_ATTACK_END,
	kigrax_frames_attack,
	kigrax_run_select
};

static qboolean kigrax_moves_initialized;

/*
=============
kigrax_init_moves

Fill the Kigrax mmove tables recovered from the HLIL dump so the state
machine can reuse the original animation layout.
=============
*/
static void kigrax_init_moves (void)
{
	size_t i;

	if (kigrax_moves_initialized)
		return;

	for (i = 0; i < ARRAY_LEN(kigrax_frames_hover); i++)
		kigrax_frames_hover[i] = (mframe_t){ai_stand, 0.0f, NULL};

	for (i = 0; i < ARRAY_LEN(kigrax_frames_scan); i++)
		kigrax_frames_scan[i] = (mframe_t){ai_stand, 0.0f, NULL};

	for (i = 0; i < ARRAY_LEN(kigrax_frames_patrol_ccw); i++)
		kigrax_frames_patrol_ccw[i] = (mframe_t){ai_walk, 4.0f, NULL};

	for (i = 0; i < ARRAY_LEN(kigrax_frames_patrol_cw); i++)
		kigrax_frames_patrol_cw[i] = (mframe_t){ai_walk, 4.0f, NULL};

	for (i = 0; i < ARRAY_LEN(kigrax_frames_strafe_long); i++)
		kigrax_frames_strafe_long[i] = (mframe_t){ai_run, 10.0f, NULL};

	for (i = 0; i < ARRAY_LEN(kigrax_frames_strafe_dash); i++)
		kigrax_frames_strafe_dash[i] = (mframe_t){ai_run, 15.0f, NULL};

	for (i = 0; i < ARRAY_LEN(kigrax_frames_attack_prep); i++)
		kigrax_frames_attack_prep[i] = (mframe_t){ai_move, 0.0f, NULL};

	for (i = 0; i < ARRAY_LEN(kigrax_frames_attack); i++)
		kigrax_frames_attack[i] = (mframe_t){ai_move, 0.0f, NULL};

	kigrax_frames_attack[KIGRAX_FRAME_ATTACK_FIRE - KIGRAX_FRAME_ATTACK_START].thinkfunc = kigrax_fire;

	kigrax_moves_initialized = true;
}

/*
=============
kigrax_idle_select

Pick either the hover loop or the scan loop based on a simple random roll
and emit the idle vocalizations recovered from the HLIL spawn snapshot.
=============
*/
static void kigrax_idle_select (edict_t *self)
{
	if (random () < 0.5f)
	{
		self->monsterinfo.currentmove = &kigrax_move_hover;
	}
	else
	{
		self->monsterinfo.currentmove = &kigrax_move_scan;
	}

	if (random () < 0.5f)
tgi.sound (self, CHAN_VOICE, sound_idle, 1.0f, ATTN_IDLE, 0.0f);
}

/*
=============
kigrax_walk_select

Select one of the slow patrol loops unless the Kigrax is pinned by
AI_STAND_GROUND, in which case it falls back to the idle selector.
=============
*/
static void kigrax_walk_select (edict_t *self)
{
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		kigrax_idle_select (self);
		return;
	}

	if (random () < 0.5f)
tself->monsterinfo.currentmove = &kigrax_move_patrol_ccw;
	else
tself->monsterinfo.currentmove = &kigrax_move_patrol_cw;
}

/*
=============
kigrax_run_select

Choose between the longer strafing glide and the short dash loop that the
HLIL controller used while chasing or attacking targets.
=============
*/
static void kigrax_run_select (edict_t *self)
{
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		kigrax_idle_select (self);
		return;
	}

	if (random () < 0.4f)
tself->monsterinfo.currentmove = &kigrax_move_strafe_dash;
	else
tself->monsterinfo.currentmove = &kigrax_move_strafe_long;
}

/*
=============
kigrax_attack_execute

Switch into the per-frame blaster burst after the zero-distance hover prep
completes.
=============
*/
static void kigrax_attack_execute (edict_t *self)
{
	self->monsterinfo.currentmove = &kigrax_move_attack;
}

/*
=============
kigrax_search

Play the hover search chatter and resume the patrol selector so scripted
controllers can reuse the HLIL scouting behaviour.
=============
*/
static void kigrax_search (edict_t *self)
{
	if (random () < 0.33f)
tgi.sound (self, CHAN_VOICE, sound_idle, 1.0f, ATTN_IDLE, 0.0f);
	else
tgi.sound (self, CHAN_VOICE, sound_search, 1.0f, ATTN_IDLE, 0.0f);

	kigrax_walk_select (self);
}

/*
=============
kigrax_sight

Emit the hover sight cue and immediately drop into the aggressive strafing
loop to mirror the HLIL state machine.
=============
*/
static void kigrax_sight (edict_t *self, edict_t *other)
{
	gi.sound (self, CHAN_VOICE, sound_sight, 1.0f, ATTN_NORM, 0.0f);
	kigrax_run_select (self);
}

/*
=============
kigrax_fire

Fire the Kigrax blaster burst from the recovered muzzle offset.
=============
*/
static void kigrax_fire (edict_t *self)
{
	vec3_t start, dir, forward, right, target;

	if (!self->enemy)
treturn;

	AngleVectors (self->s.angles, forward, right, NULL);
	G_ProjectSource (self->s.origin, kigrax_flash_offset, forward, right, start);

	VectorCopy (self->enemy->s.origin, target);
	target[2] += self->enemy->viewheight * 0.5f;

	VectorSubtract (target, start, dir);
	VectorNormalize (dir);

	gi.sound (self, CHAN_WEAPON, sound_attack, 1.0f, ATTN_NORM, 0.0f);
	monster_fire_blaster (self, start, dir, 8, 1000, MZ2_HOVER_BLASTER_1, EF_BLASTER);
}

/*
=============
kigrax_attack

Kick off the HLIL attack chain (prep hover → burst → run selector) while
throttling repeated bursts via attack_finished.
=============
*/
static void kigrax_attack (edict_t *self)
{
	self->monsterinfo.attack_finished = level.time + 1.2f;
	self->monsterinfo.currentmove = &kigrax_move_attack_prep;
}

/*
=============
kigrax_pain
=============
*/
static void kigrax_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	static mframe_t pain_frames[] = {
		{ai_move, 0.0f, NULL},
		{ai_move, 0.0f, NULL}
	};
	static mmove_t pain_move = {
		KIGRAX_FRAME_PAIN_START,
		KIGRAX_FRAME_PAIN_END,
		pain_frames,
		kigrax_run_select
	};

	if (level.time < self->pain_debounce_time)
treturn;

	self->pain_debounce_time = level.time + 1.0f;
	gi.sound (self, CHAN_VOICE, sound_pain, 1.0f, ATTN_NORM, 0.0f);
	self->monsterinfo.currentmove = &pain_move;
}

/*
=============
kigrax_dead
=============
*/
static void kigrax_dead (edict_t *self)
{
	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_YES;
}

/*
=============
kigrax_die
=============
*/
static void kigrax_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	static mframe_t death_frames[] = {
		{ai_move, 0.0f, NULL},
		{ai_move, 0.0f, NULL},
		{ai_move, 0.0f, kigrax_dead},
		{ai_move, 0.0f, kigrax_dead}
	};
	static mmove_t death_move = {
		KIGRAX_FRAME_DEATH_START,
		KIGRAX_FRAME_DEATH_END,
		death_frames,
		kigrax_dead
	};

	gi.sound (self, CHAN_VOICE, sound_death, 1.0f, ATTN_NORM, 0.0f);

	if (self->health <= self->gib_health)
	{
		gi.sound (self, CHAN_VOICE, gi.soundindex ("misc/udeath.wav"), 1.0f, ATTN_NORM, 0.0f);
		ThrowGib (self, "models/objects/gibs/sm_meat/tris.md2", damage, GIB_ORGANIC);
		ThrowGib (self, "models/objects/gibs/sm_meat/tris.md2", damage, GIB_ORGANIC);
		ThrowHead (self, "models/objects/gibs/head2/tris.md2", damage, GIB_ORGANIC);
		return;
	}

	self->monsterinfo.currentmove = &death_move;
}

/*
=============
SP_monster_kigrax

Register the hovering Kigrax sentry and align its spawn defaults with the
extracted HLIL manifest.
=============
*/
void SP_monster_kigrax (edict_t *self)
{
	if (deathmatch->value)
	{
		G_FreeEdict (self);
		return;
	}

	kigrax_init_moves ();

	self->s.modelindex = gi.modelindex ("models/monsters/kigrax/tris.md2");
	VectorSet (self->mins, -20.0f, -20.0f, -32.0f);
	VectorSet (self->maxs, 20.0f, 20.0f, 12.0f);
	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->flags |= FL_FLY;
	self->mass = 150;
	self->yaw_speed = 20.0f;

	sound_sight = gi.soundindex ("hover/hovsght1.wav");
	sound_search = gi.soundindex ("hover/hovsrch1.wav");
	sound_idle = gi.soundindex ("kigrax/hovidle1.wav");
	sound_pain = gi.soundindex ("hover/hovpain1.wav");
	sound_death = gi.soundindex ("hover/hovdeth1.wav");
	sound_attack = gi.soundindex ("kigrax/hovatck1.wav");

	self->s.sound = sound_idle;

	self->health = 200;
	self->gib_health = -100;
	self->viewheight = 90;

	self->pain = kigrax_pain;
	self->die = kigrax_die;

	self->monsterinfo.stand = kigrax_idle_select;
	self->monsterinfo.idle = kigrax_idle_select;
	self->monsterinfo.walk = kigrax_walk_select;
	self->monsterinfo.run = kigrax_run_select;
	self->monsterinfo.attack = kigrax_attack;
	self->monsterinfo.melee = NULL;
	self->monsterinfo.sight = kigrax_sight;
	self->monsterinfo.search = kigrax_search;
	self->monsterinfo.aiflags |= AI_FLOAT;
	self->monsterinfo.scale = 1.0f;

	kigrax_idle_select (self);
	flymonster_start (self);
}
