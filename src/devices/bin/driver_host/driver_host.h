// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST_DRIVER_HOST_H_
#define SRC_DEVICES_BIN_DRIVER_HOST_DRIVER_HOST_H_

#include <fuchsia/device/manager/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/channel.h>
#include <stdint.h>
#include <threads.h>
#include <zircon/compiler.h>
#include <zircon/fidl.h>
#include <zircon/types.h>

#include <memory>

#include <ddktl/fidl.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/string.h>

#include "async_loop_owned_rpc_handler.h"
#include "defaults.h"
#include "driver_host_context.h"
#include "lock.h"
#include "zx_device.h"

// Nothing outside of devmgr/{devmgr,driver_host,rpc-device}.c
// should be calling internal::*() APIs, as this could
// violate the internal locking design.

// Safe external APIs are in device.h and device_internal.h

namespace internal {

// Get the DriverHostContext that should be used by all external API methods
DriverHostContext* ContextForApi();
void RegisterContextForApi(DriverHostContext* context);

class DevhostControllerConnection
    : public AsyncLoopOwnedRpcHandler<DevhostControllerConnection>,
      public fidl::WireServer<fuchsia_device_manager::DevhostController> {
 public:
  // |ctx| must outlive this connection
  explicit DevhostControllerConnection(DriverHostContext* ctx) : driver_host_context_(ctx) {}

  static void HandleRpc(std::unique_ptr<DevhostControllerConnection> conn,
                        async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                        const zx_packet_signal_t* signal);
  zx_status_t HandleRead();

 private:
  void CreateDevice(CreateDeviceRequestView request,
                    CreateDeviceCompleter::Sync& completer) override;
  void CreateCompositeDevice(CreateCompositeDeviceRequestView request,
                             CreateCompositeDeviceCompleter::Sync& completer) override;
  void CreateDeviceStub(CreateDeviceStubRequestView request,
                        CreateDeviceStubCompleter::Sync& completer) override;
  void Restart(RestartRequestView request, RestartCompleter::Sync& completer) override;

  DriverHostContext* const driver_host_context_;
  fbl::RefPtr<zx_driver> proxy_driver_;
};

}  // namespace internal

// Construct a string describing the path of |dev| relative to its most
// distant ancestor in this driver_host.
const char* mkdevpath(const zx_device_t& dev, char* path, size_t max);

#endif  // SRC_DEVICES_BIN_DRIVER_HOST_DRIVER_HOST_H_
