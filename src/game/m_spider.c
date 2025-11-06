/*
 * Ground based spider tank encountered in later Oblivion levels.  The
 * original AI had fairly involved melee combos so this recreation keeps
 * things aggressive and close range – the spider rapidly closes distance
 * and delivers heavy slash attacks while shrugging off lighter hits.
 */

#include "g_local.h"

#define SPIDER_FRAME_STAND      0
#define SPIDER_FRAME_WALK_START         1
#define SPIDER_FRAME_WALK_END   6
#define SPIDER_FRAME_RUN_START          7
#define SPIDER_FRAME_RUN_END    12
#define SPIDER_FRAME_ATTACK_START       13
#define SPIDER_FRAME_ATTACK_END 18
#define SPIDER_FRAME_PAIN_START         19
#define SPIDER_FRAME_PAIN_END   21
#define SPIDER_FRAME_DEATH_START        22
#define SPIDER_FRAME_DEATH_END  28

static int sound_sight;
static int sound_search;
static int sound_idle;
static int sound_pain;
static int sound_death;
static int sound_melee[3];
static int sound_thud;

static void spider_idle (edict_t *self)
{
    if (random () < 0.25f)
        gi.sound (self, CHAN_VOICE, sound_idle, 1, ATTN_IDLE, 0);
}

static void spider_sight (edict_t *self, edict_t *other)
{
    gi.sound (self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

static void spider_search (edict_t *self)
{
    gi.sound (self, CHAN_VOICE, sound_search, 1, ATTN_IDLE, 0);
}

static void spider_step (edict_t *self)
{
    gi.sound (self, CHAN_BODY, sound_thud, 1, ATTN_NORM, 0);
}

static void spider_claw (edict_t *self)
{
    vec3_t  forward;
    float   damage = 30.0f;

    if (!self->enemy)
        return;

    AngleVectors (self->s.angles, forward, NULL, NULL);

    if (range (self, self->enemy) > RANGE_MELEE)
        return;

    gi.sound (self, CHAN_WEAPON, sound_melee[rand () % 3], 1, ATTN_NORM, 0);
    T_Damage (self->enemy, self, self, forward, self->enemy->s.origin, vec3_origin, (int)damage, (int)damage, 0, MOD_HIT);
}

static mframe_t spider_frames_stand[] = {
    {ai_stand, 0, spider_idle}
};
static mmove_t spider_move_stand = {SPIDER_FRAME_STAND, SPIDER_FRAME_STAND, spider_frames_stand, NULL};

static mframe_t spider_frames_walk[] = {
    {ai_walk, 6, spider_step},
    {ai_walk, 4, NULL},
    {ai_walk, 6, spider_step},
    {ai_walk, 4, NULL},
    {ai_walk, 6, spider_step},
    {ai_walk, 4, NULL}
};
static mmove_t spider_move_walk = {SPIDER_FRAME_WALK_START, SPIDER_FRAME_WALK_END, spider_frames_walk, NULL};

static mframe_t spider_frames_run[] = {
    {ai_run, 10, spider_step},
    {ai_run, 6, NULL},
    {ai_run, 10, spider_step},
    {ai_run, 6, NULL},
    {ai_run, 10, spider_step},
    {ai_run, 6, NULL}
};
static mmove_t spider_move_run = {SPIDER_FRAME_RUN_START, SPIDER_FRAME_RUN_END, spider_frames_run, NULL};

static mframe_t spider_frames_attack[] = {
    {ai_charge, 0, spider_claw},
    {ai_charge, 0, NULL},
    {ai_charge, 0, spider_claw},
    {ai_charge, 0, NULL},
    {ai_charge, 0, spider_claw},
    {ai_charge, 0, NULL}
};
static mmove_t spider_move_attack = {SPIDER_FRAME_ATTACK_START, SPIDER_FRAME_ATTACK_END, spider_frames_attack, NULL};

static void spider_stand (edict_t *self)
{
    self->monsterinfo.currentmove = &spider_move_stand;
}

static void spider_walk (edict_t *self)
{
    self->monsterinfo.currentmove = &spider_move_walk;
}

static void spider_run (edict_t *self)
{
    self->monsterinfo.currentmove = &spider_move_run;
}

static void spider_attack (edict_t *self)
{
    self->monsterinfo.currentmove = &spider_move_attack;
    self->monsterinfo.attack_finished = level.time + 1.0f;
}

static void spider_pain (edict_t *self, edict_t *other, float kick, int damage)
{
    static mframe_t pain_frames[] = {
        {ai_move, 0, NULL},
        {ai_move, 0, NULL},
        {ai_move, 0, NULL}
    };
    static mmove_t pain_move = {SPIDER_FRAME_PAIN_START, SPIDER_FRAME_PAIN_END, pain_frames, spider_run};

    if (level.time < self->pain_debounce_time)
        return;

    self->pain_debounce_time = level.time + 1.5f;
    gi.sound (self, CHAN_VOICE, sound_pain, 1, ATTN_NORM, 0);
    self->monsterinfo.currentmove = &pain_move;
}

static void spider_dead (edict_t *self)
{
    self->deadflag = DEAD_DEAD;
    self->takedamage = DAMAGE_YES;
}

static void spider_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
    static mframe_t death_frames[] = {
        {ai_move, 0, NULL},
        {ai_move, 0, NULL},
        {ai_move, 0, spider_dead},
        {ai_move, 0, spider_dead},
        {ai_move, 0, spider_dead},
        {ai_move, 0, spider_dead},
        {ai_move, 0, spider_dead}
    };
    static mmove_t death_move = {SPIDER_FRAME_DEATH_START, SPIDER_FRAME_DEATH_END, death_frames, spider_dead};

    gi.sound (self, CHAN_VOICE, sound_death, 1, ATTN_NORM, 0);

    if (self->health <= self->gib_health)
    {
        gi.sound (self, CHAN_VOICE, gi.soundindex ("misc/udeath.wav"), 1, ATTN_NORM, 0);
        ThrowGib (self, "models/objects/gibs/sm_metal/tris.md2", damage, GIB_METALLIC);
        ThrowGib (self, "models/objects/gibs/chest/tris.md2", damage, GIB_METALLIC);
        ThrowHead (self, "models/objects/gibs/head2/tris.md2", damage, GIB_ORGANIC);
        return;
    }

    self->monsterinfo.currentmove = &death_move;
}

void SP_monster_spider (edict_t *self)
{
    if (deathmatch->value)
    {
        G_FreeEdict (self);
        return;
    }

    self->s.modelindex = gi.modelindex ("models/monsters/spider/tris.md2");
    VectorSet (self->mins, -32.0f, -32.0f, -32.0f);
    VectorSet (self->maxs, 32.0f, 32.0f, 32.0f);
    self->movetype = MOVETYPE_STEP;
    self->solid = SOLID_BBOX;
    self->mass = 300;

    sound_sight = gi.soundindex ("spider/sight.wav");
    sound_search = gi.soundindex ("gladiator/gldsrch1.wav");
    sound_idle = gi.soundindex ("gladiator/gldidle1.wav");
    sound_pain = gi.soundindex ("gladiator/pain.wav");
    sound_death = gi.soundindex ("mutant/thud1.wav");
    sound_melee[0] = gi.soundindex ("gladiator/melee1.wav");
    sound_melee[1] = gi.soundindex ("gladiator/melee2.wav");
    sound_melee[2] = gi.soundindex ("gladiator/melee3.wav");
    sound_thud = gi.soundindex ("mutant/thud1.wav");

    self->s.sound = sound_idle;

    self->health = 400;
    self->gib_health = -120;

    self->pain = spider_pain;
    self->die = spider_die;

    self->monsterinfo.stand = spider_stand;
    self->monsterinfo.walk = spider_walk;
    self->monsterinfo.run = spider_run;
    self->monsterinfo.attack = spider_attack;
    self->monsterinfo.melee = spider_attack;
    self->monsterinfo.sight = spider_sight;
    self->monsterinfo.search = spider_search;

    spider_stand (self);

    walkmonster_start (self);
}
