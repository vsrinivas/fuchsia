// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/screen_capture2/screen_capture2.h"

#include <fuchsia/ui/composition/cpp/fidl.h>

#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_import_export_tokens.h"

using fuchsia::ui::composition::internal::ScreenCaptureConfig;
using fuchsia::ui::composition::internal::ScreenCaptureError;

namespace screen_capture2::test {

class ScreenCapture2Test : public gtest::TestLoopFixture {
 public:
  ScreenCapture2Test() = default;
};

TEST_F(ScreenCapture2Test, ConfigureWithMissingArguments) {
  fuchsia::ui::composition::internal::ScreenCapturePtr screencapturer;
  screen_capture2::ScreenCapture sc(screencapturer.NewRequest());

  ScreenCaptureError error;
  sc.Configure({}, [&error](fpromise::result<void, ScreenCaptureError> result) {
    EXPECT_TRUE(result.is_error());
    error = result.error();
  });
  RunLoopUntilIdle();
  EXPECT_EQ(error, ScreenCaptureError::MISSING_ARGS);
}

TEST_F(ScreenCapture2Test, ConfigureSuccess) {
  fuchsia::ui::composition::internal::ScreenCapturePtr screencapturer;
  screen_capture2::ScreenCapture sc(screencapturer.NewRequest());

  allocation::BufferCollectionImportExportTokens ref_pair =
      allocation::BufferCollectionImportExportTokens::New();
  ScreenCaptureConfig args;
  args.set_import_token(std::move(ref_pair.import_token));
  args.set_image_size({1, 1});

  bool configure = false;
  sc.Configure(std::move(args), [&configure](fpromise::result<void, ScreenCaptureError> result) {
    EXPECT_FALSE(result.is_error());
    configure = true;
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(configure);
}

}  // namespace screen_capture2::test
