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

#define CYBORG_FRAME_STAND     0
#define CYBORG_FRAME_WALK_START        1
#define CYBORG_FRAME_WALK_END  4
#define CYBORG_FRAME_RUN_START         5
#define CYBORG_FRAME_RUN_END   8
#define CYBORG_FRAME_ATTACK_START      9
#define CYBORG_FRAME_ATTACK_END        12
#define CYBORG_FRAME_PAIN_START        13
#define CYBORG_FRAME_PAIN_END  14
#define CYBORG_FRAME_DEATH_START       15
#define CYBORG_FRAME_DEATH_END 17

static int sound_sight;
static int sound_search;
static int sound_idle;
static int sound_step[3];
static int sound_pain;
static int sound_death;
static int sound_attack;

static void cyborg_step (edict_t *self)
{
    gi.sound (self, CHAN_BODY, sound_step[rand () % 3], 1, ATTN_NORM, 0);
}

static void cyborg_idle (edict_t *self)
{
    if (random () < 0.3f)
        gi.sound (self, CHAN_VOICE, sound_idle, 1, ATTN_IDLE, 0);
}

static void cyborg_sight (edict_t *self, edict_t *other)
{
    gi.sound (self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

static void cyborg_search (edict_t *self)
{
    gi.sound (self, CHAN_VOICE, sound_search, 1, ATTN_IDLE, 0);
}

static vec3_t cyborg_flash_offset = {20.0f, 7.0f, 24.0f};

static void cyborg_fire_deatom (edict_t *self)
{
    vec3_t  start, dir, forward, right, target;

    if (!self->enemy)
        return;

    AngleVectors (self->s.angles, forward, right, NULL);
    G_ProjectSource (self->s.origin, cyborg_flash_offset, forward, right, start);

    VectorCopy (self->enemy->s.origin, target);
    target[2] += self->enemy->viewheight;

    VectorSubtract (target, start, dir);
    VectorNormalize (dir);

    gi.sound (self, CHAN_WEAPON, sound_attack, 1, ATTN_NORM, 0);
    monster_fire_blaster (self, start, dir, 12, 1000, MZ2_TANK_BLASTER_1, EF_BLASTER);
}

static void cyborg_attack_think (edict_t *self)
{
    if (visible (self, self->enemy) && range (self, self->enemy) <= RANGE_FAR)
        cyborg_fire_deatom (self);
}

static mframe_t cyborg_frames_stand[] = {
    {ai_stand, 0, cyborg_idle}
};
static mmove_t cyborg_move_stand = {CYBORG_FRAME_STAND, CYBORG_FRAME_STAND, cyborg_frames_stand, NULL};

static mframe_t cyborg_frames_walk[] = {
    {ai_walk, 6, cyborg_step},
    {ai_walk, 3, NULL},
    {ai_walk, 6, cyborg_step},
    {ai_walk, 3, NULL}
};
static mmove_t cyborg_move_walk = {CYBORG_FRAME_WALK_START, CYBORG_FRAME_WALK_END, cyborg_frames_walk, NULL};

static mframe_t cyborg_frames_run[] = {
    {ai_run, 10, cyborg_step},
    {ai_run, 6, NULL},
    {ai_run, 10, cyborg_step},
    {ai_run, 6, NULL}
};
static mmove_t cyborg_move_run = {CYBORG_FRAME_RUN_START, CYBORG_FRAME_RUN_END, cyborg_frames_run, NULL};

static mframe_t cyborg_frames_attack[] = {
    {ai_charge, 0, NULL},
    {ai_charge, 0, cyborg_attack_think},
    {ai_charge, 0, NULL},
    {ai_charge, 0, cyborg_attack_think}
};
static mmove_t cyborg_move_attack = {CYBORG_FRAME_ATTACK_START, CYBORG_FRAME_ATTACK_END, cyborg_frames_attack, NULL};

static void cyborg_stand (edict_t *self)
{
    self->monsterinfo.currentmove = &cyborg_move_stand;
}

static void cyborg_walk (edict_t *self)
{
    self->monsterinfo.currentmove = &cyborg_move_walk;
}

static void cyborg_run (edict_t *self)
{
    self->monsterinfo.currentmove = &cyborg_move_run;
}

static void cyborg_attack (edict_t *self)
{
    self->monsterinfo.currentmove = &cyborg_move_attack;
    self->monsterinfo.attack_finished = level.time + 1.2f;
}

static void cyborg_pain (edict_t *self, edict_t *other, float kick, int damage)
{
    static mframe_t pain_frames[] = {
        {ai_move, 0, NULL},
        {ai_move, 0, NULL}
    };
    static mmove_t pain_move = {CYBORG_FRAME_PAIN_START, CYBORG_FRAME_PAIN_END, pain_frames, cyborg_run};

    if (level.time < self->pain_debounce_time)
        return;

    self->pain_debounce_time = level.time + 2.0f;
    gi.sound (self, CHAN_VOICE, sound_pain, 1, ATTN_NORM, 0);
    self->monsterinfo.currentmove = &pain_move;
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
    sound_death = gi.soundindex ("cyborg/mutdeth1.wav");
    sound_attack = gi.soundindex ("deatom/dfire.wav");
    sound_step[0] = gi.soundindex ("cyborg/step1.wav");
    sound_step[1] = gi.soundindex ("cyborg/step2.wav");
    sound_step[2] = gi.soundindex ("cyborg/step3.wav");

    self->s.sound = gi.soundindex ("cyborg/mutidle1.wav");

    self->health = 300;
    self->gib_health = -120;

    self->pain = cyborg_pain;
    self->die = cyborg_die;

    self->monsterinfo.stand = cyborg_stand;
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
