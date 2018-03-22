// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_TESTS_MOCKS_MOCK_VIEW_LISTENER_H_
#define LIB_UI_TESTS_MOCKS_MOCK_VIEW_LISTENER_H_

#include "lib/ui/views/fidl/view_manager.fidl.h"
#include <fuchsia/cpp/views_v1.h>
#include "lib/fxl/macros.h"

#include <functional>

namespace mozart {
namespace test {

using OnMockViewPropertiesCallback =
    std::function<void(views_v1::ViewPropertiesPtr v)>;

class MockViewListener : public views_v1::ViewListener {
 public:
  MockViewListener();
  MockViewListener(const OnMockViewPropertiesCallback& callback);
  ~MockViewListener() override;

  void OnPropertiesChanged(
      views_v1::ViewPropertiesPtr properties,
      const OnPropertiesChangedCallback& callback) override;

 private:
  OnMockViewPropertiesCallback callback_;
  FXL_DISALLOW_COPY_AND_ASSIGN(MockViewListener);
};

}  // namespace test
}  // namespace mozart

#endif  // LIB_UI_TESTS_MOCKS_MOCK_VIEW_LISTENER_H_
