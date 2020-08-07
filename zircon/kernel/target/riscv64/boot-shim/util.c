// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "util.h"

#include "debug.h"

void fail(const char* message) {
  uart_puts(message);
  while (1) {
  }
}
