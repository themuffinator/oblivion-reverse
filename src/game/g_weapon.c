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
#include "g_local.h"


/*
=================
check_dodge

This is a support routine used when a client is firing
a non-instant attack weapon.  It checks to see if a
monster's dodge function should be called.
=================
*/
static void check_dodge (edict_t *self, vec3_t start, vec3_t dir, int speed)
{
	vec3_t	end;
	vec3_t	v;
	trace_t	tr;
	float	eta;

	// easy mode only ducks one quarter the time
	if (skill->value == 0)
	{
		if (random() > 0.25)
			return;
	}
	VectorMA (start, 8192, dir, end);
	tr = gi.trace (start, NULL, NULL, end, self, MASK_SHOT);
	if ((tr.ent) && (tr.ent->svflags & SVF_MONSTER) && (tr.ent->health > 0) && (tr.ent->monsterinfo.dodge) && infront(tr.ent, self))
	{
		VectorSubtract (tr.endpos, start, v);
		eta = (VectorLength(v) - tr.ent->maxs[0]) / speed;
		tr.ent->monsterinfo.dodge (tr.ent, self, eta);
	}
}


/*
=================
fire_hit

Used for all impact (hit/punch/slash) attacks
=================
*/
qboolean fire_hit (edict_t *self, vec3_t aim, int damage, int kick)
{
	trace_t		tr;
	vec3_t		forward, right, up;
	vec3_t		v;
	vec3_t		point;
	float		range;
	vec3_t		dir;

	//see if enemy is in range
	VectorSubtract (self->enemy->s.origin, self->s.origin, dir);
	range = VectorLength(dir);
	if (range > aim[0])
		return false;

	if (aim[1] > self->mins[0] && aim[1] < self->maxs[0])
	{
		// the hit is straight on so back the range up to the edge of their bbox
		range -= self->enemy->maxs[0];
	}
	else
	{
		// this is a side hit so adjust the "right" value out to the edge of their bbox
		if (aim[1] < 0)
			aim[1] = self->enemy->mins[0];
		else
			aim[1] = self->enemy->maxs[0];
	}

	VectorMA (self->s.origin, range, dir, point);

	tr = gi.trace (self->s.origin, NULL, NULL, point, self, MASK_SHOT);
	if (tr.fraction < 1)
	{
		if (!tr.ent->takedamage)
			return false;
		// if it will hit any client/monster then hit the one we wanted to hit
		if ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client))
			tr.ent = self->enemy;
	}

	AngleVectors(self->s.angles, forward, right, up);
	VectorMA (self->s.origin, range, forward, point);
	VectorMA (point, aim[1], right, point);
	VectorMA (point, aim[2], up, point);
	VectorSubtract (point, self->enemy->s.origin, dir);

	// do the damage
	T_Damage (tr.ent, self, self, dir, point, vec3_origin, damage, kick/2, DAMAGE_NO_KNOCKBACK, MOD_HIT);

	if (!(tr.ent->svflags & SVF_MONSTER) && (!tr.ent->client))
		return false;

	// do our special form of knockback here
	VectorMA (self->enemy->absmin, 0.5, self->enemy->size, v);
	VectorSubtract (v, point, v);
	VectorNormalize (v);
	VectorMA (self->enemy->velocity, kick, v, self->enemy->velocity);
	if (self->enemy->velocity[2] > 0)
		self->enemy->groundentity = NULL;
	return true;
}


/*
=================
fire_lead

This is an internal support routine used for bullet/pellet based weapons.
=================
*/
static void fire_lead (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick, int te_impact, int hspread, int vspread, int mod)
{
	trace_t		tr;
	vec3_t		dir;
	vec3_t		forward, right, up;
	vec3_t		end;
	float		r;
	float		u;
	vec3_t		water_start;
	qboolean	water = false;
	int			content_mask = MASK_SHOT | MASK_WATER;

	tr = gi.trace (self->s.origin, NULL, NULL, start, self, MASK_SHOT);
	if (!(tr.fraction < 1.0))
	{
		vectoangles (aimdir, dir);
		AngleVectors (dir, forward, right, up);

		r = crandom()*hspread;
		u = crandom()*vspread;
		VectorMA (start, 8192, forward, end);
		VectorMA (end, r, right, end);
		VectorMA (end, u, up, end);

		if (gi.pointcontents (start) & MASK_WATER)
		{
			water = true;
			VectorCopy (start, water_start);
			content_mask &= ~MASK_WATER;
		}

		tr = gi.trace (start, NULL, NULL, end, self, content_mask);

		// see if we hit water
		if (tr.contents & MASK_WATER)
		{
			int		color;

			water = true;
			VectorCopy (tr.endpos, water_start);

			if (!VectorCompare (start, tr.endpos))
			{
				if (tr.contents & CONTENTS_WATER)
				{
					if (strcmp(tr.surface->name, "*brwater") == 0)
						color = SPLASH_BROWN_WATER;
					else
						color = SPLASH_BLUE_WATER;
				}
				else if (tr.contents & CONTENTS_SLIME)
					color = SPLASH_SLIME;
				else if (tr.contents & CONTENTS_LAVA)
					color = SPLASH_LAVA;
				else
					color = SPLASH_UNKNOWN;

				if (color != SPLASH_UNKNOWN)
				{
					gi.WriteByte (svc_temp_entity);
					gi.WriteByte (TE_SPLASH);
					gi.WriteByte (8);
					gi.WritePosition (tr.endpos);
					gi.WriteDir (tr.plane.normal);
					gi.WriteByte (color);
					gi.multicast (tr.endpos, MULTICAST_PVS);
				}

				// change bullet's course when it enters water
				VectorSubtract (end, start, dir);
				vectoangles (dir, dir);
				AngleVectors (dir, forward, right, up);
				r = crandom()*hspread*2;
				u = crandom()*vspread*2;
				VectorMA (water_start, 8192, forward, end);
				VectorMA (end, r, right, end);
				VectorMA (end, u, up, end);
			}

			// re-trace ignoring water this time
			tr = gi.trace (water_start, NULL, NULL, end, self, MASK_SHOT);
		}
	}

	// send gun puff / flash
	if (!((tr.surface) && (tr.surface->flags & SURF_SKY)))
	{
		if (tr.fraction < 1.0)
		{
			if (tr.ent->takedamage)
			{
				T_Damage (tr.ent, self, self, aimdir, tr.endpos, tr.plane.normal, damage, kick, DAMAGE_BULLET, mod);
			}
			else
			{
				if (strncmp (tr.surface->name, "sky", 3) != 0)
				{
					gi.WriteByte (svc_temp_entity);
					gi.WriteByte (te_impact);
					gi.WritePosition (tr.endpos);
					gi.WriteDir (tr.plane.normal);
					gi.multicast (tr.endpos, MULTICAST_PVS);

					if (self->client)
						PlayerNoise(self, tr.endpos, PNOISE_IMPACT);
				}
			}
		}
	}

	// if went through water, determine where the end and make a bubble trail
	if (water)
	{
		vec3_t	pos;

		VectorSubtract (tr.endpos, water_start, dir);
		VectorNormalize (dir);
		VectorMA (tr.endpos, -2, dir, pos);
		if (gi.pointcontents (pos) & MASK_WATER)
			VectorCopy (pos, tr.endpos);
		else
			tr = gi.trace (pos, NULL, NULL, water_start, tr.ent, MASK_WATER);

		VectorAdd (water_start, tr.endpos, pos);
		VectorScale (pos, 0.5, pos);

		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_BUBBLETRAIL);
		gi.WritePosition (water_start);
		gi.WritePosition (tr.endpos);
		gi.multicast (pos, MULTICAST_PVS);
	}
}


/*
=================
fire_bullet

Fires a single round.  Used for machinegun and chaingun.  Would be fine for
pistols, rifles, etc....
=================
*/
void fire_bullet (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick, int hspread, int vspread, int mod)
{
	fire_lead (self, start, aimdir, damage, kick, TE_GUNSHOT, hspread, vspread, mod);
}


/*
=================
fire_shotgun

Shoots shotgun pellets.  Used by shotgun and super shotgun.
=================
*/
void fire_shotgun (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick, int hspread, int vspread, int count, int mod)
{
	int		i;

	for (i = 0; i < count; i++)
		fire_lead (self, start, aimdir, damage, kick, TE_SHOTGUN, hspread, vspread, mod);
}


/*
=================
fire_blaster

Fires a single blaster bolt.  Used by the blaster and hyper blaster.
=================
*/
void blaster_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	int		mod;

	if (other == self->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (self);
		return;
	}

	if (self->owner->client)
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);

	if (other->takedamage)
	{
		if (self->count)
			mod = self->count;
		else if (self->spawnflags & 1)
			mod = MOD_HYPERBLASTER;
		else
			mod = MOD_BLASTER;
		T_Damage (other, self, self->owner, self->velocity, self->s.origin, plane->normal, self->dmg, 1, DAMAGE_ENERGY, mod);
	}
	else
	{
		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_BLASTER);
		gi.WritePosition (self->s.origin);
		if (!plane)
			gi.WriteDir (vec3_origin);
		else
			gi.WriteDir (plane->normal);
		gi.multicast (self->s.origin, MULTICAST_PVS);
	}

	G_FreeEdict (self);
}

void fire_blaster_with_mod (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, int effect, qboolean hyper, int mod)
{
	edict_t *bolt;
	trace_t tr;

	VectorNormalize (dir);

	bolt = G_Spawn();
	bolt->svflags = SVF_DEADMONSTER;
	// yes, I know it looks weird that projectiles are deadmonsters
	// what this means is that when prediction is used against the object
	// (blaster/hyperblaster shots), the player won't be solid clipped against
	// the object.  Right now trying to run into a firing hyperblaster
	// is very jerky since you are predicted 'against' the shots.
	VectorCopy (start, bolt->s.origin);
	VectorCopy (start, bolt->s.old_origin);
	vectoangles (dir, bolt->s.angles);
	VectorScale (dir, speed, bolt->velocity);
	bolt->movetype = MOVETYPE_FLYMISSILE;
	bolt->clipmask = MASK_SHOT;
	bolt->solid = SOLID_BBOX;
	bolt->s.effects |= effect;
	VectorClear (bolt->mins);
	VectorClear (bolt->maxs);
	bolt->s.modelindex = gi.modelindex ("models/objects/laser/tris.md2");
	bolt->s.sound = gi.soundindex ("misc/lasfly.wav");
	bolt->owner = self;
	bolt->touch = blaster_touch;
	bolt->nextthink = level.time + 2;
	bolt->think = G_FreeEdict;
	bolt->dmg = damage;
	bolt->classname = "bolt";
	if (hyper)
		bolt->spawnflags = 1;
	bolt->count = mod;
	gi.linkentity (bolt);

	if (self->client)
		check_dodge (self, bolt->s.origin, dir, speed);

	tr = gi.trace (self->s.origin, NULL, NULL, bolt->s.origin, bolt, MASK_SHOT);
	if (tr.fraction < 1.0)
	{
		VectorMA (bolt->s.origin, -10, dir, bolt->s.origin);
		bolt->touch (bolt, tr.ent, NULL, NULL);
	}
}

void fire_blaster (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, int effect, qboolean hyper)
{
	fire_blaster_with_mod (self, start, dir, damage, speed, effect, hyper, 0);
}



/*
=================
fire_grenade
=================
*/
static void Grenade_Explode (edict_t *ent)
{
	vec3_t		origin;
	int			mod;

	if (ent->owner->client)
		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);

	//FIXME: if we are onground then raise our Z just a bit since we are a point?
	if (ent->enemy)
	{
		float	points;
		vec3_t	v;
		vec3_t	dir;

		VectorAdd (ent->enemy->mins, ent->enemy->maxs, v);
		VectorMA (ent->enemy->s.origin, 0.5, v, v);
		VectorSubtract (ent->s.origin, v, v);
		points = ent->dmg - 0.5 * VectorLength (v);
		VectorSubtract (ent->enemy->s.origin, ent->s.origin, dir);
		if (ent->spawnflags & 1)
			mod = MOD_HANDGRENADE;
		else
			mod = MOD_GRENADE;
		T_Damage (ent->enemy, ent, ent->owner, dir, ent->s.origin, vec3_origin, (int)points, (int)points, DAMAGE_RADIUS, mod);
	}

	if (ent->spawnflags & 2)
		mod = MOD_HELD_GRENADE;
	else if (ent->spawnflags & 1)
		mod = MOD_HG_SPLASH;
	else
		mod = MOD_G_SPLASH;
	T_RadiusDamage(ent, ent->owner, ent->dmg, ent->enemy, ent->dmg_radius, mod);

	VectorMA (ent->s.origin, -0.02, ent->velocity, origin);
	gi.WriteByte (svc_temp_entity);
	if (ent->waterlevel)
	{
		if (ent->groundentity)
			gi.WriteByte (TE_GRENADE_EXPLOSION_WATER);
		else
			gi.WriteByte (TE_ROCKET_EXPLOSION_WATER);
	}
	else
	{
		if (ent->groundentity)
			gi.WriteByte (TE_GRENADE_EXPLOSION);
		else
			gi.WriteByte (TE_ROCKET_EXPLOSION);
	}
	gi.WritePosition (origin);
	gi.multicast (ent->s.origin, MULTICAST_PHS);

        G_FreeEdict (ent);
}

void fire_plasma_bolt (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, int effect, int mod)
{
	edict_t *bolt;
	trace_t tr;

	VectorNormalize (dir);

	bolt = G_Spawn();
	bolt->svflags = SVF_DEADMONSTER;
	VectorCopy (start, bolt->s.origin);
	VectorCopy (start, bolt->s.old_origin);
	vectoangles (dir, bolt->s.angles);
	VectorScale (dir, speed, bolt->velocity);
	bolt->movetype = MOVETYPE_FLYMISSILE;
	bolt->clipmask = MASK_SHOT;
	bolt->solid = SOLID_BBOX;
	bolt->s.effects |= effect;
	VectorClear (bolt->mins);
	VectorClear (bolt->maxs);
	bolt->s.modelindex = gi.modelindex ("models/objects/laser/tris.md2");
	bolt->s.sound = gi.soundindex ("misc/lasfly.wav");
	bolt->owner = self;
	bolt->touch = blaster_touch;
	bolt->nextthink = level.time + 2;
	bolt->think = G_FreeEdict;
	bolt->dmg = damage;
	bolt->classname = "bolt";
	bolt->count = mod;
	gi.linkentity (bolt);

	if (self->client)
		check_dodge (self, bolt->s.origin, dir, speed);

	tr = gi.trace (self->s.origin, NULL, NULL, bolt->s.origin, bolt, MASK_SHOT);
	if (tr.fraction < 1.0)
	{
		VectorMA (bolt->s.origin, -10, dir, bolt->s.origin);
		bolt->touch (bolt, tr.ent, NULL, NULL);
	}
}

static void Grenade_Touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other == ent->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (ent);
		return;
	}

	if (!other->takedamage)
	{
		if (ent->spawnflags & 1)
		{
			if (random() > 0.5)
				gi.sound (ent, CHAN_VOICE, gi.soundindex ("weapons/hgrenb1a.wav"), 1, ATTN_NORM, 0);
			else
				gi.sound (ent, CHAN_VOICE, gi.soundindex ("weapons/hgrenb2a.wav"), 1, ATTN_NORM, 0);
		}
		else
		{
			gi.sound (ent, CHAN_VOICE, gi.soundindex ("weapons/grenlb1b.wav"), 1, ATTN_NORM, 0);
		}
		return;
	}

	ent->enemy = other;
	Grenade_Explode (ent);
}

void fire_grenade (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int speed, float timer, float damage_radius)
{
	edict_t	*grenade;
	vec3_t	dir;
	vec3_t	forward, right, up;

	vectoangles (aimdir, dir);
	AngleVectors (dir, forward, right, up);

	grenade = G_Spawn();
	VectorCopy (start, grenade->s.origin);
	VectorScale (aimdir, speed, grenade->velocity);
	VectorMA (grenade->velocity, 200 + crandom() * 10.0, up, grenade->velocity);
	VectorMA (grenade->velocity, crandom() * 10.0, right, grenade->velocity);
	VectorSet (grenade->avelocity, 300, 300, 300);
	grenade->movetype = MOVETYPE_BOUNCE;
	grenade->clipmask = MASK_SHOT;
	grenade->solid = SOLID_BBOX;
	grenade->s.effects |= EF_GRENADE;
	VectorClear (grenade->mins);
	VectorClear (grenade->maxs);
	grenade->s.modelindex = gi.modelindex ("models/objects/grenade/tris.md2");
	grenade->owner = self;
	grenade->touch = Grenade_Touch;
	grenade->nextthink = level.time + timer;
	grenade->think = Grenade_Explode;
	grenade->dmg = damage;
	grenade->dmg_radius = damage_radius;
	grenade->classname = "grenade";

	gi.linkentity (grenade);
}

void fire_grenade2 (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int speed, float timer, float damage_radius, qboolean held)
{
	edict_t	*grenade;
	vec3_t	dir;
	vec3_t	forward, right, up;

	vectoangles (aimdir, dir);
	AngleVectors (dir, forward, right, up);

	grenade = G_Spawn();
	VectorCopy (start, grenade->s.origin);
	VectorScale (aimdir, speed, grenade->velocity);
	VectorMA (grenade->velocity, 200 + crandom() * 10.0, up, grenade->velocity);
	VectorMA (grenade->velocity, crandom() * 10.0, right, grenade->velocity);
	VectorSet (grenade->avelocity, 300, 300, 300);
	grenade->movetype = MOVETYPE_BOUNCE;
	grenade->clipmask = MASK_SHOT;
	grenade->solid = SOLID_BBOX;
	grenade->s.effects |= EF_GRENADE;
	VectorClear (grenade->mins);
	VectorClear (grenade->maxs);
	grenade->s.modelindex = gi.modelindex ("models/objects/grenade2/tris.md2");
	grenade->owner = self;
	grenade->touch = Grenade_Touch;
	grenade->nextthink = level.time + timer;
	grenade->think = Grenade_Explode;
	grenade->dmg = damage;
	grenade->dmg_radius = damage_radius;
	grenade->classname = "hgrenade";
	if (held)
		grenade->spawnflags = 3;
	else
		grenade->spawnflags = 1;
	grenade->s.sound = gi.soundindex("weapons/hgrenc1b.wav");

	if (timer <= 0.0)
		Grenade_Explode (grenade);
	else
	{
		gi.sound (self, CHAN_WEAPON, gi.soundindex ("weapons/hgrent1a.wav"), 1, ATTN_NORM, 0);
		gi.linkentity (grenade);
	}
}


/*
=================
fire_rocket
=================
*/
void rocket_touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t		origin;
	int			n;

	if (other == ent->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (ent);
		return;
	}

	if (ent->owner->client)
		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);

	// calculate position for the explosion entity
	VectorMA (ent->s.origin, -0.02, ent->velocity, origin);

	if (other->takedamage)
	{
		int mod = ent->count ? ent->count : MOD_ROCKET;
		T_Damage (other, ent, ent->owner, ent->velocity, ent->s.origin, plane->normal, ent->dmg, 0, 0, mod);
	}
	else
	{
		// don't throw any debris in net games
		if (!deathmatch->value && !coop->value)
		{
			if ((surf) && !(surf->flags & (SURF_WARP|SURF_TRANS33|SURF_TRANS66|SURF_FLOWING)))
			{
				n = rand() % 5;
				while(n--)
					ThrowDebris (ent, "models/objects/debris2/tris.md2", 2, ent->s.origin);
			}
		}
	}

	{
		int splash_mod = ent->mass ? ent->mass : MOD_R_SPLASH;
		T_RadiusDamage(ent, ent->owner, ent->radius_dmg, other, ent->dmg_radius, splash_mod);
	}

	gi.WriteByte (svc_temp_entity);
	if (ent->waterlevel)
		gi.WriteByte (TE_ROCKET_EXPLOSION_WATER);
	else
		gi.WriteByte (TE_ROCKET_EXPLOSION);
	gi.WritePosition (origin);
	gi.multicast (ent->s.origin, MULTICAST_PHS);

	G_FreeEdict (ent);
}

void fire_rocket (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, float damage_radius, int radius_damage)
{
	edict_t	*rocket;

	rocket = G_Spawn();
	VectorCopy (start, rocket->s.origin);
	VectorCopy (dir, rocket->movedir);
	vectoangles (dir, rocket->s.angles);
	VectorScale (dir, speed, rocket->velocity);
	rocket->movetype = MOVETYPE_FLYMISSILE;
	rocket->clipmask = MASK_SHOT;
	rocket->solid = SOLID_BBOX;
	rocket->s.effects |= EF_ROCKET;
	VectorClear (rocket->mins);
	VectorClear (rocket->maxs);
	rocket->s.modelindex = gi.modelindex ("models/objects/rocket/tris.md2");
	rocket->owner = self;
	rocket->touch = rocket_touch;
	rocket->nextthink = level.time + 8000/speed;
	rocket->think = G_FreeEdict;
	rocket->dmg = damage;
	rocket->radius_dmg = radius_damage;
	rocket->dmg_radius = damage_radius;
	rocket->s.sound = gi.soundindex ("weapons/rockfly.wav");
	rocket->classname = "rocket";

	if (self->client)
		check_dodge (self, rocket->s.origin, dir, speed);

	gi.linkentity (rocket);
}

void fire_oblivion_rocket (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, float damage_radius, int radius_damage, int direct_mod, int splash_mod)
{
	edict_t *rocket;

	rocket = G_Spawn();
	VectorCopy (start, rocket->s.origin);
	VectorCopy (dir, rocket->movedir);
	vectoangles (dir, rocket->s.angles);
	VectorScale (dir, speed, rocket->velocity);
	rocket->movetype = MOVETYPE_FLYMISSILE;
	rocket->clipmask = MASK_SHOT;
	rocket->solid = SOLID_BBOX;
	rocket->s.effects |= EF_ROCKET;
	VectorClear (rocket->mins);
	VectorClear (rocket->maxs);
	rocket->s.modelindex = gi.modelindex ("models/objects/rocket/tris.md2");
	rocket->owner = self;
	rocket->touch = rocket_touch;
	rocket->nextthink = level.time + 8000/speed;
	rocket->think = G_FreeEdict;
	rocket->dmg = damage;
	rocket->radius_dmg = radius_damage;
	rocket->dmg_radius = damage_radius;
	rocket->s.sound = gi.soundindex ("weapons/rockfly.wav");
	rocket->classname = "rocket";
	rocket->count = direct_mod;
	rocket->mass = splash_mod;

	if (self->client)
		check_dodge (self, rocket->s.origin, dir, speed);

	gi.linkentity (rocket);
}


/*
=================
fire_rail
=================
*/
void fire_rail (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick)
{
	vec3_t		from;
	vec3_t		end;
	trace_t		tr;
	edict_t		*ignore;
	int			mask;
	qboolean	water;

	VectorMA (start, 8192, aimdir, end);
	VectorCopy (start, from);
	ignore = self;
	water = false;
	mask = MASK_SHOT|CONTENTS_SLIME|CONTENTS_LAVA;
	while (ignore)
	{
		tr = gi.trace (from, NULL, NULL, end, ignore, mask);

		if (tr.contents & (CONTENTS_SLIME|CONTENTS_LAVA))
		{
			mask &= ~(CONTENTS_SLIME|CONTENTS_LAVA);
			water = true;
		}
		else
		{
			if ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client))
				ignore = tr.ent;
			else
				ignore = NULL;

			if ((tr.ent != self) && (tr.ent->takedamage))
				T_Damage (tr.ent, self, self, aimdir, tr.endpos, tr.plane.normal, damage, kick, 0, MOD_RAILGUN);
		}

		VectorCopy (tr.endpos, from);
	}

	// send gun puff / flash
	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_RAILTRAIL);
	gi.WritePosition (start);
	gi.WritePosition (tr.endpos);
	gi.multicast (self->s.origin, MULTICAST_PHS);
//	gi.multicast (start, MULTICAST_PHS);
	if (water)
	{
		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_RAILTRAIL);
		gi.WritePosition (start);
		gi.WritePosition (tr.endpos);
		gi.multicast (tr.endpos, MULTICAST_PHS);
	}

	if (self->client)
		PlayerNoise(self, tr.endpos, PNOISE_IMPACT);
}


/*
=================
fire_bfg
=================
*/
void bfg_explode (edict_t *self)
{
	edict_t	*ent;
	float	points;
	vec3_t	v;
	float	dist;

	if (self->s.frame == 0)
	{
		// the BFG effect
		ent = NULL;
		while ((ent = findradius(ent, self->s.origin, self->dmg_radius)) != NULL)
		{
			if (!ent->takedamage)
				continue;
			if (ent == self->owner)
				continue;
			if (!CanDamage (ent, self))
				continue;
			if (!CanDamage (ent, self->owner))
				continue;

			VectorAdd (ent->mins, ent->maxs, v);
			VectorMA (ent->s.origin, 0.5, v, v);
			VectorSubtract (self->s.origin, v, v);
			dist = VectorLength(v);
			points = self->radius_dmg * (1.0 - sqrt(dist/self->dmg_radius));
			if (ent == self->owner)
				points = points * 0.5;

			gi.WriteByte (svc_temp_entity);
			gi.WriteByte (TE_BFG_EXPLOSION);
			gi.WritePosition (ent->s.origin);
			gi.multicast (ent->s.origin, MULTICAST_PHS);
			T_Damage (ent, self, self->owner, self->velocity, ent->s.origin, vec3_origin, (int)points, 0, DAMAGE_ENERGY, MOD_BFG_EFFECT);
		}
	}

	self->nextthink = level.time + FRAMETIME;
	self->s.frame++;
	if (self->s.frame == 5)
		self->think = G_FreeEdict;
}

void bfg_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other == self->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (self);
		return;
	}

	if (self->owner->client)
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);

	// core explosion - prevents firing it into the wall/floor
	if (other->takedamage)
		T_Damage (other, self, self->owner, self->velocity, self->s.origin, plane->normal, 200, 0, 0, MOD_BFG_BLAST);
	T_RadiusDamage(self, self->owner, 200, other, 100, MOD_BFG_BLAST);

	gi.sound (self, CHAN_VOICE, gi.soundindex ("weapons/bfg__x1b.wav"), 1, ATTN_NORM, 0);
	self->solid = SOLID_NOT;
	self->touch = NULL;
	VectorMA (self->s.origin, -1 * FRAMETIME, self->velocity, self->s.origin);
	VectorClear (self->velocity);
	self->s.modelindex = gi.modelindex ("sprites/s_bfg3.sp2");
	self->s.frame = 0;
	self->s.sound = 0;
	self->s.effects &= ~EF_ANIM_ALLFAST;
	self->think = bfg_explode;
	self->nextthink = level.time + FRAMETIME;
	self->enemy = other;

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_BFG_BIGEXPLOSION);
	gi.WritePosition (self->s.origin);
	gi.multicast (self->s.origin, MULTICAST_PVS);
}


void bfg_think (edict_t *self)
{
	edict_t	*ent;
	edict_t	*ignore;
	vec3_t	point;
	vec3_t	dir;
	vec3_t	start;
	vec3_t	end;
	int		dmg;
	trace_t	tr;

	if (deathmatch->value)
		dmg = 5;
	else
		dmg = 10;

	ent = NULL;
	while ((ent = findradius(ent, self->s.origin, 256)) != NULL)
	{
		if (ent == self)
			continue;

		if (ent == self->owner)
			continue;

		if (!ent->takedamage)
			continue;

		if (!(ent->svflags & SVF_MONSTER) && (!ent->client) && (strcmp(ent->classname, "misc_explobox") != 0))
			continue;

		VectorMA (ent->absmin, 0.5, ent->size, point);

		VectorSubtract (point, self->s.origin, dir);
		VectorNormalize (dir);

		ignore = self;
		VectorCopy (self->s.origin, start);
		VectorMA (start, 2048, dir, end);
		while(1)
		{
			tr = gi.trace (start, NULL, NULL, end, ignore, CONTENTS_SOLID|CONTENTS_MONSTER|CONTENTS_DEADMONSTER);

			if (!tr.ent)
				break;

			// hurt it if we can
			if ((tr.ent->takedamage) && !(tr.ent->flags & FL_IMMUNE_LASER) && (tr.ent != self->owner))
				T_Damage (tr.ent, self, self->owner, dir, tr.endpos, vec3_origin, dmg, 1, DAMAGE_ENERGY, MOD_BFG_LASER);

			// if we hit something that's not a monster or player we're done
			if (!(tr.ent->svflags & SVF_MONSTER) && (!tr.ent->client))
			{
				gi.WriteByte (svc_temp_entity);
				gi.WriteByte (TE_LASER_SPARKS);
				gi.WriteByte (4);
				gi.WritePosition (tr.endpos);
				gi.WriteDir (tr.plane.normal);
				gi.WriteByte (self->s.skinnum);
				gi.multicast (tr.endpos, MULTICAST_PVS);
				break;
			}

			ignore = tr.ent;
			VectorCopy (tr.endpos, start);
		}

		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_BFG_LASER);
		gi.WritePosition (self->s.origin);
		gi.WritePosition (tr.endpos);
		gi.multicast (self->s.origin, MULTICAST_PHS);
	}

	self->nextthink = level.time + FRAMETIME;
}


void fire_bfg (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, float damage_radius)
{
	edict_t	*bfg;

	bfg = G_Spawn();
	VectorCopy (start, bfg->s.origin);
	VectorCopy (dir, bfg->movedir);
	vectoangles (dir, bfg->s.angles);
	VectorScale (dir, speed, bfg->velocity);
	bfg->movetype = MOVETYPE_FLYMISSILE;
	bfg->clipmask = MASK_SHOT;
	bfg->solid = SOLID_BBOX;
	bfg->s.effects |= EF_BFG | EF_ANIM_ALLFAST;
	VectorClear (bfg->mins);
	VectorClear (bfg->maxs);
	bfg->s.modelindex = gi.modelindex ("sprites/s_bfg1.sp2");
	bfg->owner = self;
	bfg->touch = bfg_touch;
	bfg->nextthink = level.time + 8000/speed;
	bfg->think = G_FreeEdict;
	bfg->radius_dmg = damage;
	bfg->dmg_radius = damage_radius;
	bfg->classname = "bfg blast";
	bfg->s.sound = gi.soundindex ("weapons/bfg__l1a.wav");

	bfg->think = bfg_think;
	bfg->nextthink = level.time + FRAMETIME;
	bfg->teammaster = bfg;
	bfg->teamchain = NULL;

	if (self->client)
		check_dodge (self, bfg->s.origin, dir, speed);

	gi.linkentity (bfg);
}


/*
=================
Oblivion projectile helpers
=================
*/

static void deatomizer_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
    vec3_t  dir;

    if (other == self->owner)
        return;

    if (surf && (surf->flags & SURF_SKY))
    {
        G_FreeEdict (self);
        return;
    }

    if (other->takedamage)
    {
        if (!VectorCompare (self->velocity, vec3_origin))
            VectorNormalize2 (self->velocity, dir);
        else
            VectorClear (dir);
        T_Damage (other, self, self->owner, dir, self->s.origin,
                plane ? plane->normal : vec3_origin, self->dmg, 0, DAMAGE_ENERGY, MOD_DEATOMIZER);
    }
    else
    {
        gi.WriteByte (svc_temp_entity);
        gi.WriteByte (TE_PLASMA_EXPLOSION);
        gi.WritePosition (self->s.origin);
        gi.multicast (self->s.origin, MULTICAST_PVS);
    }

    if (self->dmg_radius > 0)
        T_RadiusDamage (self, self->owner, self->radius_dmg, other, self->dmg_radius, MOD_DEATOMIZER_SPLASH);

    G_FreeEdict (self);
}

void fire_deatomizer (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, float damage_radius, int splash_damage)
{
    edict_t *bolt;

    bolt = G_Spawn ();
    VectorCopy (start, bolt->s.origin);
    VectorCopy (start, bolt->s.old_origin);
    vectoangles (dir, bolt->s.angles);
    VectorScale (dir, speed, bolt->velocity);
    bolt->movetype = MOVETYPE_FLYMISSILE;
    bolt->clipmask = MASK_SHOT;
    bolt->solid = SOLID_BBOX;
    VectorClear (bolt->mins);
    VectorClear (bolt->maxs);
    bolt->s.effects = EF_BLASTER | EF_HYPERBLASTER;
    bolt->s.sound = gi.soundindex ("misc/lasfly.wav");
    bolt->s.modelindex = gi.modelindex ("models/objects/laser/tris.md2");
    bolt->owner = self;
    bolt->touch = deatomizer_touch;
    bolt->nextthink = level.time + 8000 / speed;
    bolt->think = G_FreeEdict;
    bolt->dmg = damage;
    bolt->radius_dmg = splash_damage;
    bolt->dmg_radius = damage_radius;
    bolt->classname = "deatomizer bolt";

    if (self->client)
        check_dodge (self, bolt->s.origin, dir, speed);

    gi.linkentity (bolt);
}

static void plasma_pistol_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
    vec3_t dir;

    if (other == self->owner)
        return;

    if (surf && (surf->flags & SURF_SKY))
    {
        G_FreeEdict (self);
        return;
    }

    if (other->takedamage)
    {
        if (!VectorCompare (self->velocity, vec3_origin))
            VectorNormalize2 (self->velocity, dir);
        else
            VectorClear (dir);
        T_Damage (other, self, self->owner, dir, self->s.origin,
                plane ? plane->normal : vec3_origin, self->dmg, 0, DAMAGE_ENERGY, MOD_PLASMA_PISTOL);
    }
    else
    {
        gi.WriteByte (svc_temp_entity);
        gi.WriteByte (TE_PLASMA_EXPLOSION);
        gi.WritePosition (self->s.origin);
        gi.multicast (self->s.origin, MULTICAST_PVS);
    }

    G_FreeEdict (self);
}

void fire_plasma_pistol (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed)
{
    edict_t *bolt;

    bolt = G_Spawn ();
    VectorCopy (start, bolt->s.origin);
    VectorCopy (start, bolt->s.old_origin);
    vectoangles (dir, bolt->s.angles);
    VectorScale (dir, speed, bolt->velocity);
    bolt->movetype = MOVETYPE_FLYMISSILE;
    bolt->clipmask = MASK_SHOT;
    bolt->solid = SOLID_BBOX;
    VectorClear (bolt->mins);
    VectorClear (bolt->maxs);
    bolt->s.effects = EF_PLASMA;
    bolt->s.sound = gi.soundindex ("misc/lasfly.wav");
    bolt->s.modelindex = gi.modelindex ("models/objects/laser/tris.md2");
    bolt->owner = self;
    bolt->touch = plasma_pistol_touch;
    bolt->nextthink = level.time + 8000 / speed;
    bolt->think = G_FreeEdict;
    bolt->dmg = damage;
    bolt->classname = "plasma pistol";

    if (self->client)
        check_dodge (self, bolt->s.origin, dir, speed);

    gi.linkentity (bolt);
}

static void plasma_rifle_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
    vec3_t dir;

    if (other == self->owner)
        return;

    if (surf && (surf->flags & SURF_SKY))
    {
        G_FreeEdict (self);
        return;
    }

    if (other->takedamage)
    {
        if (!VectorCompare (self->velocity, vec3_origin))
            VectorNormalize2 (self->velocity, dir);
        else
            VectorClear (dir);
        T_Damage (other, self, self->owner, dir, self->s.origin,
                plane ? plane->normal : vec3_origin, self->dmg, 0, DAMAGE_ENERGY, MOD_PLASMA_RIFLE);
    }
    else
    {
        gi.WriteByte (svc_temp_entity);
        gi.WriteByte (TE_PLASMA_EXPLOSION);
        gi.WritePosition (self->s.origin);
        gi.multicast (self->s.origin, MULTICAST_PVS);
    }

    G_FreeEdict (self);
}

void fire_plasma_rifle (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed)
{
    edict_t *bolt;

    bolt = G_Spawn ();
    VectorCopy (start, bolt->s.origin);
    VectorCopy (start, bolt->s.old_origin);
    vectoangles (dir, bolt->s.angles);
    VectorScale (dir, speed, bolt->velocity);
    bolt->movetype = MOVETYPE_FLYMISSILE;
    bolt->clipmask = MASK_SHOT;
    bolt->solid = SOLID_BBOX;
    VectorClear (bolt->mins);
    VectorClear (bolt->maxs);
    bolt->s.effects = EF_PLASMA | EF_ANIM_ALLFAST;
    bolt->s.sound = gi.soundindex ("misc/lasfly.wav");
    bolt->s.modelindex = gi.modelindex ("models/objects/laser/tris.md2");
    bolt->owner = self;
    bolt->touch = plasma_rifle_touch;
    bolt->nextthink = level.time + 8000 / speed;
    bolt->think = G_FreeEdict;
    bolt->dmg = damage;
    bolt->classname = "plasma rifle";

    if (self->client)
        check_dodge (self, bolt->s.origin, dir, speed);

    gi.linkentity (bolt);
}

void fire_donut (edict_t *self, vec3_t origin, float damage_radius, int splash_damage, edict_t *ignore)
{
    edict_t *attacker;

    attacker = self->owner ? self->owner : self;
    T_RadiusDamage (self, attacker, splash_damage, ignore, damage_radius, MOD_DONUT);
}

static void dod_explode (edict_t *self)
{
    edict_t *ignore;

    if (!self->inuse)
        return;

    ignore = self->enemy;

    self->s.sound = 0;
    gi.sound (self, CHAN_AUTO, gi.soundindex("sound/dod/DoD.wav"), 1, ATTN_NORM, 0);

    gi.WriteByte (svc_temp_entity);
    gi.WriteByte (TE_EXPLOSION2);
    gi.WritePosition (self->s.origin);
    gi.multicast (self->s.origin, MULTICAST_PHS);

    fire_donut (self, self->s.origin, self->dmg_radius, self->radius_dmg, ignore);

    G_FreeEdict (self);
}

static void dod_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
    vec3_t dir;

    if (other == self->owner)
        return;

    if (surf && (surf->flags & SURF_SKY))
    {
        G_FreeEdict (self);
        return;
    }

    self->enemy = NULL;

    if (other->takedamage)
    {
        self->enemy = other;

        if (!VectorCompare (self->velocity, vec3_origin))
            VectorNormalize2 (self->velocity, dir);
        else
            VectorClear (dir);

        T_Damage (other, self, self->owner, dir, self->s.origin,
                plane ? plane->normal : vec3_origin, self->dmg, 0, DAMAGE_ENERGY, MOD_DONUT);
    }

    dod_explode (self);
}

void fire_dod (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, float damage_radius, int splash_damage)
{
    edict_t *bolt;

    bolt = G_Spawn ();
    VectorCopy (start, bolt->s.origin);
    VectorCopy (start, bolt->s.old_origin);
    vectoangles (dir, bolt->s.angles);
    VectorScale (dir, speed, bolt->velocity);
    bolt->movetype = MOVETYPE_FLYMISSILE;
    bolt->clipmask = MASK_SHOT;
    bolt->solid = SOLID_BBOX;
    VectorClear (bolt->mins);
    VectorClear (bolt->maxs);
    bolt->s.effects = EF_PLASMA | EF_ANIM_ALLFAST;
    bolt->s.renderfx = RF_FULLBRIGHT;
    bolt->s.modelindex = gi.modelindex ("models/objects/dod/tris.md2");
    bolt->s.sound = gi.soundindex ("sound/dod/DoD_hum.wav");
    bolt->owner = self;
    bolt->enemy = NULL;
    bolt->touch = dod_touch;
    bolt->nextthink = level.time + 2.0f;
    bolt->think = dod_explode;
    bolt->dmg = damage;
    bolt->radius_dmg = splash_damage;
    bolt->dmg_radius = damage_radius;
    bolt->classname = "dod";

    if (self->client)
        check_dodge (self, bolt->s.origin, dir, speed);

    gi.linkentity (bolt);
}

static void hellfury_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
    vec3_t dir;

    if (other == self->owner)
        return;

    if (surf && (surf->flags & SURF_SKY))
    {
        G_FreeEdict (self);
        return;
    }

    if (other->takedamage)
    {
        VectorNormalize2 (self->velocity, dir);
        T_Damage (other, self, self->owner, dir, self->s.origin,
                plane ? plane->normal : vec3_origin, self->dmg, 0, DAMAGE_ENERGY | DAMAGE_RADIUS, MOD_HELLFURY);
    }

    fire_donut (self, self->s.origin, self->dmg_radius, self->radius_dmg, other);

    gi.WriteByte (svc_temp_entity);
    gi.WriteByte (TE_EXPLOSION1);
    gi.WritePosition (self->s.origin);
    gi.multicast (self->s.origin, MULTICAST_PVS);

    G_FreeEdict (self);
}

void fire_hellfury (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, float damage_radius, int splash_damage)
{
    edict_t *bolt;

    bolt = G_Spawn ();
    VectorCopy (start, bolt->s.origin);
    VectorCopy (start, bolt->s.old_origin);
    vectoangles (dir, bolt->s.angles);
    VectorScale (dir, speed, bolt->velocity);
    bolt->movetype = MOVETYPE_FLYMISSILE;
    bolt->clipmask = MASK_SHOT;
    bolt->solid = SOLID_BBOX;
    VectorClear (bolt->mins);
    VectorClear (bolt->maxs);
    bolt->s.effects = EF_ROCKET;
    bolt->s.modelindex = gi.modelindex ("models/objects/rocket/tris.md2");
    bolt->owner = self;
    bolt->touch = hellfury_touch;
    bolt->nextthink = level.time + 8000 / speed;
    bolt->think = G_FreeEdict;
    bolt->dmg = damage;
    bolt->radius_dmg = splash_damage;
    bolt->dmg_radius = damage_radius;
    bolt->classname = "hellfury";

    if (self->client)
        check_dodge (self, bolt->s.origin, dir, speed);

    gi.linkentity (bolt);
}

void fire_laser_cannon (edict_t *self, vec3_t start, vec3_t dir, int damage, int kick)
{
    vec3_t  end;
    trace_t tr;

    VectorMA (start, 8192, dir, end);
    tr = gi.trace (start, NULL, NULL, end, self, MASK_SHOT);

    if (tr.ent && tr.ent->takedamage)
    {
        T_Damage (tr.ent, self, self, dir, tr.endpos, tr.plane.normal, damage, kick, DAMAGE_ENERGY, MOD_LASERCANNON);
    }

    gi.WriteByte (svc_temp_entity);
    gi.WriteByte (TE_BFG_LASER);
    gi.WritePosition (start);
    gi.WritePosition (tr.endpos);
    gi.multicast (start, MULTICAST_PVS);
}

/*
=============
detpack_die

Explode the detpack when it is destroyed by external damage.
=============
*/
static void detpack_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	if (attacker)
		self->owner = attacker;

	detpack_detonate (self);
}

/*
=============
detpack_arm

Clear the detpack's temporary flight handlers once it has landed and armed.
=============
*/
static void detpack_arm (edict_t *self)
{
	self->think = NULL;
	self->nextthink = 0;
	self->touch = NULL;
}

void detpack_detonate (edict_t *self)
{
	vec3_t	origin;
	edict_t	*attacker;

	if (self->owner && self->owner->client)
		PlayerNoise (self->owner, self->s.origin, PNOISE_IMPACT);

	VectorMA (self->s.origin, -0.02f, self->velocity, origin);
	attacker = self->owner ? self->owner : self;

	gi.WriteByte (svc_temp_entity);
	if (self->waterlevel)
	{
		if (self->groundentity)
		{
			gi.WriteByte (TE_GRENADE_EXPLOSION_WATER);
		}
		else
		{
			gi.WriteByte (TE_ROCKET_EXPLOSION_WATER);
		}
	}
	else
	{
		if (self->groundentity)
		{
			gi.WriteByte (TE_GRENADE_EXPLOSION);
		}
		else
		{
			gi.WriteByte (TE_ROCKET_EXPLOSION);
		}
	}
	gi.WritePosition (origin);
	gi.multicast (self->s.origin, MULTICAST_PHS);

	T_RadiusDamage (self, attacker,
		self->radius_dmg ? self->radius_dmg : self->dmg,
		NULL,
		self->dmg_radius ? self->dmg_radius : self->radius_dmg,
		MOD_DETPACK);

	G_FreeEdict (self);
}
/*
=============
detpack_touch

Handle the detpack coming to rest on world geometry.
=============
*/
static void detpack_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other == self->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (self);
		return;
	}

	if (!self->groundentity)
	{
		VectorClear (self->velocity);
		VectorClear (self->avelocity);
		self->movetype = MOVETYPE_NONE;
		self->touch = NULL;
		self->think = detpack_arm;
		self->nextthink = level.time + 0.2f;
		self->groundentity = other;
	}
}

#define MAX_ACTIVE_DETPACKS 5

/*
=============
detpack_enforce_limit

Ensure a single owner cannot exceed the detpack count that the HLIL logic enforces.
=============
*/
static void detpack_enforce_limit (edict_t *charge)
{
	edict_t	*ent;
	edict_t	*oldest;
	int		count;
	int		i;

	if (!charge || !charge->owner)
		return;

	oldest = charge;
	count = 0;

	for (i = 1; i < globals.num_edicts; i++)
	{
		ent = &g_edicts[i];
		if (!ent->inuse)
			continue;
		if (!ent->classname)
			continue;
		if (strcmp (ent->classname, "detpack"))
			continue;
		if (ent->owner != charge->owner)
			continue;

		count++;

		if ((ent != charge) && (oldest == charge || ent->timestamp < oldest->timestamp))
			oldest = ent;
	}

	if (count > MAX_ACTIVE_DETPACKS && oldest)
		detpack_detonate (oldest);
}

/*
=============
fire_detpack

Spawn the thrown detpack projectile and enforce the per-owner count cap.
=============
*/
edict_t *fire_detpack (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int speed, float damage_radius)
{
	edict_t *charge;

	charge = G_Spawn ();
	VectorCopy (start, charge->s.origin);
	VectorCopy (start, charge->s.old_origin);
	vectoangles (aimdir, charge->s.angles);
	VectorScale (aimdir, speed, charge->velocity);
	charge->movetype = MOVETYPE_BOUNCE;
	charge->clipmask = MASK_SHOT;
	charge->solid = SOLID_BBOX;
	VectorSet (charge->mins, -8, -8, 0);
	VectorSet (charge->maxs, 8, 8, 16);
	charge->s.modelindex = gi.modelindex ("models/objects/detpack/tris.md2");
	charge->s.effects = EF_GRENADE;
	charge->owner = self;
	charge->touch = detpack_touch;
	charge->think = detpack_arm;
	charge->nextthink = level.time + 0.2f;
	charge->dmg = damage;
	charge->radius_dmg = damage;
	charge->dmg_radius = damage_radius;
	charge->classname = "detpack";
	charge->takedamage = DAMAGE_YES;
	charge->die = detpack_die;
	charge->timestamp = level.time;

	gi.linkentity (charge);
	detpack_enforce_limit (charge);

	return charge;
}

/*
=============
remote_detonator_trigger

Detonate every active detpack that belongs to the specified owner.
=============
*/
void remote_detonator_trigger (edict_t *owner)
{
	int		i;
	edict_t	*ent;

	for (i = 1; i < globals.num_edicts; i++)
	{
		ent = &g_edicts[i];
		if (!ent->inuse)
			continue;
		if (!ent->classname)
			continue;
		if (strcmp (ent->classname, "detpack"))
			continue;
		if (ent->owner != owner)
			continue;

		detpack_detonate (ent);
	}
}

static void proximity_mine_explode (edict_t *self, edict_t *target)
{
    if (target && target->takedamage)
    {
        vec3_t dir;
        VectorSubtract (target->s.origin, self->s.origin, dir);
        VectorNormalize (dir);
        T_Damage (target, self, self->owner ? self->owner : self, dir, self->s.origin, vec3_origin, self->dmg, 0, DAMAGE_ENERGY, MOD_MINE);
    }

    gi.WriteByte (svc_temp_entity);
    gi.WriteByte (TE_PLASMA_EXPLOSION);
    gi.WritePosition (self->s.origin);
    gi.multicast (self->s.origin, MULTICAST_PVS);

    if (self->dmg_radius > 0)
        T_RadiusDamage (self, self->owner ? self->owner : self, self->radius_dmg, target, self->dmg_radius, MOD_MINE_SPLASH);

    G_FreeEdict (self);
}

static void proximity_mine_think (edict_t *self)
{
    edict_t *ent;

    ent = NULL;
    while ((ent = findradius (ent, self->s.origin, self->dmg_radius)) != NULL)
    {
        if (ent == self->owner)
            continue;
        if (!ent->takedamage)
            continue;
        if (!(ent->svflags & SVF_MONSTER) && !ent->client)
            continue;

        proximity_mine_explode (self, ent);
        return;
    }

    self->nextthink = level.time + 0.1f;
}

static void proximity_mine_arm (edict_t *self)
{
    self->think = proximity_mine_think;
    self->nextthink = level.time + 0.1f;
}

static void proximity_mine_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
    if (other == self->owner)
        return;

    if (surf && (surf->flags & SURF_SKY))
    {
        G_FreeEdict (self);
        return;
    }

    if (!self->groundentity)
    {
        VectorClear (self->velocity);
        VectorClear (self->avelocity);
        self->movetype = MOVETYPE_NONE;
        self->touch = NULL;
        self->think = proximity_mine_arm;
        self->nextthink = level.time + 0.2f;
        self->groundentity = other;
    }
}

edict_t *fire_proximity_mine (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int speed, float damage_radius, int splash_damage)
{
    edict_t *mine;

    mine = G_Spawn ();
    VectorCopy (start, mine->s.origin);
    VectorCopy (start, mine->s.old_origin);
    vectoangles (aimdir, mine->s.angles);
    VectorScale (aimdir, speed, mine->velocity);
    mine->movetype = MOVETYPE_BOUNCE;
    mine->clipmask = MASK_SHOT;
    mine->solid = SOLID_BBOX;
    VectorSet (mine->mins, -8, -8, 0);
    VectorSet (mine->maxs, 8, 8, 16);
    mine->s.effects = EF_GRENADE;
    mine->s.modelindex = gi.modelindex ("models/objects/laser/tris.md2");
    mine->owner = self;
    mine->touch = proximity_mine_touch;
    mine->think = proximity_mine_arm;
    mine->nextthink = level.time + 0.2f;
    mine->dmg = damage;
    mine->radius_dmg = splash_damage;
    mine->dmg_radius = damage_radius;
    mine->classname = "prox_mine";

    gi.linkentity (mine);

    return mine;
}
