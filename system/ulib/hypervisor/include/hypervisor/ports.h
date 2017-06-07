// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

/* UART ports. */
#define UART_RECEIVE_IO_PORT           0x3f8
#define UART_LINE_CONTROL_IO_PORT      0x3fb
#define UART_MODEM_CONTROL_IO_PORT     0x3fc
#define UART_LINE_STATUS_IO_PORT       0x3fd
#define UART_SCR_SCRATCH_IO_PORT       0x3ff

/* RTC ports. */
#define RTC_INDEX_PORT          0x70
#define RTC_DATA_PORT           0x71

/* I8042 ports. */
#define I8042_DATA_PORT         0x60
#define I8042_COMMAND_PORT      0x64

/* PM1 ports. */
#define PM1_EVENT_PORT          0x1000
#define PM1_CONTROL_PORT        0x2000

/* Miscellaneous ports. */
#define PIC1_PORT               0x20
#define PIC2_PORT               0xa0
#define I8253_CONTROL_PORT      0x43
