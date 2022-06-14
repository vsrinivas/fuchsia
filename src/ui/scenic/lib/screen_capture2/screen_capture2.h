// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCREEN_CAPTURE2_SCREEN_CAPTURE2_H_
#define SRC_UI_SCENIC_LIB_SCREEN_CAPTURE2_SCREEN_CAPTURE2_H_

#include <fuchsia/ui/composition/internal/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

namespace screen_capture2 {

class ScreenCapture : public fuchsia::ui::composition::internal::ScreenCapture {
 public:
  ScreenCapture(fidl::InterfaceRequest<fuchsia::ui::composition::internal::ScreenCapture> request);

  ~ScreenCapture() override;

  void Configure(fuchsia::ui::composition::internal::ScreenCaptureConfig args,
                 ConfigureCallback callback) override;

  void GetNextFrame(GetNextFrameCallback callback) override;

 private:
  fidl::Binding<fuchsia::ui::composition::internal::ScreenCapture> binding_;
};

}  // namespace screen_capture2

#endif  // SRC_UI_SCENIC_LIB_SCREEN_CAPTURE2_SCREEN_CAPTURE2_H_
