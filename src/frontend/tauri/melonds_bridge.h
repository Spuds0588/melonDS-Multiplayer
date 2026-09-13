/*
    melonDS Multiplayer - native bridge C API

    This is the only surface the Tauri (Rust) host talks to. Everything here is
    plain C so it can be consumed through FFI without a binding generator.

    Threading contract: each instance is expected to be driven by exactly one
    thread at a time. Different instances may run concurrently on different
    threads (that is the whole point - one emulator per player), and the
    instances are linked together through the local multiplayer bus so that
    in-game wireless play works between them.
*/

#ifndef MELONDS_MULTIPLAYER_BRIDGE_H
#define MELONDS_MULTIPLAYER_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MD_SCREEN_WIDTH   256
#define MD_SCREEN_HEIGHT  192
#define MD_SCREEN_PIXELS  (MD_SCREEN_WIDTH * MD_SCREEN_HEIGHT)
#define MD_MAX_INSTANCES  16

/* Button bit layout, matches the emulated DS keypad register. */
#define MD_BTN_A      (1u << 0)
#define MD_BTN_B      (1u << 1)
#define MD_BTN_SELECT (1u << 2)
#define MD_BTN_START  (1u << 3)
#define MD_BTN_RIGHT  (1u << 4)
#define MD_BTN_LEFT   (1u << 5)
#define MD_BTN_UP     (1u << 6)
#define MD_BTN_DOWN   (1u << 7)
#define MD_BTN_R      (1u << 8)
#define MD_BTN_L      (1u << 9)
#define MD_BTN_X      (1u << 10)
#define MD_BTN_Y      (1u << 11)

/* Stop reasons reported by md_stop_reason(). */
enum
{
    MD_STOP_NONE = 0,
    MD_STOP_EXTERNAL = 11,          /* md_reset()/md_link_end() style stop */
    MD_STOP_GBAMODE_NOT_SUPPORTED = 12,
    MD_STOP_BAD_EXCEPTION_REGION = 13,
    MD_STOP_POWER_OFF = 14,
    MD_STOP_UNKNOWN = 15,
};

typedef struct MDInstance MDInstance;

/* ---------------------------------------------------------------- lifecycle */

/* console_type: 0 = NDS, 1 = DSi. Returns NULL on failure. */
MDInstance* md_create(int console_type, const char* name);
void md_destroy(MDInstance* inst);

/* Loads a .nds file. Battery RAM is picked up from "<rom>.sav" when present.
   Returns 0 on success, non-zero on failure (see md_last_error). */
int md_load_rom(MDInstance* inst, const char* rom_path);
void md_eject_rom(MDInstance* inst);

/* Optional BIOS/firmware overrides. Pass NULL to keep the built-in FreeBIOS. */
int md_set_bios(MDInstance* inst, const char* arm9_path, const char* arm7_path);
int md_set_firmware(MDInstance* inst, const char* firmware_path);

void md_reset(MDInstance* inst);

/* Writes into the emulated console's main RAM. addr and len must both be
   multiples of 4. Used to stamp the diagnostic identity block (see
   tools/mkdiagrom) before the first frame. */
int md_write_ram(MDInstance* inst, uint32_t addr, const void* data, size_t len);

/* Reads back main RAM (addr, len must be multiples of 4). Handy for diagnostics
   and for tests that watch a ROM's progress markers. */
int md_read_ram(MDInstance* inst, uint32_t addr, void* out, size_t len);

/* -------------------------------------------------------------------- input */

void md_set_buttons(MDInstance* inst, uint32_t mask);
void md_touch(MDInstance* inst, int x, int y);
void md_release_touch(MDInstance* inst);
void md_set_lid(MDInstance* inst, int closed);

/* ---------------------------------------------------------------- execution */

/* Runs one DS frame (both screens). Returns the number of scanlines executed. */
uint32_t md_run_frame(MDInstance* inst);

/* Borrowed pointers to the current top/bottom framebuffers, 256x192 each,
   stored as 0xAARRGGBB in native byte order (BGRA in memory), exactly as the
   software renderer produces them. Valid until the next md_run_frame(). */
int md_get_screens(MDInstance* inst, const uint32_t** top, const uint32_t** bottom);

/* -------------------------------------------------------------------- state */

/* Size of the scratch buffer md_save_state() needs. */
size_t md_max_state_size(void);

/* Serializes the whole console into out. Returns bytes written, 0 on error. */
size_t md_save_state(MDInstance* inst, uint8_t* out, size_t cap);
/* Restores a state produced by md_save_state(). Returns 0 on success. */
int md_load_state(MDInstance* inst, const uint8_t* data, size_t len);

/* Battery-backed save RAM (borrowed, may be NULL for homebrew with no save). */
const uint8_t* md_get_save(MDInstance* inst, size_t* len);
int md_flush_save(MDInstance* inst, const char* path);

/* --------------------------------------------------------------- link cable */

/* Enables the local multiplayer bus and resets all slot assignments. */
void md_link_init(void);
void md_link_shutdown(void);
/* Assigns/releases a slot (0-15) on the shared link bus. All instances taking
   part in a multiplayer game must be assigned slots before their first frame. */
void md_link_attach(MDInstance* inst, int slot);
void md_link_detach(MDInstance* inst);
int  md_link_slot(MDInstance* inst);

/* -------------------------------------------------------------- diagnostics */

const char* md_last_error(MDInstance* inst);
/* ROM internal title (12 chars max) or "" when no cart is inserted. */
int md_rom_title(MDInstance* inst, char* out, size_t cap);
/* Zero-based index of the frame currently being displayed. */
uint32_t md_frame_counter(MDInstance* inst);
/* Non-zero once the emulated console asked to stop (crash/power off). */
int md_stop_reason(MDInstance* inst);

/* Reads a 32-bit word out of the GPU address space (VRAM windows, palettes,
   OAM). Useful for diagnostics and tests that need to see what the emulated
   console actually stored. */
uint32_t md_read_gpu(MDInstance* inst, uint32_t addr);
/* Same, for the many 16-bit display registers (BGxPA, BGxCNT, ...). */
uint16_t md_read_gpu16(MDInstance* inst, uint32_t addr);

/* Reads a 16-bit register from either CPU's I/O space (cpu is 9 or 7). The two
   cores do not see the same register map - X/Y and the ARM7's peripherals live
   in registers the ARM9 does not decode at all - so testing keypad handling
   means being able to ask each core separately. */
uint16_t md_read_io16(MDInstance* inst, int cpu, uint32_t addr);

/* Program counter of the ARM9 (cpu 9) or ARM7 (cpu 7) core. */
uint32_t md_get_pc(MDInstance* inst, int cpu);
/* Non-zero while the emulated console is asleep (nothing will advance). */
int md_console_asleep(MDInstance* inst);

#ifdef __cplusplus
}
#endif

#endif /* MELONDS_MULTIPLAYER_BRIDGE_H */
