// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2015 Intel Corporation
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <platform/pc/iomap.h>
#include <arch/x86/interrupts.h>

/* NOTE: keep arch/x86/crt0.S in sync with these definitions */

/* defined interrupts */
#define ISA_IRQ_PIT         0
#define ISA_IRQ_KEYBOARD    1
#define ISA_IRQ_PIC2        2
#define ISA_IRQ_SERIAL2     3
#define ISA_IRQ_SERIAL1     4
#define ISA_IRQ_PRINTER1    7
#define ISA_IRQ_CMOSRTC     8
#define ISA_IRQ_PS2MOUSE    12
#define ISA_IRQ_IDE0        14
#define ISA_IRQ_IDE1        15

/* PIC remap bases */
#define PIC1_BASE X86_INT_PLATFORM_BASE
#define PIC2_BASE (PIC1_BASE + 8)
