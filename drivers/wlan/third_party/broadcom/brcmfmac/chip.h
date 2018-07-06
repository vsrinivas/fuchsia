/*
 * Copyright (c) 2014 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef BRCMF_CHIP_H
#define BRCMF_CHIP_H

#include "linuxisms.h"

#define CORE_CC_REG(base, field) (base + offsetof(struct chipcregs, field))

/**
 * struct brcmf_chip - chip level information.
 *
 * @chip: chip identifier.
 * @chiprev: chip revision.
 * @cc_caps: chipcommon core capabilities.
 * @cc_caps_ext: chipcommon core extended capabilities.
 * @pmucaps: PMU capabilities.
 * @pmurev: PMU revision.
 * @rambase: RAM base address (only applicable for ARM CR4 chips).
 * @ramsize: amount of RAM on chip including retention.
 * @srsize: amount of retention RAM on chip.
 * @name: string representation of the chip identifier.
 */
struct brcmf_chip {
    uint32_t chip;
    uint32_t chiprev;
    uint32_t cc_caps;
    uint32_t cc_caps_ext;
    uint32_t pmucaps;
    uint32_t pmurev;
    uint32_t rambase;
    uint32_t ramsize;
    uint32_t srsize;
    char name[8];
};

/**
 * struct brcmf_core - core related information.
 *
 * @id: core identifier.
 * @rev: core revision.
 * @base: base address of core register space.
 */
struct brcmf_core {
    uint16_t id;
    uint16_t rev;
    uint32_t base;
};

/**
 * struct brcmf_buscore_ops - buscore specific callbacks.
 *
 * @read32: read 32-bit value over bus.
 * @write32: write 32-bit value over bus.
 * @prepare: prepare bus for core configuration.
 * @setup: bus-specific core setup.
 * @active: chip becomes active.
 *  The callback should use the provided @rstvec when non-zero.
 */
struct brcmf_buscore_ops {
    uint32_t (*read32)(void* ctx, uint32_t addr);
    void (*write32)(void* ctx, uint32_t addr, uint32_t value);
    zx_status_t (*prepare)(void* ctx);
    int (*reset)(void* ctx, struct brcmf_chip* chip);
    int (*setup)(void* ctx, struct brcmf_chip* chip);
    void (*activate)(void* ctx, struct brcmf_chip* chip, uint32_t rstvec);
};

zx_status_t brcmf_chip_attach(void* ctx, const struct brcmf_buscore_ops* ops,
                              struct brcmf_chip** chip_out);
void brcmf_chip_detach(struct brcmf_chip* chip);
struct brcmf_core* brcmf_chip_get_core(struct brcmf_chip* chip, uint16_t coreid);
struct brcmf_core* brcmf_chip_get_chipcommon(struct brcmf_chip* chip);
struct brcmf_core* brcmf_chip_get_pmu(struct brcmf_chip* pub);
bool brcmf_chip_iscoreup(struct brcmf_core* core);
void brcmf_chip_coredisable(struct brcmf_core* core, uint32_t prereset, uint32_t reset);
void brcmf_chip_resetcore(struct brcmf_core* core, uint32_t prereset, uint32_t reset,
                          uint32_t postreset);
void brcmf_chip_set_passive(struct brcmf_chip* ci);
bool brcmf_chip_set_active(struct brcmf_chip* ci, uint32_t rstvec);
bool brcmf_chip_sr_capable(struct brcmf_chip* pub);

#endif /* BRCMF_AXIDMP_H */
