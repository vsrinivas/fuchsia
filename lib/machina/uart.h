// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_UART_H_
#define GARNET_LIB_MACHINA_UART_H_

#if __x86_64__

#include "garnet/lib/machina/arch/x86/i8250.h"

using Uart = I8250;

#elif __aarch64__

#include "garnet/lib/machina/arch/arm64/pl011.h"

using Uart = Pl011;

#endif // __aarch64__

#endif  // GARNET_LIB_MACHINA_UART_H_
