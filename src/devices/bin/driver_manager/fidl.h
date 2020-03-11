// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_FIDL_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_FIDL_H_

#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>

class CompositeDevice;
class Devhost;
class Device;

// Methods for composing FIDL RPCs to the devhosts
zx_status_t dh_send_create_device(Device* dev, Devhost* dh, zx::channel coordinator_rpc,
                                  zx::channel device_controller_rpc, zx::vmo driver,
                                  const char* args, zx::handle rpc_proxy);
zx_status_t dh_send_create_device_stub(Device* dev, Devhost* dh, zx::channel coordinator_rpc,
                                       zx::channel device_controller_rpc, uint32_t protocol_id);
zx_status_t dh_send_bind_driver(Device* dev, const char* libname, zx::vmo driver,
                                fit::function<void(zx_status_t, zx::channel)> cb);
zx_status_t dh_send_connect_proxy(const Device* dev, zx::channel proxy);
zx_status_t dh_send_init(Device* dev);
zx_status_t dh_send_suspend(Device* dev, uint32_t flags);
zx_status_t dh_send_resume(Device* dev, uint32_t target_system_state);
zx_status_t dh_send_unbind(Device* dev);
zx_status_t dh_send_complete_removal(Device* dev, fit::function<void()> cb);
zx_status_t dh_send_complete_compatibility_tests(const Device* dev, zx_status_t test_status_);
zx_status_t dh_send_create_composite_device(Devhost* dh, const Device* composite_dev,
                                            const CompositeDevice& composite,
                                            const uint64_t* fragment_local_ids,
                                            zx::channel coordinator_rpc,
                                            zx::channel device_controller_rpc);

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_FIDL_H_
