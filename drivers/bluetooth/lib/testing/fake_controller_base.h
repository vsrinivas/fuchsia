// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <thread>

#include <async/wait.h>
#include <zx/channel.h>

#include "apps/bluetooth/lib/common/byte_buffer.h"
#include "apps/bluetooth/lib/common/packet_view.h"
#include "apps/bluetooth/lib/hci/hci.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/synchronization/thread_checker.h"

namespace bluetooth {
namespace testing {

// Abstract base for implementing a fake HCI controller endpoint. This can directly send
// ACL data and event packets on request and forward outgoing ACL data packets to subclass
// implementations.
class FakeControllerBase {
 public:
  FakeControllerBase(zx::channel cmd_channel, zx::channel acl_data_channel);
  virtual ~FakeControllerBase();

  // Kicks off the FakeController thread and message loop and starts processing transactions.
  // |debug_name| will be assigned as the name of the thread.
  void Start();

  // Stops the message loop and thread.
  void Stop();

  // Sends the given packet over this FakeController's command channel endpoint.
  void SendCommandChannelPacket(const common::ByteBuffer& packet);

  // Sends the given packet over this FakeController's ACL data channel endpoint.
  void SendACLDataChannelPacket(const common::ByteBuffer& packet);

  // Immediately closes the command channel endpoint.
  void CloseCommandChannel();

  // Immediately closes the ACL data channel endpoint.
  void CloseACLDataChannel();

  // Returns true if Start() has been called without a call to Stop().
  bool IsStarted() const { return static_cast<bool>(task_runner_); }

 protected:
  fxl::RefPtr<fxl::TaskRunner> task_runner() const { return task_runner_; }

  // Getters for our channel endpoints.
  const zx::channel& command_channel() const { return cmd_channel_; }
  const zx::channel& acl_data_channel() const { return acl_channel_; }

  // Called when there is an incoming command packet.
  virtual void OnCommandPacketReceived(
      const common::PacketView<hci::CommandHeader>& command_packet) = 0;

  // Called when there is an outgoing ACL data packet.
  virtual void OnACLDataPacketReceived(const common::ByteBuffer& acl_data_packet) = 0;

 private:
  // Read and handle packets received over the channels.
  async_wait_result_t HandleCommandPacket(async_t* async, zx_status_t wait_status,
                                          const zx_packet_signal_t* signal);
  async_wait_result_t HandleACLPacket(async_t* async, zx_status_t wait_status,
                                      const zx_packet_signal_t* signal);

  // Cleans up the channel handles. This must be run on |task_runner_|'s thread.
  void CloseCommandChannelInternal();
  void CloseACLDataChannelInternal();

  // Used to assert that certain public functions are only called on the creation thread.
  fxl::ThreadChecker thread_checker_;

  zx::channel cmd_channel_;
  zx::channel acl_channel_;
  std::thread thread_;
  fxl::RefPtr<fxl::TaskRunner> task_runner_;
  async::Wait cmd_channel_wait_;
  async::Wait acl_channel_wait_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeControllerBase);
};

}  // namespace testing
}  // namespace bluetooth
