// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_TYPES_H_
#define LIB_ZXIO_TYPES_H_

#include <stdint.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

typedef uint32_t zxio_signals_t;

// These values match the corresponding values in zircon/types.h
#define ZXIO_SIGNAL_NONE ((zxio_signals_t)0u)
#define ZXIO_READABLE ((zxio_signals_t)1u << 0)
#define ZXIO_WRITABLE ((zxio_signals_t)1u << 1)
#define ZXIO_READ_DISABLED ((zxio_signals_t)1u << 4)
#define ZXIO_WRITE_DISABLED ((zxio_signals_t)1u << 5)
#define ZXIO_READ_THRESHOLD ((zxio_signals_t)1u << 10)
#define ZXIO_WRITE_THRESHOLD ((zxio_signals_t)1u << 11)

#define ZXIO_SIGNAL_ALL                                                       \
  (ZXIO_READABLE | ZXIO_WRITABLE | ZXIO_READ_DISABLED | ZXIO_WRITE_DISABLED | \
   ZXIO_READ_THRESHOLD | ZXIO_WRITE_THRESHOLD)

__END_CDECLS

#endif  // LIB_ZXIO_TYPES_H_
