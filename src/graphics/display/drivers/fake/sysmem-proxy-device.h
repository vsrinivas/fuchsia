// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_FAKE_SYSMEM_PROXY_DEVICE_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_FAKE_SYSMEM_PROXY_DEVICE_H_

#include <fuchsia/sysmem/llcpp/fidl.h>
#include <fuchsia/sysmem2/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/llcpp/heap_allocator.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/zx/bti.h>
#include <lib/zx/channel.h>

#include <limits>
#include <map>
#include <memory>
#include <unordered_set>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/sysmem.h>
#include <ddktl/device.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/sysmem.h>
#include <fbl/vector.h>
#include <region-alloc/region-alloc.h>

namespace sysmem_driver {
class Driver;
}  // namespace sysmem_driver

namespace display {

class SysmemProxyDevice;
using DdkDeviceType2 = ddk::Device<SysmemProxyDevice, ddk::Messageable, ddk::Unbindable>;

// SysmemProxyDevice is a replacement for sysmem_driver::Device, intended for use in tests.  Instead
// of instantiating a separate/hermetic Sysmem, SysmemProxyDevice connects to the allocator made
// available via the test-component's environment (i.e. "/svc/fuchsia.sysmem.Allocator").  This is
// useful for testing use-cases where multiple components must share the same allocator to negotiate
// which memory to use.  For example, consider a scenario where Scenic wishes to use Vulkan for
// image compositing, and then wishes to display the resulting image on the screen.  In order to do
// so, it must allocate an image which is acceptable both to Vulkan and the display driver.
class SysmemProxyDevice final : public DdkDeviceType2,
                                public ddk::SysmemProtocol<SysmemProxyDevice, ddk::base_protocol> {
 public:
  SysmemProxyDevice(zx_device_t* parent_device, sysmem_driver::Driver* parent_driver);

  zx_status_t Bind();

  //
  // The rest of the methods are only valid to call after Bind().
  //

  // SysmemProtocol implementation.
  zx_status_t SysmemConnect(zx::channel allocator_request);
  zx_status_t SysmemRegisterHeap(uint64_t heap, zx::channel heap_connection);
  zx_status_t SysmemRegisterSecureMem(zx::channel tee_connection);
  zx_status_t SysmemUnregisterSecureMem();

  // Ddk mixin implementations.
  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  // Quits the async loop and joins with all spawned threads.  Note: this doesn't tear down
  // connections already made via SysmemConnect().  This is because these connections are made by
  // passing the channel handle to an external Sysmem service, after which SysmemProxyDevice has no
  // further knowledge of the connection.
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease() {
    // Don't do anything. The sysmem driver assumes it's alive for the
    // lifetime of the system.
  }

  zx_status_t Connect(zx_handle_t allocator_request);

  const sysmem_protocol_t* proto() const { return &in_proc_sysmem_protocol_; }
  const zx_device_t* device() const { return zxdev_; }
  async_dispatcher_t* dispatcher() { return loop_.dispatcher(); }

 private:
  sysmem_driver::Driver* parent_driver_ = nullptr;
  inspect::Inspector inspector_;
  async::Loop loop_;
  thrd_t loop_thrd_;

  ddk::PDevProtocolClient pdev_;

  // In-proc sysmem interface.  Essentially an in-proc version of
  // fuchsia.sysmem.DriverConnector.
  sysmem_protocol_t in_proc_sysmem_protocol_;
};

}  // namespace display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_FAKE_SYSMEM_PROXY_DEVICE_H_
