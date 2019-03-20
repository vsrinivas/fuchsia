// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/bt/hci.h>
#include <ddk/protocol/test.h>

#include <fbl/unique_ptr.h>
#include <fuchsia/hardware/bluetooth/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>

#include <zircon/compiler.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include "src/connectivity/bluetooth/core/bt-host/testing/fake_controller.h"

namespace bthci_fake {

enum class Channel {
  ACL,
  COMMAND,
  SNOOP,
};

class Device {
 public:
  Device(zx_device_t* device);

  zx_status_t Bind();
  void Unbind();
  void Release();

  zx_status_t Message(fidl_msg_t* msg, fidl_txn_t* txn);
  zx_status_t GetProtocol(uint32_t proto_id, void* out_proto);
  zx_status_t OpenChan(Channel chan_type, zx_handle_t chan);

  static zx_status_t OpenCommandChannel(void* ctx, zx_handle_t channel);
  static zx_status_t OpenAclDataChannel(void* ctx, zx_handle_t channel);
  static zx_status_t OpenSnoopChannel(void* ctx, zx_handle_t channel);
  static constexpr fuchsia_hardware_bluetooth_Hci_ops_t fidl_ops_ = {
    .OpenCommandChannel = OpenCommandChannel,
    .OpenAclDataChannel = OpenAclDataChannel,
    .OpenSnoopChannel = OpenSnoopChannel,
  };

 private:
  async::Loop loop_ __TA_GUARDED(device_lock_);
  fbl::RefPtr<bt::testing::FakeController> fake_device_
      __TA_GUARDED(device_lock_);

  std::mutex device_lock_;

  zx_device_t* parent_;
  zx_device_t* zxdev_;
};

}  // namespace bthci_fake
