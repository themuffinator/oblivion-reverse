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

#ifndef ARRAY_LEN
#define ARRAY_LEN(x) (sizeof (x) / sizeof ((x)[0]))
#endif

#define CYBORG_FRAME_STAND     0
#define CYBORG_FRAME_WALK_START        1
#define CYBORG_FRAME_WALK_END  4
#define CYBORG_FRAME_RUN_START         5
#define CYBORG_FRAME_RUN_END   8
#define CYBORG_FRAME_ATTACK_PRIMARY_START      24
#define CYBORG_FRAME_ATTACK_PRIMARY_END        35
#define CYBORG_FRAME_ATTACK_ALT_START           47
#define CYBORG_FRAME_ATTACK_ALT_END             52
#define CYBORG_FRAME_ATTACK_FINISH_START        53
#define CYBORG_FRAME_ATTACK_FINISH_END          58
#define CYBORG_FRAME_PAIN_START        13
#define CYBORG_FRAME_PAIN_END  14
#define CYBORG_FRAME_DEATH_START       15
#define CYBORG_FRAME_DEATH_END 17

static int sound_sight;
static int sound_search;
static int sound_idle;
static int sound_step[3];
static int sound_pain[2];
static int sound_death;
static int sound_attack[3];
static int sound_land;

static void cyborg_step (edict_t *self)
{
    gi.sound (self, CHAN_BODY, sound_step[rand () % 3], 1, ATTN_NORM, 0);
}

static void cyborg_land (edict_t *self)
{
    gi.sound (self, CHAN_BODY, sound_land, 1, ATTN_NORM, 0);
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

    gi.sound (self, CHAN_WEAPON, sound_attack[rand () % 3], 1, ATTN_NORM, 0);

    // The original DLL rolled cyborg deatom damage from a narrow band per shot
    // before spawning a high-speed tracking projectile.  Match that behaviour
    // here so kill feeds attribute the hits to the proper deatomizer mods.
    {
        int     damage;
        int     splash;
        const int       speed = 1000;
        const float     damage_radius = 480.0f;

        damage = 90 + (int) (random () * 30.0f);
        if (damage > 119)
            damage = 119;

        splash = damage / 2;

        fire_deatomizer (self, start, dir, damage, speed, damage_radius, splash);
    }
}

static void cyborg_attack_fire (edict_t *self)
{
    if (visible (self, self->enemy) && range (self, self->enemy) <= RANGE_FAR)
        cyborg_fire_deatom (self);
}

static float cyborg_attack_duration (const mmove_t *move)
{
    return (float)((move->lastframe - move->firstframe) + 1) * FRAMETIME;
}

static void cyborg_attack_recover (edict_t *self)
{
    if (self->monsterinfo.attack_finished < level.time + 0.2f)
        self->monsterinfo.attack_finished = level.time + 0.2f;

    if (self->monsterinfo.run)
        self->monsterinfo.run (self);
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

static mframe_t cyborg_frames_attack_primary[] = {
    {ai_charge, 0, cyborg_land},
    {ai_charge, 0, NULL},
    {ai_charge, 0, cyborg_attack_fire},
    {ai_charge, 0, NULL},
    {ai_charge, 0, cyborg_attack_fire},
    {ai_charge, 0, NULL},
    {ai_charge, 0, cyborg_attack_fire},
    {ai_charge, 0, NULL},
    {ai_charge, 0, cyborg_attack_fire},
    {ai_charge, 0, NULL},
    {ai_charge, 0, cyborg_attack_fire},
    {ai_charge, 0, NULL}
};
static mmove_t cyborg_move_attack_primary = {
    CYBORG_FRAME_ATTACK_PRIMARY_START,
    CYBORG_FRAME_ATTACK_PRIMARY_END,
    cyborg_frames_attack_primary,
    cyborg_attack_recover
};

static mframe_t cyborg_frames_attack_alt[] = {
    {ai_charge, 0, cyborg_land},
    {ai_charge, 0, NULL},
    {ai_charge, 0, cyborg_attack_fire},
    {ai_charge, 0, NULL},
    {ai_charge, 0, cyborg_attack_fire},
    {ai_charge, 0, NULL}
};
static mmove_t cyborg_move_attack_alt = {
    CYBORG_FRAME_ATTACK_ALT_START,
    CYBORG_FRAME_ATTACK_ALT_END,
    cyborg_frames_attack_alt,
    cyborg_attack_recover
};

static mframe_t cyborg_frames_attack_finisher[] = {
    {ai_charge, 0, cyborg_land},
    {ai_charge, 0, NULL},
    {ai_charge, 0, cyborg_attack_fire},
    {ai_charge, 0, cyborg_step},
    {ai_charge, 0, cyborg_attack_fire},
    {ai_charge, 0, NULL}
};
static mmove_t cyborg_move_attack_finisher = {
    CYBORG_FRAME_ATTACK_FINISH_START,
    CYBORG_FRAME_ATTACK_FINISH_END,
    cyborg_frames_attack_finisher,
    cyborg_attack_recover
};

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

typedef struct
{
    mmove_t *move;
    float    cooldown;
} cyborg_attack_choice_t;

static void cyborg_begin_attack (edict_t *self, const cyborg_attack_choice_t *choice)
{
    self->monsterinfo.currentmove = choice->move;
    self->monsterinfo.attack_finished = level.time + cyborg_attack_duration (choice->move) + choice->cooldown;
}

static void cyborg_attack (edict_t *self)
{
    static const cyborg_attack_choice_t attack_table[] = {
        {&cyborg_move_attack_primary, 0.2f},
        {&cyborg_move_attack_alt, 0.3f},
        {&cyborg_move_attack_finisher, 0.35f}
    };
    const cyborg_attack_choice_t *choice;

    choice = &attack_table[rand () % ARRAY_LEN (attack_table)];
    cyborg_begin_attack (self, choice);
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

    self->pain_debounce_time = level.time + 3.0f;
    gi.sound (self, CHAN_VOICE, sound_pain[rand () & 1], 1, ATTN_NORM, 0);
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
    sound_pain[0] = gi.soundindex ("cyborg/mutpain1.wav");
    sound_pain[1] = gi.soundindex ("cyborg/mutpain2.wav");
    sound_death = gi.soundindex ("cyborg/mutdeth1.wav");
    sound_attack[0] = gi.soundindex ("cyborg/mutatck1.wav");
    sound_attack[1] = gi.soundindex ("cyborg/mutatck2.wav");
    sound_attack[2] = gi.soundindex ("cyborg/mutatck3.wav");
    sound_step[0] = gi.soundindex ("cyborg/step1.wav");
    sound_step[1] = gi.soundindex ("cyborg/step2.wav");
    sound_step[2] = gi.soundindex ("cyborg/step3.wav");
    sound_land = gi.soundindex ("mutant/thud1.wav");

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
