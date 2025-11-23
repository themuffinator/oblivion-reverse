/*
 * Ground based spider tank encountered in later Oblivion levels.  The
 * original AI had fairly involved melee combos so this recreation keeps
 * things aggressive and close range – the spider rapidly closes distance
 * and delivers heavy slash attacks while shrugging off lighter hits.
 */

#include "g_local.h"

#define SPIDER_FRAME_STAND_START	0x00
#define SPIDER_FRAME_STAND_END		0x36
#define SPIDER_FRAME_WALK_START		0x37
#define SPIDER_FRAME_WALK_END		0x40
#define SPIDER_FRAME_ATTACKA_START	0x41
#define SPIDER_FRAME_ATTACKA_END	0x4a
#define SPIDER_FRAME_ATTACKB_START	0x4b
#define SPIDER_FRAME_ATTACKB_END	0x50
#define SPIDER_FRAME_RUN_START		0x51
#define SPIDER_FRAME_RUN_END		0x55
#define SPIDER_FRAME_COMBO_PRIMARY_START	0x56
#define SPIDER_FRAME_COMBO_PRIMARY_END		0x58
#define SPIDER_FRAME_COMBO_SECONDARY_START	0x59
#define SPIDER_FRAME_COMBO_SECONDARY_END	0x5a
#define SPIDER_FRAME_PAIN_START		0x5b
#define SPIDER_FRAME_PAIN_END		0x62
#define SPIDER_FRAME_ATTACK_FINISH_START	0x63
#define SPIDER_FRAME_ATTACK_FINISH_END		0x67
#define SPIDER_FRAME_ATTACK_RECOVER_START	0x68
#define SPIDER_FRAME_ATTACK_RECOVER_END		0x6e
#define SPIDER_FRAME_DEATH_START	0x6f
#define SPIDER_FRAME_DEATH_END		0x7c

#define SPIDER_CHAIN_PRIMARY	0
#define SPIDER_CHAIN_SECONDARY	1

#define SPIDER_STAGE_NONE	0
#define SPIDER_STAGE_FIRST	1
#define SPIDER_STAGE_SECOND	2
#define SPIDER_STAGE_FINISH	3

#define SPIDER_PAIN_DEBOUNCE	3.0f
#define SPIDER_COMBO_FIRST_WINDOW	0.8f
#define SPIDER_COMBO_CHAIN_WINDOW	0.6f
#define SPIDER_COMBO_FINISH_WINDOW	0.5f
#define SPIDER_COMBO_RECOVER_COOLDOWN	1.0f

#define SPIDER_STATE_COMBO_READY	0x00000001
#define SPIDER_STATE_COMBO_DISPATCHED	0x00000002

static int sound_sight;
static int sound_search;
static int sound_idle;
static int sound_pain1;
static int sound_pain2;
static int sound_death;
static int sound_death_thud;
static int sound_melee[3];
static int sound_step;

static void spider_idle(edict_t *self);
static void spider_sight(edict_t *self, edict_t *other);
static void spider_search(edict_t *self);
static void spider_step(edict_t *self);
static void spider_death_thud(edict_t *self);
static void spider_claw(edict_t *self);
static void spider_idle_loop(edict_t *self);
static void spider_stand(edict_t *self);
static void spider_select_locomotion(edict_t *self);
static void spider_walk(edict_t *self);
static void spider_run(edict_t *self);
static void spider_attack(edict_t *self);
static void spider_combo_entry(edict_t *self);
static void spider_continue_combo(edict_t *self);
static void spider_combo_primary_start(edict_t *self);
static void spider_combo_secondary_start(edict_t *self);
static void spider_begin_recover(edict_t *self);
static void spider_attack_recover_end(edict_t *self);
static void spider_clear_combo_state(edict_t *self);
static qboolean spider_combo_window_active(edict_t *self);
static void spider_set_combo_window(edict_t *self, float duration);
static void spider_mark_stagger(edict_t *self);
static void spider_clear_stagger(edict_t *self);
static void spider_hold_stagger(edict_t *self);
static void spider_pain(edict_t *self, edict_t *other, float kick, int damage);
static void spider_pain_recover(edict_t *self);
static void spider_dead(edict_t *self);
static void spider_die(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point);
static void spider_boss_idle(edict_t *self);

static mframe_t spider_frames_stand[] = {
	{ai_stand, 0, spider_idle},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, spider_idle},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, spider_idle},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, spider_idle},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, spider_idle},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, spider_idle},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, spider_idle},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL}
};
static mmove_t spider_move_stand = {
	SPIDER_FRAME_STAND_START,
	SPIDER_FRAME_STAND_END,
	spider_frames_stand,
	spider_idle_loop
};

static mframe_t spider_frames_boss_idle[] = {
	{ai_stand, 0, NULL},
	{ai_stand, 0, spider_idle},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, spider_idle},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, spider_idle}
};
static mmove_t spider_move_boss_idle = {
	SPIDER_FRAME_STAND_START,
	SPIDER_FRAME_STAND_START + 7,
	spider_frames_boss_idle,
	spider_idle_loop
};

static mframe_t spider_frames_walk[] = {
	{ai_walk, 10, spider_step},
	{ai_walk, 4, NULL},
	{ai_walk, 12, spider_step},
	{ai_walk, 4, NULL},
	{ai_walk, 10, spider_step},
	{ai_walk, 4, NULL},
	{ai_walk, 12, spider_step},
	{ai_walk, 4, NULL},
	{ai_walk, 10, spider_step},
	{ai_walk, 0, NULL}
};
static mmove_t spider_move_walk = {
	SPIDER_FRAME_WALK_START,
	SPIDER_FRAME_WALK_END,
	spider_frames_walk,
	spider_select_locomotion
};

static mframe_t spider_frames_run[] = {
	{ai_run, 24, spider_step},
	{ai_run, 10, NULL},
	{ai_run, 24, spider_step},
	{ai_run, 10, NULL},
	{ai_run, 24, spider_step}
};
static mmove_t spider_move_run = {
	SPIDER_FRAME_RUN_START,
	SPIDER_FRAME_RUN_END,
	spider_frames_run,
	spider_select_locomotion
};

static mframe_t spider_frames_combo_primary_entry[] = {
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL}
};
static mmove_t spider_move_combo_primary_entry = {
	SPIDER_FRAME_COMBO_PRIMARY_START,
	SPIDER_FRAME_COMBO_PRIMARY_END,
	spider_frames_combo_primary_entry,
	spider_combo_primary_start
};

static mframe_t spider_frames_combo_secondary_entry[] = {
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL}
};
static mmove_t spider_move_combo_secondary_entry = {
	SPIDER_FRAME_COMBO_SECONDARY_START,
	SPIDER_FRAME_COMBO_SECONDARY_END,
	spider_frames_combo_secondary_entry,
	spider_combo_secondary_start
};

static mframe_t spider_frames_attack_primary[] = {
	{ai_charge, 0, NULL},
	{ai_charge, 0, spider_claw},
	{ai_charge, 0, NULL},
	{ai_charge, 0, spider_claw},
	{ai_charge, 0, NULL},
	{ai_charge, 0, spider_claw},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, spider_claw},
	{ai_charge, 0, NULL}
};
static mmove_t spider_move_attack_primary = {
	SPIDER_FRAME_ATTACKA_START,
	SPIDER_FRAME_ATTACKA_END,
	spider_frames_attack_primary,
	spider_continue_combo
};

static mframe_t spider_frames_attack_secondary[] = {
	{ai_charge, 0, NULL},
	{ai_charge, 0, spider_claw},
	{ai_charge, 0, NULL},
	{ai_charge, 0, spider_claw},
	{ai_charge, 0, NULL},
	{ai_charge, 0, spider_claw}
};
static mmove_t spider_move_attack_secondary = {
	SPIDER_FRAME_ATTACKB_START,
	SPIDER_FRAME_ATTACKB_END,
	spider_frames_attack_secondary,
	spider_continue_combo
};

static mframe_t spider_frames_attack_finisher[] = {
	{ai_charge, 0, NULL},
	{ai_charge, 0, spider_claw},
	{ai_charge, 0, NULL},
	{ai_charge, 0, spider_claw},
	{ai_charge, 0, NULL}
};
static mmove_t spider_move_attack_finisher = {
	SPIDER_FRAME_ATTACK_FINISH_START,
	SPIDER_FRAME_ATTACK_FINISH_END,
	spider_frames_attack_finisher,
	spider_begin_recover
};

static mframe_t spider_frames_attack_recover[] = {
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}
};
static mmove_t spider_move_attack_recover = {
	SPIDER_FRAME_ATTACK_RECOVER_START,
	SPIDER_FRAME_ATTACK_RECOVER_END,
	spider_frames_attack_recover,
	spider_attack_recover_end
};

static mframe_t spider_frames_pain[] = {
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}
};
static mmove_t spider_move_pain = {
	SPIDER_FRAME_PAIN_START,
	SPIDER_FRAME_PAIN_END,
	spider_frames_pain,
	spider_pain_recover
};

static mframe_t spider_frames_death[] = {
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, spider_death_thud},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, spider_dead},
	{ai_move, 0, spider_dead},
	{ai_move, 0, spider_dead},
	{ai_move, 0, spider_dead},
	{ai_move, 0, spider_dead},
	{ai_move, 0, spider_dead}
};
static mmove_t spider_move_death = {
	SPIDER_FRAME_DEATH_START,
	SPIDER_FRAME_DEATH_END,
	spider_frames_death,
	spider_dead
};

/*
=============
spider_idle

Play the idle breathing bark with a modest random cadence.
=============
*/
static void spider_idle(edict_t *self)
{
	if (random() < 0.25f)
	{
	gi.sound(self, CHAN_VOICE, sound_idle, 1.0f, ATTN_IDLE, 0.0f);
	}
}

/*
=============
spider_sight

Trigger the spider's alert bark on first sight of an enemy.
=============
*/
static void spider_sight(edict_t *self, edict_t *other)
{
	gi.sound(self, CHAN_VOICE, sound_sight, 1.0f, ATTN_NORM, 0.0f);
}

/*
=============
spider_search

Emit the passive search loop when the spider has lost track of enemies.
=============
*/
static void spider_search(edict_t *self)
{
	gi.sound(self, CHAN_VOICE, sound_search, 1.0f, ATTN_IDLE, 0.0f);
}

/*
=============
spider_step

Play the heavy metal footstep for locomotion beats.
=============
*/
static void spider_step(edict_t *self)
{
	gi.sound(self, CHAN_BODY, sound_step, 1.0f, ATTN_NORM, 0.0f);
}

/*
=============
spider_death_thud

Play the separate impact for fatal knockdowns.
=============
*/
static void spider_death_thud(edict_t *self)
{
	gi.sound(self, CHAN_BODY, sound_death_thud, 1.0f, ATTN_NORM, 0.0f);
}

/*
=============
spider_claw

Deliver a melee slash if the target is still within range.
=============
*/
static void spider_claw(edict_t *self)
{
	vec3_t forward;
	float damage = 30.0f;

	if (!self->enemy)
	{
	return;
	}

	AngleVectors(self->s.angles, forward, NULL, NULL);

	if (range(self, self->enemy) > RANGE_MELEE)
	{
	return;
	}

	gi.sound(self, CHAN_WEAPON, sound_melee[rand() % 3], 1.0f, ATTN_NORM, 0.0f);
	T_Damage(self->enemy, self, self, forward, self->enemy->s.origin, vec3_origin, (int)damage, (int)damage, 0, MOD_HIT);
}

/*
=============
spider_idle_loop

Select the appropriate idle sequence based on spawn configuration.
=============
*/
static void spider_idle_loop(edict_t *self)
{
	if (self->oblivion.spider_alt_idle)
	{
	self->monsterinfo.currentmove = &spider_move_boss_idle;
	}
	else
	{
	self->monsterinfo.currentmove = &spider_move_stand;
	}
}

/*
=============
spider_boss_idle

Force the boss variant into its alternate idle chain.
=============
*/
static void spider_boss_idle(edict_t *self)
{
	self->monsterinfo.currentmove = &spider_move_boss_idle;
}

/*
=============
spider_stand

Reset the spider to its default idle behaviour.
=============
*/
static void spider_stand(edict_t *self)
{
	spider_idle_loop(self);
}

/*
=============
spider_combo_window_active

Return whether the melee combo chaining window is active.
=============
*/
static qboolean spider_combo_window_active(edict_t *self)
{
	return (self->state_flags & SPIDER_STATE_COMBO_READY) && self->state_time > level.time;
}

/*
=============
spider_set_combo_window

Arm or refresh the combo window using the supplied duration.
=============
*/
static void spider_set_combo_window(edict_t *self, float duration)
{
	self->state_flags |= SPIDER_STATE_COMBO_READY;
	self->state_time = level.time + duration;
}

/*
=============
spider_clear_combo_state

Reset the combo bookkeeping fields after attack interruption.
=============
*/
static void spider_clear_combo_state(edict_t *self)
{
	self->oblivion.spider_combo_stage = SPIDER_STAGE_NONE;
	self->oblivion.spider_combo_last = SPIDER_CHAIN_PRIMARY;
	self->state_flags &= ~(SPIDER_STATE_COMBO_READY | SPIDER_STATE_COMBO_DISPATCHED);
	self->state_time = 0.0f;
}

/*
=============
spider_mark_stagger

Record that the spider is currently staggered by a pain reaction.
=============
*/
static void spider_mark_stagger(edict_t *self)
{
	self->oblivion.spider_staggered = true;
	self->oblivion.spider_stagger_time = self->pain_debounce_time;
	spider_clear_combo_state(self);
}

/*
=============
spider_clear_stagger

Clear the stagger flag so locomotion may resume.
=============
*/
static void spider_clear_stagger(edict_t *self)
{
	self->oblivion.spider_staggered = false;
	self->oblivion.spider_stagger_time = 0.0f;
}

/*
=============
spider_hold_stagger

Pin the final pain frame until the stagger timer expires.
=============
*/
static void spider_hold_stagger(edict_t *self)
{
	if (level.time < self->oblivion.spider_stagger_time)
	{
		self->monsterinfo.nextframe = self->s.frame;
		return;
	}

	spider_clear_stagger(self);
}

/*
=============
spider_select_locomotion

Choose between idle, walk, run, or melee initiation based on range.
=============
*/
static void spider_select_locomotion(edict_t *self)
{
	if ((self->monsterinfo.aiflags & AI_STAND_GROUND) || !self->enemy)
	{
	spider_stand(self);
	return;
	}

	if (self->oblivion.spider_staggered)
	{
	spider_stand(self);
	return;
	}

	if (range(self, self->enemy) > RANGE_MELEE)
	{
	if (random() > 0.35f)
	{
	    self->monsterinfo.currentmove = &spider_move_run;
	}
	else
	{
	    self->monsterinfo.currentmove = &spider_move_walk;
	}
	}
	else
	{
	spider_attack(self);
	}
}

/*
=============
spider_walk

Entry point for walk requests; delegates to locomotion selection.
=============
*/
static void spider_walk(edict_t *self)
{
	spider_select_locomotion(self);
}

/*
=============
spider_run

Entry point for run requests; delegates to locomotion selection.
=============
*/
static void spider_run(edict_t *self)
{
	spider_select_locomotion(self);
}

/*
=============
spider_attack

Request that the spider begin or continue a melee combo.
=============
*/
static void spider_attack(edict_t *self)
{
	if (self->oblivion.spider_staggered)
	{
		return;
	}

	if (self->monsterinfo.attack_finished > level.time)
	{
		return;
	}

	if (self->state_flags & SPIDER_STATE_COMBO_DISPATCHED)
	{
		return;
	}

	self->state_flags |= SPIDER_STATE_COMBO_DISPATCHED;
	spider_combo_entry(self);
}

/*
=============
spider_combo_primary_start

Switch into the primary strike chain after the combo entry windup.
=============
*/
static void spider_combo_primary_start(edict_t *self)
{
	self->monsterinfo.currentmove = &spider_move_attack_primary;
}

/*
=============
spider_combo_secondary_start

Switch into the secondary strike chain after the combo entry windup.
=============
*/
static void spider_combo_secondary_start(edict_t *self)
{
	self->monsterinfo.currentmove = &spider_move_attack_secondary;
}

/*
=============
spider_combo_entry

Start a melee combo using the alternating chain logic.
=============
*/
static void spider_combo_entry(edict_t *self)
{
	int next_chain;

	if (!self->enemy)
	{
		spider_stand(self);
		return;
	}

	if (self->oblivion.spider_combo_stage != SPIDER_STAGE_NONE && spider_combo_window_active(self))
	{
		return;
	}

	next_chain = self->oblivion.spider_combo_next;
	self->oblivion.spider_combo_next ^= 1;
	self->oblivion.spider_combo_last = next_chain;
	self->oblivion.spider_combo_stage = SPIDER_STAGE_FIRST;
	spider_set_combo_window(self, SPIDER_COMBO_FIRST_WINDOW);

	if (next_chain == SPIDER_CHAIN_PRIMARY)
	{
		self->monsterinfo.currentmove = &spider_move_combo_primary_entry;
	}
	else
	{
		self->monsterinfo.currentmove = &spider_move_combo_secondary_entry;
	}
}

/*
=============
spider_continue_combo

Advance through the chained melee sequences while the window is open.
=============
*/
static void spider_continue_combo(edict_t *self)
{
	if (!self->enemy || range(self, self->enemy) > RANGE_MELEE)
	{
		spider_begin_recover(self);
		return;
	}

	if (!spider_combo_window_active(self))
	{
		spider_begin_recover(self);
		return;
	}

	if (self->oblivion.spider_combo_stage == SPIDER_STAGE_FIRST)
	{
		int follow_up = (self->oblivion.spider_combo_last == SPIDER_CHAIN_PRIMARY) ? SPIDER_CHAIN_SECONDARY : SPIDER_CHAIN_PRIMARY;

		self->oblivion.spider_combo_last = follow_up;
		self->oblivion.spider_combo_stage = SPIDER_STAGE_SECOND;
		spider_set_combo_window(self, SPIDER_COMBO_CHAIN_WINDOW);

		if (follow_up == SPIDER_CHAIN_PRIMARY)
		{
			self->monsterinfo.currentmove = &spider_move_combo_primary_entry;
		}
		else
		{
			self->monsterinfo.currentmove = &spider_move_combo_secondary_entry;
		}
		return;
	}

	self->oblivion.spider_combo_stage = SPIDER_STAGE_FINISH;
	spider_set_combo_window(self, SPIDER_COMBO_FINISH_WINDOW);
	self->monsterinfo.currentmove = &spider_move_attack_finisher;
}

/*
=============
spider_begin_recover

Enter the recovery animation and clear combo state.
=============
*/
static void spider_begin_recover(edict_t *self)
{
	spider_clear_combo_state(self);
	self->monsterinfo.currentmove = &spider_move_attack_recover;
}

/*
=============
spider_attack_recover_end

Finish the recovery cycle and decide on the next behaviour.
=============
*/
static void spider_attack_recover_end(edict_t *self)
{
	self->monsterinfo.attack_finished = level.time + SPIDER_COMBO_RECOVER_COOLDOWN + random() * 0.4f;
	spider_clear_combo_state(self);

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
	spider_stand(self);
	}
	else if (self->enemy && range(self, self->enemy) <= RANGE_MELEE && random() > 0.6f)
	{
	spider_combo_entry(self);
	}
	else
	{
	spider_select_locomotion(self);
	}
}

/*
=============
spider_pain

Handle stagger tracking, cooldown management, and pain animation entry.
=============
*/
static void spider_pain(edict_t *self, edict_t *other, float kick, int damage)
{
	qboolean play_secondary = (rand() & 1);

	if (level.time < self->pain_debounce_time)
	{
		return;
	}

	self->pain_debounce_time = level.time + SPIDER_PAIN_DEBOUNCE;
	gi.sound(self, CHAN_VOICE, sound_pain1, 1.0f, ATTN_NORM, 0.0f);

	if (play_secondary)
	{
		gi.sound(self, CHAN_BODY, sound_pain2, 1.0f, ATTN_NORM, 0.0f);
	}
	spider_mark_stagger(self);
	self->monsterinfo.currentmove = &spider_move_pain;
}

/*
=============
spider_pain_recover

Clear the stagger flag and resume locomotion after a pain reaction.
=============
*/
static void spider_pain_recover(edict_t *self)
{
	spider_hold_stagger(self);

	if (self->oblivion.spider_staggered)
	{
		return;
	}

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		spider_stand(self);
	}
	else
	{
		spider_select_locomotion(self);
	}
}

/*
=============
spider_dead

Finalise death state and allow further damage to gib the corpse.
=============
*/
static void spider_dead(edict_t *self)
{
	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_YES;
}

/*
=============
spider_die

Play death audio, spawn gibs when appropriate, and trigger the death mmove.
=============
*/
static void spider_die(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	gi.sound(self, CHAN_VOICE, sound_death, 1.0f, ATTN_NORM, 0.0f);

	if (self->health <= self->gib_health)
	{
		gi.sound(self, CHAN_VOICE, gi.soundindex("misc/udeath.wav"), 1.0f, ATTN_NORM, 0.0f);
		ThrowGib(self, "models/objects/gibs/sm_metal/tris.md2", damage, GIB_METALLIC);
		ThrowGib(self, "models/objects/gibs/chest/tris.md2", damage, GIB_METALLIC);
		ThrowHead(self, "models/objects/gibs/head2/tris.md2", damage, GIB_ORGANIC);
		return;
	}

	self->monsterinfo.currentmove = &spider_move_death;
}

/*
=============
SP_monster_spider

Spawn function for the Oblivion spider tank.
=============
*/
void SP_monster_spider(edict_t *self)
{
	if (deathmatch->value)
	{
		G_FreeEdict(self);
		return;
	}

	self->s.modelindex = gi.modelindex("models/monsters/spider/tris.md2");
	VectorSet(self->mins, -32.0f, -32.0f, -32.0f);
	VectorSet(self->maxs, 32.0f, 32.0f, 32.0f);
	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->mass = 300;

	sound_sight = gi.soundindex("spider/sight.wav");
	sound_search = gi.soundindex("gladiator/gldsrch1.wav");
	sound_idle = gi.soundindex("gladiator/gldidle1.wav");
	sound_pain1 = gi.soundindex("gladiator/pain.wav");
	sound_pain2 = gi.soundindex("gladiator/gldpain2.wav");
	sound_death = gi.soundindex("gladiator/glddeth1.wav");
	sound_melee[0] = gi.soundindex("gladiator/melee1.wav");
	sound_melee[1] = gi.soundindex("gladiator/melee2.wav");
	sound_melee[2] = gi.soundindex("gladiator/melee3.wav");
	sound_step = gi.soundindex("mutant/thud1.wav");
	sound_death_thud = gi.soundindex("mutant/thud2.wav");

	self->s.sound = sound_idle;

	self->health = 400;
	self->gib_health = -120;

	self->pain = spider_pain;
	self->die = spider_die;

	self->monsterinfo.stand = spider_stand;
	self->monsterinfo.idle = spider_idle_loop;
	self->monsterinfo.walk = spider_walk;
	self->monsterinfo.run = spider_run;
	self->monsterinfo.attack = spider_attack;
	self->monsterinfo.melee = spider_attack;
	self->monsterinfo.sight = spider_sight;
	self->monsterinfo.search = spider_search;

	self->oblivion.spider_combo_next = SPIDER_CHAIN_PRIMARY;
	spider_clear_combo_state(self);
	spider_clear_stagger(self);
	self->oblivion.spider_alt_idle = (self->spawnflags & 0x100) != 0;

	if (self->oblivion.spider_alt_idle)
	{
		VectorSet(self->mins, -48.0f, -48.0f, -40.0f);
		VectorSet(self->maxs, 48.0f, 48.0f, 48.0f);
		self->movetype = MOVETYPE_STEP;
	}

	spider_stand(self);

	walkmonster_start(self);
}
