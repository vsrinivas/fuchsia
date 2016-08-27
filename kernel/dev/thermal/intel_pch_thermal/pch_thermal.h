// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <assert.h>
#include <kernel/vm.h>

struct pcie_device_state;

struct pch_thermal_context {
    vmm_aspace_t *aspace;
    struct pch_thermal_registers *regs;
    struct pcie_device_state* pci_device;
};

static inline uint16_t encode_temp(int16_t temp_c)
{
    uint16_t val = (temp_c + 50) * 2;
    ASSERT(!(val & ~0x1ff));
    return val;
}

static inline int16_t decode_temp(uint16_t val)
{
    val = val & 0x1ff;
    return val / 2 - 50;
}

/* Thermal registers */
struct __PACKED pch_thermal_registers {
    /* Temperature */
    volatile uint16_t temp;
    volatile uint16_t _reserved0;
    /* Thermal Sensor Control */
    volatile uint8_t tsc;
    volatile uint8_t _reserved1;
    /* Thermal Sensor Status */
    volatile uint8_t tss;
    volatile uint8_t _reserved2;
    /* Thermal Sensor Enable and Lock */
    volatile uint8_t tsel;
    volatile uint8_t _reserved3;
    /* Thermal Sensor Report Enable and Lock */
    volatile uint8_t tsrel;
    volatile uint8_t _reserved4;
    /* Thermal Sensor SMI Control */
    volatile uint8_t tsmic;
    volatile uint8_t _reserved5[3];
    /* Catastrophic trip Point */
    volatile uint16_t ctt;
    volatile uint16_t _reserved6;
    /* Thermal Alert High Value */
    volatile uint16_t tahv;
    volatile uint16_t _reserved7;
    /* Thermal Alert Low Value */
    volatile uint16_t talv;
    volatile uint16_t _reserved8;
    /* Thermal Sensor Power Management */
    volatile uint16_t tspm;
    volatile uint8_t _reserved9[0x40-0x1e];
    /* Throttle Levels */
    volatile uint32_t tl;
    volatile uint8_t _reserved10[0x50-0x44];
    volatile uint32_t tl2;
    volatile uint8_t _reserved11[0x60-0x54];
    /* PCH Hot Level */
    volatile uint16_t phl;
    /* PCH Control */
    volatile uint8_t phlc;
    volatile uint8_t _reserved12[0x80-0x63];
    /* Thermal Alert Status */
    volatile uint8_t tas;
    volatile uint8_t _reserved13;
    /* PCI Interrupt Event Enables */
    volatile uint8_t tspien;
    volatile uint8_t _reserved14;
    /* PCI Interrupt Event Enables */
    volatile uint8_t tsgpen;
};
static_assert(__offsetof(struct pch_thermal_registers, tsgpen) == 0x84, "");

#if WITH_LIB_CONSOLE
extern struct pch_thermal_context g_pch_thermal_context;
#endif
