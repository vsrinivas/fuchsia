// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_BLUETOOTH_TOOLS_BT_INTEL_TOOL_COMMAND_CHANNEL_H_
#define GARNET_BIN_BLUETOOTH_TOOLS_BT_INTEL_TOOL_COMMAND_CHANNEL_H_

#include <fbl/unique_fd.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/channel.h>
#include <lib/fit/function.h>
#include <zircon/types.h>

#include "garnet/drivers/bluetooth/lib/hci/control_packets.h"

// Sends and receives events from a command channel that it retrieves from a
// Zircon Bluetooth HCI device.  It parses the incoming event packets, only
// returning complete and valid event packets on to the event handler set.
class CommandChannel {
 public:
  // |hcidev_path| is a path to the hci device (e.g. /dev/class/bt-hci/000)
  CommandChannel(std::string hcidev_path);
  ~CommandChannel();

  // Indicates whether this channel is valid.  This should be checked after
  // construction.
  bool is_valid() { return valid_; }

  // Sets the event callback to be called when an HCI Event arrives on the
  // channel.
  using EventCallback =
      fit::function<void(const ::btlib::hci::EventPacket& event_packet)>;
  void SetEventCallback(EventCallback callback);

  // Sends the command in |command| to the controller. The channel must
  // be Ready when this is called.
  void SendCommand(
      const ::btlib::common::PacketView<::btlib::hci::CommandHeader>& command);

  // Sends the command in |command| to the controller and waits for
  // an Event, which is delivered to |callback| before this function
  // returns.
  void SendCommandSync(
      const ::btlib::common::PacketView<::btlib::hci::CommandHeader>& command,
      EventCallback callback);

 private:
  // Common read handler implemntation
  void HandleChannelReady(const zx::channel& channel, async_dispatcher_t* dispatcher,
                          async::WaitBase* wait, zx_status_t status,
                          const zx_packet_signal_t* signal);

  // Read ready handler for |cmd_channel_|
  void OnCmdChannelReady(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                         zx_status_t status, const zx_packet_signal_t* signal);

  // Read ready handler for |acl_channel_|
  void OnAclChannelReady(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                         zx_status_t status, const zx_packet_signal_t* signal);

  bool valid_;
  EventCallback event_callback_;
  fbl::unique_fd hci_fd_;
  zx::channel cmd_channel_;
  async::WaitMethod<CommandChannel, &CommandChannel::OnCmdChannelReady>
      cmd_channel_wait_{this};
  zx::channel acl_channel_;
  async::WaitMethod<CommandChannel, &CommandChannel::OnAclChannelReady>
      acl_channel_wait_{this};
};

#endif  // GARNET_BIN_BLUETOOTH_TOOLS_BT_INTEL_TOOL_COMMAND_CHANNEL_H_
