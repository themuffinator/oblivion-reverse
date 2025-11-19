/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// g_actor.c

#include "g_local.h"
#include "m_actor.h"
#include "m_actor_anim.h"

#define	MAX_ACTOR_NAMES		8
#define ACTOR_CHAT_COOLDOWN	2.0f

extern mmove_t actor_move_stand;
extern mmove_t actor_move_flipoff;
extern mmove_t actor_move_taunt;

static void Actor_PathAssignController(edict_t *self, edict_t *controller);

/*
 * Oblivion extends the Quake II actor AI flags with additional high bits
 * that coordinate scripted mission controllers.  Use local names so the
 * spawn routine can describe the behaviour from the HLIL dump without
 * resorting to raw literals.
 */
#define	AI_ACTOR_SHOOT_ONCE		0x04000000
#define	AI_ACTOR_PATH_IDLE		0x02000000
#define	AI_ACTOR_FRIENDLY		0x01000000

/* misc_actor spawnflags are declared in m_actor.h */

static const char *Actor_FallbackName(edict_t *self)
{
        static const char *const fallback[MAX_ACTOR_NAMES] =
        {
                "Hellrot",
                "Tokay",
                "Killme",
                "Disruptor",
                "Adrianator",
                "Rambear",
                "Titus",
                "Bitterman"
        };
        int index;

        if (!self)
                return fallback[0];

        index = (int)(self - g_edicts);
        if (index < 0)
                index = 0;

        return fallback[index % MAX_ACTOR_NAMES];
}

static const char *Actor_DisplayName(edict_t *self)
{
        if (self && self->oblivion.custom_name && self->oblivion.custom_name[0])
                return self->oblivion.custom_name;

        return Actor_FallbackName(self);
}

/*
=============
Actor_ResetChatCooldown

Allow the actor to speak immediately by aligning the broadcast timer with the
current level clock.
=============
*/
static void Actor_ResetChatCooldown(edict_t *self)
{
	if (!self)
		return;

	self->oblivion.custom_name_time = level.time;
}

/*
=============
Actor_BroadcastMessage

Broadcast a chat line to every active client while honouring the cooldown
timer stored in the oblivion extension.
=============
*/
static void Actor_BroadcastMessage(edict_t *self, const char *message)
{
	const char *name;
	int i;

	if (!self || !message || !message[0])
		return;

	if (level.time < self->oblivion.custom_name_time)
		return;

	name = Actor_DisplayName(self);
	self->oblivion.custom_name_time = level.time + ACTOR_CHAT_COOLDOWN;

	for (i = 1; i <= game.maxclients; i++)
	{
		edict_t *ent = &g_edicts[i];

		if (!ent->inuse)
			continue;

		gi.cprintf(ent, PRINT_CHAT, "%s: %s\n", name, message);
	}
}

static void Actor_ConfigureMovementState(edict_t *self)
{
        if (!self)
                return;

        self->movetype = MOVETYPE_STEP;
        self->solid = SOLID_BBOX;
        self->clipmask = MASK_MONSTERSOLID;
        VectorSet(self->mins, -16, -16, -24);
        VectorSet(self->maxs, 16, 16, 32);

        if (self->spawnflags & ACTOR_SPAWNFLAG_CORPSE)
        {
                VectorSet(self->mins, -16, -16, -24);
                VectorSet(self->maxs, 16, 16, -8);
                self->health = -1;
                self->max_health = self->health;
                self->deadflag = DEAD_DEAD;
                self->takedamage = DAMAGE_YES;
                self->svflags |= SVF_DEADMONSTER;
        }

        self->monsterinfo.currentmove = &actor_move_stand;
        self->monsterinfo.scale = MODEL_SCALE;
}

static void Actor_InitMissionTimer(edict_t *self)
{
	if (!self)
		return;

	if (self->oblivion.mission_timer_limit < 0)
		self->oblivion.mission_timer_limit = 0;

	if (self->oblivion.mission_timer_remaining <= 0
		&& self->oblivion.mission_timer_limit > 0)
	{
		self->oblivion.mission_timer_remaining = self->oblivion.mission_timer_limit;
	}
}

/*
=============
Actor_PathResetState

Clear the Oblivion path bookkeeping so dormant actors begin in a
consistent idle state.
=============
*/
static void Actor_PathResetState(edict_t *self)
{
	if (!self)
		return;

	self->oblivion.controller = NULL;
	self->oblivion.last_controller = NULL;
	self->oblivion.prev_path = NULL;
	self->oblivion.path_target = NULL;
	self->oblivion.script_target = NULL;
	self->oblivion.controller_serial = 0;
	self->oblivion.controller_distance = 0.0f;
	self->oblivion.controller_resume = 0.0f;
	self->oblivion.path_wait_time = -1.0f;
	self->oblivion.path_time = 0.0f;
	self->oblivion.path_speed = (self->speed > 0.0f) ? self->speed : 0.0f;
	self->oblivion.path_step_speed = 0.0f;
	self->oblivion.path_remaining = 0.0f;
	self->oblivion.path_state = ACTOR_PATH_STATE_IDLE;
	VectorClear(self->oblivion.path_dir);
	VectorClear(self->oblivion.path_velocity);
	self->oblivion.path_toggle = 0;
}

/*
=============
Actor_ApplySpawnAIFeatures

Mirror the `sub_1001f460` HLIL writes so the actor spawn only touches
`AI_ACTOR_PATH_IDLE`, `AI_ACTOR_FRIENDLY`, and `AI_STAND_GROUND` the way the
retail DLL does. Unlike Quake II, the Oblivion binary never toggles
`AI_GOOD_GUY` here, so the translation leaves that bit untouched on purpose to
avoid inventing behaviour that the dump does not prove exists.
=============
*/
static void Actor_ApplySpawnAIFeatures(edict_t *self)
{
	const int spawnflags = self ? self->spawnflags : 0;

	if (!self)
		return;

	if (self->target)
		self->monsterinfo.aiflags &= ~AI_ACTOR_PATH_IDLE;
	else
		self->monsterinfo.aiflags |= AI_ACTOR_PATH_IDLE;

	if (spawnflags & ACTOR_SPAWNFLAG_WIMPY)
		self->monsterinfo.aiflags &= ~AI_ACTOR_FRIENDLY;
	else
		self->monsterinfo.aiflags |= AI_ACTOR_FRIENDLY;

	self->monsterinfo.aiflags |= AI_STAND_GROUND;
}

/*
=============
Actor_PathSelectIdleAnimation

Randomise the idle animation and face the current controller while the
actor waits at a mission node.
=============
*/
static void Actor_PathSelectIdleAnimation(edict_t *self, edict_t *controller)
{
	vec3_t dir;
	float delay;
	int choice;

	if (!self)
		return;

	if (level.time < self->oblivion.controller_resume)
		return;

	self->oblivion.controller_resume = level.time + 3.0f;

	if (controller)
	{
		VectorSubtract(controller->s.origin, self->s.origin, dir);
		self->ideal_yaw = self->s.angles[YAW] = vectoyaw(dir);
		VectorNormalize(dir);
		VectorCopy(dir, self->oblivion.path_dir);
		VectorScale(dir, self->oblivion.path_speed, self->oblivion.path_velocity);
	}

	choice = rand() % 3;
	if (choice == 0)
	{
		if (self->monsterinfo.stand)
			self->monsterinfo.stand(self);
	}
	else if (choice == 1)
	{
		self->monsterinfo.currentmove = &actor_move_flipoff;
	}
	else
	{
		self->monsterinfo.currentmove = &actor_move_taunt;
	}

	delay = 1.0f + random();
	self->monsterinfo.pausetime = level.time + delay;
}

/*
=============
Actor_PathScheduleIdle

Drive the pause timer while the actor waits without an active
controller, mirroring the scheduling in the HLIL think loop.
=============
*/
static void Actor_PathScheduleIdle(edict_t *self)
{
	float delay;

	if (!self)
		return;

	if (self->monsterinfo.stand)
		self->monsterinfo.stand(self);

	delay = 1.0f + random();
	self->monsterinfo.pausetime = level.time + delay;
	self->monsterinfo.aiflags |= AI_ACTOR_PATH_IDLE;
}

/*
=============
Actor_PathReconcileTargets

Keep the cached path controller in sync with live entities so the HUD
and scripted movement survive save/load cycles.
=============
*/
static void Actor_PathReconcileTargets(edict_t *self)
{
	edict_t *controller;

	if (!self)
		return;

	controller = self->oblivion.controller;

	if (controller && !controller->inuse)
		controller = NULL;

	if (self->oblivion.path_target && !self->oblivion.path_target->inuse)
		self->oblivion.path_target = NULL;

	if (!controller && self->oblivion.path_target)
		controller = self->oblivion.path_target;

	if (controller != self->oblivion.controller)
		Actor_PathAssignController(self, controller);

	if (self->oblivion.script_target && !self->oblivion.script_target->inuse)
		self->oblivion.script_target = NULL;

	controller = self->oblivion.controller;

	if (controller && !self->goalentity)
		self->goalentity = controller;

	if (self->oblivion.controller && !self->goalentity)
		self->goalentity = self->oblivion.controller;

	self->oblivion.controller_serial = controller ? controller->count : 0;

	if (self->oblivion.controller)
		self->oblivion.controller_serial = self->oblivion.controller->count;
}

/*
=============
Actor_PathThink

Advance the scripted mission controller and preserve the standard
monster think behaviour.
=============
*/
static void Actor_PathThink(edict_t *self)
{
	if (!self)
		return;

	Actor_PathReconcileTargets(self);

	if ((self->oblivion.path_state == ACTOR_PATH_STATE_WAITING
		|| self->oblivion.path_state == ACTOR_PATH_STATE_IDLE)
		&& self->oblivion.prev_path)
	{
		Actor_PathSelectIdleAnimation(self, self->oblivion.prev_path);
	}

	if (self->oblivion.path_state == ACTOR_PATH_STATE_WAITING)
	{
		if (level.time >= self->oblivion.path_time)
		{
			if (self->oblivion.controller)
			{
				self->oblivion.path_state = ACTOR_PATH_STATE_SEEKING;
				self->monsterinfo.aiflags &= ~AI_ACTOR_PATH_IDLE;
				self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;
				if (!self->enemy && self->monsterinfo.walk)
					self->monsterinfo.walk(self);
			}
			else
			{
				self->oblivion.path_state = ACTOR_PATH_STATE_IDLE;
				Actor_PathScheduleIdle(self);
			}
		}
		else
		{
			self->monsterinfo.aiflags |= AI_HOLD_FRAME;
		}
	}
	else if (!self->oblivion.controller
		&& self->oblivion.path_state == ACTOR_PATH_STATE_IDLE)
	{
		if (!(self->monsterinfo.aiflags & AI_ACTOR_PATH_IDLE))
			Actor_PathScheduleIdle(self);
	}
	else
	{
		self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;
	}

	if (self->oblivion.controller
		&& self->oblivion.controller_serial != self->oblivion.controller->count)
	{
		self->oblivion.controller_serial = self->oblivion.controller->count;
	}

	self->think = Actor_PathThink;
	self->nextthink = level.time + FRAMETIME;
	monster_think(self);
}


/*
=============
Actor_PathAssignController

Bind the actor to a target_actor so the auxiliary path tracking mirrors
the Quake II goalentity bookkeeping.
=============
*/
static void Actor_PathAssignController(edict_t *self, edict_t *controller)
{
	vec3_t delta;
	float distance;

	if (!self)
		return;

	self->oblivion.controller = controller;
	self->oblivion.path_target = controller;
	self->oblivion.controller_serial = controller ? controller->count : 0;
	self->oblivion.controller_resume = level.time;
	self->oblivion.path_time = level.time;
	self->oblivion.path_speed = (self->speed > 0.0f) ? self->speed : self->oblivion.path_speed;

	if (!controller)
	{
		self->oblivion.controller_distance = 0.0f;
		self->oblivion.path_remaining = 0.0f;
		self->oblivion.path_step_speed = 0.0f;
		self->oblivion.path_state = ACTOR_PATH_STATE_IDLE;
		VectorClear(self->oblivion.path_dir);
		VectorClear(self->oblivion.path_velocity);
		self->oblivion.path_toggle = 0;
		Actor_PathScheduleIdle(self);
		return;
	}

	VectorSubtract(controller->s.origin, self->s.origin, delta);
	distance = VectorNormalize(delta);
	self->oblivion.controller_distance = distance;
	self->oblivion.path_remaining = distance;
	self->oblivion.path_step_speed = 0.0f;
	self->oblivion.path_state = ACTOR_PATH_STATE_SEEKING;
	VectorCopy(delta, self->oblivion.path_dir);
	VectorScale(delta, self->oblivion.path_speed, self->oblivion.path_velocity);
}


/*
=============
Actor_PathAdvance

Record the latest waypoint and begin tracking the next controller in the
scripted sequence.
=============
*/
static void Actor_PathAdvance(edict_t *self, edict_t *current, edict_t *next_target)
{
	if (!self)
		return;

	self->oblivion.prev_path = current;
	self->oblivion.last_controller = current;
	self->oblivion.path_wait_time = -1.0f;
	self->oblivion.script_target = NULL;
	self->oblivion.path_toggle ^= 1;
	self->oblivion.controller_serial = next_target ? next_target->count : 0;

	Actor_PathAssignController(self, next_target);
}


/*
=============
Actor_PathResolveWait

Return the wait duration that applies when the actor reaches a node,
falling back to the waypoint's wait key when no override is queued.
=============
*/
static float Actor_PathResolveWait(edict_t *self, edict_t *node)
{
	float wait;

	if (!self)
		return 0.0f;

	wait = self->oblivion.path_wait_time;
	if (wait < 0.0f)
	{
		if (node)
			wait = node->wait;
		else
			wait = 0.0f;
	}

	if (wait < 0.0f)
		wait = 0.0f;

	return wait;
}

/*
=============
Actor_PathApplyWait

Update the actor's internal path state machine to respect a scripted
pause before resuming motion toward the next controller.
=============
*/
static void Actor_PathApplyWait(edict_t *self, float wait)
{
	if (!self)
		return;

	self->oblivion.path_wait_time = -1.0f;

	if (wait > 0.0f)
	{
		self->oblivion.path_state = ACTOR_PATH_STATE_WAITING;
		self->oblivion.path_time = level.time + wait;
		self->monsterinfo.aiflags |= AI_HOLD_FRAME;
		return;
	}

	self->oblivion.path_time = level.time;
	self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;

	if (self->oblivion.controller)
	{
		self->oblivion.path_state = ACTOR_PATH_STATE_SEEKING;
		return;
	}

	self->oblivion.path_state = ACTOR_PATH_STATE_IDLE;
	Actor_PathScheduleIdle(self);
}

/*
=============
Actor_PathTrackController

Refresh the cached direction and velocity used by the mission HUD while
the actor marches toward its controller target.
=============
*/
static void Actor_PathTrackController(edict_t *self)
{
	vec3_t delta;
	float distance;

	if (!self)
		return;

	if (!self->oblivion.controller || !self->oblivion.controller->inuse)
	{
		if (self->oblivion.controller && !self->oblivion.controller->inuse)
		{
			self->oblivion.controller = NULL;
			self->oblivion.path_target = NULL;
		}

		self->oblivion.controller_distance = 0.0f;
		self->oblivion.path_remaining = 0.0f;
		self->oblivion.path_step_speed = VectorLength(self->velocity);
		VectorClear(self->oblivion.path_dir);
		VectorCopy(self->velocity, self->oblivion.path_velocity);

		if (!self->oblivion.controller && self->oblivion.path_state != ACTOR_PATH_STATE_WAITING)
			self->oblivion.path_state = ACTOR_PATH_STATE_IDLE;

		return;
	}

	VectorSubtract(self->oblivion.controller->s.origin, self->s.origin, delta);
	distance = VectorNormalize(delta);
	self->oblivion.controller_distance = distance;
	self->oblivion.path_remaining = distance;
	VectorCopy(delta, self->oblivion.path_dir);
	self->oblivion.path_step_speed = VectorLength(self->velocity);
	VectorCopy(self->velocity, self->oblivion.path_velocity);

	if (self->oblivion.path_step_speed <= 0.0f)
		VectorScale(delta, self->oblivion.path_speed, self->oblivion.path_velocity);
}

/*
=============
Actor_UpdateMissionObjective

Publish pending mission events to the HUD via Mission_TargetHelpFired.
=============
*/
static void Actor_UpdateMissionObjective(edict_t *self)
{
	if (!self)
		return;

	if (self->oblivion.mission_state)
	{
		if (self->oblivion.mission_timer_limit > 0
			&& self->oblivion.mission_timer_remaining <= 0)
		{
			self->oblivion.mission_timer_remaining = self->oblivion.mission_timer_limit;
		}

		if (Mission_TargetHelpFired(self, self))
			self->oblivion.mission_state = 0;
	}
}

/*
=============
Actor_PreThink

Update the actor's mission and path bookkeeping before physics runs.
=============
*/
static void Actor_PreThink(edict_t *self)
{
	if (!self)
		return;

	Actor_PathTrackController(self);

	if (self->oblivion.path_state == ACTOR_PATH_STATE_WAITING
		&& level.time >= self->oblivion.path_time)
	{
		if (self->oblivion.controller)
		{
			self->oblivion.path_state = ACTOR_PATH_STATE_SEEKING;
			if (!self->enemy && self->monsterinfo.walk)
				self->monsterinfo.walk(self);
		}
		else
		{
			self->oblivion.path_state = ACTOR_PATH_STATE_IDLE;
		}
	}

	Actor_UpdateMissionObjective(self);
}


static qboolean Actor_AttachController(edict_t *self, edict_t *controller)
{
	vec3_t dir;

	if (!self)
		return false;

	self->goalentity = controller;
	self->movetarget = controller;
	self->monsterinfo.aiflags &= ~AI_ACTOR_PATH_IDLE;

	if (!controller || !controller->classname
		|| strcmp(controller->classname, "target_actor") != 0)
	{
		self->goalentity = NULL;
		self->movetarget = NULL;
		return false;
	}

	VectorSubtract(controller->s.origin, self->s.origin, dir);
	self->ideal_yaw = self->s.angles[YAW] = vectoyaw(dir);
	self->monsterinfo.walk(self);

	Actor_PathAssignController(self, controller);
	self->oblivion.last_controller = controller;
	self->oblivion.controller_distance = VectorLength(dir);
	self->oblivion.controller_resume = level.time;
	Actor_ResetChatCooldown(self);

	return true;
}

mframe_t actor_frames_stand [] =
{
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,

	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,

	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,

	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL
};
mmove_t actor_move_stand = {FRAME_stand101, FRAME_stand140, actor_frames_stand, NULL};

void actor_stand (edict_t *self)
{
	self->monsterinfo.currentmove = &actor_move_stand;

	// randomize on startup
	if (level.time < 1.0)
		self->s.frame = self->monsterinfo.currentmove->firstframe + (rand() % (self->monsterinfo.currentmove->lastframe - self->monsterinfo.currentmove->firstframe + 1));
}


mframe_t actor_frames_walk [] =
{
	ai_walk, 0,  NULL,
	ai_walk, 6,  NULL,
	ai_walk, 10, NULL,
	ai_walk, 3,  NULL,
	ai_walk, 2,  NULL,
	ai_walk, 7,  NULL,
	ai_walk, 10, NULL,
	ai_walk, 1,  NULL,
	ai_walk, 4,  NULL,
	ai_walk, 0,  NULL,
	ai_walk, 0,  NULL
};
mmove_t actor_move_walk = {FRAME_walk01, FRAME_walk08, actor_frames_walk, NULL};

/*
=============
actor_walk

Synchronise the actor walk cycle with the Oblivion path state machine.
=============
*/
void actor_walk (edict_t *self)
{
	if (!self)
		return;

	self->monsterinfo.aiflags &= ~AI_ACTOR_PATH_IDLE;

	if (self->oblivion.path_state != ACTOR_PATH_STATE_WAITING)
	{
		self->oblivion.path_state = ACTOR_PATH_STATE_SEEKING;
		self->oblivion.path_time = level.time;
	}

	if (self->oblivion.controller && !self->enemy)
	{
		self->goalentity = self->oblivion.controller;
		self->movetarget = self->oblivion.controller;
	}

	self->monsterinfo.currentmove = &actor_move_walk;
}



mframe_t actor_frames_run [] =
{
	ai_run, 4,  NULL,
	ai_run, 15, NULL,
	ai_run, 15, NULL,
	ai_run, 8,  NULL,
	ai_run, 20, NULL,
	ai_run, 15, NULL,
	ai_run, 8,  NULL,
	ai_run, 17, NULL,
	ai_run, 12, NULL,
	ai_run, -2, NULL,
	ai_run, -2, NULL,
	ai_run, -1, NULL
};
mmove_t actor_move_run = {FRAME_run02, FRAME_run07, actor_frames_run, NULL};

/*
=============
actor_run

Advance the actor's run behaviour, clearing single-shot locks before
resuming scripted movement after scripted attacks.
=============
*/
void actor_run (edict_t *self)
{
	if (!self)
		return;

	if (self->monsterinfo.aiflags & AI_ACTOR_SHOOT_ONCE)
	{
		self->monsterinfo.aiflags &= ~(AI_ACTOR_SHOOT_ONCE | AI_STAND_GROUND);
		self->enemy = NULL;

		if (self->movetarget)
		{
			self->goalentity = self->movetarget;
			self->monsterinfo.aiflags &= ~AI_ACTOR_PATH_IDLE;
		}
	}

	if ((level.time < self->pain_debounce_time) && (!self->enemy))
	{
		if (self->movetarget)
			actor_walk(self);
		else
			actor_stand(self);
		return;
	}

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		actor_stand(self);
		return;
	}

	self->monsterinfo.aiflags &= ~AI_ACTOR_PATH_IDLE;

	if (self->oblivion.path_state != ACTOR_PATH_STATE_WAITING)
	{
		self->oblivion.path_state = ACTOR_PATH_STATE_SEEKING;
		self->oblivion.path_time = level.time;
	}

	if (self->oblivion.controller && !self->enemy)
	{
		self->goalentity = self->oblivion.controller;
		if (!self->movetarget)
			self->movetarget = self->oblivion.controller;
	}

	self->monsterinfo.currentmove = &actor_move_run;
}




mframe_t actor_frames_pain1 [] =
{
	ai_move, -5, NULL,
	ai_move, 4,  NULL,
	ai_move, 1,  NULL
};
mmove_t actor_move_pain1 = {FRAME_pain101, FRAME_pain103, actor_frames_pain1, actor_run};

mframe_t actor_frames_pain2 [] =
{
	ai_move, -4, NULL,
	ai_move, 4,  NULL,
	ai_move, 0,  NULL
};
mmove_t actor_move_pain2 = {FRAME_pain201, FRAME_pain203, actor_frames_pain2, actor_run};

mframe_t actor_frames_pain3 [] =
{
	ai_move, -1, NULL,
	ai_move, 1,  NULL,
	ai_move, 0,  NULL
};
mmove_t actor_move_pain3 = {FRAME_pain301, FRAME_pain303, actor_frames_pain3, actor_run};

mframe_t actor_frames_flipoff [] =
{
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL
};
mmove_t actor_move_flipoff = {FRAME_flip01, FRAME_flip14, actor_frames_flipoff, actor_run};

mframe_t actor_frames_taunt [] =
{
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL,
	ai_turn, 0,  NULL
};
mmove_t actor_move_taunt = {FRAME_taunt01, FRAME_taunt17, actor_frames_taunt, actor_run};

static const char *const messages[] =
{
	"Watch it",
	"#$@*&",
	"Idiot",
	"Check your targets"
};

void actor_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	int		n;

	if (self->health < (self->max_health / 2))
		self->s.skinnum = 1;

	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 3;
//	gi.sound (self, CHAN_VOICE, actor.sound_pain, 1, ATTN_NORM, 0);

	if ((other->client) && (random() < 0.4))
	{
		vec3_t	v;
		const char	*name;

		VectorSubtract (other->s.origin, self->s.origin, v);
		self->ideal_yaw = vectoyaw (v);
		if (random() < 0.5)
			self->monsterinfo.currentmove = &actor_move_flipoff;
		else
			self->monsterinfo.currentmove = &actor_move_taunt;
		name = Actor_DisplayName(self);
		gi.cprintf (other, PRINT_CHAT, "%s: %s!\n", name, messages[rand()%3]);
		return;
	}

	n = rand() % 3;
	if (n == 0)
		self->monsterinfo.currentmove = &actor_move_pain1;
	else if (n == 1)
		self->monsterinfo.currentmove = &actor_move_pain2;
	else
		self->monsterinfo.currentmove = &actor_move_pain3;
}


void actorMachineGun (edict_t *self)
{
	vec3_t	start, target;
	vec3_t	forward, right;

	AngleVectors (self->s.angles, forward, right, NULL);
	G_ProjectSource (self->s.origin, monster_flash_offset[MZ2_ACTOR_MACHINEGUN_1], forward, right, start);
	if (self->enemy)
	{
		if (self->enemy->health > 0)
		{
			VectorMA (self->enemy->s.origin, -0.2, self->enemy->velocity, target);
			target[2] += self->enemy->viewheight;
		}
		else
		{
			VectorCopy (self->enemy->absmin, target);
			target[2] += (self->enemy->size[2] / 2);
		}
		VectorSubtract (target, start, forward);
		VectorNormalize (forward);
	}
	else
	{
		AngleVectors (self->s.angles, forward, NULL, NULL);
	}
	monster_fire_bullet (self, start, forward, 3, 4, DEFAULT_BULLET_HSPREAD, DEFAULT_BULLET_VSPREAD, MZ2_ACTOR_MACHINEGUN_1);
}


void actor_dead (edict_t *self)
{
	VectorSet (self->mins, -16, -16, -24);
	VectorSet (self->maxs, 16, 16, -8);
	self->movetype = MOVETYPE_TOSS;
	self->svflags |= SVF_DEADMONSTER;
	self->nextthink = 0;
	gi.linkentity (self);
}

mframe_t actor_frames_death1 [] =
{
	ai_move, 0,   NULL,
	ai_move, 0,   NULL,
	ai_move, -13, NULL,
	ai_move, 14,  NULL,
	ai_move, 3,   NULL,
	ai_move, -2,  NULL,
	ai_move, 1,   NULL
};
mmove_t actor_move_death1 = {FRAME_death101, FRAME_death107, actor_frames_death1, actor_dead};

mframe_t actor_frames_death2 [] =
{
	ai_move, 0,   NULL,
	ai_move, 7,   NULL,
	ai_move, -6,  NULL,
	ai_move, -5,  NULL,
	ai_move, 1,   NULL,
	ai_move, 0,   NULL,
	ai_move, -1,  NULL,
	ai_move, -2,  NULL,
	ai_move, -1,  NULL,
	ai_move, -9,  NULL,
	ai_move, -13, NULL,
	ai_move, -13, NULL,
	ai_move, 0,   NULL
};
mmove_t actor_move_death2 = {FRAME_death201, FRAME_death213, actor_frames_death2, actor_dead};

void actor_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	int		n;

// check for gib
	if (self->health <= -80)
	{
//		gi.sound (self, CHAN_VOICE, actor.sound_gib, 1, ATTN_NORM, 0);
		for (n= 0; n < 2; n++)
			ThrowGib (self, "models/objects/gibs/bone/tris.md2", damage, GIB_ORGANIC);
		for (n= 0; n < 4; n++)
			ThrowGib (self, "models/objects/gibs/sm_meat/tris.md2", damage, GIB_ORGANIC);
		ThrowHead (self, "models/objects/gibs/head2/tris.md2", damage, GIB_ORGANIC);
		self->deadflag = DEAD_DEAD;
		return;
	}

	if (self->deadflag == DEAD_DEAD)
		return;

// regular death
//	gi.sound (self, CHAN_VOICE, actor.sound_die, 1, ATTN_NORM, 0);
	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_YES;

	n = rand() % 2;
	if (n == 0)
		self->monsterinfo.currentmove = &actor_move_death1;
	else
		self->monsterinfo.currentmove = &actor_move_death2;
}


void actor_fire (edict_t *self)
{
	actorMachineGun (self);

	if (level.time >= self->monsterinfo.pausetime)
		self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;
	else
		self->monsterinfo.aiflags |= AI_HOLD_FRAME;
}

mframe_t actor_frames_attack [] =
{
	ai_charge, -2,  actor_fire,
	ai_charge, -2,  NULL,
	ai_charge, 3,   NULL,
	ai_charge, 2,   NULL
};
mmove_t actor_move_attack = {FRAME_attak01, FRAME_attak04, actor_frames_attack, actor_run};

void actor_attack(edict_t *self)
{
	int		n;

	self->monsterinfo.currentmove = &actor_move_attack;
	n = (rand() & 15) + 3 + 7;
	self->monsterinfo.pausetime = level.time + n * FRAMETIME;
}


/*
=============
actor_use

Activate the actor's scripted path and schedule the Oblivion think loop.
=============
*/
void actor_use (edict_t *self, edict_t *other, edict_t *activator)
{
	edict_t *controller;

	if (!self)
		return;

	(void)other;
	(void)activator;

	controller = G_PickTarget(self->target);
	if (!Actor_AttachController(self, controller))
	{
		gi.dprintf("%s has bad target %s at %s\n", self->classname,
			self->target ? self->target : "<null>", vtos(self->s.origin));
		self->target = NULL;
		Actor_PathAssignController(self, NULL);
		self->monsterinfo.aiflags |= AI_ACTOR_PATH_IDLE;
		self->monsterinfo.pausetime = 100000000.0f;
		if (self->monsterinfo.stand)
			self->monsterinfo.stand(self);
		self->think = Actor_PathThink;
		self->nextthink = level.time + FRAMETIME;
		return;
	}

	self->target = NULL;
	self->oblivion.prev_path = NULL;
	self->oblivion.path_wait_time = -1.0f;
	self->oblivion.script_target = NULL;
	self->oblivion.path_toggle = 0;
	self->monsterinfo.aiflags &= ~AI_ACTOR_PATH_IDLE;
	self->think = Actor_PathThink;
	self->nextthink = level.time + FRAMETIME;
}



/*
=============
Actor_UseOblivion

Select the actor's initial path target and clear any idle markers so the
actor resumes scripted motion when activated.
=============
*/
static void Actor_UseOblivion(edict_t *self, edict_t *other, edict_t *activator)
{
	edict_t *target;

	if (!self)
		return;

	Actor_ResetChatCooldown(self);
	Actor_InitMissionTimer(self);

	target = G_PickTarget(self->target);

	if (Actor_AttachController(self, target))
	{
		self->target = NULL;
		self->oblivion.prev_path = NULL;
		self->oblivion.path_wait_time = -1.0f;
		self->oblivion.script_target = NULL;
		self->oblivion.path_toggle = 0;
		self->monsterinfo.aiflags &= ~AI_ACTOR_PATH_IDLE;
		self->think = Actor_PathThink;
		self->nextthink = level.time + FRAMETIME;
		Actor_UpdateMissionObjective(self);
		return;
	}

	self->target = NULL;
	Actor_PathAssignController(self, NULL);
	self->monsterinfo.aiflags |= AI_ACTOR_PATH_IDLE;
	self->monsterinfo.pausetime = 100000000.0f;
	self->think = Actor_PathThink;
	self->nextthink = level.time + FRAMETIME;

	if (self->monsterinfo.stand)
		self->monsterinfo.stand(self);
}


static qboolean Actor_SpawnOblivion(edict_t *self)
{
	static const char *const kDefaultTargetName = "Yo Mama";

	if (deathmatch->value)
	{
		G_FreeEdict(self);
		return false;
	}

	if (!self->targetname)
	{
		/* `sub_1001f460` seeds the "Yo Mama" targetname and flips the hidden
		 * START_ON bit whenever a mapper omits one, so mirror that behaviour
		 * instead of treating the fallback as an idle actor.
		 */
		self->targetname = (char *)kDefaultTargetName;
		self->spawnflags |= ACTOR_SPAWNFLAG_START_ON;
	}

	self->s.modelindex = 0xff;
	self->s.modelindex2 = 0xff;

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	VectorSet(self->mins, -16, -16, -24);
	VectorSet(self->maxs, 16, 16, 32);

	Actor_ConfigureMovementState(self);
	Actor_ResetChatCooldown(self);
	Actor_InitMissionTimer(self);

	if (!(self->spawnflags & ACTOR_SPAWNFLAG_CORPSE))
	{
		if (!self->health)
		{
			if (self->spawnflags & ACTOR_SPAWNFLAG_WIMPY)
				self->health = 50;
			else
				self->health = 100;
		}
		self->max_health = self->health;
	}

	self->speed = 200;
	self->mass = 200;
	Actor_PathResetState(self);
	Actor_ApplySpawnAIFeatures(self);

	self->pain = actor_pain;
	self->die = actor_die;
	self->use = Actor_UseOblivion;
	self->prethink = Actor_PreThink;

	self->monsterinfo.stand = actor_stand;
	self->monsterinfo.walk = actor_walk;
	self->monsterinfo.run = actor_run;
	self->monsterinfo.attack = actor_attack;
	self->monsterinfo.melee = NULL;
	self->monsterinfo.sight = NULL;

	self->monsterinfo.currentmove = &actor_move_stand;
	self->monsterinfo.scale = MODEL_SCALE;

	if (self->spawnflags & ACTOR_SPAWNFLAG_CORPSE)
	{
		static const int corpse_frames[] = { FRAME_stand216, FRAME_stand222, FRAME_swim07 };

		self->s.frame = corpse_frames[rand() % 3];
		self->svflags |= SVF_DEADMONSTER;
		self->health = -1;
		self->deadflag = DEAD_DEAD;
		VectorSet(self->mins, -16, -16, -24);
		VectorSet(self->maxs, 16, 16, -8);
		self->nextthink = 0;
		gi.linkentity(self);
		return false;
	}

	gi.linkentity(self);
	walkmonster_start(self);

	if (self->spawnflags & ACTOR_SPAWNFLAG_START_ON)
	{
		edict_t *world_ent = &g_edicts[0];

		if (self->use)
			self->use(self, world_ent, world_ent);
	}

	return true;
}


/*QUAKED misc_actor (1 .5 0) (-16 -16 -24) (16 16 32)  Ambush Trigger_Spawn Sight Corpse x START_ON WIMPY
START_ON		actor immediately begins walking its path instead of waiting for a use event
WIMPY		reduce the actor's health so it can be dispatched quickly
*/

void SP_misc_actor (edict_t *self)
{
	if (!Actor_SpawnOblivion(self))
		return;
}



/*QUAKED target_actor (.5 .3 0) (-8 -8 -8) (8 8 8) JUMP SHOOT ATTACK x HOLD BRUTAL
JUMP			jump in set direction upon reaching this target
SHOOT			take a single shot at the pathtarget
ATTACK			attack pathtarget until it or actor is dead 

"target"		next target_actor
"pathtarget"	target of any action to be taken at this point
"wait"			amount of time actor should pause at this point
"message"		actor will "say" this to the player

for JUMP only:
"speed"			speed thrown forward (default 200)
"height"		speed thrown upwards (default 200)
*/

/*
=============
TargetActor_ConfigureJump

Apply the HLIL-confirmed jump defaults so `SP_target_actor` mirrors
`sub_1001f930` while keeping spawnflag parsing out of the spawn function
itself.
=============
*/
static void TargetActor_ConfigureJump(edict_t *self)
{
	const int spawnflags = self->spawnflags;

	if (!(spawnflags & TARGET_ACTOR_FLAG_JUMP))
		return;

	if (!self->speed)
		self->speed = 200;
	if (!st.height)
		st.height = 200;
	if (self->s.angles[YAW] == 0)
		self->s.angles[YAW] = 360;
	G_SetMovedir(self->s.angles, self->movedir);
	self->movedir[2] = st.height;
}

/*
=============
target_actor_touch

Handle scripted path targets and immediate actions when an actor
reaches a target_actor waypoint.
=============
*/
void target_actor_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t	v;
	edict_t *pathtarget_ent;
	edict_t *next_target;
	float wait;
	const int spawnflags = self->spawnflags;

	if (other->movetarget != self)
		return;

	if (other->enemy)
		return;

	other->goalentity = other->movetarget = NULL;
	other->monsterinfo.aiflags &= ~AI_ACTOR_PATH_IDLE;

	pathtarget_ent = NULL;
	if (self->pathtarget)
		pathtarget_ent = G_PickTarget(self->pathtarget);

	other->oblivion.script_target = pathtarget_ent;

	if (self->message)
	{
		Actor_BroadcastMessage(other, self->message);
	}

	if (spawnflags & TARGET_ACTOR_FLAG_JUMP)	//jump
	{
		other->velocity[0] = self->movedir[0] * self->speed;
		other->velocity[1] = self->movedir[1] * self->speed;

		if (other->groundentity)
		{
			other->groundentity = NULL;
			other->velocity[2] = self->movedir[2];
			gi.sound(other, CHAN_VOICE, gi.soundindex("player/male/jump1.wav"), 1, ATTN_NORM, 0);
		}
	}

	if (spawnflags & TARGET_ACTOR_FLAG_SHOOT)	//shoot
	{
		if (self->pathtarget)
			pathtarget_ent = G_PickTarget(self->pathtarget);

		other->enemy = pathtarget_ent;
		other->goalentity = pathtarget_ent;
		other->movetarget = pathtarget_ent;

		if (spawnflags & TARGET_ACTOR_FLAG_BRUTAL)
			other->monsterinfo.aiflags |= AI_BRUTAL;

		other->monsterinfo.aiflags |= AI_STAND_GROUND | AI_ACTOR_SHOOT_ONCE;
		actor_stand(other);

		if (other->monsterinfo.attack)
			other->monsterinfo.attack(other);
		else
			actor_attack(other);
	}
	else if (spawnflags & TARGET_ACTOR_FLAG_ATTACK)	//attack
	{
		other->enemy = pathtarget_ent;
		if (other->enemy)
		{
			other->goalentity = other->enemy;
			if (spawnflags & TARGET_ACTOR_FLAG_BRUTAL)
				other->monsterinfo.aiflags |= AI_BRUTAL;
			if (spawnflags & (TARGET_ACTOR_FLAG_HOLD | TARGET_ACTOR_FLAG_SHOOT))
			{
				other->monsterinfo.aiflags |= AI_STAND_GROUND;
				actor_stand (other);
			}
			else
			{
				actor_run (other);
			}
		}
	}

	if (self->pathtarget)
	{
		char *savetarget;

		savetarget = self->target;
		self->target = self->pathtarget;
		G_UseTargets (self, other);
		self->target = savetarget;
	}

	next_target = G_PickTarget(self->target);
	other->movetarget = next_target;

	wait = Actor_PathResolveWait(other, self);
	Actor_PathAdvance(other, self, next_target);
	other->oblivion.script_target = pathtarget_ent;
	Actor_PathApplyWait(other, wait);

	if (!other->goalentity)
		other->goalentity = other->movetarget;

	if (!other->movetarget && !other->enemy)
	{
		other->monsterinfo.pausetime = level.time + 100000000;
		other->monsterinfo.aiflags |= AI_ACTOR_PATH_IDLE;
		other->monsterinfo.stand (other);
	}
	else if (other->movetarget == other->goalentity)
	{
		VectorSubtract (other->movetarget->s.origin, other->s.origin, v);
		other->ideal_yaw = vectoyaw (v);
	}

	Actor_UpdateMissionObjective(other);
}


void SP_target_actor (edict_t *self)
{
	if (!self->targetname)
		gi.dprintf ("%s with no targetname at %s\n", self->classname, vtos(self->s.origin));

	self->solid = SOLID_TRIGGER;
	self->touch = target_actor_touch;
	VectorSet (self->mins, -8, -8, -8);
	VectorSet (self->maxs, 8, 8, 8);
	self->svflags = SVF_NOCLIENT;

	TargetActor_ConfigureJump(self);

	gi.linkentity (self);
}
