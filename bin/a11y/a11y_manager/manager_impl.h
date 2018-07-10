// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_A11Y_A11Y_MANAGER_MANAGER_IMPL_H_
#define GARNET_BIN_A11Y_A11Y_MANAGER_MANAGER_IMPL_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <unordered_map>

#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace a11y_manager {

// Manager interface implementation.
class ManagerImpl {
 public:
  explicit ManagerImpl() = default;
  ~ManagerImpl() = default;

 private:
  // |Manager|:

  FXL_DISALLOW_COPY_AND_ASSIGN(ManagerImpl);
};

}  // namespace a11y_manager

#endif  // GARNET_BIN_A11Y_A11Y_MANAGER_MANAGER_IMPL_H_
