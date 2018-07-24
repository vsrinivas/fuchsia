// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_TESTS_MOCKS_MOCK_VIEW_LISTENER_H_
#define LIB_UI_TESTS_MOCKS_MOCK_VIEW_LISTENER_H_

#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include "lib/fxl/macros.h"

#include <functional>

namespace mozart {
namespace test {

using OnMockViewPropertiesCallback =
    std::function<void(::fuchsia::ui::viewsv1::ViewProperties)>;

class MockViewListener : public ::fuchsia::ui::viewsv1::ViewListener {
 public:
  MockViewListener();
  MockViewListener(OnMockViewPropertiesCallback callback);
  ~MockViewListener() override;

  void OnPropertiesChanged(::fuchsia::ui::viewsv1::ViewProperties properties,
                           OnPropertiesChangedCallback callback) override;

 private:
  OnMockViewPropertiesCallback callback_;
  FXL_DISALLOW_COPY_AND_ASSIGN(MockViewListener);
};

}  // namespace test
}  // namespace mozart

#endif  // LIB_UI_TESTS_MOCKS_MOCK_VIEW_LISTENER_H_
