// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/goldfish-display/render_control.h"

#include <fidl/fuchsia.hardware.goldfish.pipe/cpp/markers.h>
#include <lib/ddk/trace/event.h>
#include <lib/fidl/cpp/wire/channel.h>

#include <memory>

#include "src/devices/lib/goldfish/pipe_io/pipe_io.h"

namespace goldfish {

namespace {

const char* kPipeName = "pipe:opengles";

constexpr uint32_t GL_UNSIGNED_BYTE = 0x1401;

// All the render control (rc*) functions are defined in Android device/generic/
// goldfish-opengl/system/renderControl_enc/renderControl.in file.
// The opcodes are available at Android device/generic/goldfish-opengl/system/
// renderControl_enc/renderControl_opcodes.h.
constexpr uint32_t kOP_rcGetFbParam = 10007;
struct GetFbParamCmd {
  uint32_t op = kOP_rcGetFbParam;
  uint32_t size = sizeof(GetFbParamCmd);
  uint32_t param;
};

constexpr uint32_t kOP_rcCreateColorBuffer = 10012;
struct CreateColorBufferCmd {
  uint32_t op = kOP_rcCreateColorBuffer;
  uint32_t size = sizeof(CreateColorBufferCmd);
  uint32_t width;
  uint32_t height;
  uint32_t internalformat;
};

constexpr uint32_t kOP_rcOpenColorBuffer = 10013;
struct OpenColorBufferCmd {
  uint32_t op = kOP_rcOpenColorBuffer;
  uint32_t size = sizeof(OpenColorBufferCmd);
  uint32_t id;
};

constexpr uint32_t kOP_rcCloseColorBuffer = 10014;
struct CloseColorBufferCmd {
  uint32_t op = kOP_rcCloseColorBuffer;
  uint32_t size = sizeof(CloseColorBufferCmd);
  uint32_t id;
};

constexpr uint32_t kOP_rcSetColorBufferVulkanMode = 10045;
struct SetColorBufferVulkanModeCmd {
  uint32_t op = kOP_rcSetColorBufferVulkanMode;
  uint32_t size = sizeof(SetColorBufferVulkanModeCmd);
  uint32_t id;
  uint32_t mode;
};

constexpr uint32_t kOP_rcUpdateColorBuffer = 10024;
struct UpdateColorBufferCmd {
  uint32_t op = kOP_rcUpdateColorBuffer;
  uint32_t size = sizeof(UpdateColorBufferCmd);
  uint32_t id;
  uint32_t x;
  uint32_t y;
  uint32_t width;
  uint32_t height;
  uint32_t format;
  uint32_t type;
  uint32_t size_pixels;
};

constexpr uint32_t kOP_rcFbPost = 10018;
struct FbPostCmd {
  uint32_t op = kOP_rcFbPost;
  uint32_t size = sizeof(FbPostCmd);
  uint32_t id;
};

constexpr uint32_t kOP_rcCreateDisplay = 10038;
struct CreateDisplayCmd {
  uint32_t op = kOP_rcCreateDisplay;
  uint32_t size = sizeof(CreateDisplayCmd);
  uint32_t size_display_id;
};

constexpr uint32_t kOP_rcDestroyDisplay = 10039;
struct DestroyDisplayCmd {
  uint32_t op = kOP_rcDestroyDisplay;
  uint32_t size = sizeof(DestroyDisplayCmd);
  uint32_t display_id;
};

constexpr uint32_t kOP_rcSetDisplayColorBuffer = 10040;
struct SetDisplayColorBufferCmd {
  uint32_t op = kOP_rcSetDisplayColorBuffer;
  uint32_t size = sizeof(SetDisplayColorBufferCmd);
  uint32_t display_id;
  uint32_t id;
};

constexpr uint32_t kOP_rcSetDisplayPose = 10044;
struct SetDisplayPoseCmd {
  uint32_t op = kOP_rcSetDisplayPose;
  uint32_t size = sizeof(SetDisplayPoseCmd);
  uint32_t display_id;
  int32_t x;
  int32_t y;
  uint32_t w;
  uint32_t h;
};

template <class T>
cpp20::span<const uint8_t> ToByteSpan(const T& t) {
  return cpp20::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&t),
                                    reinterpret_cast<const uint8_t*>(&t + 1));
}

}  // namespace

zx_status_t RenderControl::InitRcPipe(
    fidl::WireSyncClient<fuchsia_hardware_goldfish_pipe::GoldfishPipe> pipe) {
  pipe_io_ = std::make_unique<PipeIo>(std::move(pipe), kPipeName);
  if (!pipe_io_->valid()) {
    zxlogf(ERROR, "PipeIo failed to initialize");
    return ZX_ERR_NOT_SUPPORTED;
  }

  constexpr uint32_t kClientFlags = 0;
  PipeIo::WriteSrc src[] = {{.data = ToByteSpan(kClientFlags)}};
  auto status = pipe_io_->Write(src, true);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Write client flags failed");
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

int32_t RenderControl::GetFbParam(uint32_t param, int32_t default_value) {
  TRACE_DURATION("gfx", "RenderControl::GetFbParam", "param", param);

  GetFbParamCmd cmd = {
      .param = param,
  };

  PipeIo::WriteSrc src[] = {{.data = ToByteSpan(cmd)}};
  auto result = pipe_io_->Call<int32_t>(src, 1, true);
  return (result.is_ok()) ? result.value()[0] : default_value;
}

zx::result<RenderControl::ColorBufferId> RenderControl::CreateColorBuffer(uint32_t width,
                                                                          uint32_t height,
                                                                          uint32_t format) {
  TRACE_DURATION("gfx", "RenderControl::CreateColorBuffer", "width", width, "height", height,
                 "format", format);

  CreateColorBufferCmd cmd = {
      .width = width,
      .height = height,
      .internalformat = format,
  };

  PipeIo::WriteSrc src[] = {{.data = ToByteSpan(cmd)}};
  auto result = pipe_io_->Call<ColorBufferId>(src, 1, true);
  return result.is_ok() ? zx::ok(result.value()[0])
                        : zx::result<ColorBufferId>(result.take_error());
}

zx_status_t RenderControl::OpenColorBuffer(ColorBufferId id) {
  TRACE_DURATION("gfx", "RenderControl::OpenColorBuffer", "id", id);

  OpenColorBufferCmd cmd = {
      .id = id,
  };

  PipeIo::WriteSrc src[] = {{.data = ToByteSpan(cmd)}};
  return pipe_io_->Write(src, true);
}

zx_status_t RenderControl::CloseColorBuffer(ColorBufferId id) {
  TRACE_DURATION("gfx", "RenderControl::CloseColorBuffer", "id", id);

  CloseColorBufferCmd cmd = {
      .id = id,
  };

  PipeIo::WriteSrc src[] = {{.data = ToByteSpan(cmd)}};
  return pipe_io_->Write(src, true);
}

zx::result<RenderControl::RcResult> RenderControl::SetColorBufferVulkanMode(ColorBufferId id,
                                                                            uint32_t mode) {
  TRACE_DURATION("gfx", "RenderControl::SetColorBufferVulkanMode", "id", id, "mode", mode);

  SetColorBufferVulkanModeCmd cmd = {
      .id = id,
      .mode = mode,
  };

  PipeIo::WriteSrc src[] = {{.data = ToByteSpan(cmd)}};
  auto result = pipe_io_->Call<RcResult>(src, 1, true);
  return result.is_ok() ? zx::ok(result.value()[0]) : zx::result<RcResult>(result.take_error());
}

zx::result<RenderControl::RcResult> RenderControl::UpdateColorBuffer(
    ColorBufferId id, const fzl::PinnedVmo& pinned_vmo, uint32_t width, uint32_t height,
    uint32_t format, size_t size) {
  TRACE_DURATION("gfx", "RenderControl::UpdateColorBuffer", "size", size);

  UpdateColorBufferCmd cmd = {
      .size = static_cast<uint32_t>(size + sizeof(cmd)),
      .id = id,
      .x = 0,
      .y = 0,
      .width = width,
      .height = height,
      .format = format,
      .type = GL_UNSIGNED_BYTE,
      .size_pixels = static_cast<uint32_t>(size),
  };

  PipeIo::WriteSrc src[] = {
      {.data = ToByteSpan(cmd)},
      {.data =
           PipeIo::WriteSrc::PinnedVmo{
               .vmo = &pinned_vmo,
               .offset = 0,
               .size = size,
           }},
  };

  auto write_result = pipe_io_->Write(src, false);
  if (write_result != ZX_OK) {
    // It's possible that there's some back pressure when updating the color
    // buffer. In that case we just skip it for this frame.
    return zx::ok(0);
  }
  auto result = pipe_io_->Read<RcResult>(1, true);
  return result.is_ok() ? zx::ok(result.value()[0]) : zx::result<RcResult>(result.take_error());
}

zx_status_t RenderControl::FbPost(ColorBufferId id) {
  TRACE_DURATION("gfx", "RenderControl::FbPost", "id", id);

  FbPostCmd cmd = {
      .id = id,
  };

  PipeIo::WriteSrc src[] = {{.data = ToByteSpan(cmd)}};
  auto result = pipe_io_->Write(src, false);
  return result;
}

zx::result<RenderControl::DisplayId> RenderControl::CreateDisplay() {
  TRACE_DURATION("gfx", "RenderControl::CreateDisplay");

  CreateDisplayCmd cmd = {
      .size_display_id = sizeof(uint32_t),
  };

  using CreateDisplayResult = struct {
    uint32_t id;
    int32_t result;
  };

  PipeIo::WriteSrc src[] = {{.data = ToByteSpan(cmd)}};
  auto result = pipe_io_->Call<CreateDisplayResult>(src, 1, true);

  if (result.is_error()) {
    return result.take_error();
  }
  if (result.value()[0].result != 0) {
    return zx::error(ZX_ERR_INTERNAL);
  }
  return zx::ok(result.value()[0].id);
}

zx::result<RenderControl::RcResult> RenderControl::DestroyDisplay(DisplayId display_id) {
  TRACE_DURATION("gfx", "RenderControl::DestroyDisplay", "display_id", display_id);

  DestroyDisplayCmd cmd = {
      .display_id = display_id,
  };

  PipeIo::WriteSrc src[] = {{.data = ToByteSpan(cmd)}};
  auto result = pipe_io_->Call<RcResult>(src, 1, true);
  return result.is_ok() ? zx::ok(result.value()[0]) : zx::result<RcResult>(result.take_error());
}

zx::result<RenderControl::RcResult> RenderControl::SetDisplayColorBuffer(DisplayId display_id,
                                                                         ColorBufferId id) {
  TRACE_DURATION("gfx", "RenderControl::SetDisplayColorBuffer", "display_id", display_id, "id", id);

  SetDisplayColorBufferCmd cmd = {
      .display_id = display_id,
      .id = id,
  };

  PipeIo::WriteSrc src[] = {{.data = ToByteSpan(cmd)}};
  auto result = pipe_io_->Call<RcResult>(src, 1, true);
  return result.is_ok() ? zx::ok(result.value()[0]) : zx::result<RcResult>(result.take_error());
}

zx::result<RenderControl::RcResult> RenderControl::SetDisplayPose(DisplayId display_id, int32_t x,
                                                                  int32_t y, uint32_t w,
                                                                  uint32_t h) {
  TRACE_DURATION("gfx", "RenderControl::SetDisplayPose", "display_id", display_id);

  SetDisplayPoseCmd cmd = {
      .display_id = display_id,
      .x = x,
      .y = y,
      .w = w,
      .h = h,
  };

  PipeIo::WriteSrc src[] = {{.data = ToByteSpan(cmd)}};
  auto result = pipe_io_->Call<RcResult>(src, 1, true);
  return result.is_ok() ? zx::ok(result.value()[0]) : zx::result<RcResult>(result.take_error());
}

}  // namespace goldfish
