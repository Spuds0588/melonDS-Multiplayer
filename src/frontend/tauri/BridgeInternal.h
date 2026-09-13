/*
    Internal glue between the C API (Bridge.cpp) and the Platform backend
    (PlatformBridge.cpp). Not part of the public surface.
*/

#ifndef MELONDS_MULTIPLAYER_BRIDGE_INTERNAL_H
#define MELONDS_MULTIPLAYER_BRIDGE_INTERNAL_H

#include "types.h"

namespace melonDS
{
class NDS;
class LocalMP;
class Firmware;
}

namespace md
{

/* Resolves the instance a Platform callback fired for. May return nullptr. */
melonDS::NDS* nds_for(void* userdata);
/* Slot (0-15) of the instance on the link bus, or -1 when unattached. */
int slot_for(void* userdata);

void notify_stop(void* userdata, int reason);
void notify_save_write(void* userdata, const melonDS::u8* savedata, melonDS::u32 savelen,
                       melonDS::u32 writeoffset, melonDS::u32 writelen);
void notify_gba_save_write(void* userdata, const melonDS::u8* savedata, melonDS::u32 savelen,
                           melonDS::u32 writeoffset, melonDS::u32 writelen);
void notify_firmware_write(void* userdata, const melonDS::Firmware& firmware,
                           melonDS::u32 writeoffset, melonDS::u32 writelen);

/* Shared link bus. Both return nullptr while linking is disabled. */
melonDS::LocalMP* link_bus();
bool link_enabled();

}

#endif /* MELONDS_MULTIPLAYER_BRIDGE_INTERNAL_H */
