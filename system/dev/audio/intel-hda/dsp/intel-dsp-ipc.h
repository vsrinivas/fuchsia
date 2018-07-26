// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/intrusive_double_list.h>
#include <lib/sync/completion.h>
#include <zircon/thread_annotations.h>

#include <intel-hda/utils/intel-audio-dsp-ipc.h>

#include "debug-logging.h"

namespace audio {
namespace intel_hda {

class IntelAudioDsp;

class IntelDspIpc {
public:
    IntelDspIpc(IntelAudioDsp& dsp);

    const char*  log_prefix() const { return log_prefix_; }

    class Txn : public fbl::DoublyLinkedListable<Txn*> {
    public:
        Txn(const void* tx, size_t txs, void* rx, size_t rxs)
            : tx_data(tx), tx_size(txs), rx_data(rx), rx_size(rxs) { }
        Txn(uint32_t pri, uint32_t ext, const void* tx, size_t txs, void* rx, size_t rxs)
            : request(pri, ext), tx_data(tx), tx_size(txs), rx_data(rx), rx_size(rxs) { }

        DISALLOW_NEW;

        bool success() {
            return done && reply.status() == MsgStatus::IPC_SUCCESS;
        }

        IpcMessage request;
        IpcMessage reply;

        bool done = false;

        const void* tx_data = nullptr;
        size_t      tx_size = 0;
        void*       rx_data = nullptr;
        size_t      rx_size = 0;
        size_t      rx_actual = 0;

        sync_completion_t completion;
    };

    void SetLogPrefix(const char* new_prefix);

    zx_status_t WaitForFirmwareReady(zx_time_t timeout) {
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
    zx_status_t CreatePipeline(uint8_t instance_id, uint8_t ppl_priority,
                               uint16_t ppl_mem_size, bool lp);
    zx_status_t SetPipelineState(uint8_t ppl_id, PipelineState state, bool sync_stop_start);

    // Process responses from DSP
    void ProcessIpc(const IpcMessage& message);
    void ProcessIpcNotification(const IpcMessage& reply);
    void ProcessIpcReply(const IpcMessage& reply);
    void ProcessLargeConfigGetReply(Txn* txn);

private:
    // Send an IPC message and wait for response
    zx_status_t SendIpcWait(Txn* txn);

    void SendIpc(const Txn& txn);

    zx_status_t dsp_to_zx_status(MsgStatus status) {
        return (status == MsgStatus::IPC_SUCCESS) ? ZX_OK : ZX_ERR_INTERNAL;
    }

    // Log prefix storage
    char log_prefix_[LOG_PREFIX_STORAGE] = { 0 };

    // Pending IPC
    fbl::Mutex ipc_lock_;
    fbl::DoublyLinkedList<Txn*> ipc_queue_ TA_GUARDED(ipc_lock_);

    // A reference to the owning DSP
    IntelAudioDsp& dsp_;

    // Used to wait for firmware ready
    sync_completion_t fw_ready_completion_;
};

}  // namespace intel_hda
}  // namespace audio
