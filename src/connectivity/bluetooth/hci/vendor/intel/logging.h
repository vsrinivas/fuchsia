// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_HCI_VENDOR_INTEL_LOGGING_H_
#define SRC_CONNECTIVITY_BLUETOOTH_HCI_VENDOR_INTEL_LOGGING_H_

#include <ddk/debug.h>

#define logf(level, args...)         \
  do {                               \
    zxlogf(level, "btintel: " args); \
  } while (false)

#define errorf(args...) logf(ERROR, args)
#define infof(args...) logf(INFO, args)
#define tracef(args...) logf(TRACE, args)
#define spewf(args...) logf(SPEW, args)

#endif  // SRC_CONNECTIVITY_BLUETOOTH_HCI_VENDOR_INTEL_LOGGING_H_
