// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_TESTS_VK_SESSION_TEST_H_
#define GARNET_LIB_UI_GFX_TESTS_VK_SESSION_TEST_H_

#include "garnet/lib/ui/gfx/tests/session_test.h"

namespace scenic_impl {
namespace gfx {
namespace test {

class VkSessionTest : public SessionTest {
 public:
  // |SessionTest|
  fxl::RefPtr<SessionForTest> CreateSession() override;

 private:
  std::unique_ptr<escher::Escher> escher_;
  std::unique_ptr<escher::ImageFactoryAdapter> image_factory_;
  std::unique_ptr<escher::ReleaseFenceSignaller> release_fence_signaller_;
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_TESTS_VK_SESSION_TEST_H_
