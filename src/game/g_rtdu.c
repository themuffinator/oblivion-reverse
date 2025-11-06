#include "g_local.h"

#define RTDU_SEARCH_RADIUS      768
#define RTDU_FIRE_INTERVAL      0.2f
#define RTDU_COOLDOWN_TIME      1.0f
#define RTDU_DEPLOY_DISTANCE    64.0f
#define RTDU_MAX_PITCH          0.0f
#define RTDU_PROJECTILE_OFFSET  32.0f

static int rtdu_model_index;
static int rtdu_tripod_model_index;
static int rtdu_fire_sound;
static int rtdu_spawn_sound;

static const vec3_t rtdu_mins = {-16.0f, -16.0f, 0.0f};
static const vec3_t rtdu_maxs = { 16.0f,  16.0f, 48.0f};

static void RTDU_ClearTripod(edict_t *turret)
{
        if (!turret)
                return;

        if (turret->target_ent && turret->target_ent->inuse)
                G_FreeEdict(turret->target_ent);

        turret->target_ent = NULL;
}

static void RTDU_UnlinkClient(edict_t *owner, edict_t *turret)
{
        if (!owner || !owner->client)
                return;

        if (owner->client->rtdu.turret == turret)
                owner->client->rtdu.turret = NULL;
}

static void RTDU_TurretDie(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
        edict_t *owner = self->owner;

        RTDU_ClearTripod(self);
        RTDU_UnlinkClient(owner, self);

        if (owner && owner->client)
                owner->client->rtdu.next_use_time = level.time + RTDU_COOLDOWN_TIME;

        self->takedamage = DAMAGE_NO;
        BecomeExplosion1(self);
}

static qboolean RTDU_CanSee(edict_t *self, edict_t *target)
{
        edict_t *owner;

        if (!target)
                return false;

        if (!target->inuse || target->health <= 0)
                return false;

        owner = self->owner;
        if (target == owner)
                return false;

        if (!(target->takedamage))
                return false;

        if (target->client)
        {
                if (!deathmatch->value && owner && owner->client == target->client)
                        return false;
        }
        else if (!(target->svflags & SVF_MONSTER))
        {
                return false;
        }

        if (!visible(self, target))
                return false;

        return true;
}

static edict_t *RTDU_FindEnemy(edict_t *self)
{
        edict_t *ent = NULL;
        edict_t *best = NULL;
        float best_dist = 0.0f;

        while ((ent = findradius(ent, self->s.origin, RTDU_SEARCH_RADIUS)) != NULL)
        {
                vec3_t delta;
                float dist;

                if (!RTDU_CanSee(self, ent))
                        continue;

                VectorSubtract(ent->s.origin, self->s.origin, delta);
                dist = VectorLength(delta);

                if (!best || dist < best_dist)
                {
                        best = ent;
                        best_dist = dist;
                }
        }

        return best;
}

static void RTDU_UpdateTripod(edict_t *turret)
{
        if (!turret->target_ent)
                return;

        VectorCopy(turret->s.origin, turret->target_ent->s.origin);
        turret->target_ent->s.origin[2] -= 20.0f;
        VectorCopy(turret->s.angles, turret->target_ent->s.angles);
        gi.linkentity(turret->target_ent);
}

static void RTDU_TurretThink(edict_t *self)
{
        if (!self->inuse)
                return;

        if (!self->owner || !self->owner->inuse)
        {
                self->owner = NULL;
        }

        if (self->enemy && !RTDU_CanSee(self, self->enemy))
                self->enemy = NULL;

        if (!self->enemy)
                self->enemy = RTDU_FindEnemy(self);

        if (self->enemy)
        {
                vec3_t forward;
                vec3_t start;

                VectorSubtract(self->enemy->s.origin, self->s.origin, forward);
                VectorNormalize(forward);

                vectoangles(forward, self->s.angles);
                self->s.angles[0] = RTDU_MAX_PITCH;

                VectorCopy(self->s.origin, start);
                start[2] += RTDU_PROJECTILE_OFFSET;

                if (level.time >= self->wait)
                {
                        int damage = 12;

                        fire_blaster(self->owner ? self->owner : self,
                                     start, forward, damage, 1000, EF_BLASTER, false);
                        if (!rtdu_fire_sound)
                                rtdu_fire_sound = gi.soundindex("weapons/blastf1a.wav");
                        gi.sound(self, CHAN_WEAPON, rtdu_fire_sound, 1, ATTN_NORM, 0);
                        self->wait = level.time + RTDU_FIRE_INTERVAL;
                        self->s.frame = (self->s.frame + 1) % 4;
                }
        }
        else
        {
                self->s.frame = 0;
        }

        RTDU_UpdateTripod(self);

        self->nextthink = level.time + FRAMETIME;
        gi.linkentity(self);
}

static qboolean RTDU_FindDeployLocation(edict_t *player, vec3_t origin, vec3_t angles)
{
        vec3_t forward, right, up;
        vec3_t end;
        trace_t tr;

        if (!player || !player->client)
                return false;

        AngleVectors(player->client->v_angle, forward, right, up);
        VectorNormalize(forward);

        VectorMA(player->s.origin, RTDU_DEPLOY_DISTANCE, forward, origin);
        origin[2] = player->s.origin[2];

        tr = gi.trace(player->s.origin, rtdu_mins, rtdu_maxs, origin, player, MASK_SOLID);
        if (tr.startsolid || tr.allsolid)
                return false;

        VectorCopy(tr.endpos, origin);

        VectorCopy(origin, end);
        end[2] -= 128.0f;
        tr = gi.trace(origin, rtdu_mins, rtdu_maxs, end, player, MASK_SOLID);
        if (tr.fraction == 1.0f)
                return false;

        VectorCopy(tr.endpos, origin);
        origin[2] -= rtdu_mins[2];

        angles[0] = 0.0f;
        angles[1] = player->s.angles[1];
        angles[2] = 0.0f;

        return true;
}

static edict_t *RTDU_CreateTripod(edict_t *turret)
{
        edict_t *tripod;

        tripod = G_Spawn();
        tripod->movetype = MOVETYPE_NONE;
        tripod->solid = SOLID_NOT;
        tripod->s.modelindex = rtdu_tripod_model_index;
        VectorCopy(turret->s.origin, tripod->s.origin);
        VectorCopy(turret->s.angles, tripod->s.angles);
        tripod->owner = turret;
        gi.linkentity(tripod);

        turret->target_ent = tripod;

        return tripod;
}

static edict_t *RTDU_SpawnTurret(edict_t *owner, vec3_t origin, vec3_t angles)
{
        edict_t *turret;

        turret = G_Spawn();
        turret->classname = "rtdu_turret";
        turret->movetype = MOVETYPE_NONE;
        turret->solid = SOLID_BBOX;
        VectorCopy(rtdu_mins, turret->mins);
        VectorCopy(rtdu_maxs, turret->maxs);
        VectorCopy(origin, turret->s.origin);
        VectorCopy(angles, turret->s.angles);
        turret->s.modelindex = rtdu_model_index;
        turret->takedamage = DAMAGE_YES;
        turret->die = RTDU_TurretDie;
        turret->health = 200;
        turret->max_health = 200;
        turret->clipmask = MASK_SHOT;
        turret->owner = owner;
        turret->nextthink = level.time + FRAMETIME;
        turret->think = RTDU_TurretThink;
        turret->wait = level.time;
        gi.linkentity(turret);

        RTDU_CreateTripod(turret);

        return turret;
}

static void RTDU_RemoveTurret(edict_t *player, qboolean refund)
{
        gitem_t *item;
        int index;
        edict_t *turret;

        if (!player || !player->client)
                return;

        turret = player->client->rtdu.turret;
        if (!turret)
                return;

        RTDU_ClearTripod(turret);
        RTDU_UnlinkClient(player, turret);
        G_FreeEdict(turret);

        if (!refund)
                return;

        item = FindItem("RTDU");
        if (!item)
                return;

        index = ITEM_INDEX(item);
        player->client->pers.inventory[index]++;
}

static void RTDU_PrecacheModels(void)
{
        if (!rtdu_model_index)
                rtdu_model_index = gi.modelindex("models/objects/rtdu/rtdu.md2");
        if (!rtdu_tripod_model_index)
                rtdu_tripod_model_index = gi.modelindex("models/objects/rtdu/tripod.md2");
        if (!rtdu_fire_sound)
                rtdu_fire_sound = gi.soundindex("weapons/blastf1a.wav");
        if (!rtdu_spawn_sound)
                rtdu_spawn_sound = gi.soundindex("misc/tele1.wav");
}

qboolean Pickup_RTDU (edict_t *ent, edict_t *other)
{
        int index;

        if (!other->client)
                return false;

        index = ITEM_INDEX(ent->item);

        if ((skill->value == 1 && other->client->pers.inventory[index] >= 2) ||
            (skill->value >= 2 && other->client->pers.inventory[index] >= 1))
        {
                return false;
        }

        if ((coop->value) && (ent->item->flags & IT_STAY_COOP) &&
            (other->client->pers.inventory[index] > 0))
        {
                return false;
        }

        other->client->pers.inventory[index]++;

        if (deathmatch->value)
        {
                if (!(ent->spawnflags & DROPPED_ITEM))
                        SetRespawn(ent, ent->item->quantity);

                if ((int)dmflags->value & DF_INSTANT_ITEMS)
                        rtdu_use(other, ent->item);
        }

        return true;
}

void rtdu_use (edict_t *ent, gitem_t *item)
{
        int index;
        vec3_t origin;
        vec3_t angles;
        edict_t *turret;

        if (!ent->client)
                return;

        if (ent->client->rtdu.next_use_time > level.time)
                return;

        index = ITEM_INDEX(item);

        if (ent->client->rtdu.turret && ent->client->rtdu.turret->inuse)
        {
                RTDU_RemoveTurret(ent, true);
                ent->client->rtdu.next_use_time = level.time + RTDU_COOLDOWN_TIME;
                return;
        }

        if (ent->client->pers.inventory[index] <= 0)
        {
                gi.cprintf(ent, PRINT_HIGH, "No RTDU available.\n");
                return;
        }

        RTDU_PrecacheModels();

        if (!RTDU_FindDeployLocation(ent, origin, angles))
        {
                gi.cprintf(ent, PRINT_HIGH, "Cannot deploy the RTDU here.\n");
                return;
        }

        turret = RTDU_SpawnTurret(ent, origin, angles);
        ent->client->rtdu.turret = turret;
        ent->client->pers.inventory[index]--;
        ent->client->rtdu.next_use_time = level.time + RTDU_COOLDOWN_TIME;
        if (!rtdu_spawn_sound)
                rtdu_spawn_sound = gi.soundindex("misc/tele1.wav");
        gi.sound(ent, CHAN_AUTO, rtdu_spawn_sound, 1, ATTN_NORM, 0);
}

void Drop_RTDU (edict_t *ent, gitem_t *item)
{
        if (ent->client)
                RTDU_RemoveTurret(ent, true);

        Drop_General(ent, item);
}

void RTDU_PlayerDisconnect (edict_t *ent)
{
        RTDU_RemoveTurret(ent, false);
}

void RTDU_PlayerDie (edict_t *ent)
{
        RTDU_RemoveTurret(ent, false);
}

void RTDU_RunFrame (void)
{
        int i;

        for (i = 0; i < maxclients->value; i++)
        {
                edict_t *ent = g_edicts + 1 + i;

                if (!ent->inuse || !ent->client)
                        continue;

                if (ent->client->rtdu.turret && !ent->client->rtdu.turret->inuse)
                        ent->client->rtdu.turret = NULL;
        }
}
