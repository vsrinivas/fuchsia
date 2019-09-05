// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_AUDIO_INTEL_HDA_CONTROLLER_INTEL_DSP_IPC_H_
#define ZIRCON_SYSTEM_DEV_AUDIO_INTEL_HDA_CONTROLLER_INTEL_DSP_IPC_H_

#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>

#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/string.h>
#include <intel-hda/utils/intel-audio-dsp-ipc.h>
#include <intel-hda/utils/intel-hda-registers.h>

namespace audio {
namespace intel_hda {

class IntelDspIpc {
 public:
  // Mailbox constants
  static constexpr size_t MAILBOX_SIZE = 0x1000;

  // Create an IPC object, able to send and receive messages to the SST DSP.
  //
  // |regs| is the address of the ADSP MMIO register set in our address space.
  IntelDspIpc(fbl::String log_prefix, adsp_registers_t* regs);

  class Txn : public fbl::DoublyLinkedListable<Txn*> {
   public:
    Txn(const void* tx, size_t txs, void* rx, size_t rxs)
        : tx_data(tx), tx_size(txs), rx_data(rx), rx_size(rxs) {}
    Txn(uint32_t pri, uint32_t ext, const void* tx, size_t txs, void* rx, size_t rxs)
        : request(pri, ext), tx_data(tx), tx_size(txs), rx_data(rx), rx_size(rxs) {}

    DISALLOW_NEW;

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

  zx_status_t WaitForFirmwareReady(zx_duration_t timeout) {
    return sync_completion_wait(&fw_ready_completion_, timeout);
  }
  void Shutdown();

  // Library & Module Management IPC
  zx_status_t InitInstance(uint16_t module_id, uint8_t instance_id, ProcDomain proc_domain,
                           uint8_t core_id, uint8_t ppl_instance_id, uint16_t param_block_size,
                           const void* param_data);
  zx_status_t LargeConfigGet(Txn* txn, uint16_t module_id, uint8_t instance_id,
                             uint8_t large_param_id, uint32_t data_off_size);
  zx_status_t Bind(uint16_t src_module_id, uint8_t src_instance_id, uint8_t src_queue,
                   uint16_t dst_module_id, uint8_t dst_instance_id, uint8_t dst_queue);

  // Pipeline Management IPC
  zx_status_t CreatePipeline(uint8_t instance_id, uint8_t ppl_priority, uint16_t ppl_mem_size,
                             bool lp);
  zx_status_t SetPipelineState(uint8_t ppl_id, PipelineState state, bool sync_stop_start);

  // Process an interrupt.
  void ProcessIrq();

  const char* log_prefix() const { return log_prefix_.c_str(); }

 private:
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

  // Process responses from DSP
  void ProcessIpc(const IpcMessage& message);
  void ProcessIpcNotification(const IpcMessage& notif);
  void ProcessIpcReply(const IpcMessage& reply);
  void ProcessLargeConfigGetReply(Txn* txn);

  void IpcMailboxWrite(const void* data, size_t size) { mailbox_out_.Write(data, size); }
  void IpcMailboxRead(void* data, size_t size) { mailbox_in_.Read(data, size); }

  // Send an IPC message and wait for response
  zx_status_t SendIpcWait(Txn* txn);

  void SendIpcMessage(const IpcMessage& message);

  void SendIpc(const Txn& txn);

  zx_status_t dsp_to_zx_status(MsgStatus status) {
    return (status == MsgStatus::IPC_SUCCESS) ? ZX_OK : ZX_ERR_INTERNAL;
  }

  Mailbox mailbox_in_;
  Mailbox mailbox_out_;

  // Log prefix storage
  const fbl::String log_prefix_;

  // Pending IPC
  fbl::Mutex ipc_lock_;
  fbl::DoublyLinkedList<Txn*> ipc_queue_ TA_GUARDED(ipc_lock_);

  // Used to wait for firmware ready
  sync_completion_t fw_ready_completion_;

  // Hardware registers.
  adsp_registers_t* regs_;
};

}  // namespace intel_hda
}  // namespace audio

#endif  // ZIRCON_SYSTEM_DEV_AUDIO_INTEL_HDA_CONTROLLER_INTEL_DSP_IPC_H_
