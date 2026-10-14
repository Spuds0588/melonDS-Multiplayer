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

/* Called by the platform layer when a console powers its wireless hardware on
   or off. The bridge tracks this so a host can see which slots are actually
   live, as opposed to merely assigned. */
void notify_mp_begin(void* userdata);
void notify_mp_end(void* userdata);

/* Called on every wireless frame the emulated console sends and accepts, so the
   host can report whether a link is actually carrying traffic. Note that a send
   with no listener still counts: the console transmitted regardless. The _kind
   variants split the counters by LocalMP packet type (0=broadcast MP frame,
   1=host cmd, 2=client reply, 3=ack) so stalls can be pinned to a layer.
   note_poll counts receive *attempts*, including ones that returned nothing -
   Wifi::CheckRX polls far more often than packets arrive. */
void note_packet_sent(void* userdata, int len);
void note_packet_sent_kind(void* userdata, int kind, int len);
void note_packet_received_kind(void* userdata, int kind, int len);
void note_poll(void* userdata, bool got_packet);

/* The receive half of a Link test. A probe is armed on the *receiving* console
   with the exact bytes and timestamp the sender is about to queue; every path
   that hands a frame to that console's game (MP_RecvPacket) or to a scripted
   reader (md_link_recv_packet) calls this with what it just read, and the first
   one to see the tag is credited. Written because the drain used to race the
   console's own game for the same inbox and lose: the game polls every frame
   from inside the emulation, so a test that only counted its own drains could
   report "no reply" for frames the game had already consumed (2026-09-30).
   `who` is "game" or "drain" and is reported back verbatim. */
void probe_observe(void* userdata, const char* who, const melonDS::u8* frame, size_t len,
                   melonDS::u64 timestamp);

/* Shared link bus. Both return nullptr while linking is disabled. */
melonDS::LocalMP* link_bus();
bool link_enabled();

}

#endif /* MELONDS_MULTIPLAYER_BRIDGE_INTERNAL_H */
