// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdint.h>

#include "debug.h"

static volatile uint32_t* ns16550a_thr = (uint32_t*)0x10000000;

void uart_pputc(char c) {
  *ns16550a_thr = c;
}
