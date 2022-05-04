// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>

#include "src/media/audio/drivers/virtual_audio/virtual_audio_bind.h"
#include "src/media/audio/drivers/virtual_audio/virtual_audio_control_impl.h"

// Define a bus driver that binds to /dev/sys/platform/00:00:2f/virtual_audio.
static constexpr zx_driver_ops_t virtual_audio_bus_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = &virtual_audio::VirtualAudioControlImpl::DdkBind;
  return ops;
}();

__BEGIN_CDECLS

ZIRCON_DRIVER(virtual_audio, virtual_audio_bus_driver_ops, "fuchsia", "0.1");

__END_CDECLS
