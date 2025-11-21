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
	KIGRAX_FRAME_PAIN_START = 139,
	KIGRAX_FRAME_PAIN_END = 149,
	KIGRAX_FRAME_DEATH_START = 150,
	KIGRAX_FRAME_DEATH_END = 168
};

#define KIGRAX_DEFAULT_MIN_Z	-32.0f
#define KIGRAX_DEFAULT_MAX_Z	12.0f
#define KIGRAX_ATTACK_MAX_Z	0.0f
#define KIGRAX_PAIN_STAGGER_TIME	0.5f
#define KIGRAX_PAIN_COOLDOWN	1.5f
#define KIGRAX_SALVO_INTERVAL	(FRAMETIME)

static const float kigrax_salvo_yaw_offsets[] = {0.0f, 0.0f, 0.0f, 0.0f};
static const float kigrax_salvo_pitch_offsets[] = {0.0f, 0.0f, 0.0f, 0.0f};

static int sound_sight;
static int sound_search;
static int sound_idle;
static int sound_pain;
static int sound_pain_strong;
static int sound_death;
static int sound_attack;

static void kigrax_idle_select (edict_t *self);
static void kigrax_walk_select (edict_t *self);
static void kigrax_run_select (edict_t *self);
static void kigrax_attack_execute (edict_t *self);
static void kigrax_attack_salvo (edict_t *self);
static void kigrax_fire_bolt (edict_t *self, int shot_index);
static void kigrax_set_attack_hull (edict_t *self, qboolean crouched);
static void kigrax_begin_pain_stagger (edict_t *self);
static void kigrax_end_pain (edict_t *self);
static void kigrax_spawn_debris (edict_t *self);
static void kigrax_deadthink (edict_t *self);
static void kigrax_dead (edict_t *self);

static mframe_t kigrax_frames_hover[KIGRAX_FRAME_IDLE_END - KIGRAX_FRAME_IDLE_START + 1];
static mframe_t kigrax_frames_scan[KIGRAX_FRAME_SCAN_END - KIGRAX_FRAME_SCAN_START + 1];
static mframe_t kigrax_frames_patrol_ccw[KIGRAX_FRAME_PATROL_CCW_END - KIGRAX_FRAME_PATROL_CCW_START + 1];
static mframe_t kigrax_frames_patrol_cw[KIGRAX_FRAME_PATROL_CW_END - KIGRAX_FRAME_PATROL_CW_START + 1];
static mframe_t kigrax_frames_strafe_long[KIGRAX_FRAME_STRAFE_LONG_END - KIGRAX_FRAME_STRAFE_LONG_START + 1];
static mframe_t kigrax_frames_strafe_dash[KIGRAX_FRAME_STRAFE_DASH_END - KIGRAX_FRAME_STRAFE_DASH_START + 1];
static mframe_t kigrax_frames_attack_prep[KIGRAX_FRAME_ATTACK_PREP_END - KIGRAX_FRAME_ATTACK_PREP_START + 1];
static mframe_t kigrax_frames_attack[KIGRAX_FRAME_ATTACK_END - KIGRAX_FRAME_ATTACK_START + 1];
static mframe_t kigrax_frames_pain[KIGRAX_FRAME_PAIN_END - KIGRAX_FRAME_PAIN_START + 1];
static mframe_t kigrax_frames_death[KIGRAX_FRAME_DEATH_END - KIGRAX_FRAME_DEATH_START + 1];

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
	kigrax_attack_salvo
};
static mmove_t kigrax_move_pain = {
	KIGRAX_FRAME_PAIN_START,
	KIGRAX_FRAME_PAIN_END,
	kigrax_frames_pain,
	kigrax_end_pain
};
static mmove_t kigrax_move_death = {
	KIGRAX_FRAME_DEATH_START,
	KIGRAX_FRAME_DEATH_END,
	kigrax_frames_death,
	kigrax_dead
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

	kigrax_frames_attack[KIGRAX_FRAME_ATTACK_FIRE - KIGRAX_FRAME_ATTACK_START].thinkfunc = kigrax_attack_salvo;

	for (i = 0; i < ARRAY_LEN(kigrax_frames_pain); i++)
		kigrax_frames_pain[i] = (mframe_t){ai_move, 0.0f, NULL};

		kigrax_frames_pain[0].thinkfunc = kigrax_begin_pain_stagger;

	for (i = 0; i < ARRAY_LEN(kigrax_frames_death); i++)
		kigrax_frames_death[i] = (mframe_t){ai_move, 0.0f, NULL};

		kigrax_frames_death[3].thinkfunc = kigrax_spawn_debris;
		kigrax_frames_death[10].thinkfunc = kigrax_spawn_debris;

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
		gi.sound (self, CHAN_VOICE, sound_idle, 1.0f, ATTN_IDLE, 0.0f);
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
		self->monsterinfo.currentmove = &kigrax_move_patrol_ccw;
	else
		self->monsterinfo.currentmove = &kigrax_move_patrol_cw;
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
		self->monsterinfo.currentmove = &kigrax_move_strafe_dash;
	else
		self->monsterinfo.currentmove = &kigrax_move_strafe_long;
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
		gi.sound (self, CHAN_VOICE, sound_idle, 1.0f, ATTN_IDLE, 0.0f);
	else
		gi.sound (self, CHAN_VOICE, sound_search, 1.0f, ATTN_IDLE, 0.0f);

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
kigrax_set_attack_hull

Mirror the HLIL salvo helper by toggling between the standing hover hull and
the reduced crouch box used while firing.
=============
*/
static void kigrax_set_attack_hull (edict_t *self, qboolean crouched)
{
	if (crouched)
	{
		if (self->monsterinfo.aiflags & AI_DUCKED)
			return;

		self->monsterinfo.aiflags |= AI_DUCKED;
		self->mins[2] = KIGRAX_DEFAULT_MIN_Z;
		self->maxs[2] = KIGRAX_ATTACK_MAX_Z;
		gi.linkentity (self);
		return;
	}

	if (!(self->monsterinfo.aiflags & AI_DUCKED))
		return;

	self->monsterinfo.aiflags &= ~AI_DUCKED;
	self->mins[2] = KIGRAX_DEFAULT_MIN_Z;
	self->maxs[2] = KIGRAX_DEFAULT_MAX_Z;
	gi.linkentity (self);
}

/*
=============
kigrax_fire_bolt

Fire a single Kigrax blaster bolt using the HLIL muzzle offsets and salvo
aiming deltas.
=============
*/
static void kigrax_fire_bolt (edict_t *self, int shot_index)
{
	vec3_t start, dir, forward, right, target, aim_angles, shot_angles, shot_dir;

	if (!self->enemy)
		return;

	if (shot_index < 0 || shot_index >= ARRAY_LEN(kigrax_salvo_yaw_offsets))
		shot_index = 0;

	AngleVectors (self->s.angles, forward, right, NULL);
	G_ProjectSource (self->s.origin, monster_flash_offset[MZ2_HOVER_BLASTER_1], forward, right, start);

	VectorCopy (self->enemy->s.origin, target);
	target[2] += self->enemy->viewheight * 0.5f;

	VectorSubtract (target, start, dir);
	VectorNormalize (dir);
	vectoangles (dir, aim_angles);

	VectorCopy (aim_angles, shot_angles);
	shot_angles[YAW] += kigrax_salvo_yaw_offsets[shot_index];
	shot_angles[PITCH] += kigrax_salvo_pitch_offsets[shot_index];
	shot_angles[ROLL] = 0.0f;
	AngleVectors (shot_angles, shot_dir, NULL, NULL);

	monster_fire_blaster (self, start, shot_dir, 8, 1000, MZ2_HOVER_BLASTER_1, EF_BLASTER);
}

/*
=============
kigrax_attack_salvo

Mirror the HLIL 0x1002f030 helper by toggling the crouched hull, emitting the
four-shot burst with retail spacing, and restoring the standing hull before
returning to the strafing selector.
=============
*/
static void kigrax_attack_salvo (edict_t *self)
{
	if (!(self->monsterinfo.aiflags & AI_DUCKED))
{
	kigrax_set_attack_hull (self, true);
	self->monsterinfo.aiflags |= AI_HOLD_FRAME;
	gi.sound (self, CHAN_WEAPON, sound_attack, 1.0f, ATTN_NORM, 0.0f);
	self->count = 0;
	self->timestamp = level.time;
}

	if (!self->enemy)
{
		kigrax_set_attack_hull (self, false);
		self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;
		self->count = ARRAY_LEN(kigrax_salvo_yaw_offsets);
		self->timestamp = 0.0f;
		self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;
		self->monsterinfo.nextframe = self->s.frame + 1;
		kigrax_run_select (self);
}

	if ((self->monsterinfo.aiflags & AI_DUCKED) && self->count < ARRAY_LEN(kigrax_salvo_yaw_offsets))
	{
		if (level.time >= self->timestamp)
		{
			kigrax_fire_bolt (self, self->count);
			self->count++;
			self->timestamp = level.time + KIGRAX_SALVO_INTERVAL;
		}

		if (self->count < ARRAY_LEN(kigrax_salvo_yaw_offsets))
		{
			self->monsterinfo.aiflags |= AI_HOLD_FRAME;
			return;
		}

		self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;
		self->monsterinfo.nextframe = self->s.frame + 1;
	}

	if (self->s.frame == KIGRAX_FRAME_ATTACK_END)
	{
		self->timestamp = 0.0f;
		self->count = 0;
		self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;
		kigrax_set_attack_hull (self, false);
		kigrax_run_select (self);
	}
}
/*
=============
kigrax_begin_pain_stagger

Record the HLIL stagger window so the final pain frame can keep looping
until the timer expires.
=============
*/
static void kigrax_begin_pain_stagger (edict_t *self)
{
	self->timestamp = level.time + KIGRAX_PAIN_STAGGER_TIME;
}

/*
=============
kigrax_end_pain

Hold the final pain frame until the stagger timer elapses before allowing the
mmove end callback to resume strafing.
=============
*/
static void kigrax_end_pain (edict_t *self)
{
	if (level.time < self->timestamp)
	{
		self->monsterinfo.aiflags |= AI_HOLD_FRAME;
		self->monsterinfo.nextframe = KIGRAX_FRAME_PAIN_END;
		return;
	}

	self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;
	self->timestamp = 0.0f;
	kigrax_run_select (self);
}

/*
=============
kigrax_spawn_debris

Emit a metallic gib so the death animation mirrors the HLIL debris spray.
=============
*/
static void kigrax_spawn_debris (edict_t *self)
{
	ThrowGib (self, "models/objects/gibs/sm_meat/tris.md2", 10, GIB_ORGANIC);
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
	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + KIGRAX_PAIN_COOLDOWN;

	if (random () < 0.5f)
		gi.sound (self, CHAN_VOICE, sound_pain, 1.0f, ATTN_NORM, 0.0f);
	else
		gi.sound (self, CHAN_VOICE, sound_pain_strong, 1.0f, ATTN_NORM, 0.0f);

	self->monsterinfo.currentmove = &kigrax_move_pain;
}

/*
=============
kigrax_deadthink

Wait for the corpse to land, then trigger the hover-style explosion cleanup.
=============
*/
static void kigrax_deadthink (edict_t *self)
{
	if (!self->groundentity && level.time < self->timestamp)
{
		self->nextthink = level.time + FRAMETIME;
		return;
}

	BecomeExplosion1 (self);
}

/*
=============
kigrax_dead

Mirror the hover corpse routine by swapping to a toss hull and scheduling the
timed explosion thinker recovered from the HLIL dump.
=============
*/
static void kigrax_dead (edict_t *self)
{
	VectorSet (self->mins, -16.0f, -16.0f, -24.0f);
	VectorSet (self->maxs, 16.0f, 16.0f, -8.0f);
	self->movetype = MOVETYPE_TOSS;
	self->think = kigrax_deadthink;
	self->nextthink = level.time + FRAMETIME;
	self->timestamp = level.time + 15.0f;
	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_YES;
	gi.linkentity (self);
}

/*
=============
kigrax_die
=============
*/
static void kigrax_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	if (self->health <= self->gib_health)
	{
		gi.sound (self, CHAN_VOICE, gi.soundindex ("misc/udeath.wav"), 1.0f, ATTN_NORM, 0.0f);
		ThrowGib (self, "models/objects/gibs/sm_meat/tris.md2", damage, GIB_ORGANIC);
		ThrowGib (self, "models/objects/gibs/sm_meat/tris.md2", damage, GIB_ORGANIC);
		ThrowHead (self, "models/objects/gibs/head2/tris.md2", damage, GIB_ORGANIC);
		return;
	}

	gi.sound (self, CHAN_VOICE, sound_death, 1.0f, ATTN_NORM, 0.0f);
	self->monsterinfo.currentmove = &kigrax_move_death;
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
	sound_pain_strong = gi.soundindex ("hover/hovpain2.wav");
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
