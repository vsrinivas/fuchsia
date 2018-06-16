// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_ctx.h"

#include "amlogic-video.h"

#include "lib/fxl/logging.h"

DeviceCtx::DeviceCtx(DriverCtx* driver) : driver_(driver) {
  video_ = std::make_unique<AmlogicVideo>();
  device_fidl_ = std::make_unique<DeviceFidl>(this);
}

DeviceCtx::~DeviceCtx() {
  // TODO(dustingreen): Depending on whether device .release() can get called on
  // this deivce, we'll likely need to sequence the shutdown more explicitly.
  // This destruction order seems like a reasonable starting point, but is not
  // tested:
  //
  // ~device_fidl_
  // ~video_
  //
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
  // For now, it's not clear that we actually need to implement this destructor
  // however, since this device is very likely to have a dedicated devhost and
  // may not .release() the device - and even if it does .release() the device
  // there is no clear need for the cleanup described above to actually be
  // implemented.

  // TODO(dustingreen): Implement this destructor iff it's actually used/called.
  FXL_LOG(FATAL) << "not implemented";
}
