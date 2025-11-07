/*
 * Badass – placeholder entry for future implementation.
 */

#include "g_local.h"
#include "m_badass.h"

void SP_monster_badass (edict_t *self)
{
        if (!self)
                return;

        gi.dprintf ("monster_badass is not implemented yet\n");
        G_FreeEdict (self);
}
