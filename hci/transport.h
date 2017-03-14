// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <atomic>
#include <memory>
#include <thread>

#include "apps/bluetooth/hci/command_channel.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/ftl/tasks/task_runner.h"

namespace bluetooth {
namespace hci {

// Represents the HCI transport layer. This object owns the HCI command, ACL,
// and SCO channels and provides the necessary control-flow mechanisms to send
// and receive HCI packets from the underlying Bluetooth controller.
class Transport final {
 public:
  // |device_fd| must be a valid file descriptor to a Bluetooth HCI device.
  explicit Transport(ftl::UniqueFD device_fd);
  Transport() = default;
  ~Transport();

  // Initializes the transport channels, starts the I/O event loop, and kicks off a new I/O thread
  // all HCI communication. Care must be taken such that the public methods of this class and those
  // of the individual channel classes are not called in a manner that would race with the execution
  // of Initialize().
  bool Initialize();

  // Cleans up the transport channels, stops the I/O event loop, and joins the I/O thread.
  // NOTE: Care must be taken such that this method is not called from a thread
  // that would race with a call to Initialize(). ShutDown() is not thread-safe
  // and should not be called from multiple threads at the same time.
  void ShutDown();

  // Returns a pointer to the HCI command and event control-flow handler.
  CommandChannel* command_channel() const { return command_channel_.get(); }

  // Returns the I/O thread task runner. If this is called when this Transport instance is not
  // initialized, the return value will be nullptr.
  ftl::RefPtr<ftl::TaskRunner> io_task_runner() const { return io_task_runner_; }

  // Initialize function called from tests.
  bool InitializeForTesting(std::unique_ptr<CommandChannel> cmd_channel);

 private:
  // The Bluetooth HCI device file descriptor.
  ftl::UniqueFD device_fd_;

  // True if the I/O event loop is currently running.
  std::atomic_bool is_running_;

  // The thread that performs all HCI I/O operations.
  std::thread io_thread_;

  // The task runner used for posting tasks on the HCI transport I/O thread.
  ftl::RefPtr<ftl::TaskRunner> io_task_runner_;

  // The HCI command and event control-flow handler.
  std::unique_ptr<CommandChannel> command_channel_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Transport);
};

}  // namespace hci
}  // namespace bluetooth
