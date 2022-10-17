// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_GOLDFISH_DISPLAY_RENDER_CONTROL_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_GOLDFISH_DISPLAY_RENDER_CONTROL_H_

#include <fidl/fuchsia.hardware.goldfish.pipe/cpp/markers.h>
#include <fidl/fuchsia.hardware.goldfish.pipe/cpp/wire.h>
#include <lib/fzl/pinned-vmo.h>
#include <zircon/types.h>

#include <map>
#include <memory>

#include <ddktl/device.h>

#include "src/devices/lib/goldfish/pipe_io/pipe_io.h"

namespace goldfish {

// This implements a client of goldfish renderControl API over
// goldfish pipe communication. The methods are defined in
// https://android.googlesource.com/device/generic/goldfish-opengl/+/master/system/renderControl_enc/README
class RenderControl {
 public:
  RenderControl() = default;
  zx_status_t InitRcPipe(fidl::WireSyncClient<fuchsia_hardware_goldfish_pipe::GoldfishPipe>);

  int32_t GetFbParam(uint32_t param, int32_t default_value);
  using ColorBufferId = uint32_t;
  zx::result<ColorBufferId> CreateColorBuffer(uint32_t width, uint32_t height, uint32_t format);
  zx_status_t OpenColorBuffer(ColorBufferId id);
  zx_status_t CloseColorBuffer(ColorBufferId id);

  // Zero means success; non-zero value means the call failed.
  using RcResult = int32_t;
  zx::result<RcResult> SetColorBufferVulkanMode(ColorBufferId id, uint32_t mode);
  zx::result<RcResult> UpdateColorBuffer(ColorBufferId id, const fzl::PinnedVmo& pinned_vmo,
                                         uint32_t width, uint32_t height, uint32_t format,
                                         size_t size);
  zx_status_t FbPost(uint32_t id);
  using DisplayId = uint32_t;
  zx::result<DisplayId> CreateDisplay();
  zx::result<RcResult> DestroyDisplay(DisplayId display_id);
  zx::result<RcResult> SetDisplayColorBuffer(DisplayId display_id, uint32_t id);
  zx::result<RcResult> SetDisplayPose(DisplayId display_id, int32_t x, int32_t y, uint32_t w,
                                      uint32_t h);

  PipeIo* pipe_io() { return pipe_io_.get(); }

 private:
  std::unique_ptr<PipeIo> pipe_io_;
  DISALLOW_COPY_ASSIGN_AND_MOVE(RenderControl);
};

}  // namespace goldfish

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_GOLDFISH_DISPLAY_RENDER_CONTROL_H_
