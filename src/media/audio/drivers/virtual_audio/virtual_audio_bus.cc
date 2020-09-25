// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <lib/async/default.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>

#include "src/media/audio/drivers/virtual_audio/virtual_audio_control_impl.h"
#include "src/media/audio/drivers/virtual_audio/virtual_audio_device_impl.h"

namespace virtual_audio {

// The VirtualAudioBus driver uses ZX_PROTOCOL_TEST_PARENT, causing the /dev/test device to call its
// Bind() at startup time. In response, the bus driver creates a VirtualAudioControlImpl, registers
// and publishes it at /dev/test/virtual_audio, and transfers ownership to the device manager.
class VirtualAudioBus {
 public:
  // VirtualAudioBus is static-only and never actually instantiated.
  VirtualAudioBus() = delete;
  ~VirtualAudioBus() = delete;

  // The parent /dev/test node auto-triggers this Bind call. The Bus driver's job is to create the
  // virtual audio control driver, and publish its devnode.
  static zx_status_t DdkBind(void* ctx, zx_device_t* parent_test_bus) {
    auto control = std::make_unique<VirtualAudioControlImpl>(async_get_default_dispatcher());

    // Define entry-point operations for this control device.
    static zx_protocol_device_t device_ops = {
        .version = DEVICE_OPS_VERSION,
        .unbind = &VirtualAudioControlImpl::DdkUnbind,
        .release = &VirtualAudioControlImpl::DdkRelease,
        .message = &VirtualAudioControlImpl::DdkMessage,
    };

    // Define other metadata, incl. "control" as our entry-point context.
    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "virtual_audio";
    args.ctx = control.get();
    args.ops = &device_ops;

    // Add the virtual_audio device node, under parent /dev/test.
    zx_status_t status = device_add(parent_test_bus, &args, &control->dev_node_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "*** %s: could not add device '%s': %d", __func__, args.name, status);
      return status;
    }

    // On successful Add, Devmgr takes ownership (relinquished on DdkRelease), so transfer our
    // ownership to a local var, and let it go out of scope.
    auto __UNUSED temp_ref = control.release();

    return ZX_OK;
  }
};

}  // namespace virtual_audio

// Define a bus driver that binds to the everpresent /dev/test devnode.
static constexpr zx_driver_ops_t virtual_audio_bus_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = &virtual_audio::VirtualAudioBus::DdkBind;
  return ops;
}();

__BEGIN_CDECLS

// clang-format off
ZIRCON_DRIVER_BEGIN(virtual_audio, virtual_audio_bus_driver_ops, "fuchsia", "0.1", 1)
  BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_TEST_PARENT)
ZIRCON_DRIVER_END(virtual_audio)
// clang-format on

__END_CDECLS
