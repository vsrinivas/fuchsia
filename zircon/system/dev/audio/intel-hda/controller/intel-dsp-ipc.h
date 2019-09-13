// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_AUDIO_INTEL_HDA_CONTROLLER_INTEL_DSP_IPC_H_
#define ZIRCON_SYSTEM_DEV_AUDIO_INTEL_HDA_CONTROLLER_INTEL_DSP_IPC_H_

#include <lib/fit/function.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/time.h>

#include <functional>
#include <optional>

#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/span.h>
#include <fbl/string.h>
#include <intel-hda/utils/intel-audio-dsp-ipc.h>
#include <intel-hda/utils/intel-hda-registers.h>
#include <intel-hda/utils/status.h>
#include <refcount/blocking_refcount.h>

namespace audio {
namespace intel_hda {

class IntelDspIpc {
 public:
  static constexpr auto kDefaultTimeout = zx::msec(1000);

  // Create an IPC object, able to send and receive messages to the SST DSP.
  //
  // |regs| is the address of the ADSP MMIO register set in our address space.
  //
  // |hardware_timeout| specifies how long we should wait for hardware to respond
  // to our requests before failing operations.
  IntelDspIpc(
      fbl::String log_prefix, adsp_registers_t* regs,
      std::optional<std::function<void(NotificationType)>> notification_callback = std::nullopt,
      zx::duration hardware_timeout = kDefaultTimeout);

  // Will block until all pending operations have been cancelled and callbacks completed.
  ~IntelDspIpc();

  // Shutdown the object, cancelling all in-flight transactions.
  void Shutdown() TA_EXCL(lock_);

  // Process an interrupt.
  //
  // Should be called each time the DSP receives an interrupt, allowing this
  // object to process any IPC-related interrupts that may be pending.
  void ProcessIrq();

  // Send an IPC message and wait for response.
  //
  // The second variant |SendWithData| allows a data payload to be sent and/or
  // received from the DSP. Empty spans are allowed to indicate no data should
  // be sent or received, and both the send and receive spans may point to the
  // same underlying memory if the same buffer should be used for both reading
  // and writing.
  Status Send(uint32_t primary, uint32_t extension);
  Status SendWithData(uint32_t primary, uint32_t extension, fbl::Span<const uint8_t> payload,
                      fbl::Span<uint8_t> recv_buffer, size_t* bytes_received);

  // Return true if at least one operation is pending.
  bool IsOperationPending() const;

  const char* log_prefix() const { return log_prefix_.c_str(); }

 private:
  // In-flight IPC to the DSP.
  class Txn : public fbl::DoublyLinkedListable<Txn*> {
   public:
    Txn(const void* tx, size_t txs, void* rx, size_t rxs)
        : tx_data(tx), tx_size(txs), rx_data(rx), rx_size(rxs) {}
    Txn(uint32_t pri, uint32_t ext, const void* tx, size_t txs, void* rx, size_t rxs)
        : request(pri, ext), tx_data(tx), tx_size(txs), rx_data(rx), rx_size(rxs) {}

    bool success() { return done && reply.status() == MsgStatus::IPC_SUCCESS; }

    IpcMessage request;
    IpcMessage reply;

    bool done = false;

    const void* tx_data = nullptr;
    size_t tx_size = 0;
    void* rx_data = nullptr;
    size_t rx_size = 0;
    size_t rx_actual = 0;

    sync_completion_t completion;
  };

  // IPC Mailboxes
  class Mailbox {
   public:
    void Initialize(void* base, size_t size) {
      base_ = base;
      size_ = size;
    }

    size_t size() const { return size_; }

    void Write(const void* data, size_t size) {
      // It is the caller's responsibility to ensure size fits in the mailbox.
      ZX_DEBUG_ASSERT(size <= size_);
      memcpy(base_, data, size);
    }
    void Read(void* data, size_t size) {
      // It is the caller's responsibility to ensure size fits in the mailbox.
      ZX_DEBUG_ASSERT(size <= size_);
      memcpy(data, base_, size);
    }

   private:
    void* base_;
    size_t size_;
  };

  // Send an IPC message and wait for response
  zx_status_t SendIpcWait(Txn* txn) TA_EXCL(lock_);

  // Process a notification received from the DSP, creating
  // a fit::inline_function which, when invoked, will actually perform the
  // callback.
  //
  // The fit::inline_function which should be called outside |lock_| to notify
  // the registered callback.
  std::optional<fit::inline_function<void()>> CreateNotificationCallback(const IpcMessage& notif)
      TA_REQ(lock_);

  // Process responses from DSP
  void ProcessIpcReply(const IpcMessage& reply) TA_REQ(lock_);
  void ProcessLargeConfigGetReply(Txn* txn) TA_REQ(lock_);

  void IpcMailboxWrite(const void* data, size_t size) TA_REQ(lock_);
  void IpcMailboxRead(void* data, size_t size) TA_REQ(lock_);

  void SendIpc(const Txn& txn) TA_REQ(lock_);

  mutable fbl::Mutex lock_;
  refcount::BlockingRefCount in_flight_callbacks_;  // Number of in-flight send operations

  Mailbox mailbox_in_ TA_GUARDED(lock_);
  Mailbox mailbox_out_ TA_GUARDED(lock_);

  // Log prefix storage
  const fbl::String log_prefix_;

  // Pending IPC
  fbl::DoublyLinkedList<Txn*> ipc_queue_ TA_GUARDED(lock_);

  // Hardware registers.
  adsp_registers_t* const regs_ TA_GUARDED(lock_);

  // Callback for unsolicited notifications from the DSP.
  const std::optional<const std::function<void(NotificationType)>> callback_;

  // Timeout for hardware responses.
  const zx::duration hardware_timeout_;
};

}  // namespace intel_hda
}  // namespace audio

#endif  // ZIRCON_SYSTEM_DEV_AUDIO_INTEL_HDA_CONTROLLER_INTEL_DSP_IPC_H_
