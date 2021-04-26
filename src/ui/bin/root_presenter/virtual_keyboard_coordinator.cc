// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/virtual_keyboard_coordinator.h"

#include <lib/sys/cpp/component_context.h>

namespace root_presenter {

VirtualKeyboardCoordinator::VirtualKeyboardCoordinator(sys::ComponentContext* component_context)
    : controller_creator_(component_context), manager_(component_context) {}

}  // namespace root_presenter
