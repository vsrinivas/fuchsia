// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-dsp-ipc.h"

#include <lib/zx/time.h>
#include <string.h>

#include <functional>

#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/span.h>
#include <fbl/string_printf.h>
#include <intel-hda/utils/intel-hda-registers.h>
#include <intel-hda/utils/status.h>
#include <intel-hda/utils/utils.h>
#include <refcount/blocking_refcount.h>

#include "debug-logging.h"

namespace audio {
namespace intel_hda {

// Concrete Implementation of DspChannel.
class HardwareDspChannel : public DspChannel {
 public:
  // Create an IPC object, able to send and receive messages to the SST DSP.
  //
  // |regs| is the address of the ADSP MMIO register set in our address space.
  //
  // |hardware_timeout| specifies how long we should wait for hardware to respond
  // to our requests before failing operations.
  HardwareDspChannel(
      fbl::String log_prefix, MMIO_PTR adsp_registers_t* regs,
      std::optional<std::function<void(NotificationType)>> notification_callback = std::nullopt,
      zx::duration hardware_timeout = kDefaultTimeout);

  // Will block until all pending operations have been cancelled and callbacks completed.
  ~HardwareDspChannel();

  // Implementation of DspChannel.
  void Shutdown() override TA_EXCL(lock_);
  void ProcessIrq() override;
  Status Send(uint32_t primary, uint32_t extension) override;
  Status SendWithData(uint32_t primary, uint32_t extension, fbl::Span<const uint8_t> payload,
                      fbl::Span<uint8_t> recv_buffer, size_t* bytes_received) override;

  // Return true if at least one operation is pending.
  bool IsOperationPending() const override;

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
    void Initialize(MMIO_PTR void* base, size_t size) {
      // TODO(fxbug.dev/56253): avoid casting away the MMIO_PTR annotation.
      base_ = (void*)base;
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

  // Process a notification received from the DSP.
  //
  // We return a fit::inline_function which should be called outside |lock_| to
  // notify the registered callback.
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
  MMIO_PTR adsp_registers_t* const regs_ TA_GUARDED(lock_);

  // Callback for unsolicited notifications from the DSP.
  const std::optional<const std::function<void(NotificationType)>> callback_;

  // Timeout for hardware responses.
  const zx::duration hardware_timeout_;
};

// Mailbox constants
constexpr size_t MAILBOX_SIZE = 0x1000;

HardwareDspChannel::HardwareDspChannel(
    fbl::String log_prefix, MMIO_PTR adsp_registers_t* regs,
    std::optional<std::function<void(NotificationType)>> notification_callback,
    zx::duration hardware_timeout)
    : log_prefix_(std::move(log_prefix)),
      regs_(regs),
      callback_(std::move(notification_callback)),
      hardware_timeout_(hardware_timeout) {
  MMIO_PTR uint8_t* mapped_base = reinterpret_cast<MMIO_PTR uint8_t*>(regs);
  mailbox_in_.Initialize(
      static_cast<MMIO_PTR void*>(mapped_base + SKL_ADSP_SRAM0_OFFSET + ADSP_MAILBOX_IN_OFFSET),
      MAILBOX_SIZE);
  mailbox_out_.Initialize(static_cast<MMIO_PTR void*>(mapped_base + SKL_ADSP_SRAM1_OFFSET),
                          MAILBOX_SIZE);
}

HardwareDspChannel::~HardwareDspChannel() { Shutdown(); }

void HardwareDspChannel::Shutdown() {
  fbl::AutoLock ipc_lock(&lock_);

  // Fail all pending IPCs
  while (!ipc_queue_.is_empty()) {
    sync_completion_signal(&ipc_queue_.pop_front()->completion);
  }

  // Wait for refcount to hit zero.
  in_flight_callbacks_.WaitForZero();
}

Status HardwareDspChannel::SendWithData(uint32_t primary, uint32_t extension,
                                        fbl::Span<const uint8_t> payload,
                                        fbl::Span<uint8_t> recv_buffer, size_t* bytes_received) {
  if (payload.size() > MAILBOX_SIZE) {
    return Status(ZX_ERR_INVALID_ARGS);
  }

  Txn txn{primary,
          extension,
          payload.data(),
          payload.size_bytes(),
          recv_buffer.data(),
          recv_buffer.size_bytes()};

  zx_status_t res = SendIpcWait(&txn);
  if (res != ZX_OK) {
    return Status(res);
  }
  if (!txn.done) {
    return Status(ZX_ERR_CANCELED, "Operation cancelled due to IPC shutdown.");
  }
  if (txn.reply.status() != MsgStatus::IPC_SUCCESS) {
    return Status(ZX_ERR_INTERNAL,
                  fbl::StringPrintf("DSP returned error %d", to_underlying(txn.reply.status())));
  }

  if (bytes_received != nullptr) {
    *bytes_received = txn.rx_actual;
  }
  return OkStatus();
}

Status HardwareDspChannel::Send(uint32_t primary, uint32_t extension) {
  return SendWithData(primary, extension, fbl::Span<uint8_t>(), fbl::Span<uint8_t>(), nullptr);
}

bool HardwareDspChannel::IsOperationPending() const {
  fbl::AutoLock ipc_lock(&lock_);
  return !ipc_queue_.is_empty();
}

void HardwareDspChannel::SendIpc(const Txn& txn) {
  // Copy tx data to outbox
  if (txn.tx_size > 0) {
    IpcMailboxWrite(txn.tx_data, txn.tx_size);
  }

  // Copy metadata to hardware registers.
  REG_WR(&regs_->hipcie, txn.request.extension);
  REG_WR(&regs_->hipci, txn.request.primary | ADSP_REG_HIPCI_BUSY);
}

zx_status_t HardwareDspChannel::SendIpcWait(Txn* txn) {
  in_flight_callbacks_.Inc();
  auto cleanup = fbl::MakeAutoCall([this]() { in_flight_callbacks_.Dec(); });

  {
    // Add to the pending queue and start the ipc if necessary
    fbl::AutoLock ipc_lock(&lock_);
    bool needs_start = ipc_queue_.is_empty();
    ipc_queue_.push_back(txn);
    if (needs_start) {
      SendIpc(ipc_queue_.front());
    }
  }

  // Wait for completion.
  zx_status_t res = sync_completion_wait(&txn->completion, hardware_timeout_.get());
  if (res != ZX_OK) {
    fbl::AutoLock ipc_lock(&lock_);
    // When we wake up, our transaction might still be in the list, or it might have
    // been removed (because we are racing with the receive that timed out).
    // Ensure it is removed before returning to the caller.
    if (txn->InContainer()) {
      ipc_queue_.erase(*txn);
    }
    return res;
  }

  // TODO(yky): fxbug.dev/32120: Figure out why this is needed and eliminate it.
  zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
  return res;
}

void HardwareDspChannel::ProcessIrq() {
  std::optional<fit::inline_function<void()>> callback;

  // Process any pending IRQs.
  {
    fbl::AutoLock l(&lock_);

    uint32_t adspis = REG_RD(&regs_->adspis);
    if (adspis & ADSP_REG_ADSPIC_IPC) {
      IpcMessage message(REG_RD(&regs_->hipct), REG_RD(&regs_->hipcte));
      if (message.primary & ADSP_REG_HIPCT_BUSY) {
        // Process the incoming message
        if (message.is_notif()) {
          callback = CreateNotificationCallback(message);
        } else if (message.is_reply()) {
          ProcessIpcReply(message);
        }

        // Ack the IRQ after reading mailboxes.
        REG_SET_BITS(&regs_->hipct, ADSP_REG_HIPCT_BUSY);
      }
    }

    // Ack the IPC target done IRQ
    uint32_t val = REG_RD(&regs_->hipcie);
    if (val & ADSP_REG_HIPCIE_DONE) {
      REG_WR(&regs_->hipcie, val);
    }
  }

  // If a notification was required, issue it outside the lock.
  if (callback.has_value()) {
    (*callback)();
  }
}

std::optional<fit::inline_function<void()>> HardwareDspChannel::CreateNotificationCallback(
    const IpcMessage& notif) {
  if (!callback_.has_value()) {
    return std::nullopt;
  }
  NotificationType type = notif.notif_type();
  in_flight_callbacks_.Inc();
  // Use a fit::inline_function<void()>() to avoid heap allocations on the notification pass.
  return fit::inline_function<void()>([this, type]() {
    (*callback_)(type);
    in_flight_callbacks_.Dec();
  });
}

void HardwareDspChannel::ProcessIpcReply(const IpcMessage& reply) {
  if (ipc_queue_.is_empty()) {
    LOG(INFO, "got spurious reply message\n");
    return;
  }
  Txn& pending = ipc_queue_.front();

  // Check if the reply matches the pending request.
  IpcMessage* req = &pending.request;
  if ((req->msg_tgt() != reply.msg_tgt()) || (req->type() != reply.type())) {
    LOG(INFO, "reply msg mismatch, got pri 0x%08x ext 0x%08x, expect pri 0x%08x ext 0x%08x\n",
        reply.primary, reply.extension, req->primary, req->extension);
    return;
  }

  // The pending txn is done
  ipc_queue_.pop_front();
  pending.reply = reply;
  pending.done = true;

  LOG(TRACE, "got reply (status %u) for pending msg, pri 0x%08x ext 0x%08x\n",
      to_underlying(reply.status()), reply.primary, reply.extension);

  if (reply.msg_tgt() == MsgTarget::MODULE_MSG) {
    ModuleMsgType type = static_cast<ModuleMsgType>(reply.type());
    switch (type) {
      case ModuleMsgType::LARGE_CONFIG_GET:
        ProcessLargeConfigGetReply(&pending);
        break;
      default:
        break;
    }
  }

  sync_completion_signal(&pending.completion);

  // Send the next ipc in the queue
  if (!ipc_queue_.is_empty()) {
    SendIpc(ipc_queue_.front());
  }
}

void HardwareDspChannel::ProcessLargeConfigGetReply(Txn* txn) {
  ZX_DEBUG_ASSERT_MSG(txn->request.large_param_id() == txn->reply.large_param_id(),
                      "large_param_id mismatch, expected %u got %u\n",
                      txn->request.large_param_id(), txn->reply.large_param_id());

  LOG(TRACE, "got LARGE_CONFIG_GET reply, id %u init_block %d final_block %d data_off_size %u\n",
      txn->reply.large_param_id(), txn->reply.init_block(), txn->reply.final_block(),
      txn->reply.data_off_size());

  if (txn->reply.status() == MsgStatus::IPC_SUCCESS) {
    // Only support single reads for now.
    uint32_t size = txn->reply.data_off_size();
    ZX_DEBUG_ASSERT(txn->reply.init_block());
    ZX_DEBUG_ASSERT(txn->reply.final_block());
    ZX_DEBUG_ASSERT(size > 0);
    ZX_DEBUG_ASSERT(size <= txn->rx_size);

    IpcMailboxRead(txn->rx_data, size);
    txn->rx_actual = size;
  } else {
    txn->rx_actual = 0;
  }
}

void HardwareDspChannel::IpcMailboxWrite(const void* data, size_t size) {
  mailbox_out_.Write(data, size);
}

void HardwareDspChannel::IpcMailboxRead(void* data, size_t size) { mailbox_in_.Read(data, size); }

std::unique_ptr<DspChannel> CreateHardwareDspChannel(
    fbl::String log_prefix, MMIO_PTR adsp_registers_t* regs,
    std::optional<std::function<void(NotificationType)>> notification_callback,
    zx::duration hardware_timeout) {
  return std::make_unique<HardwareDspChannel>(log_prefix, regs, std::move(notification_callback),
                                              hardware_timeout);
}

}  // namespace intel_hda
}  // namespace audio
