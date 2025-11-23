/*
 * Simplified reconstruction of the Oblivion cyborg monster based on
 * the surviving high level IL dump.  The original AI relied on a
 * fairly involved set of animation tables which are not available in
 * the open source drop, so this translation recreates the behaviour in
 * a compact form that still respects the same inputs and gameplay
 * beats.  The cyborg is a heavy biped that advances quickly and fires
 * paired deatomiser bursts at medium range.
 */

#include "g_local.h"

#define CYBORG_FRAME_STAND_START       0x6c
#define CYBORG_FRAME_STAND_END         0x7d
#define CYBORG_FRAME_IDLE_START        0x52
#define CYBORG_FRAME_IDLE_END          0x6b
#define CYBORG_FRAME_WALK_START        0x12
#define CYBORG_FRAME_WALK_END          0x17
#define CYBORG_FRAME_RUN_START         0x4f
#define CYBORG_FRAME_RUN_END           0x51
#define CYBORG_FRAME_ATTACK1_START     0x18
#define CYBORG_FRAME_ATTACK1_END       0x23
#define CYBORG_FRAME_ATTACK2_START     0x2f
#define CYBORG_FRAME_ATTACK2_END       0x34
#define CYBORG_FRAME_ATTACK3_START     0x35
#define CYBORG_FRAME_ATTACK3_END       0x3a
#define CYBORG_FRAME_PAIN_STAGGER_START        0x49
#define CYBORG_FRAME_PAIN_STAGGER_END          0x4e
#define CYBORG_FRAME_PAIN_RECOVER_START        0x4f
#define CYBORG_FRAME_PAIN_RECOVER_END          0x51
#define CYBORG_FRAME_DEATH_START       15
#define CYBORG_FRAME_DEATH_END         17
#define CYBORG_STAND_GROUND_DURATION   3.0f

static int sound_sight;
static int sound_search;
static int sound_idle;
static int sound_step[3];
static int sound_pain_samples[2];
static int sound_pain;
static int sound_death;
static int sound_attack[3];
static int sound_thud;

/*
=============
cyborg_step

Play one of the retail footstep samples while advancing.
=============
*/
static void cyborg_step (edict_t *self)
{
	gi.sound (self, CHAN_BODY, sound_step[rand () % 3], 1, ATTN_NORM, 0);
}

/*
=============
cyborg_sight

Emit the HLIL sight bark when the cyborg first spots an enemy.
=============
*/
static void cyborg_sight (edict_t *self, edict_t *other)
{
	gi.sound (self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

/*
=============
cyborg_search

Loop the search bark while the cyborg is hunting for enemies.
=============
*/
static void cyborg_search (edict_t *self)
{
	gi.sound (self, CHAN_VOICE, sound_search, 1, ATTN_IDLE, 0);
}

static vec3_t cyborg_muzzle_right = {15.0f, 12.0f, 12.0f};
static vec3_t cyborg_muzzle_left = {15.0f, -12.0f, 12.0f};

/*
=============
cyborg_fire_deatom

Fire a deatomizer bolt using the frame-selectable weapon sample recovered from the HLIL snapshot.
=============
*/
static void cyborg_fire_deatom (edict_t *self, const vec3_t muzzle_offset, int sample_index)
{
	vec3_t	start, dir, forward, right, target;
	vec3_t	offset;

	if (!self->enemy)
		return;

if (sample_index < 0 || sample_index >= (int) (sizeof (sound_attack) / sizeof (sound_attack[0])))
sample_index = rand () % (int) (sizeof (sound_attack) / sizeof (sound_attack[0]));

	AngleVectors (self->s.angles, forward, right, NULL);
	VectorCopy (muzzle_offset, offset);
	G_ProjectSource (self->s.origin, offset, forward, right, start);

	VectorCopy (self->enemy->s.origin, target);
	target[2] += self->enemy->viewheight;

	VectorSubtract (target, start, dir);
	VectorNormalize (dir);

	gi.sound (self, CHAN_WEAPON, sound_attack[sample_index], 1.0f, ATTN_NORM, 0.0f);

	/* The original DLL rolled cyborg deatom damage from a narrow band per shot
	 * before spawning a high-speed tracking projectile.  Match that behaviour
	 * here so kill feeds attribute the hits to the proper deatomizer mods.
	 */
	{
		int		 damage;
		int		 splash;
		const int	 speed = 1000;
		const float	 damage_radius = 480.0f;

		damage = 90 + (int) (random () * 30.0f);
		if (damage > 119)
			damage = 119;

		splash = damage / 2;

		fire_deatomizer (self, start, dir, damage, speed, damage_radius, splash);
	}
}

/*
=============
cyborg_fire_muzzle_right

Fire the right-arm deatomizer burst using the recovered muzzle offset.
=============
*/
static void cyborg_fire_muzzle_right (edict_t *self)
{
	cyborg_fire_deatom (self, cyborg_muzzle_right, -1);
}

/*
=============
cyborg_fire_muzzle_left

Fire the left-arm deatomizer burst and alternate the retail firing samples.
=============
*/
static void cyborg_fire_muzzle_left (edict_t *self)
{
	int		 sample_index;

	sample_index = (self->monsterinfo.lefty & 1) ? 2 : 1;
	self->monsterinfo.lefty ^= 1;

	cyborg_fire_deatom (self, cyborg_muzzle_left, sample_index);
}


static void cyborg_land (edict_t *self);
static void cyborg_idle_loop (edict_t *self);
static void cyborg_locomotion_resume (edict_t *self);
static void cyborg_locomotion_stage (edict_t *self);
static void cyborg_attack_dispatch (edict_t *self);
static qboolean cyborg_update_stand_ground (edict_t *self);
static void cyborg_schedule_stand_ground (edict_t *self, float duration);
static void cyborg_wound_stand_ground (edict_t *self);
static void cyborg_stand_ground_think (edict_t *self);
static void cyborg_stand (edict_t *self);

/*
 * These long-form idle and stand tables recreate the retail animation
 * blocks recovered in the HLIL dump as data_100516a0 (idle sway) and
 * data_10051730 (hold-ready), preserving the original 0x52–0x7d span
 * instead of the earlier single-frame placeholders.
 */
static mframe_t cyborg_frames_idle[] = {
	{ai_stand, 0.0f, cyborg_stand_ground_think},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, cyborg_land},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, cyborg_stand_ground_think},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, cyborg_land},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, cyborg_stand_ground_think},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, cyborg_land}
};
static mmove_t cyborg_move_idle = {
	CYBORG_FRAME_IDLE_START, CYBORG_FRAME_IDLE_END, cyborg_frames_idle, cyborg_stand
};

static mframe_t cyborg_frames_stand[] = {
	{ai_stand, 0.0f, cyborg_stand_ground_think},
	{ai_stand, 0.0f, cyborg_land},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, cyborg_stand_ground_think},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, cyborg_land},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, cyborg_stand_ground_think},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, cyborg_land},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, cyborg_stand_ground_think},
	{ai_stand, 0.0f, NULL},
	{ai_stand, 0.0f, cyborg_land},
	{ai_stand, 0.0f, NULL}
};
static mmove_t cyborg_move_stand = {
	CYBORG_FRAME_STAND_START, CYBORG_FRAME_STAND_END, cyborg_frames_stand, cyborg_idle_loop
};

static mframe_t cyborg_frames_walk[] = {
	{ai_walk, 6.0f, NULL},
	{ai_walk, 23.0f, cyborg_step},
	{ai_walk, 8.0f, NULL},
	{ai_walk, 6.0f, cyborg_step},
	{ai_walk, 23.0f, NULL},
	{ai_walk, 8.0f, NULL}
};
static mmove_t cyborg_move_walk = {
	CYBORG_FRAME_WALK_START, CYBORG_FRAME_WALK_END, cyborg_frames_walk, NULL
};

static mframe_t cyborg_frames_run[] = {
	{ai_run, -11.0f, NULL},
	{ai_run, -8.0f, NULL},
	{ai_run, 4.0f, NULL}
};
static mmove_t cyborg_move_run = {
	CYBORG_FRAME_RUN_START, CYBORG_FRAME_RUN_END, cyborg_frames_run, cyborg_locomotion_resume
};

static mframe_t cyborg_frames_pain_stagger[] = {
	{ai_move, 0.0f, NULL},
	{ai_move, 0.0f, NULL},
	{ai_move, 0.0f, NULL},
	{ai_move, 0.0f, NULL},
	{ai_move, 0.0f, NULL},
	{ai_move, 0.0f, NULL}
};
static mmove_t cyborg_move_pain_stagger = {
	CYBORG_FRAME_PAIN_STAGGER_START, CYBORG_FRAME_PAIN_STAGGER_END, cyborg_frames_pain_stagger, cyborg_locomotion_resume
};

static mmove_t cyborg_move_pain_recover = {
	CYBORG_FRAME_PAIN_RECOVER_START, CYBORG_FRAME_PAIN_RECOVER_END, cyborg_frames_run, cyborg_locomotion_resume
};

static mframe_t cyborg_frames_attack_primary[] = {
	{ai_charge, 4.0f, NULL},
	{ai_charge, 4.0f, NULL},
	{ai_charge, 5.0f, NULL},
	{ai_charge, 7.0f, NULL},
	{ai_charge, 7.0f, NULL},
	{ai_charge, 9.0f, cyborg_fire_muzzle_right},
	{ai_charge, 4.0f, NULL},
	{ai_charge, 4.0f, NULL},
	{ai_charge, 5.0f, NULL},
	{ai_charge, 7.0f, NULL},
	{ai_charge, 7.0f, NULL},
	{ai_charge, 9.0f, cyborg_fire_muzzle_left}
};
static mmove_t cyborg_move_attack_primary = {
	CYBORG_FRAME_ATTACK1_START, CYBORG_FRAME_ATTACK1_END, cyborg_frames_attack_primary, cyborg_locomotion_stage
};

static mframe_t cyborg_frames_attack_secondary[] = {
	{ai_charge, 0.0f, cyborg_fire_muzzle_right},
	{ai_charge, 0.0f, NULL},
	{ai_charge, 0.0f, NULL},
	{ai_charge, 0.0f, NULL},
	{ai_charge, 0.0f, NULL},
	{ai_charge, 0.0f, NULL}
};
static mmove_t cyborg_move_attack_secondary = {
	CYBORG_FRAME_ATTACK2_START, CYBORG_FRAME_ATTACK2_END, cyborg_frames_attack_secondary, cyborg_locomotion_stage
};

static mframe_t cyborg_frames_attack_barrage[] = {
	{ai_charge, 0.0f, cyborg_fire_muzzle_left},
	{ai_charge, 0.0f, NULL},
	{ai_charge, 0.0f, NULL},
	{ai_charge, 0.0f, NULL},
	{ai_charge, 0.0f, NULL},
	{ai_charge, 0.0f, NULL}
};
static mmove_t cyborg_move_attack_barrage = {
	CYBORG_FRAME_ATTACK3_START, CYBORG_FRAME_ATTACK3_END, cyborg_frames_attack_barrage, cyborg_locomotion_stage
};

/*
=============
cyborg_land

Emit the heavy landing impact when the pending flag is set.
=============
*/
static void cyborg_land (edict_t *self)
{
	if (!self->oblivion.cyborg_landing_thud)
		return;

	self->oblivion.cyborg_landing_thud = false;
	gi.sound (self, CHAN_BODY, sound_thud, 1.0f, ATTN_NORM, 0.0f);
}

/*
=============
cyborg_update_stand_ground

Check whether the wounded stand-ground timer has elapsed and clear the flag.
=============
*/
static qboolean cyborg_update_stand_ground (edict_t *self)
{
if (!(self->monsterinfo.aiflags & AI_STAND_GROUND))
return false;

	if (self->oblivion.cyborg_anchor_time <= 0.0f)
		return false;

	if (level.time < self->oblivion.cyborg_anchor_time)
		return false;

	self->monsterinfo.aiflags &= ~(AI_STAND_GROUND | AI_TEMP_STAND_GROUND);
	self->oblivion.cyborg_anchor_time = 0.0f;
	cyborg_land (self);
	return true;
}

/*
=============
cyborg_schedule_stand_ground

Apply the wounded stand-ground anchor and extend the release timer.
=============
*/
static void cyborg_schedule_stand_ground (edict_t *self, float duration)
{
	float		anchor_expire;

if (duration <= 0.0f)
return;

self->monsterinfo.aiflags |= (AI_STAND_GROUND | AI_TEMP_STAND_GROUND);
	self->oblivion.cyborg_landing_thud = true;
	anchor_expire = level.time + duration;

	if (self->oblivion.cyborg_anchor_time <= level.time || self->oblivion.cyborg_anchor_time < anchor_expire)
		self->oblivion.cyborg_anchor_time = anchor_expire;
}

/*
=============
cyborg_stand_ground_think

Drive the scripted stand-ground timer while the cyborg is anchored in place.
=============
*/
static void cyborg_stand_ground_think (edict_t *self)
{
	if (cyborg_update_stand_ground (self))
		cyborg_locomotion_stage (self);
}

/*
=============
cyborg_wound_stand_ground

Trigger the wounded stand-ground timer when health thresholds are crossed.
=============
*/
static void cyborg_wound_stand_ground (edict_t *self)
{
	int		max_health;

	max_health = self->max_health;
	if (!max_health)
		max_health = self->health;

	if (!max_health)
		return;

	if (self->health <= max_health / 4 && self->oblivion.cyborg_anchor_stage < 2)
	{
		self->oblivion.cyborg_anchor_stage = 2;
		cyborg_schedule_stand_ground (self, CYBORG_STAND_GROUND_DURATION);
		return;
	}

	if (self->health <= max_health / 2 && self->oblivion.cyborg_anchor_stage < 1)
	{
		self->oblivion.cyborg_anchor_stage = 1;
		cyborg_schedule_stand_ground (self, CYBORG_STAND_GROUND_DURATION);
	}
}

/*
=============
cyborg_idle_loop

Queue the retail idle mmove and trigger the ambient vocal line.
=============
*/
static void cyborg_idle_loop (edict_t *self)
{
	self->monsterinfo.currentmove = &cyborg_move_idle;
	gi.sound (self, CHAN_VOICE, sound_idle, 1.0f, ATTN_IDLE, 0.0f);
}

/*
=============
cyborg_stand

Route the monsterinfo state back to the one-frame stand loop.
=============
*/
static void cyborg_stand (edict_t *self)
{
	self->monsterinfo.currentmove = &cyborg_move_stand;
}

/*
=============
cyborg_locomotion_stage

Select between the walk and run chains based on the enemy state.
=============
*/
static void cyborg_locomotion_stage (edict_t *self)
{
	cyborg_update_stand_ground (self);
	cyborg_land (self);

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		cyborg_stand (self);
		return;
	}

	if (!self->enemy)
	{
		cyborg_idle_loop (self);
		return;
	}

	if (range (self, self->enemy) > RANGE_NEAR && random () > 0.4f)
	{
		self->monsterinfo.currentmove = &cyborg_move_run;
	}
	else
	{
		self->monsterinfo.currentmove = &cyborg_move_walk;
	}
}

/*
=============
cyborg_locomotion_resume

Return to the staged walk/run loop after a transient animation.
=============
*/
static void cyborg_locomotion_resume (edict_t *self)
{
	cyborg_update_stand_ground (self);
	cyborg_land (self);

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		cyborg_stand (self);
		return;
	}

	if (!self->enemy)
	{
		cyborg_idle_loop (self);
		return;
	}

	if (range (self, self->enemy) > RANGE_NEAR)
	{
		self->monsterinfo.currentmove = &cyborg_move_run;
	}
	else if (random () > 0.5f)
	{
		self->monsterinfo.currentmove = &cyborg_move_walk;
	}
	else
	{
		self->monsterinfo.currentmove = &cyborg_move_run;
	}
}

/*
=============
cyborg_walk

Dispatch walk requests to the locomotion staging helper.
=============
*/
static void cyborg_walk (edict_t *self)
{
	cyborg_locomotion_stage (self);
}

/*
=============
cyborg_run

Delegate run requests through the locomotion selector.
=============
*/
static void cyborg_run (edict_t *self)
{
	cyborg_locomotion_stage (self);
}

/*
=============
cyborg_attack_roll

Mirror the DLL's 15-bit random scaling used by the dispatcher to keep
the retail selection probabilities intact.
=============
*/
static float cyborg_attack_roll (void)
{
	return (rand () & 0x7fff) * (1.0f / 32768.0f);
}

/*
=============
cyborg_attack

Entry point that routes into the retail-style attack dispatcher.
=============
*/
static void cyborg_attack (edict_t *self)
{
	cyborg_attack_dispatch (self);
}

static void cyborg_attack_dispatch (edict_t *self)
{
	float	choice;

	cyborg_update_stand_ground (self);

	if (!self->enemy)
	{
		cyborg_stand (self);
		return;
	}

	self->monsterinfo.attack_finished = level.time + 0.9f + random () * 0.6f;

	choice = cyborg_attack_roll ();

	if (choice < 0.5f)
	{
		self->oblivion.cyborg_landing_thud = true;
		self->monsterinfo.currentmove = &cyborg_move_attack_primary;
	}
	else if (choice < 0.7f)
	{
		self->oblivion.cyborg_landing_thud = true;
		self->monsterinfo.currentmove = &cyborg_move_attack_barrage;
	}
	else
	{
		self->oblivion.cyborg_landing_thud = true;
		self->monsterinfo.currentmove = &cyborg_move_attack_secondary;
	}
}

/*
=============
cyborg_pain

Mirror the retail stagger handler by enforcing the dedicated cooldown,
alternating the voice samples, and branching into the extended mmove
tables recovered from the HLIL dump.
=============
*/
static void cyborg_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	int	slot;

if (level.time < self->pain_debounce_time)
return;

self->pain_debounce_time = level.time + 3.0f;
	self->oblivion.cyborg_pain_time = self->pain_debounce_time;

	/* Update the wounded anchor thresholds on every damage event so the
	 * locomotion helpers can later release the cyborg through
	 * cyborg_update_stand_ground even if the pain animation is skipped.
	 */
	cyborg_wound_stand_ground (self);

	slot = self->oblivion.cyborg_pain_slot & 1;
	gi.sound (self, CHAN_VOICE, sound_pain_samples[slot], 1, ATTN_NORM, 0);
	self->oblivion.cyborg_pain_slot ^= 1;

	if (damage > 40 || random () > 0.5f)
		self->monsterinfo.currentmove = &cyborg_move_pain_stagger;
	else
		self->monsterinfo.currentmove = &cyborg_move_pain_recover;
}

static void cyborg_dead (edict_t *self)
{
	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_YES;
}

static void cyborg_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
static mframe_t death_frames[] = {
{ai_move, 0, NULL},
{ai_move, 0, NULL},
{ai_move, 0, cyborg_dead}
};
static mmove_t death_move = {CYBORG_FRAME_DEATH_START, CYBORG_FRAME_DEATH_END, death_frames, cyborg_dead};

	self->oblivion.cyborg_anchor_time = 0.0f;
	self->oblivion.cyborg_anchor_stage = 0;
	self->oblivion.cyborg_landing_thud = false;
	self->monsterinfo.aiflags &= ~AI_STAND_GROUND;

	gi.sound (self, CHAN_VOICE, sound_death, 1, ATTN_NORM, 0);

    if (self->health <= self->gib_health)
    {
        gi.sound (self, CHAN_VOICE, gi.soundindex ("misc/udeath.wav"), 1, ATTN_NORM, 0);
        ThrowGib (self, "models/objects/gibs/sm_meat/tris.md2", damage, GIB_ORGANIC);
        ThrowGib (self, "models/objects/gibs/bone/tris.md2", damage, GIB_ORGANIC);
        ThrowHead (self, "models/objects/gibs/head2/tris.md2", damage, GIB_ORGANIC);
        return;
    }

    self->monsterinfo.currentmove = &death_move;
}

void SP_monster_cyborg (edict_t *self)
{
    if (deathmatch->value)
    {
        G_FreeEdict (self);
        return;
    }

    self->s.modelindex = gi.modelindex ("models/monsters/cyborg/tris.md2");
    VectorSet (self->mins, -16.0f, -16.0f, -38.0f);
    VectorSet (self->maxs, 16.0f, 16.0f, 27.0f);
    self->movetype = MOVETYPE_STEP;
    self->solid = SOLID_BBOX;
    self->mass = 300;

    sound_sight = gi.soundindex ("cyborg/mutsght1.wav");
    sound_search = gi.soundindex ("cyborg/mutsrch1.wav");
    sound_idle = gi.soundindex ("cyborg/mutidle1.wav");
	sound_pain = gi.soundindex ("cyborg/mutpain1.wav");
	sound_pain_samples[0] = sound_pain;
	sound_pain_samples[1] = gi.soundindex ("cyborg/mutpain2.wav");
sound_death = gi.soundindex ("cyborg/mutdeth1.wav");
sound_attack[0] = gi.soundindex ("cyborg/mutatck1.wav");
sound_attack[1] = gi.soundindex ("cyborg/mutatck2.wav");
sound_attack[2] = gi.soundindex ("cyborg/mutatck3.wav");
sound_step[0] = gi.soundindex ("cyborg/step1.wav");
sound_step[1] = gi.soundindex ("cyborg/step2.wav");
sound_step[2] = gi.soundindex ("cyborg/step3.wav");
sound_thud = sound_step[2];

    self->s.sound = gi.soundindex ("cyborg/mutidle1.wav");

	self->health = 300;
	self->gib_health = -120;
	self->max_health = self->health;
	self->oblivion.cyborg_anchor_time = 0.0f;
	self->oblivion.cyborg_anchor_stage = 0;
	self->oblivion.cyborg_landing_thud = false;

	self->oblivion.cyborg_pain_time = 0.0f;
	self->oblivion.cyborg_pain_slot = 0;
	self->pain = cyborg_pain;
    self->die = cyborg_die;

    self->monsterinfo.stand = cyborg_stand;
    self->monsterinfo.idle = cyborg_stand;
    self->monsterinfo.walk = cyborg_walk;
    self->monsterinfo.run = cyborg_run;
    self->monsterinfo.sight = cyborg_sight;
    self->monsterinfo.search = cyborg_search;
    self->monsterinfo.melee = NULL;
    self->monsterinfo.attack = cyborg_attack;

    self->monsterinfo.max_ideal_distance = 512;

    cyborg_stand (self);

    walkmonster_start (self);
}
