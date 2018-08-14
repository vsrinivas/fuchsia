// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_INPUT_INPUT_SYSTEM_H_
#define GARNET_LIB_UI_INPUT_INPUT_SYSTEM_H_

#include <memory>

#include "garnet/lib/ui/scenic/system.h"

namespace scenic {
namespace input {

// Routes input events from a root presenter to Scenic clients.
// Manages input-related state, such as Focus.
class InputSystem : public System {
 public:
  static constexpr TypeId kTypeId = kInput;

  explicit InputSystem(SystemContext context,
                       bool initialized_after_construction);
  virtual ~InputSystem();

  virtual std::unique_ptr<CommandDispatcher> CreateCommandDispatcher(
      CommandDispatcherContext context) override;

 private:
};

}  // namespace input
}  // namespace scenic

#endif  // GARNET_LIB_UI_INPUT_INPUT_SYSTEM_H_
