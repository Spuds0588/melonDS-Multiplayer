#!/usr/bin/env python3
"""
mkdiagrom.py - build the melonDS Multiplayer diagnostic ROM.

Why this exists
---------------
The splitscreen host needs something to run that exercises exactly what we care
about - both screens, per-instance input, frame pacing, per-instance identity -
without depending on a commercial game. No test ROM ships with the repository
and no ARM toolchain is guaranteed to be present, so this script hand-assembles
a small ARM9 program and writes a valid NDS ROM around it.

What the ROM shows
------------------
Top screen:
    * horizontally scrolling stripes (proves the frame counter advances)
    * a moving bar in the instance's own colour (proves animation + identity)

Bottom screen:
    * twelve blocks, one per DS button; a block lights up in the instance's
      colour while that button is held (proves per-instance input routing).
      X and Y come from the ARM7, since EXKEY (0x04000136) is an ARM7-only
      register and the ARM9 cannot read it directly
    * one to four "pips" showing the instance's player index
    * a progress bar showing the low bits of the frame counter

Diagnostic ABI
--------------
The host may stamp an identity block into ARM9 main RAM before the ROM starts
reading it:

    0x02FFF000  u32  magic  'MDGM' (0x4D44474D)          (in)
    0x02FFF004  u32  player index (0-3)                  (in)
    0x02FFF008  u32  player colour, BGR555               (in)
    0x02FFF00C  u32  flags (bit 0 = link test)           (in)
    0x02FFF010  u32  main-loop iterations completed      (out)
    0x02FFF014  u32  progress marker for this iteration  (out)
    0x02FFF018  u32  ARM9's first instruction marker     (out)
    0x02FFF01C  u32  pressed-button mask this console sees (out)
    0x02FFF020  u32  colour the ROM actually resolved to     (out)

The last word is how a test can check input routing directly instead of
guessing from pixels: it holds exactly the twelve bits the ARM9 read back from
the ARM7's keypad mailbox.

The ARM7 publishes that mailbox at 0x02FFF800:

    0x02FFF800  u32  all twelve pressed-button bits, as the ARM9 reads them
    0x02FFF804  u32  just the two X/Y bits, for debugging the EXKEY path

When the magic is absent the ROM falls back to green, player 0.

Usage
-----
    python3 mkdiagrom.py -o diag.nds          # build
    python3 mkdiagrom.py --verify diag.nds    # re-parse and validate
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

# --------------------------------------------------------------------------
# NDS memory map constants used by the ROM
# --------------------------------------------------------------------------

IO_BASE = 0x04000000
DISPCNT_A = 0x04000000
VCOUNT_A = 0x04000006
KEYINPUT = 0x04000130
# EXKEY: X/Y, readable from the ARM7 only (GBATEK: "Extended Key Input, ARM7
# only"). melonDS follows the hardware here - NDS::ARM9IORead* has no case for
# 0x04000136 - so the ARM7 publishes the full keypad state for the ARM9.
KEYEX = 0x04000136
# Main-RAM mailbox the ARM7 drops the combined 12-bit pressed-button mask into.
KEYPAD_MAILBOX = 0x02FFF800
DISPCNT_B = 0x04001000
VRAM_MAIN = 0x06000000      # engine A bitmap, VRAM bank A
VRAM_SUB = 0x06200000       # engine B bitmap, VRAM bank C
DIAG_BASE = 0x02FFF000      # identity block stamped by the host

DIAG_MAGIC = 0x4D44474D     # 'MDGM'
DIAG_BOOT_MARKER = 0xB0070000   # written by the ROM's first instruction

ARM9_ROM_OFFSET = 0x1000
ARM9_RAM_ADDRESS = 0x02000000
ARM7_ROM_OFFSET = 0x3000
ARM7_RAM_ADDRESS = 0x02380000
ROM_SIZE = 0x8000

# DS colour format is BGR555. In a 16bpp bitmap, bit 15 has to be set or the
# pixel is treated as transparent, so every colour below is BGR555 | 0x8000.
# The other half of the top-screen stripes. Pairing this against the instance's
# own colour is what makes the two stripe bands read as different colours.
COL_STRIPE_FIXED = 0x98E3
COL_BOTTOM_BG = 0x9082
COL_KEY_IDLE = 0xA104
COL_DEFAULT_PLAYER = 0x83E0
COL_OPAQUE = 0x8000

# DS keypad bits (1 = pressed once inverted; the register itself is active low)
BTN_A, BTN_B, BTN_SELECT, BTN_START = 0, 1, 2, 3
BTN_RIGHT, BTN_LEFT, BTN_UP, BTN_DOWN = 4, 5, 6, 7
BTN_R, BTN_L, BTN_X, BTN_Y = 8, 9, 10, 11
NUM_BUTTONS = 12

BUTTON_NAMES = [
    "A", "B", "Select", "Start", "Right", "Left", "Up", "Down", "R", "L", "X", "Y",
]


# --------------------------------------------------------------------------
# Minimal ARM (ARMv5, ARM state) encoder
# --------------------------------------------------------------------------

COND = {
    "eq": 0x0, "ne": 0x1, "cs": 0x2, "cc": 0x3,
    "hi": 0x8, "ls": 0x9, "ge": 0xA, "lt": 0xB, "gt": 0xC, "le": 0xD,
    "al": 0xE,
}

OP_AND, OP_EOR, OP_SUB, OP_RSB = 0x0, 0x1, 0x2, 0x3
OP_ADD, OP_ADC, OP_SBC, OP_RSC = 0x4, 0x5, 0x6, 0x7
OP_TST, OP_TEQ, OP_CMP, OP_CMN = 0x8, 0x9, 0xA, 0xB
OP_ORR, OP_MOV, OP_BIC, OP_MVN = 0xC, 0xD, 0xE, 0xF

SHIFT_LSL, SHIFT_LSR, SHIFT_ASR, SHIFT_ROR = 0, 1, 2, 3

SP, LR, PC = 13, 14, 15


def enc_rot_imm(value: int) -> int:
    """Encode a 32-bit constant as an ARM rotated 8-bit immediate."""
    value &= 0xFFFFFFFF
    for rot in range(16):
        r = 2 * rot
        imm8 = value if r == 0 else ((value << r) | (value >> (32 - r))) & 0xFFFFFFFF
        if imm8 <= 0xFF:
            return (rot << 8) | imm8
    raise ValueError(f"no ARM rotated-immediate encoding for 0x{value:08X}")


def _dp_imm(opcode: int, s: int, cond: int, rd: int, rn: int, imm: int) -> int:
    return ((cond << 28) | (1 << 25) | (opcode << 21) | (s << 20)
            | (rn << 16) | (rd << 12) | enc_rot_imm(imm))


def _dp_reg(opcode: int, s: int, cond: int, rd: int, rn: int, rm: int,
            shift=SHIFT_LSL, shift_amount=0) -> int:
    if not 0 <= shift_amount <= 31:
        raise ValueError("immediate shift amount must be 0..31")
    return ((cond << 28) | (opcode << 21) | (s << 20) | (rn << 16) | (rd << 12)
            | (shift_amount << 7) | (shift << 5) | rm)


def _single_half(load: int, cond: int, rd: int, rn: int, off: int) -> int:
    """LDRH/STRH with an immediate offset (offset must be a multiple of 2)."""
    if not 0 <= off <= 0xFF or off & 1:
        raise ValueError("halfword offset must be an even value below 0x100")
    # P=1, U=1, I=1 (immediate offset), W=0, then S=0/H=1 for halfword access.
    return ((cond << 28) | (1 << 24) | (1 << 23) | (1 << 22) | (load << 20)
            | (rn << 16) | (rd << 12) | ((off & 0xF0) << 4) | 0xB0 | (off & 0x0F))


def _single_dt(load: int, cond: int, rd: int, rn: int, imm12: int,
               pre: int, up: int) -> int:
    if not 0 <= imm12 <= 0xFFF:
        raise ValueError("data transfer offset must fit in 12 bits")
    return ((cond << 28) | (1 << 26) | (pre << 24) | (up << 23)
            | (load << 20) | (rn << 16) | (rd << 12) | imm12)


# name -> encoding lambda over (cond, *args)
def _i_ldr(cond, rd, rn, off):  return _single_dt(1, cond, rd, rn, off, pre=1, up=1)
def _i_str(cond, rd, rn, off):  return _single_dt(0, cond, rd, rn, off, pre=1, up=1)
def _i_strh(cond, rd, rn, off): return _single_half(0, cond, rd, rn, off)
def _i_ldrh(cond, rd, rn, off): return _single_half(1, cond, rd, rn, off)
def _i_ldrp(cond, rd, rn, off): return _single_dt(1, cond, rd, rn, off, pre=0, up=1)
def _i_strp(cond, rd, rn, off): return _single_dt(0, cond, rd, rn, off, pre=0, up=1)
def _i_mov(cond, rd, imm):      return _dp_imm(OP_MOV, 0, cond, rd, 0, imm)
def _i_add(cond, rd, rn, imm):  return _dp_imm(OP_ADD, 0, cond, rd, rn, imm)
def _i_sub(cond, rd, rn, imm):  return _dp_imm(OP_SUB, 0, cond, rd, rn, imm)
def _i_sub_pc(cond, rd, imm):   return _dp_imm(OP_SUB, 0, cond, rd, PC, imm)
def _i_cmp(cond, rn, imm):      return _dp_imm(OP_CMP, 1, cond, 0, rn, imm)
def _i_tst(cond, rn, imm):      return _dp_imm(OP_TST, 1, cond, 0, rn, imm)
def _i_cmp_reg(cond, rn, rm):   return _dp_reg(OP_CMP, 1, cond, 0, rn, rm)
def _i_and(cond, rd, rn, imm):  return _dp_imm(OP_AND, 0, cond, rd, rn, imm)
def _i_eor(cond, rd, rn, imm):  return _dp_imm(OP_EOR, 0, cond, rd, rn, imm)
def _i_and_reg(cond, rd, rn, rm): return _dp_reg(OP_AND, 0, cond, rd, rn, rm)
def _i_eor_reg(cond, rd, rn, rm): return _dp_reg(OP_EOR, 0, cond, rd, rn, rm)


def _i_subs(cond, rd, rn, imm): return _dp_imm(OP_SUB, 1, cond, rd, rn, imm)
def _i_mov_reg(cond, rd, rm):   return _dp_reg(OP_MOV, 0, cond, rd, 0, rm)
def _i_mov_shift(cond, rd, rm, shift, amount):
    return _dp_reg(OP_MOV, 0, cond, rd, 0, rm, shift, amount)
def _i_add_shift(cond, rd, rn, rm, shift, amount):
    return _dp_reg(OP_ADD, 0, cond, rd, rn, rm, shift, amount)
def _i_bx(cond, rm):            return (cond << 28) | 0x012FFF10 | rm


def _i_branch(cond: int, addr: int, target: int, link: bool) -> int:
    offset = (target - (addr + 8)) >> 2
    if offset < -(1 << 23) or offset >= (1 << 23):
        raise ValueError("branch out of range")
    return ((cond << 28) | (0b101 << 25) | (1 if link else 0) << 24
            | (offset & 0x00FFFFFF))


# --------------------------------------------------------------------------
# Tiny two-pass assembler with a literal pool
# --------------------------------------------------------------------------

class Assembler:
    def __init__(self) -> None:
        self.items = []          # ('label', name) | ('insn', enc) | ('const', rd, value)
        self.labels: dict[str, int] = {}

    # -- program building ---------------------------------------------------

    def label(self, name: str) -> None:
        self.items.append(("label", name))

    def _emit(self, encoded: int) -> None:
        self.items.append(("insn", encoded))

    def mov(self, rd: int, imm: int) -> None:
        self._emit(_i_mov(COND["al"], rd, imm))

    def mov_reg(self, rd: int, rm: int) -> None:
        self._emit(_i_mov_reg(COND["al"], rd, rm))

    def mov_shift(self, rd: int, rm: int, shift: int, amount: int) -> None:
        self._emit(_i_mov_shift(COND["al"], rd, rm, shift, amount))

    def mov_cond(self, cond: int, rd: int, rm: int) -> None:
        self._emit(_i_mov_reg(cond, rd, rm))

    def add(self, rd: int, rn: int, imm: int) -> None:
        self._emit(_i_add(COND["al"], rd, rn, imm))

    def sub(self, rd: int, rn: int, imm: int) -> None:
        self._emit(_i_sub(COND["al"], rd, rn, imm))

    def subs(self, rd: int, rn: int, imm: int) -> None:
        self._emit(_i_subs(COND["al"], rd, rn, imm))

    def add_shift(self, rd: int, rn: int, rm: int, shift: int, amount: int) -> None:
        self._emit(_i_add_shift(COND["al"], rd, rn, rm, shift, amount))

    def and_(self, rd: int, rn: int, imm: int) -> None:
        self._emit(_i_and(COND["al"], rd, rn, imm))

    def eor(self, rd: int, rn: int, imm: int) -> None:
        self._emit(_i_eor(COND["al"], rd, rn, imm))

    def eor_reg(self, rd: int, rn: int, rm: int) -> None:
        self._emit(_i_eor_reg(COND["al"], rd, rn, rm))

    def and_reg(self, rd: int, rn: int, rm: int) -> None:
        self._emit(_i_and_reg(COND["al"], rd, rn, rm))

    def cmp(self, rn: int, imm: int) -> None:
        """Compare a register against an *immediate*.

        Beware: cmp(r0, r1) does not compare two registers, it compares r0 with
        the literal 1. Use cmp_reg for that. Getting this wrong is silent - the
        code assembles and simply never takes the branch.
        """
        self._emit(_i_cmp(COND["al"], rn, imm))

    def cmp_reg(self, rn: int, rm: int) -> None:
        """Compare two registers."""
        self._emit(_i_cmp_reg(COND["al"], rn, rm))

    def tst(self, rn: int, imm: int) -> None:
        self._emit(_i_tst(COND["al"], rn, imm))

    def ldr(self, rd: int, rn: int, off: int = 0) -> None:
        self._emit(_i_ldr(COND["al"], rd, rn, off))

    def str(self, rd: int, rn: int, off: int = 0) -> None:
        self._emit(_i_str(COND["al"], rd, rn, off))

    def str_post(self, rd: int, rn: int, off: int) -> None:
        self._emit(_i_strp(COND["al"], rd, rn, off))

    def strh(self, rd: int, rn: int, off: int) -> None:
        """Halfword store: needed for the 16-bit display registers."""
        self._emit(_i_strh(COND["al"], rd, rn, off))

    def ldrh(self, rd: int, rn: int, off: int = 0) -> None:
        """Halfword load. Required for EXKEY, the ARM7-only X/Y keypad register:
        a 32-bit load at 0x04000136 is word-aligned down to 0x04000134, which is
        the RTC counter, not the keypad."""
        self._emit(_i_ldrh(COND["al"], rd, rn, off))

    def ldr_const(self, rd: int, value: int) -> None:
        """LDR rd, =value via the literal pool."""
        self.items.append(("const", rd, value & 0xFFFFFFFF))

    def bx_lr(self) -> None:
        self._emit(_i_bx(COND["al"], LR))

    def b(self, name: str) -> None:
        self.items.append(("branch", COND["al"], name, False))

    def bl(self, name: str) -> None:
        self.items.append(("branch", COND["al"], name, True))

    def b_cond(self, cond_name: str, name: str) -> None:
        self.items.append(("branch", COND[cond_name], name, False))

    # -- assembly -----------------------------------------------------------

    def assemble(self, base_address: int) -> bytes:
        addr = base_address
        for item in self.items:
            if item[0] == "label":
                self.labels[item[1]] = addr
            else:
                addr += 4

        pool_addr = addr
        pool_values: list[int] = []
        for item in self.items:
            if item[0] == "const" and item[2] not in pool_values:
                pool_values.append(item[2])
        pool_index = {v: i for i, v in enumerate(pool_values)}

        out = bytearray()
        addr = base_address
        for item in self.items:
            kind = item[0]
            if kind == "label":
                continue

            if kind == "insn":
                encoded = item[1]
            elif kind == "const":
                rd, value = item[1], item[2]
                target = pool_addr + 4 * pool_index[value]
                offset = target - (addr + 8)
                if offset < 0:
                    raise ValueError("literal pool must follow the code")
                encoded = 0xE59F0000 | (rd << 12) | offset
            elif kind == "branch":
                _, cond, name, link = item
                if name not in self.labels:
                    raise ValueError(f"unresolved label {name!r}")
                encoded = _i_branch(cond, addr, self.labels[name], link)
            else:
                raise ValueError(f"unknown item {kind!r}")

            out += struct.pack("<I", encoded)
            addr += 4

        for value in pool_values:
            out += struct.pack("<I", value)

        # If the release address changed (labels resolved differently), redo.
        return bytes(out)


# --------------------------------------------------------------------------
# The diagnostic program
# --------------------------------------------------------------------------

# Register plan across the whole program:
#   r6  = main-engine bitmap base (top screen)
#   r7  = sub-engine bitmap base (bottom screen)
#   r8  = frame counter
#   r9  = diagnostic identity block
#   r10 = currently pressed buttons (active high)
#   r11 = this instance's colour (BGR555)
#   r0-r5, r12 are scratch; draw_rect only touches r0-r3, r12 (and reads r4/r5)


def build_arm9() -> bytes:
    a = Assembler()

    # ---- entry point ------------------------------------------------------
    # The console jumps straight to ARM9EntryAddress, which is the start of this
    # image, so the first thing emitted must be the boot code.
    a.label("_start")

    # Very first instruction: prove the ARM9 reached our entry point. The host
    # can read DIAG_BASE+0x18 to tell "ROM never started" apart from "ROM faulted
    # halfway through".
    a.ldr_const(0, DIAG_BASE)
    a.ldr_const(1, DIAG_BOOT_MARKER)
    a.str(1, 0, 0x18)

    # Enable 16bpp bitmap mode (mode 5) with BG2 on both engines.
    #
    # Nothing configures VRAM for us: a direct-booting ROM has to map the banks
    # itself. Bank A becomes the main engine's BG data (visible at 0x06000000)
    # and bank C the sub engine's (0x06200000).
    a.ldr_const(0, DISPCNT_A)
    a.ldr_const(2, DISPCNT_B)

    # One 32-bit store covers VRAMCNT_A..D. Note the byte order: bits 0-7 are
    # bank A (main BG) and bits 16-23 bank C (sub BG). A 32-bit store at
    # 0x04000242 would silently round down to 0x04000240, so keep it aligned.
    a.ldr_const(1, 0x00840081)                # A -> ABG, C -> BBG
    a.str(1, 0, 0x0240)                       # VRAMCNT_A..D
    # DISPCNT bits 16-17 select the display mode: 0 turns the screen off (white),
    # 1 is regular LCDC display, which is what a bitmap mode needs.
    #
    # On the DS the layer enables sit at bits 8..11 (BG0..BG3) with OBJ at bit 12
    # - unlike the GBA layout - so a bitmap-mode 5 BG2 layer is bit 10. OBJ stays
    # off because the ROM draws everything with rectangles.
    a.ldr_const(1, 0x00010405)                # regular display | mode 5 | BG2 on
    a.str(1, 0, 0x0000)                       # DISPCNT (engine A)
    a.str(1, 2, 0x0000)                       # DISPCNT (engine B)

    # BG2CNT: 256x256 area | bitmap | direct colour (16bpp).
    a.ldr_const(1, 0x00004084)
    a.str(1, 0, 0x000C)                       # BG2CNT (engine A)
    a.str(1, 2, 0x000C)                       # BG2CNT (engine B)

    # Extended modes sample the bitmap through an affine transform, so the
    # identity matrix has to be written explicitly or everything reads (0,0).
    # BG2PA/BG2PD are 16-bit registers: a 32-bit store simply gets ignored.
    a.ldr_const(1, 0x00000100)
    a.strh(1, 0, 0x0020)                      # BG2PA (engine A)
    a.strh(1, 0, 0x0026)                      # BG2PD (engine A)
    a.strh(1, 2, 0x0020)                      # BG2PA (engine B)
    a.strh(1, 2, 0x0026)                      # BG2PD (engine B)

    # Paint both backdrops a non-black colour so the host can tell "the LCD
    # pipeline is dead" apart from "the bitmap layer drew nothing".
    a.ldr_const(0, 0x05000000)
    a.ldr_const(1, 0x801F)
    a.strh(1, 0, 0x0000)
    a.ldr_const(0, 0x05000400)
    a.strh(1, 0, 0x0000)

    a.ldr_const(6, VRAM_MAIN)
    a.ldr_const(7, VRAM_SUB)
    a.ldr_const(9, DIAG_BASE)
    a.mov(8, 0)                               # frame counter

    # Resolve our colour: use the host-stamped identity when present.
    a.ldr_const(11, COL_DEFAULT_PLAYER)
    a.ldr(0, 9, 0)                            # r0 = magic
    a.ldr_const(1, DIAG_MAGIC)                # DIAG_MAGIC has no rotated form
    a.cmp_reg(0, 1)                           # NB: cmp() would compare r0 with 1
    a.b_cond("ne", "identity_default")
    a.ldr(11, 9, 8)
    a.label("identity_default")
    # The host stamps a plain BGR555 colour; make it opaque for the bitmap.
    a.add(11, 11, COL_OPAQUE)
    # Publish the colour we ended up with, so the host can tell "the identity
    # never arrived" apart from "the ROM read it but rendered something else".
    a.str(11, 9, 0x20)

    # Progress marker: the host and the smoke test can watch these to tell how
    # far the boot got (DIAG_BASE+0x14) and that frames are still advancing
    # (DIAG_BASE+0x10).
    a.ldr_const(1, 1)
    a.str(1, 9, 0x14)

    # ---- background, painted once ------------------------------------------
    a.mov_reg(0, 7)
    a.mov(1, 0)
    a.mov(2, 0)
    a.mov(3, 256)
    a.mov(4, 192)
    a.ldr_const(5, COL_BOTTOM_BG)
    a.bl("draw_rect")

    # ---- main loop --------------------------------------------------------
    a.label("main_loop")

    # Pick up the pressed-button mask the ARM7 publishes. It is already
    # active-high and covers all twelve buttons, including the X/Y the ARM9
    # cannot read for itself. Until the ARM7 has run once this reads as zero,
    # which is the correct "nothing held" state.
    a.ldr_const(0, KEYPAD_MAILBOX)
    a.ldr(10, 0, 0)
    a.ldr_const(1, 0x00000FFF)
    a.and_reg(10, 10, 1)
    a.str(10, 9, 0x1C)                        # publish what this console sees
    a.ldr_const(1, 2)
    a.str(1, 9, 0x14)                         # stage 2: keypad read

    # Top screen: horizontally scrolling stripes. One of the two stripe colours
    # is this instance's own colour, so the top screen always carries the
    # identity - a host can tell two instances apart from any single frame,
    # instead of having to catch the moment the sliding bar is on screen.
    a.mov(0, 0)                               # y
    a.mov_reg(1, 6)                           # destination pointer
    a.label("top_row")
    a.add_shift(2, 0, 8, SHIFT_LSR, 1)        # y + frame/2
    a.and_(2, 2, 32)
    a.mov_reg(3, 11)                          # stripe colour = our colour
    a.ldr_const(4, COL_STRIPE_FIXED)
    a.cmp(2, 0)
    a.mov_cond(COND["eq"], 3, 4)              # MOVEQ r3, r4
    a.add_shift(3, 3, 3, SHIFT_LSL, 16)       # 32-bit store = two 16bpp pixels
    a.mov(2, 128)                             # 128 32-bit stores = 256 pixels
    a.label("top_col")
    a.str_post(3, 1, 4)
    a.subs(2, 2, 1)
    a.b_cond("ne", "top_col")
    a.add(0, 0, 1)
    a.cmp(0, 192)
    a.b_cond("ne", "top_row")

    a.ldr_const(1, 3)
    a.str(1, 9, 0x14)                         # stage 3: top screen filled

    # Top screen: a bar in our own colour, sliding down the screen.
    a.mov_reg(2, 8)
    a.and_(2, 2, 0x0F)
    a.mov_shift(2, 2, SHIFT_LSL, 3)           # y = (frame & 0xF) * 8
    a.mov_reg(0, 6)
    a.mov(1, 0)
    a.mov(3, 256)
    a.mov(4, 16)
    a.mov_reg(5, 11)
    a.bl("draw_rect")

    # Bottom screen: one block per button, lit while held. The background is
    # painted once before the loop rather than every iteration, so a host
    # sampling at an arbitrary moment never catches the bottom screen flat and
    # blank.
    for button in range(NUM_BUTTONS):
        row, col = divmod(button, 6)
        x = 12 + col * 40
        y = 30 + row * 60
        idle = f"key{button}_idle"
        draw = f"key{button}_draw"

        a.tst(10, 1 << button)
        a.b_cond("eq", idle)
        a.mov_reg(5, 11)                      # held: light up in our own colour
        a.b(draw)
        a.label(idle)
        a.ldr_const(5, COL_KEY_IDLE)
        a.label(draw)
        a.mov_reg(0, 7)
        a.mov(1, x)
        a.mov(2, y)
        a.mov(3, 24)
        a.mov(4, 28)
        a.bl("draw_rect")

    # Bottom screen: player pips in the top-left corner.
    a.mov_reg(5, 11)
    for pip in range(4):
        if pip > 0:
            a.ldr(0, 9, 4)
            a.cmp(0, pip)
            a.b_cond("cc", "pips_done")
        a.mov_reg(0, 7)
        a.mov(1, 8 + pip * 16)
        a.mov(2, 8)
        a.mov(3, 12)
        a.mov(4, 12)
        a.bl("draw_rect")
    a.label("pips_done")

    # Bottom screen: frame counter as a progress bar.
    a.mov_shift(3, 8, SHIFT_LSL, 1)
    a.and_(3, 3, 0x7E)
    a.add(3, 3, 2)
    a.mov_reg(0, 7)
    a.mov(1, 0)
    a.mov(2, 180)
    a.mov(4, 8)
    a.mov_reg(5, 11)
    a.bl("draw_rect")

    a.ldr_const(1, 4)
    a.str(1, 9, 0x14)                         # stage 4: bottom screen drawn

    # Wait for VBlank so the picture updates once per frame.
    a.label("wait_vblank")
    a.ldr_const(0, VCOUNT_A)
    a.ldr(1, 0, 0)
    a.and_(1, 1, 0xFF)
    a.cmp(1, 192)
    a.b_cond("cc", "wait_vblank")

    a.str(8, 9, 0x10)                         # heartbeat: frames drawn so far
    a.ldr_const(1, 5)
    a.str(1, 9, 0x14)

    a.add(8, 8, 1)
    a.b("main_loop")

    # ---- draw_rect: r0=base, r1=x, r2=y, r3=width(px), r4=height(rows), r5=colour
    # Fills an axis-aligned rectangle of 16bpp pixels. Width must be even and
    # non-zero; all of x+w fits inside a 256 pixel scanline for our call sites.
    a.label("draw_rect")
    a.add_shift(5, 5, 5, SHIFT_LSL, 16)       # duplicate the colour across both pixels
    a.mov_shift(12, 3, SHIFT_LSR, 1)          # r12 = 32-bit stores per row
    a.add_shift(0, 0, 2, SHIFT_LSL, 9)        # r0 += y * 512
    a.add_shift(0, 0, 1, SHIFT_LSL, 1)        # r0 += x * 2
    a.mov_reg(1, 4)                           # r1 = rows remaining
    a.label("dr_row")
    a.mov_reg(2, 0)                           # r2 = row pointer
    a.mov_reg(3, 12)                          # r3 = inner counter
    a.label("dr_col")
    a.str_post(5, 2, 4)                       # *r2++ = colour
    a.subs(3, 3, 1)
    a.b_cond("ne", "dr_col")
    a.add(0, 0, 512)                          # next scanline is 512 bytes on
    a.subs(1, 1, 1)
    a.b_cond("ne", "dr_row")
    a.bx_lr()

    return a.assemble(ARM9_RAM_ADDRESS)


def build_arm7() -> bytes:
    """Publishes the keypad to main RAM for the ARM9.

    X/Y only exist in EXKEY, which the hardware exposes to the ARM7 alone, so a
    game that wants them has to ask its ARM7. This does the same thing, in the
    simplest form: the ARM7 polls both keypad registers and drops the combined
    active-high 12-bit mask into a shared main-RAM word that the ARM9 renders.
    """
    a = Assembler()
    a.label("_start")

    a.ldr_const(0, KEYINPUT)                   # r0 = 0x04000130
    a.ldr(1, 0, 0)                             # r1 = raw low keys (A..L)
    a.ldr_const(2, 0x000003FF)                 # ten buttons, one bit each
    a.eor_reg(1, 1, 2)                         # keypad is active low
    a.and_reg(1, 1, 2)                         # r1 = pressed A..L

    # X/Y are a separate, two-bit register and are still active low. Only bits
    # 0-1 of the halfword are meaningful: melonDS resets KeyInput to 0x007F03FF,
    # so a raw read of EXKEY shows 0x7F even with nothing pressed.
    a.ldr_const(0, KEYEX)                      # r0 = 0x04000136
    a.ldrh(3, 0, 0)                            # r3 = raw X/Y in bits 0-1
    a.eor(3, 3, 0x3)                           # active low too
    a.and_(3, 3, 0x3)
    a.add_shift(1, 1, 3, SHIFT_LSL, 10)        # slot X/Y in above A..L

    a.ldr_const(2, KEYPAD_MAILBOX)
    a.str(1, 2, 0)                             # +0: the full twelve-bit mask
    a.str(3, 2, 4)                             # +4: just the X/Y bits
    a.b("_start")
    return a.assemble(ARM7_RAM_ADDRESS)


# --------------------------------------------------------------------------
# ROM container
# --------------------------------------------------------------------------

def crc16_ds(data: bytes) -> int:
    """CRC16 with polynomial 0xA001, initial value 0xFFFF (as used by the DS header)."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
            crc &= 0xFFFF
    return crc


def build_rom(title: str = "MD DIAG", game_code: str = "MDSP") -> bytes:
    arm9 = build_arm9()
    arm7 = build_arm7()

    rom = bytearray(ROM_SIZE)

    title_bytes = title.encode("ascii")[:12].ljust(12, b"\0")
    rom[0x000:0x00C] = title_bytes
    rom[0x00C:0x010] = game_code.encode("ascii")[:4].ljust(4, b"\0")
    rom[0x010:0x012] = b"MD"                   # maker code
    rom[0x012] = 0x00                          # unit code: NDS
    rom[0x013] = 0x00                          # encryption seed select
    rom[0x014] = 0x00                          # device capacity
    rom[0x01C] = 0x00                          # DSi crypto flags
    rom[0x01D] = 0xFF                          # region: free
    rom[0x01E] = 0x00                          # ROM version
    rom[0x01F] = 0x00                          # autostart

    struct.pack_into("<IIII", rom, 0x020, ARM9_ROM_OFFSET, ARM9_RAM_ADDRESS,
                     ARM9_RAM_ADDRESS, len(arm9))
    struct.pack_into("<IIII", rom, 0x030, ARM7_ROM_OFFSET, ARM7_RAM_ADDRESS,
                     ARM7_RAM_ADDRESS, len(arm7))

    struct.pack_into("<IIII", rom, 0x040, 0, 0, 0, 0)          # FNT unused
    struct.pack_into("<IIII", rom, 0x050, 0, 0, 0, 0)          # overlays unused
    struct.pack_into("<II", rom, 0x060, 0x00586000, 0x001808F8)  # card control
    struct.pack_into("<I", rom, 0x068, 0)                      # no banner
    struct.pack_into("<H", rom, 0x06C, 0)                      # secure area CRC16
    struct.pack_into("<H", rom, 0x06E, 0x0D7E)                 # secure area delay
    struct.pack_into("<II", rom, 0x070, 0, 0)                  # autoload lists
    struct.pack_into("<Q", rom, 0x078, 0)                      # secure area disable
    struct.pack_into("<II", rom, 0x080, ROM_SIZE, 0x200)       # ROM / header size
    struct.pack_into("<HH", rom, 0x090, 0xFFFF, 0)             # region end / DSi start
    struct.pack_into("<HH", rom, 0x094, 0, 0)                  # NAND fields

    # The real DS logo is not reproducible here (and only the console BIOS cares
    # about it), so the logo area stays blank while both checksums remain valid.
    rom[0x0C0:0x15C] = b"\0" * 156

    rom[ARM9_ROM_OFFSET:ARM9_ROM_OFFSET + len(arm9)] = arm9
    rom[ARM7_ROM_OFFSET:ARM7_ROM_OFFSET + len(arm7)] = arm7

    struct.pack_into("<H", rom, 0x15C, crc16_ds(bytes(rom[0x0C0:0x15C])))
    struct.pack_into("<H", rom, 0x15E, crc16_ds(bytes(rom[0x000:0x15E])))

    return bytes(rom)


# --------------------------------------------------------------------------
# Validator
# --------------------------------------------------------------------------

def verify_rom(data: bytes, verbose: bool = True) -> list[str]:
    """Re-parse a ROM produced by build_rom() and return a list of problems."""
    problems: list[str] = []

    def check(condition: bool, message: str) -> None:
        if not condition:
            problems.append(message)

    check(len(data) >= 0x200, "ROM is smaller than the header")
    if problems:
        return problems

    title = data[0x000:0x00C].rstrip(b"\0").decode("ascii", "replace")
    arm9_off, arm9_entry, arm9_ram, arm9_size = struct.unpack_from("<IIII", data, 0x020)
    arm7_off, arm7_entry, arm7_ram, arm7_size = struct.unpack_from("<IIII", data, 0x030)
    rom_size, header_size = struct.unpack_from("<II", data, 0x080)

    check(arm9_off >= 0x200, "ARM9 offset must be past the first header bytes")
    check(arm9_off < 0x4000, "ARM9 offset under 0x4000 is what marks a ROM as homebrew")
    check(arm9_off + arm9_size <= len(data), "ARM9 image runs past the end of the ROM")
    check(arm7_off >= 0x200 and arm7_off + arm7_size <= len(data),
          "ARM7 image outside the ROM")
    check(rom_size == len(data), f"ROMSize field ({rom_size:#x}) != file size ({len(data):#x})")
    check(rom_size & (rom_size - 1) == 0, "ROMSize must be a power of two")
    check(arm9_entry == arm9_ram, "ARM9 entry should be inside the ARM9 RAM image")

    logo_crc, header_crc = struct.unpack_from("<HH", data, 0x15C)
    check(logo_crc == crc16_ds(data[0x0C0:0x15C]), "Nintendo logo CRC16 mismatch")
    check(header_crc == crc16_ds(data[0x000:0x15E]), "header CRC16 mismatch")

    # The ARM9 entry point must be the branch-free setup code we generated.
    entry = struct.unpack_from("<I", data, arm9_off + (arm9_entry - arm9_ram))[0]
    check((entry & 0xFFFFF000) == 0xE59F0000,
          f"ARM9 entry is not the expected literal load (got {entry:#010x})")

    # Sanity check the two display control stores: mode 5 with BG2 enabled.
    words = struct.unpack_from("<64I", data, arm9_off)
    if verbose:
        print(f"  title        : {title!r}")
        print(f"  ARM9         : offset {arm9_off:#06x} entry {arm9_entry:#010x} size {arm9_size} bytes")
        print(f"  ARM7         : offset {arm7_off:#06x} entry {arm7_entry:#010x} size {arm7_size} bytes")
        print(f"  ROM size     : {rom_size:#x} (header {header_size:#x})")
        print(f"  logo CRC16   : {logo_crc:#06x}")
        print(f"  header CRC16 : {header_crc:#06x}")

    return problems


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Build the melonDS Multiplayer diagnostic ROM")
    parser.add_argument("-o", "--output", type=Path, help="where to write the .nds file")
    parser.add_argument("--title", default="MD DIAG", help="12 character internal title")
    parser.add_argument("--game-code", default="MDSP", help="4 character game code")
    parser.add_argument("--verify", type=Path, metavar="ROM",
                        help="validate an existing ROM instead of building one")
    args = parser.parse_args(argv)

    if args.verify:
        data = args.verify.read_bytes()
        print(f"verifying {args.verify} ({len(data)} bytes)")
        problems = verify_rom(data)
        if problems:
            for problem in problems:
                print(f"  FAIL: {problem}", file=sys.stderr)
            return 1
        print("  all checks passed")
        return 0

    if not args.output:
        parser.error("either --output or --verify is required")

    rom = build_rom(args.title, args.game_code)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(rom)

    print(f"wrote {args.output} ({len(rom)} bytes)")
    problems = verify_rom(rom)
    if problems:
        for problem in problems:
            print(f"  FAIL: {problem}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
