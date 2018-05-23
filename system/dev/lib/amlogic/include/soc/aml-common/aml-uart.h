// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define AML_UART_WFIFO                  (0x0)
#define AML_UART_RFIFO                  (0x4)
#define AML_UART_CONTROL                (0x8)
#define AML_UART_STATUS                 (0xc)
#define AML_UART_MISC                   (0x10)
#define AML_UART_REG5                   (0x14)

#define AML_UART_CONTROL_INVRTS         (1 << 31)
#define AML_UART_CONTROL_MASKERR        (1 << 30)
#define AML_UART_CONTROL_INVCTS         (1 << 29)
#define AML_UART_CONTROL_TXINTEN        (1 << 28)
#define AML_UART_CONTROL_RXINTEN        (1 << 27)
#define AML_UART_CONTROL_INVTX          (1 << 26)
#define AML_UART_CONTROL_INVRX          (1 << 25)
#define AML_UART_CONTROL_CLRERR         (1 << 24)
#define AML_UART_CONTROL_RSTRX          (1 << 23)
#define AML_UART_CONTROL_RSTTX          (1 << 22)
#define AML_UART_CONTROL_XMITLEN_8      (0 << 20)
#define AML_UART_CONTROL_XMITLEN_7      (1 << 20)
#define AML_UART_CONTROL_XMITLEN_6      (2 << 20)
#define AML_UART_CONTROL_XMITLEN_5      (3 << 20)
#define AML_UART_CONTROL_XMITLEN_MASK   (3 << 20)
#define AML_UART_CONTROL_PAR_NONE       (0 << 19)
#define AML_UART_CONTROL_PAR_EVEN       (2 << 19)
#define AML_UART_CONTROL_PAR_ODD        (3 << 19)
#define AML_UART_CONTROL_PAR_MASK       (3 << 19)
#define AML_UART_CONTROL_STOPLEN_1      (0 << 16)
#define AML_UART_CONTROL_STOPLEN_2      (1 << 16)
#define AML_UART_CONTROL_STOPLEN_MASK   (3 << 16)
#define AML_UART_CONTROL_TWOWIRE        (1 << 15)
#define AML_UART_CONTROL_RXEN           (1 << 13)
#define AML_UART_CONTROL_TXEN           (1 << 12)
#define AML_UART_CONTROL_BAUD0          (1 << 0)
#define AML_UART_CONTROL_BAUD0_MASK     (0xfff << 0)

#define AML_UART_STATUS_RXBUSY          (1 << 26)
#define AML_UART_STATUS_TXBUSY          (1 << 25)
#define AML_UART_STATUS_RXOVRFLW        (1 << 24)
#define AML_UART_STATUS_CTSLEVEL        (1 << 23)
#define AML_UART_STATUS_TXEMPTY         (1 << 22)
#define AML_UART_STATUS_TXFULL          (1 << 21)
#define AML_UART_STATUS_RXEMPTY         (1 << 20)
#define AML_UART_STATUS_RXFULL          (1 << 19)
#define AML_UART_STATUS_TXOVRFLW        (1 << 18)
#define AML_UART_STATUS_FRAMEERR        (1 << 17)
#define AML_UART_STATUS_PARERR          (1 << 16)
#define AML_UART_STATUS_TXCOUNT_POS     (8)
#define AML_UART_STATUS_TXCOUNT_MASK    (0x7f << AML_UART_STATUS_TXCOUNT_POS)
#define AML_UART_STATUS_RXCOUNT_POS     (0)
#define AML_UART_STATUS_RXCOUNT_MASK    (0x7f << AML_UART_STATUS_RXCOUNT_POS)

// XMIT_IRQ_COUNT in bits 8 - 15
#define AML_UART_MISC_XMIT_IRQ_COUNT_SHIFT 8
#define AML_UART_MISC_XMIT_IRQ_COUNT_MASK 0x0000ff00
// RECV_IRQ_COUNT in bits 0 - 7
#define AML_UART_MISC_RECV_IRQ_COUNT_SHIFT 0
#define AML_UART_MISC_RECV_IRQ_COUNT_MASK 0x000000ff

#define AML_UART_REG5_XTAL_TICK         (1 << 26)
#define AML_UART_REG5_USE_XTAL_CLK      (1 << 24)
#define AML_UART_REG5_USE_NEW_BAUD_RATE (1 << 23)
#define AML_UART_REG5_NEW_BAUD_RATE_MASK (0x7fffff <<0)
