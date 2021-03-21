// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TOOLS_PRESENT_VIEW_TESTING_FAKE_INTL_MANAGER_H_
#define SRC_UI_TOOLS_PRESENT_VIEW_TESTING_FAKE_INTL_MANAGER_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/component_interceptor.h>

#include <functional>

namespace present_view::testing {

class FakeIntlManager {
 public:
  FakeIntlManager(fuchsia::sys::StartupInfo startup_info,
                  std::unique_ptr<sys::testing::InterceptedComponent> intercepted_component);
  ~FakeIntlManager();

 private:
  sys::ComponentContext component_context_;

  std::unique_ptr<sys::testing::InterceptedComponent> intercepted_component_;
};

}  // namespace present_view::testing

#endif  // SRC_UI_TOOLS_PRESENT_VIEW_TESTING_FAKE_INTL_MANAGER_H_
