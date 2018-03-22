// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_TESTS_MOCKS_MOCK_VIEW_TREE_LISTENER_H_
#define LIB_UI_TESTS_MOCKS_MOCK_VIEW_TREE_LISTENER_H_

#include "lib/ui/views/fidl/view_manager.fidl.h"
#include <fuchsia/cpp/views_v1.h>
#include "lib/fxl/macros.h"

#include <functional>

namespace mozart {
namespace test {

class MockViewTreeListener : public views_v1::ViewTreeListener {
 public:
  MockViewTreeListener();
  ~MockViewTreeListener();

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(MockViewTreeListener);
};

}  // namespace test
}  // namespace mozart

#endif  // LIB_UI_TESTS_MOCKS_MOCK_VIEW_TREE_LISTENER_H_
