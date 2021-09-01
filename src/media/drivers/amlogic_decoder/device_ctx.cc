// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_ctx.h"

#include <fidl/fuchsia.hardware.mediacodec/cpp/wire.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/sync/completion.h>
#include <lib/zx/profile.h>
#include <lib/zx/time.h>
#include <zircon/threads.h>

#include <ddktl/fidl.h>

#include "amlogic-video.h"
#include "macros.h"
#include "sdk/lib/sys/cpp/service_directory.h"

namespace amlogic_decoder {

namespace {

struct DeadlineParams {
  std::string_view name;
  zx::duration capacity;
  zx::duration deadline;
  zx::duration period;
};

DeadlineParams GetDeadlineParamsForRole(ThreadRole role) {
  switch (role) {
    case ThreadRole::kSharedFidl:
      return DeadlineParams{.name = "aml-video/fidl",
                            .capacity = zx::usec(400),
                            .deadline = zx::usec(6000),
                            .period = zx::usec(6000)};
    case ThreadRole::kParserIrq:
      return DeadlineParams{.name = "aml-video/parser_irq",
                            .capacity = zx::usec(75),
                            .deadline = zx::usec(500),
                            .period = zx::usec(6000)};
    case ThreadRole::kVdec0Irq:
    case ThreadRole::kVdec1Irq:
      return DeadlineParams{.name = "aml-video/vdec_irq",
                            .capacity = zx::usec(800),
                            .deadline = zx::usec(6000),
                            .period = zx::usec(6000)};
    case ThreadRole::kH264MultiCore:
      return DeadlineParams{.name = "aml-video/h264_core",
                            .capacity = zx::usec(300),
                            .deadline = zx::usec(6000),
                            .period = zx::usec(6000)};
    case ThreadRole::kH264MultiStreamControl:
      return DeadlineParams{.name = "aml-video/h264_stream_control",
                            .capacity = zx::usec(100),
                            .deadline = zx::usec(6000),
                            .period = zx::usec(6000)};
    case ThreadRole::kVp9InputProcessing:
      return DeadlineParams{.name = "aml-video/vp9_input_processing",
                            .capacity = zx::usec(300),
                            .deadline = zx::usec(6000),
                            .period = zx::usec(6000)};
    case ThreadRole::kVp9StreamControl:
      return DeadlineParams{.name = "aml-video/vp9_stream_control",
                            .capacity = zx::usec(100),
                            .deadline = zx::usec(6000),
                            .period = zx::usec(6000)};
  }
}

}  // namespace

DeviceCtx::DeviceCtx(DriverCtx* driver, zx_device_t* parent)
    : DdkDeviceType(parent),
      driver_(driver),
      codec_admission_control_(driver->shared_fidl_loop()->dispatcher()) {
  video_ = std::make_unique<AmlogicVideo>(this);
  video_->SetMetrics(&metrics());
  device_fidl_ = std::make_unique<DeviceFidl>(this);
}

DeviceCtx::~DeviceCtx() {
  // There are two ways to destroy a fidl::Binding safely:
  //   * Switch to FIDL thread before Unbind() or ~Binding.
  //   * async::Loop Quit() + JoinThreads() before Unbind() or ~Binding
  //
  // For now this code (if implementation needed) will choose the first option
  // by destructing DeviceFidl on the FIDL thread. The current way forces this
  // thread to wait in this destructor until the shared_fidl_thread() is done
  // processing ~DeviceFidl, which means we require that ~DeviceCtx is not
  // itself running on the shared_fidl_thread() (or we could check which thread
  // here, if we really need to).
  //
  // This code is only run when we switch between test and production drivers.
  sync_completion_t completion;
  driver_->PostToSharedFidl([this, &completion]() {
    device_fidl_ = nullptr;
    sync_completion_signal(&completion);
  });
  sync_completion_wait(&completion, ZX_TIME_INFINITE);
}

zx_status_t DeviceCtx::Bind() {
  SetThreadProfile(zx::unowned_thread(thrd_get_zx_handle(driver_->shared_fidl_thread())),
                   ThreadRole::kSharedFidl);

  zx_status_t status = DdkAdd("amlogic_video");
  zxlogf(INFO, "amlogic-video finished initialization with status %d", status);
  return status;
}

void DeviceCtx::SetThreadProfile(zx::unowned_thread thread, ThreadRole role) const {
  DeadlineParams deadline_params = GetDeadlineParamsForRole(role);

  // TODO(fxbug.dev/40858): Use role-based API instead of defining our own profiles.
  zx::profile profile;
  zx_status_t status = device_get_deadline_profile(
      parent(), deadline_params.capacity.get(), deadline_params.deadline.get(),
      deadline_params.period.get(), deadline_params.name.data(), profile.reset_and_get_address());

  if (status != ZX_OK) {
    LOG(WARNING, "Unable to get profile for %s: %s", deadline_params.name.data(),
        zx_status_get_string(status));
    return;
  }

  status = thread->set_profile(std::move(profile), 0);
  if (status != ZX_OK) {
    LOG(WARNING, "Unable to set profile for %s: %s", deadline_params.name.data(),
        zx_status_get_string(status));
  }
}

void DeviceCtx::GetCodecFactory(GetCodecFactoryRequestView request,
                                GetCodecFactoryCompleter::Sync& completer) {
  device_fidl()->ConnectChannelBoundCodecFactory(std::move(request->request));
}

void DeviceCtx::SetAuxServiceDirectory(SetAuxServiceDirectoryRequestView request,
                                       SetAuxServiceDirectoryCompleter::Sync& completer) {
  driver_->SetAuxServiceDirectory(
      fidl::InterfaceHandle<fuchsia::io::Directory>(request->service_directory.TakeChannel()));
}

CodecMetrics& DeviceCtx::metrics() { return driver_->metrics(); }

}  // namespace amlogic_decoder
