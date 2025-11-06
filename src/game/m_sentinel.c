/*
 * Sentinel – fast, rocket-centric heavy trooper from Oblivion.
 * The original unit relied on relentless forward pressure and
 * barrages of rockets.  This recreation keeps those beats by
 * leaning on high run speeds and clustered rocket salvos that
 * use the shared fire_oblivion_rocket helper.
 */

#include "g_local.h"

#define MODEL_SCALE         1.000000f

#define SENTINEL_FRAME_STAND_START       0
#define SENTINEL_FRAME_STAND_END         9
#define SENTINEL_FRAME_RUN_START         10
#define SENTINEL_FRAME_RUN_END           19
#define SENTINEL_FRAME_ATTACK_START      40
#define SENTINEL_FRAME_ATTACK_END        49
#define SENTINEL_FRAME_PAIN_START        60
#define SENTINEL_FRAME_PAIN_END          63
#define SENTINEL_FRAME_DEATH_START       70
#define SENTINEL_FRAME_DEATH_END         85

static mmove_t sentinel_move_stand;
static mmove_t sentinel_move_run;
static mmove_t sentinel_move_attack;

static int  sound_idle;
static int  sound_sight;
static int  sound_search;
static int  sound_step;
static int  sound_pain;
static int  sound_death;
static int  sound_warmup;
static int  sound_fire;

static vec3_t sentinel_flash_offset = {24.0f, 6.0f, 48.0f};

static void sentinel_idle (edict_t *self)
{
        if (random () < 0.25f)
                gi.sound (self, CHAN_VOICE, sound_idle, 1, ATTN_IDLE, 0);
}

static void sentinel_sight (edict_t *self, edict_t *other)
{
        gi.sound (self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

static void sentinel_search (edict_t *self)
{
        gi.sound (self, CHAN_VOICE, sound_search, 1, ATTN_IDLE, 0);
}

static void sentinel_step (edict_t *self)
{
        gi.sound (self, CHAN_BODY, sound_step, 1, ATTN_NORM, 0);
}

static void sentinel_warmup (edict_t *self)
{
        gi.sound (self, CHAN_WEAPON, sound_warmup, 1, ATTN_NORM, 0);
}

static void sentinel_fire_rocket (edict_t *self)
{
        vec3_t  forward, right, start, dir;

        if (!self->enemy)
                return;

        AngleVectors (self->s.angles, forward, right, NULL);
        G_ProjectSource (self->s.origin, sentinel_flash_offset, forward, right, start);

        VectorSubtract (self->enemy->s.origin, start, dir);
        dir[2] += self->enemy->viewheight - 8.0f;
        VectorNormalize (dir);

        fire_oblivion_rocket (self, start, dir, 70, 900, 140.0f, 70, MOD_ROCKET, MOD_R_SPLASH);
        gi.sound (self, CHAN_WEAPON, sound_fire, 1, ATTN_NORM, 0);
}

static void sentinel_burst (edict_t *self)
{
        sentinel_fire_rocket (self);
        self->monsterinfo.attack_finished = level.time + 0.7f;
}

static void sentinel_post_attack (edict_t *self)
{
        if (!self->enemy)
                return;

        if (visible (self, self->enemy) && range (self, self->enemy) <= RANGE_FAR)
        {
                if (random () < 0.35f)
                {
                        self->monsterinfo.currentmove = &sentinel_move_attack;
                        return;
                }
        }

        self->monsterinfo.currentmove = &sentinel_move_run;
}

static mframe_t sentinel_frames_stand[] = {
        {ai_stand, 0, sentinel_idle},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, sentinel_idle},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, NULL},
        {ai_stand, 0, sentinel_idle},
        {ai_stand, 0, NULL}
};
static mmove_t sentinel_move_stand = {SENTINEL_FRAME_STAND_START, SENTINEL_FRAME_STAND_END, sentinel_frames_stand, NULL};

static mframe_t sentinel_frames_run[] = {
        {ai_run, 20, sentinel_step},
        {ai_run, 22, NULL},
        {ai_run, 24, NULL},
        {ai_run, 26, sentinel_step},
        {ai_run, 22, NULL},
        {ai_run, 24, NULL},
        {ai_run, 26, sentinel_step},
        {ai_run, 22, NULL},
        {ai_run, 24, NULL},
        {ai_run, 26, sentinel_step}
};
static mmove_t sentinel_move_run = {SENTINEL_FRAME_RUN_START, SENTINEL_FRAME_RUN_END, sentinel_frames_run, NULL};

static mframe_t sentinel_frames_attack[] = {
        {ai_charge, 0, sentinel_warmup},
        {ai_charge, 0, NULL},
        {ai_charge, 0, sentinel_fire_rocket},
        {ai_charge, 0, sentinel_fire_rocket},
        {ai_charge, 0, sentinel_fire_rocket},
        {ai_charge, 0, NULL},
        {ai_charge, 0, sentinel_burst},
        {ai_charge, 0, NULL},
        {ai_charge, 0, NULL},
        {ai_charge, 0, NULL}
};
static mmove_t sentinel_move_attack = {SENTINEL_FRAME_ATTACK_START, SENTINEL_FRAME_ATTACK_END, sentinel_frames_attack, sentinel_post_attack};

static void sentinel_stand (edict_t *self)
{
        self->monsterinfo.currentmove = &sentinel_move_stand;
}

static void sentinel_run (edict_t *self)
{
        self->monsterinfo.currentmove = &sentinel_move_run;
}

static void sentinel_attack (edict_t *self)
{
        self->monsterinfo.currentmove = &sentinel_move_attack;
}

static void sentinel_pain (edict_t *self, edict_t *other, float kick, int damage)
{
        static mframe_t sentinel_frames_pain[] = {
                {ai_move, 0, NULL},
                {ai_move, 0, NULL},
                {ai_move, 0, NULL},
                {ai_move, 0, NULL}
        };
        static mmove_t sentinel_move_pain = {SENTINEL_FRAME_PAIN_START, SENTINEL_FRAME_PAIN_END, sentinel_frames_pain, sentinel_run};

        if (self->health < (self->max_health / 2))
                self->monsterinfo.attack_finished = 0;

        if (level.time < self->pain_debounce_time)
                return;

        self->pain_debounce_time = level.time + 1.5f;
        gi.sound (self, CHAN_VOICE, sound_pain, 1, ATTN_NORM, 0);
        self->monsterinfo.currentmove = &sentinel_move_pain;
}

static void sentinel_dead (edict_t *self)
{
        self->deadflag = DEAD_DEAD;
        self->takedamage = DAMAGE_YES;
}

static void sentinel_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
        static mframe_t sentinel_frames_death[] = {
                {ai_move, 0, NULL},
                {ai_move, -4, NULL},
                {ai_move, -6, NULL},
                {ai_move, -6, sentinel_step},
                {ai_move, -4, NULL},
                {ai_move, -6, NULL},
                {ai_move, -8, NULL},
                {ai_move, -10, NULL},
                {ai_move, -12, NULL},
                {ai_move, 0, sentinel_dead},
                {ai_move, 0, sentinel_dead},
                {ai_move, 0, sentinel_dead},
                {ai_move, 0, sentinel_dead},
                {ai_move, 0, sentinel_dead},
                {ai_move, 0, sentinel_dead},
                {ai_move, 0, sentinel_dead}
        };
        static mmove_t sentinel_move_death = {SENTINEL_FRAME_DEATH_START, SENTINEL_FRAME_DEATH_END, sentinel_frames_death, sentinel_dead};
        int n;

        if (self->health <= self->gib_health)
        {
                gi.sound (self, CHAN_VOICE, gi.soundindex ("misc/udeath.wav"), 1, ATTN_NORM, 0);
                for (n = 0; n < 2; n++)
                        ThrowGib (self, "models/monsters/badass/gib_larm.md2", damage, GIB_METALLIC);
                ThrowGib (self, "models/monsters/badass/gib_rarm.md2", damage, GIB_METALLIC);
                ThrowGib (self, "models/monsters/badass/gib_lleg.md2", damage, GIB_METALLIC);
                ThrowGib (self, "models/monsters/badass/gib_rleg.md2", damage, GIB_METALLIC);
                ThrowHead (self, "models/monsters/badass/gib_torso.md2", damage, GIB_METALLIC);
                self->deadflag = DEAD_DEAD;
                return;
        }

        if (self->deadflag == DEAD_DEAD)
                return;

        gi.sound (self, CHAN_VOICE, sound_death, 1, ATTN_NORM, 0);
        self->deadflag = DEAD_DEAD;
        self->takedamage = DAMAGE_YES;
        self->monsterinfo.currentmove = &sentinel_move_death;
}

void SP_monster_sentinel (edict_t *self)
{
        if (deathmatch->value)
        {
                G_FreeEdict (self);
                return;
        }

        self->s.modelindex = gi.modelindex ("models/monsters/badass/tris.md2");
        VectorSet (self->mins, -52.0f, -40.0f, -64.0f);
        VectorSet (self->maxs, 38.0f, 40.0f, 32.0f);
        self->movetype = MOVETYPE_STEP;
        self->solid = SOLID_BBOX;

        sound_idle = gi.soundindex ("tank/tnkidle1.wav");
        sound_sight = gi.soundindex ("tank/sight1.wav");
        sound_search = gi.soundindex ("tank/tnkatck1.wav");
        sound_step = gi.soundindex ("tank/step.wav");
        sound_pain = gi.soundindex ("tank/tnkpain2.wav");
        sound_death = gi.soundindex ("tank/tnkdeth2.wav");
        sound_warmup = gi.soundindex ("tank/tnkatck4.wav");
        sound_fire = gi.soundindex ("tank/tnkatck5.wav");

        gi.modelindex ("models/monsters/badass/gib_larm.md2");
        gi.modelindex ("models/monsters/badass/gib_rarm.md2");
        gi.modelindex ("models/monsters/badass/gib_lleg.md2");
        gi.modelindex ("models/monsters/badass/gib_rleg.md2");
        gi.modelindex ("models/monsters/badass/gib_torso.md2");

        self->health = 1000;
        self->max_health = self->health;
        self->gib_health = -200;
        self->mass = 550;

        self->pain = sentinel_pain;
        self->die = sentinel_die;

        self->monsterinfo.stand = sentinel_stand;
        self->monsterinfo.walk = sentinel_run;
        self->monsterinfo.run = sentinel_run;
        self->monsterinfo.attack = sentinel_attack;
        self->monsterinfo.melee = NULL;
        self->monsterinfo.sight = sentinel_sight;
        self->monsterinfo.search = sentinel_search;
        self->monsterinfo.idle = sentinel_idle;
        self->monsterinfo.speed = 28;

        self->monsterinfo.currentmove = &sentinel_move_stand;
        self->monsterinfo.scale = MODEL_SCALE;
        self->s.sound = sound_idle;

        gi.linkentity (self);

        walkmonster_start (self);
}
