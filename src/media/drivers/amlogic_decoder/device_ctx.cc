// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_ctx.h"

#include <fidl/fuchsia.hardware.mediacodec/cpp/wire.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/ddk/driver.h>
#include <lib/sync/completion.h>
#include <lib/zx/profile.h>
#include <lib/zx/time.h>
#include <zircon/threads.h>

#include <ddktl/device.h>
#include <ddktl/fidl.h>

#include "amlogic-video.h"
#include "macros.h"
#include "sdk/lib/sys/cpp/service_directory.h"

namespace amlogic_decoder {

namespace {

const char* GetRoleName(ThreadRole role) {
  switch (role) {
    case ThreadRole::kSharedFidl:
      return "fuchsia.media.drivers.amlogic-decoder.fidl";
    case ThreadRole::kParserIrq:
      return "fuchsia.media.drivers.amlogic-decoder.parser-irq";
    case ThreadRole::kVdec0Irq:
    case ThreadRole::kVdec1Irq:
      return "fuchsia.media.drivers.amlogic-decoder.vdec-irq";
    case ThreadRole::kH264MultiCore:
      return "fuchsia.media.drivers.amlogic-decoder.h264-core";
    case ThreadRole::kH264MultiStreamControl:
      return "fuchsia.media.drivers.amlogic-decoder.h264-stream-control";
    case ThreadRole::kVp9InputProcessing:
      return "fuchsia.media.drivers.amlogic-decoder.vp9-input-processing";
    case ThreadRole::kVp9StreamControl:
      return "fuchsia.media.drivers.amlogic-decoder.vp9-stream-control";
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
  zx_status_t status = DdkAdd(
      ddk::DeviceAddArgs("amlogic_video").set_inspect_vmo(driver_->diagnostics().DuplicateVmo()));
  zxlogf(INFO, "amlogic-video finished initialization with status %d", status);

  diagnostics().SetBindTime();

  return status;
}

void DeviceCtx::SetThreadProfile(zx::unowned_thread thread, ThreadRole role) const {
  const char* role_name = GetRoleName(role);
  const size_t role_size = strlen(role_name);

  const zx_status_t status =
      device_set_profile_by_role(parent(), thread->get(), role_name, role_size);
  if (status != ZX_OK) {
    LOG(WARNING, "Unable to set thread to role %s: %s", role_name, zx_status_get_string(status));
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

CodecDiagnostics& DeviceCtx::diagnostics() { return driver_->diagnostics(); }

}  // namespace amlogic_decoder
