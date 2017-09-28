// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/l2cap/signaling_channel.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace bluetooth {
namespace l2cap {
namespace internal {

// Implements the L2CAP LE signaling fixed channel.
class LESignalingChannel : public SignalingChannel {
 public:
  LESignalingChannel(std::unique_ptr<Channel> chan, hci::Connection::Role role);
  ~LESignalingChannel() override = default;

  // Sets a |callback| to be invoked when a Connection Parameter Update request
  // is received with the given parameters. LESignalingChannel will
  // automatically accept these parameters, however it is up to the
  // implementation of |callback| to apply them to the controller.
  //
  // This task will be posted onto the given |task_runner|.
  using ConnectionParameterUpdateCallback =
      std::function<void(uint16_t interval_min,
                         uint16_t interval_max,
                         uint16_t slave_latency,
                         uint16_t timeout_multiplier)>;
  void set_conn_param_update_callback(
      const ConnectionParameterUpdateCallback& callback,
      fxl::RefPtr<fxl::TaskRunner> task_runner) {
    FXL_DCHECK(IsCreationThreadCurrent());
    FXL_DCHECK(static_cast<bool>(callback) == static_cast<bool>(task_runner));
    conn_param_update_cb_ = callback;
    conn_param_update_runner_ = task_runner;
  }

 private:
  void OnConnParamUpdateReceived(const SignalingPacket& packet);

  // SignalingChannel override
  bool HandlePacket(const SignalingPacket& packet) override;

  ConnectionParameterUpdateCallback conn_param_update_cb_;
  fxl::RefPtr<fxl::TaskRunner> conn_param_update_runner_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LESignalingChannel);
};

}  // namespace internal
}  // namespace l2cap
}  // namespace bluetooth
