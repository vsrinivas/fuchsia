// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <atomic>
#include <memory>
#include <thread>

#include "apps/bluetooth/lib/hci/acl_data_channel.h"
#include "apps/bluetooth/lib/hci/command_channel.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_counted.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/synchronization/thread_checker.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/tasks/message_loop_handler.h"

namespace bluetooth {
namespace hci {

class DeviceWrapper;

// Represents the HCI transport layer. This object owns the HCI command, ACL,
// and SCO channels and provides the necessary control-flow mechanisms to send
// and receive HCI packets from the underlying Bluetooth controller.
//
// Transport expects to be initialized and shut down (via Initialize() and ShutDown()) on the thread
// it was created on. Initialize()/ShutDown() are NOT thread-safe.
//
// TODO(armansito): This class is ref-counted to prevent potential use-after-free errors though
// vending weak ptrs would have been more suitable since this class is intended to be uniquely owned
// by its creator. fxl::WeakPtr is not thread-safe which is why we use fxl::RefCountedThreadSafe.
// Consider making fxl::WeakPtr thread-safe.
class Transport final : public ::fsl::MessageLoopHandler,
                        public fxl::RefCountedThreadSafe<Transport> {
 public:
  static fxl::RefPtr<Transport> Create(std::unique_ptr<DeviceWrapper> hci_device);

  // Initializes the HCI command channel, starts the I/O event loop, and kicks off a new I/O thread
  // for transactions with the HCI driver. The ACLDataChannel will be left uninitialized. The
  // ACLDataChannel must be initialized after available data buffer information has been obtained
  // from the controller (via HCI_Read_Buffer_Size and HCI_LE_Read_Buffer_Size).
  //
  // This method is NOT thread-safe! Care must be taken such that the public methods of this
  // class and those of the individual channel classes are not called in a manner that would race
  // with the execution of Initialize().
  bool Initialize();

  // Initializes the ACL data channel with the given parameters. Returns false if an error occurs
  // during initialization. Initialize() must have been called successfully prior to calling this
  // method.
  bool InitializeACLDataChannel(const DataBufferInfo& bredr_buffer_info,
                                const DataBufferInfo& le_buffer_info);

  // Cleans up all transport channels, stops the I/O event loop, and joins the I/O thread. Once a
  // Transport has been shut down, it cannot be re-initialized.
  //
  // NOTE: Care must be taken such that this method is not called from a thread that would race with
  // a call to Initialize(). ShutDown() is not thread-safe; Initialize(),
  // InitializeACLDataChannel(), and ShutDown() MUST be called on the same thread.
  void ShutDown();

  // Returns true if this Transport has been fully initialized and running.
  bool IsInitialized() const;

  // Returns a pointer to the HCI command and event flow control handler.
  CommandChannel* command_channel() const { return command_channel_.get(); }

  // Returns a pointer to the HCI ACL data flow control handler.
  ACLDataChannel* acl_data_channel() const { return acl_data_channel_.get(); }

  // Returns the I/O thread task runner. If this is called when this Transport instance is not
  // initialized, the return value will be nullptr.
  fxl::RefPtr<fxl::TaskRunner> io_task_runner() const { return io_task_runner_; }

  // Set a callback that should be invoked when any one of the underlying channels gets closed
  // for any reason (e.g. the HCI device has disappeared) and the task runner on which the
  // callback should be posted.
  //
  // When this callback is called the channels will be in an invalid state and packet processing
  // is no longer guaranteed to work. It is the responsibility of the callback implementation to
  // clean up this Transport instance by calling ShutDown() and/or deleting it.
  void SetTransportClosedCallback(const fxl::Closure& callback,
                                  fxl::RefPtr<fxl::TaskRunner> task_runner);

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(Transport);

  explicit Transport(std::unique_ptr<DeviceWrapper> hci_device);
  ~Transport() override;

  // ::fsl::MessageLoopHandler overrides:
  void OnHandleReady(mx_handle_t handle, mx_signals_t pending, uint64_t count) override;
  void OnHandleError(mx_handle_t handle, mx_status_t error) override;

  // Notifies the closed callback.
  void NotifyClosedCallback();

  // Used to assert that certain public functions are only called on the creation thread.
  fxl::ThreadChecker thread_checker_;

  // The Bluetooth HCI device file descriptor.
  std::unique_ptr<DeviceWrapper> hci_device_;

  // The state of the initialization sequence.
  std::atomic_bool is_initialized_;

  // The thread that performs all HCI I/O operations.
  std::thread io_thread_;

  // The HandlerKey returned from fsl::MessageLoop::AddHandler
  fsl::MessageLoop::HandlerKey cmd_channel_handler_key_;
  fsl::MessageLoop::HandlerKey acl_channel_handler_key_;

  // The task runner used for posting tasks on the HCI transport I/O thread.
  fxl::RefPtr<fxl::TaskRunner> io_task_runner_;

  // The ACL data flow control handler.
  std::unique_ptr<ACLDataChannel> acl_data_channel_;

  // The HCI command and event flow control handler.
  std::unique_ptr<CommandChannel> command_channel_;

  // Callback invoked when the transport is closed (due to a channel error) and its task runner.
  fxl::Closure closed_cb_;
  fxl::RefPtr<fxl::TaskRunner> closed_cb_task_runner_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Transport);
};

}  // namespace hci
}  // namespace bluetooth
