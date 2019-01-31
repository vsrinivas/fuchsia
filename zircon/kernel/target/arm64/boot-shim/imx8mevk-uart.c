// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include "debug.h"

#define MX8_UTXD                    (0x40)
#define MX8_UTS                     (0xB4)
#define UTS_TXFULL                  (1 << 4)

#define UARTREG(reg) (*(volatile uint32_t*)(0x30860000 + (reg)))

void uart_pputc(char c) {
    while (UARTREG(MX8_UTS) & UTS_TXFULL)
        ;
    UARTREG(MX8_UTXD) = c;
}
