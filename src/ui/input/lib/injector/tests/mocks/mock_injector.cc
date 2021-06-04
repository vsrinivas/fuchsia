// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/lib/injector/tests/mocks/mock_injector.h"

namespace input_test {

void MockInjector::OnEvent(const fuchsia::ui::input::InputEvent& event) { on_event_called_ = true; }

bool MockInjector::on_event_called() const { return on_event_called_; }

}  // namespace input_test
