// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

/* i8253/i8254 programmable interval timer registers */
#define I8253_CONTROL_REG   0x43
#define I8253_DATA_REG      0x40

/* i8042 keyboard controller registers */
#define I8042_COMMAND_REG   0x64
#define I8042_STATUS_REG    0x64
#define I8042_DATA_REG      0x60

/* CGA registers */
#define CGA_INDEX_REG       0x3D4
#define CGA_DATA_REG        0x3D5

/* legacy i/o ports */
#define ISA_IOPORT_SERIAL1_BASE  0x3f8
#define ISA_IOPORT_SERIAL2_BASE  0x2f8
#define ISA_IOPORT_PRINTER1_BASE 0x278
