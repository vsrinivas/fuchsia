// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_HCI_VENDOR_ATHEROS_LOGGING_H_
#define GARNET_DRIVERS_BLUETOOTH_HCI_VENDOR_ATHEROS_LOGGING_H_

#include <ddk/debug.h>

#define logf(level, args...)           \
  do {                                 \
    zxlogf(level, "btatheros: " args); \
  } while (false)

#define errorf(args...) logf(ERROR, args)
#define infof(args...) logf(INFO, args)
#define tracef(args...) logf(TRACE, args)

#endif  // GARNET_DRIVERS_BLUETOOTH_HCI_VENDOR_ATHEROS_LOGGING_H_
