// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST_LOG_H_
#define SRC_DEVICES_BIN_DRIVER_HOST_LOG_H_

#include "src/devices/lib/log/log.h"

#define LOGD(severity, dev, message...)                                   \
  do {                                                                    \
    std::vector<char> buf(FX_LOG_MAX_TAG_LEN);                            \
    FX_LOGF(severity, mkdevpath((dev), buf.data(), buf.size()), message); \
  } while (false)

#define VLOGD(verbosity, dev, message...)                                   \
  do {                                                                      \
    std::vector<char> buf(FX_LOG_MAX_TAG_LEN);                              \
    FX_VLOGF(verbosity, mkdevpath((dev), buf.data(), buf.size()), message); \
  } while (false)

#endif  // SRC_DEVICES_BIN_DRIVER_HOST_LOG_H_
