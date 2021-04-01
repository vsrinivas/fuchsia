// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_ctx.h"

#include <fuchsia/hardware/mediacodec/llcpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/sync/completion.h>

#include <ddktl/fidl.h>

#include "amlogic-video.h"
#include "macros.h"
#include "sdk/lib/sys/cpp/service_directory.h"

DeviceCtx::DeviceCtx(DriverCtx* driver, zx_device_t* parent)
    : DdkDeviceType(parent),
      driver_(driver),
      codec_admission_control_(driver->shared_fidl_loop()->dispatcher()) {
  video_ = std::make_unique<AmlogicVideo>();
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

zx_status_t DeviceCtx::Bind() { return DdkAdd("amlogic_video"); }

zx_status_t DeviceCtx::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction ddk_transaction(txn);
  fidl::WireDispatch<fuchsia_hardware_mediacodec::Device>(this, msg, &ddk_transaction);
  return ddk_transaction.Status();
}

void DeviceCtx::GetCodecFactory(zx::channel request, GetCodecFactoryCompleter::Sync& completer) {
  device_fidl()->ConnectChannelBoundCodecFactory(std::move(request));
}
void DeviceCtx::SetAuxServiceDirectory(fidl::ClientEnd<fuchsia_io::Directory> directory,
                                       SetAuxServiceDirectoryCompleter::Sync& completer) {
  driver_->SetAuxServiceDirectory(
      fidl::InterfaceHandle<fuchsia::io::Directory>(directory.TakeChannel()));
}

CodecMetrics& DeviceCtx::metrics() { return driver_->metrics(); }
