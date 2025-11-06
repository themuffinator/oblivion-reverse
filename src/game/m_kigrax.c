/*
 * Hovering Kigrax sentry.  The original binary exposed a bespoke hover
 * AI with multiple strafing and scouting tables.  This reimplementation
 * keeps the same behavioural contract using a light-weight state machine
 * that leverages the stock Quake II flying helpers.
 */

#include "g_local.h"

#define KIGRAX_FRAME_HOVER      0
#define KIGRAX_FRAME_STRAFE_START       1
#define KIGRAX_FRAME_STRAFE_END 4
#define KIGRAX_FRAME_ATTACK_START       5
#define KIGRAX_FRAME_ATTACK_END  8
#define KIGRAX_FRAME_PAIN_START          9
#define KIGRAX_FRAME_PAIN_END    10
#define KIGRAX_FRAME_DEATH_START         11
#define KIGRAX_FRAME_DEATH_END   14

static int sound_sight;
static int sound_search;
static int sound_idle;
static int sound_pain;
static int sound_death;
static int sound_attack;

static void kigrax_idle (edict_t *self)
{
    if (random () < 0.5f)
        gi.sound (self, CHAN_VOICE, sound_idle, 1, ATTN_IDLE, 0);
}

static void kigrax_sight (edict_t *self, edict_t *other)
{
    gi.sound (self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

static void kigrax_search (edict_t *self)
{
    gi.sound (self, CHAN_VOICE, sound_search, 1, ATTN_IDLE, 0);
}

static vec3_t kigrax_flash_offset = {12.0f, 0.0f, -6.0f};

static void kigrax_fire (edict_t *self)
{
    vec3_t  start, dir, forward, right, target;

    if (!self->enemy)
        return;

    AngleVectors (self->s.angles, forward, right, NULL);
    G_ProjectSource (self->s.origin, kigrax_flash_offset, forward, right, start);

    VectorCopy (self->enemy->s.origin, target);
    target[2] += self->enemy->viewheight * 0.5f;

    VectorSubtract (target, start, dir);
    VectorNormalize (dir);

    gi.sound (self, CHAN_WEAPON, sound_attack, 1, ATTN_NORM, 0);
    monster_fire_blaster (self, start, dir, 8, 1000, MZ2_HOVER_BLASTER_1, EF_BLASTER);
}

static mframe_t kigrax_frames_hover[] = {
    {ai_stand, 0, kigrax_idle}
};
static mmove_t kigrax_move_hover = {KIGRAX_FRAME_HOVER, KIGRAX_FRAME_HOVER, kigrax_frames_hover, NULL};

static mframe_t kigrax_frames_fly[] = {
    {ai_fly, 4, NULL},
    {ai_fly, 2, NULL},
    {ai_fly, 4, NULL},
    {ai_fly, 2, NULL}
};
static mmove_t kigrax_move_fly = {KIGRAX_FRAME_STRAFE_START, KIGRAX_FRAME_STRAFE_END, kigrax_frames_fly, NULL};

static mframe_t kigrax_frames_attack[] = {
    {ai_charge, 0, NULL},
    {ai_charge, 0, kigrax_fire},
    {ai_charge, 0, NULL},
    {ai_charge, 0, kigrax_fire}
};
static mmove_t kigrax_move_attack = {KIGRAX_FRAME_ATTACK_START, KIGRAX_FRAME_ATTACK_END, kigrax_frames_attack, NULL};

static void kigrax_hover (edict_t *self)
{
    self->monsterinfo.currentmove = &kigrax_move_hover;
}

static void kigrax_fly (edict_t *self)
{
    self->monsterinfo.currentmove = &kigrax_move_fly;
}

static void kigrax_attack (edict_t *self)
{
    self->monsterinfo.currentmove = &kigrax_move_attack;
    self->monsterinfo.attack_finished = level.time + 1.0f;
}

static void kigrax_pain (edict_t *self, edict_t *other, float kick, int damage)
{
    static mframe_t pain_frames[] = {
        {ai_move, 0, NULL},
        {ai_move, 0, NULL}
    };
    static mmove_t pain_move = {KIGRAX_FRAME_PAIN_START, KIGRAX_FRAME_PAIN_END, pain_frames, kigrax_fly};

    if (level.time < self->pain_debounce_time)
        return;

    self->pain_debounce_time = level.time + 1.0f;
    gi.sound (self, CHAN_VOICE, sound_pain, 1, ATTN_NORM, 0);
    self->monsterinfo.currentmove = &pain_move;
}

static void kigrax_dead (edict_t *self)
{
    self->deadflag = DEAD_DEAD;
    self->takedamage = DAMAGE_YES;
}

static void kigrax_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
    static mframe_t death_frames[] = {
        {ai_move, 0, NULL},
        {ai_move, 0, NULL},
        {ai_move, 0, kigrax_dead},
        {ai_move, 0, kigrax_dead}
    };
    static mmove_t death_move = {KIGRAX_FRAME_DEATH_START, KIGRAX_FRAME_DEATH_END, death_frames, kigrax_dead};

    gi.sound (self, CHAN_VOICE, sound_death, 1, ATTN_NORM, 0);

    if (self->health <= self->gib_health)
    {
        gi.sound (self, CHAN_VOICE, gi.soundindex ("misc/udeath.wav"), 1, ATTN_NORM, 0);
        ThrowGib (self, "models/objects/gibs/sm_meat/tris.md2", damage, GIB_ORGANIC);
        ThrowGib (self, "models/objects/gibs/sm_meat/tris.md2", damage, GIB_ORGANIC);
        ThrowHead (self, "models/objects/gibs/head2/tris.md2", damage, GIB_ORGANIC);
        return;
    }

    self->monsterinfo.currentmove = &death_move;
}

void SP_monster_kigrax (edict_t *self)
{
    if (deathmatch->value)
    {
        G_FreeEdict (self);
        return;
    }

    self->s.modelindex = gi.modelindex ("models/monsters/kigrax/tris.md2");
    VectorSet (self->mins, -20.0f, -20.0f, -32.0f);
    VectorSet (self->maxs, 20.0f, 20.0f, 12.0f);
    self->movetype = MOVETYPE_FLY;
    self->solid = SOLID_BBOX;
    self->flags |= FL_FLY;
    self->mass = 120;

    sound_sight = gi.soundindex ("hover/hovsght1.wav");
    sound_search = gi.soundindex ("hover/hovsrch1.wav");
    sound_idle = gi.soundindex ("kigrax/hovidle1.wav");
    sound_pain = gi.soundindex ("hover/hovpain1.wav");
    sound_death = gi.soundindex ("hover/hovdeth1.wav");
    sound_attack = gi.soundindex ("kigrax/hovatck1.wav");

    self->s.sound = sound_idle;

    self->health = 200;
    self->gib_health = -100;

    self->pain = kigrax_pain;
    self->die = kigrax_die;

    self->monsterinfo.stand = kigrax_hover;
    self->monsterinfo.walk = kigrax_fly;
    self->monsterinfo.run = kigrax_fly;
    self->monsterinfo.attack = kigrax_attack;
    self->monsterinfo.melee = NULL;
    self->monsterinfo.sight = kigrax_sight;
    self->monsterinfo.search = kigrax_search;
    self->monsterinfo.aiflags |= AI_FLOAT;

    kigrax_hover (self);

    flymonster_start (self);
}
