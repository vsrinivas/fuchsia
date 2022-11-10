// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_V2_NODE_REMOVER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_V2_NODE_REMOVER_H_

#include <lib/fit/function.h>

namespace dfv2 {
class NodeRemover {
 public:
  virtual void ShutdownAllDrivers(fit::callback<void()> callback) = 0;
  virtual void ShutdownPkgDrivers(fit::callback<void()> callback) = 0;
};

}  // namespace dfv2
#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_V2_NODE_REMOVER_H_
