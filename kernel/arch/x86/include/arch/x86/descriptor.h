// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2014 Intel Corporation
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

/*
 * System Selectors
 */
#define NULL_SELECTOR           0x00

/********* kernel selectors *********/
#define CODE_SELECTOR           0x08
#define CODE_64_SELECTOR        0x10
#define DATA_SELECTOR           0x18

/********* user selectors *********/
#define USER_CODE_SELECTOR      (0x20 | 3)
#define USER_DATA_SELECTOR      (0x28 | 3)
#define USER_CODE_64_SELECTOR   (0x30 | 3)

#define TSS_SELECTOR(i)            ((uint16_t)(0x38 + 16 * (i)))
/* 0x40 is used by the second half of the first TSS descriptor */

/* selector priviledge level */
#define SELECTOR_PL(s) ((s) & 0x3)

/*
 * Descriptor Types
 */
#define SEG_TYPE_TSS        0x9
#define SEG_TYPE_TSS_BUSY   0xb
#define SEG_TYPE_TASK_GATE  0x5
#define SEG_TYPE_INT_GATE   0xe     /* 32 bit */
#define SEG_TYPE_DATA_RW    0x2
#define SEG_TYPE_CODE_RW    0xa

#ifndef ASSEMBLY

#include <arch/aspace.h>
#include <magenta/compiler.h>
#include <reg.h>
#include <sys/types.h>

#ifdef __cplusplus
#include <arch/x86/ioport.h>

void x86_set_tss_io_bitmap(IoBitmap& bitmap);
void x86_clear_tss_io_bitmap(IoBitmap& bitmap);
void x86_reset_tss_io_bitmap(void);
#endif // __cplusplus

__BEGIN_CDECLS

typedef uint16_t seg_sel_t;

/* fill in a descriptor in the GDT */
void set_global_desc_64(seg_sel_t sel, uint64_t base, uint32_t limit,
                        uint8_t present, uint8_t ring, uint8_t sys,
                        uint8_t type, uint8_t gran, uint8_t bits);

/* tss stuff */
void x86_initialize_percpu_tss(void);

void x86_set_tss_sp(vaddr_t sp);
void x86_clear_tss_busy(seg_sel_t sel);

__END_CDECLS

#endif
