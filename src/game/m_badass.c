#include "g_local.h"
#include "m_badass.h"

#include <stddef.h>

#define MODEL_SCALE             1.0f
#define BADASS_SPAWNFLAG_TURRET 8

static int sound_pain;
static int sound_death;
static int sound_idle;
static int sound_step;
static int sound_sight;
static int sound_attack_primary;
static int sound_attack_secondary;
static int sound_attack_variants[6];

static const vec3_t badass_rocket_offsets[2] = {
        {18.0f,  40.0f, 0.0f},
        {18.0f, -40.0f, 0.0f}
};

static mmove_t badass_move_idle_closed;
static mmove_t badass_move_activate;
static mmove_t badass_move_deactivate;
static mmove_t badass_move_stand;
static mmove_t badass_move_walk;
static mmove_t badass_move_run;
static mmove_t badass_move_attack;
static mmove_t badass_move_pain;
static mmove_t badass_move_death;

static void badass_run (edict_t *self);
static void badass_stand (edict_t *self);
static void badass_idle (edict_t *self);
static void badass_dead (edict_t *self);

static void badass_step (edict_t *self)
{
        gi.sound (self, CHAN_BODY, sound_step, 1.0f, ATTN_NORM, 0);
}

static void badass_idle_sound (edict_t *self)
{
        if (random () < 0.3f)
                gi.sound (self, CHAN_VOICE, sound_idle, 1.0f, ATTN_IDLE, 0);
}

static void badass_sight (edict_t *self, edict_t *other)
{
        if (self->monsterinfo.currentmove != &badass_move_idle_closed)
        {
                self->monsterinfo.currentmove = &badass_move_run;
        }
        else
        {
                self->monsterinfo.currentmove = &badass_move_activate;
        }

        gi.sound (self, CHAN_VOICE, sound_sight, 1.0f, ATTN_NORM, 0);
}

static void badass_fire_rocket (edict_t *self, const vec3_t offset)
{
        vec3_t forward, right, start, dir, projected_offset;

        if (!self->enemy)
                return;

        AngleVectors (self->s.angles, forward, right, NULL);
        VectorCopy (offset, projected_offset);
        G_ProjectSource (self->s.origin, projected_offset, forward, right, start);

        VectorSubtract (self->enemy->s.origin, start, dir);
        dir[2] += self->enemy->viewheight - 8.0f;
        VectorNormalize (dir);

        fire_oblivion_rocket (self, start, dir, 50, 550, 70.0f, 50, MOD_ROCKET, MOD_R_SPLASH);

        const size_t variant_count = sizeof(sound_attack_variants) / sizeof(sound_attack_variants[0]);
        int sound_index = (int)(random () * (variant_count + 2));
        if (sound_index >= (int)(variant_count + 2))
                sound_index = (int)(variant_count + 2) - 1;
        if (sound_index == 0)
                gi.sound (self, CHAN_WEAPON, sound_attack_primary, 1.0f, ATTN_NORM, 0);
        else if (sound_index == 1)
                gi.sound (self, CHAN_WEAPON, sound_attack_secondary, 1.0f, ATTN_NORM, 0);
        else
                gi.sound (self, CHAN_WEAPON, sound_attack_variants[sound_index - 2], 1.0f, ATTN_NORM, 0);
}

static void badass_rocket_right (edict_t *self)
{
        badass_fire_rocket (self, badass_rocket_offsets[0]);
}

static void badass_rocket_left (edict_t *self)
{
        badass_fire_rocket (self, badass_rocket_offsets[1]);
}

static void badass_attack (edict_t *self)
{
        self->monsterinfo.currentmove = &badass_move_attack;
}

static mframe_t badass_frames_idle_closed[] = {
        {ai_stand, 0, badass_idle_sound}
};
static mmove_t badass_move_idle_closed = {FRAME_ACTIVATE1, FRAME_ACTIVATE1, badass_frames_idle_closed, NULL};

static void badass_idle_end (edict_t *self)
{
        self->monsterinfo.currentmove = &badass_move_idle_closed;
}

static mframe_t badass_frames_activate[] = {
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL}
};
static mmove_t badass_move_activate = {FRAME_ACTIVATE1, FRAME_ACTIVATE7, badass_frames_activate, badass_run};

static mframe_t badass_frames_deactivate[] = {
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL}
};
static mmove_t badass_move_deactivate = {FRAME_DEACTIVATE1, FRAME_DEACTIVATE15, badass_frames_deactivate, badass_idle_end};

static mframe_t badass_frames_stand[] = {
        {ai_stand, 0, badass_idle_sound},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL}
};
static mmove_t badass_move_stand = {FRAME_STAND1, FRAME_STAND20, badass_frames_stand, NULL};

static mframe_t badass_frames_walk[] = {
        {ai_walk, 7.0f, NULL},
        {ai_walk, 7.0f, NULL},
        {ai_walk, 7.0f, NULL},
        {ai_walk, 7.0f, NULL},
        {ai_walk, 7.0f, NULL},
        {ai_walk, 7.0f, NULL},
        {ai_walk, 7.0f, badass_step},
        {ai_walk, 7.0f, NULL},
        {ai_walk, 7.0f, NULL},
        {ai_walk, 7.0f, NULL},
        {ai_walk, 7.0f, NULL},
        {ai_walk, 7.0f, NULL},
        {ai_walk, 7.0f, NULL},
        {ai_walk, 7.0f, badass_step}
};
static mmove_t badass_move_walk = {FRAME_WALK1, FRAME_WALK14, badass_frames_walk, NULL};

static mframe_t badass_frames_run[] = {
        {ai_run, 14.0f, NULL},
        {ai_run, 15.0f, NULL},
        {ai_run, 21.0f, NULL},
        {ai_run, 24.0f, badass_step},
        {ai_run, 14.0f, NULL},
        {ai_run, 15.0f, NULL},
        {ai_run, 21.0f, NULL},
        {ai_run, 24.0f, badass_step}
};
static mmove_t badass_move_run = {FRAME_RUN1, FRAME_RUN8, badass_frames_run, NULL};

static mframe_t badass_frames_attack[] = {
        {ai_charge, -5.0f, badass_rocket_right},
        {ai_charge, 0.0f, NULL},
        {ai_charge, -5.0f, badass_rocket_left},
        {ai_charge, 0.0f, NULL}
};
static mmove_t badass_move_attack = {FRAME_ATTACK1, FRAME_ATTACK4, badass_frames_attack, badass_run};

static mframe_t badass_frames_pain[] = {
        {ai_move, 8.0f, NULL},
        {ai_move, 0.0f, NULL},
        {ai_move, 0.0f, NULL},
        {ai_move, 0.0f, NULL},
        {ai_move, 0.0f, NULL},
        {ai_move, 0.0f, NULL},
        {ai_move, -16.0f, NULL},
        {ai_move, -16.0f, NULL},
        {ai_move, -8.0f, NULL},
        {ai_move, 0.0f, NULL}
};
static mmove_t badass_move_pain = {FRAME_PAIN1, FRAME_PAIN10, badass_frames_pain, badass_run};

static void badass_die_gibs (edict_t *self, int damage)
{
        ThrowGib (self, "models/monsters/badass/gib_larm.md2", damage, GIB_METALLIC);
        ThrowGib (self, "models/monsters/badass/gib_rarm.md2", damage, GIB_METALLIC);
        ThrowGib (self, "models/monsters/badass/gib_lleg.md2", damage, GIB_METALLIC);
        ThrowGib (self, "models/monsters/badass/gib_rleg.md2", damage, GIB_METALLIC);
        ThrowHead (self, "models/monsters/badass/gib_torso.md2", damage, GIB_METALLIC);
}

static void badass_thud (edict_t *self)
{
        gi.sound (self, CHAN_BODY, sound_death, 1.0f, ATTN_NORM, 0);
}

static mframe_t badass_frames_death[] = {
        {ai_move, -8.0f, badass_idle_sound},
        {ai_move, -8.0f, NULL},
        {ai_move, -8.0f, NULL},
        {ai_move, -7.0f, NULL},
        {ai_move, -4.0f, badass_thud},
        {ai_move, 0.0f, NULL},
        {ai_move, 0.0f, NULL},
        {ai_move, 0.0f, NULL},
        {ai_move, 0.0f, badass_idle_sound},
        {ai_move, 4.0f, NULL},
        {ai_move, 2.0f, NULL},
        {ai_move, 2.0f, NULL},
        {ai_move, 2.0f, NULL},
        {ai_move, 2.0f, NULL},
        {ai_move, 2.0f, NULL},
        {ai_move, 2.0f, badass_thud},
        {ai_move, 0.0f, badass_idle_sound},
        {ai_move, 0.0f, badass_thud},
        {ai_move, 0.0f, NULL},
        {ai_move, 0.0f, badass_thud}
};
static mmove_t badass_move_death = {FRAME_DEATH1, FRAME_DEATH20, badass_frames_death, badass_dead};

static void badass_stand (edict_t *self)
{
        if (self->monsterinfo.currentmove != &badass_move_idle_closed)
                self->monsterinfo.currentmove = &badass_move_stand;
}

static void badass_idle (edict_t *self)
{
        if (self->monsterinfo.currentmove == &badass_move_stand)
                self->monsterinfo.currentmove = &badass_move_deactivate;
        else
                self->monsterinfo.currentmove = &badass_move_idle_closed;
}

static void badass_walk (edict_t *self)
{
        self->monsterinfo.currentmove = &badass_move_walk;
}

static void badass_run (edict_t *self)
{
        self->monsterinfo.currentmove = &badass_move_run;
}

static void badass_pain (edict_t *self, edict_t *other, float kick, int damage)
{
        if (self->health <= 0)
                return;

        if (damage <= 20)
                return;

        if (level.time < self->pain_debounce_time)
                return;

        if (damage <= 50 && random () > 0.2f)
                return;

        self->pain_debounce_time = level.time + 3.0f;
        gi.sound (self, CHAN_VOICE, sound_pain, 1.0f, ATTN_NORM, 0);
        self->monsterinfo.currentmove = &badass_move_pain;
}

static void badass_dead (edict_t *self)
{
        VectorSet (self->mins, -44.0f, -62.0f, -64.0f);
        VectorSet (self->maxs,  44.0f,  62.0f,  -5.0f);
        self->movetype = MOVETYPE_TOSS;
        self->svflags |= SVF_DEADMONSTER;
        self->takedamage = DAMAGE_YES;
        gi.linkentity (self);
}

static void badass_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
        if (self->health <= self->gib_health)
        {
                gi.sound (self, CHAN_VOICE, sound_death, 1.0f, ATTN_NORM, 0);
                badass_die_gibs (self, damage);
                self->deadflag = DEAD_DEAD;
                return;
        }

        if (self->deadflag == DEAD_DEAD)
                return;

        self->deadflag = DEAD_DYING;
        gi.sound (self, CHAN_VOICE, sound_death, 1.0f, ATTN_NORM, 0);
        self->monsterinfo.currentmove = &badass_move_death;
}

void SP_monster_badass (edict_t *self)
{
        if (deathmatch->value)
        {
                G_FreeEdict (self);
                return;
        }

        self->s.modelindex = gi.modelindex ("models/monsters/badass/tris.md2");
        gi.modelindex ("models/monsters/badass/gib_larm.md2");
        gi.modelindex ("models/monsters/badass/gib_rarm.md2");
        gi.modelindex ("models/monsters/badass/gib_lleg.md2");
        gi.modelindex ("models/monsters/badass/gib_rleg.md2");
        gi.modelindex ("models/monsters/badass/gib_torso.md2");

        sound_pain = gi.soundindex ("tank/tnkpain2.wav");
        sound_death = gi.soundindex ("tank/tnkdeth2.wav");
        sound_idle = gi.soundindex ("tank/tnkidle1.wav");
        sound_step = gi.soundindex ("tank/step.wav");
        sound_sight = gi.soundindex ("tank/sight1.wav");
        sound_attack_primary = gi.soundindex ("tank/tnkatck4.wav");
        sound_attack_secondary = gi.soundindex ("tank/tnkatck5.wav");
        sound_attack_variants[0] = gi.soundindex ("tank/tnkatck1.wav");
        sound_attack_variants[1] = gi.soundindex ("tank/tnkatk2a.wav");
        sound_attack_variants[2] = gi.soundindex ("tank/tnkatk2b.wav");
        sound_attack_variants[3] = gi.soundindex ("tank/tnkatk2c.wav");
        sound_attack_variants[4] = gi.soundindex ("tank/tnkatk2d.wav");
        sound_attack_variants[5] = gi.soundindex ("tank/tnkatk2e.wav");
        gi.soundindex ("tank/tnkatck3.wav");

        VectorSet (self->mins, -52.0f, -40.0f, -64.0f);
        VectorSet (self->maxs,  38.0f,  40.0f,  32.0f);
        self->movetype = MOVETYPE_STEP;
        self->solid = SOLID_BBOX;
        self->yaw_speed = 25.0f;
        self->mass = 600;

        self->health = 1000;
        self->gib_health = -200;
        self->takedamage = DAMAGE_AIM;
        self->pain = badass_pain;
        self->die = badass_die;

        self->monsterinfo.stand = badass_stand;
        self->monsterinfo.idle = badass_idle;
        self->monsterinfo.walk = badass_walk;
        self->monsterinfo.run = badass_run;
        self->monsterinfo.attack = badass_attack;
        self->monsterinfo.melee = badass_attack;
        self->monsterinfo.sight = badass_sight;
        self->monsterinfo.currentmove = &badass_move_idle_closed;
        self->monsterinfo.scale = MODEL_SCALE;
        self->monsterinfo.max_ideal_distance = 1500.0f;

        if (self->spawnflags & BADASS_SPAWNFLAG_TURRET)
        {
                self->flags |= FL_FLY;
                self->monsterinfo.aiflags |= AI_STAND_GROUND;
                VectorSet (self->mins, -31.0f, -22.0f, -38.0f);
                VectorSet (self->maxs,  38.0f,  21.0f,  -8.0f);
                self->monsterinfo.currentmove = &badass_move_idle_closed;
        }

        walkmonster_start (self);
}
