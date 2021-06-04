// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_LIB_INJECTOR_TESTS_MOCKS_MOCK_INJECTOR_H_
#define SRC_UI_INPUT_LIB_INJECTOR_TESTS_MOCKS_MOCK_INJECTOR_H_

#include "src/ui/input/lib/injector/injector.h"

namespace input_test {

class MockInjector : public input::Injector {
 public:
  MockInjector() = default;
  ~MockInjector() override = default;

  // |Injector|
  void OnEvent(const fuchsia::ui::input::InputEvent& event) override;

  bool on_event_called() const;

 private:
  bool on_event_called_ = false;
};

}  // namespace input_test

#endif  // SRC_UI_INPUT_LIB_INJECTOR_TESTS_MOCKS_MOCK_INJECTOR_H_
