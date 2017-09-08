// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_LIB_TESTS_MOCKS_MOCK_VIEW_LISTENER_H_
#define APPS_MOZART_LIB_TESTS_MOCKS_MOCK_VIEW_LISTENER_H_

#include "apps/mozart/services/views/view_manager.fidl.h"
#include "apps/mozart/services/views/views.fidl.h"
#include "lib/ftl/macros.h"

#include <functional>

namespace mozart {
namespace test {

using OnMockViewPropertiesCallback =
    std::function<void(mozart::ViewPropertiesPtr v)>;

class MockViewListener : public mozart::ViewListener {
 public:
  MockViewListener();
  MockViewListener(const OnMockViewPropertiesCallback& callback);
  ~MockViewListener() override;

  void OnPropertiesChanged(
      mozart::ViewPropertiesPtr properties,
      const OnPropertiesChangedCallback& callback) override;

 private:
  OnMockViewPropertiesCallback callback_;
  FTL_DISALLOW_COPY_AND_ASSIGN(MockViewListener);
};

}  // namespace test
}  // namespace mozart

#endif  // APPS_MOZART_LIB_TESTS_MOCKS_MOCK_VIEW_LISTENER_H_
