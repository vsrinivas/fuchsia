// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <string.h>

#include <pretty/hexdump.h>

#include "intel-audio-dsp.h"
#include "intel-dsp-ipc.h"

namespace audio {
namespace intel_hda {

IntelDspIpc::IntelDspIpc(IntelAudioDsp& dsp) : dsp_(dsp) {
    snprintf(log_prefix_, sizeof(log_prefix_), "IHDA DSP IPC (unknown BDF)");
}

void IntelDspIpc::SetLogPrefix(const char* new_prefix) {
    snprintf(log_prefix_, sizeof(log_prefix_), "%s IPC", new_prefix);
}

zx_status_t IntelDspIpc::SendIpc(Txn* txn) {
    {
        fbl::AutoLock ipc_lock(&ipc_lock_);
        // 1 at a time
        ZX_DEBUG_ASSERT(pending_txn_ == nullptr);
        if (pending_txn_ != nullptr) {
            return ZX_ERR_BAD_STATE;
        }
        pending_txn_ = txn;
    }

    // Copy tx data to outbox
    if (txn->tx_size > 0) {
        dsp_.IpcMailboxWrite(txn->tx_data, txn->tx_size);
    }
    dsp_.SendIpcMessage(txn->request);
    return ZX_OK;
}

zx_status_t IntelDspIpc::SendIpcWait(Txn* txn) {
    zx_status_t res = SendIpc(txn);
    if (res != ZX_OK) {
        return res;
    }
    // Wait for completion
    res = completion_wait(&txn->completion, ZX_MSEC(300));
    if (res != ZX_OK) {
        dsp_.DeviceShutdown();
    }
    return res;
}

zx_status_t IntelDspIpc::InitInstance(uint16_t module_id, uint8_t instance_id,
                                      ProcDomain proc_domain, uint8_t core_id,
                                      uint8_t ppl_instance_id,
                                      uint16_t param_block_size, const void* param_data) {
    LOG(DEBUG1, "INIT_INSTANCE (mod %u inst %u)\n", module_id, instance_id);

    Txn txn(IPC_PRI(MsgTarget::MODULE_MSG, MsgDir::MSG_REQUEST,
                    ModuleMsgType::INIT_INSTANCE, instance_id, module_id),
               IPC_INIT_INSTANCE_EXT(proc_domain, core_id, ppl_instance_id, param_block_size),
               param_data, param_block_size, nullptr, 0);

    zx_status_t res = SendIpcWait(&txn);
    if (res != ZX_OK) {
        LOG(ERROR, "IPC error (res %d)\n", res);
        return res;
    }

    if (txn.reply.status() != MsgStatus::IPC_SUCCESS) {
        LOG(ERROR, "INIT_INSTANCE (mod %u inst %u) failed (err %d)\n",
                   module_id, instance_id, to_underlying(txn.reply.status()));
    } else {
        LOG(DEBUG1, "INIT_INSTANCE (mod %u inst %u) success\n", module_id, instance_id);
    }

    return dsp_to_zx_status(txn.reply.status());
}

zx_status_t IntelDspIpc::LargeConfigGet(Txn* txn, uint16_t module_id, uint8_t instance_id,
                                        uint8_t large_param_id, uint32_t data_off_size) {
    ZX_DEBUG_ASSERT(txn->rx_data != nullptr);
    ZX_DEBUG_ASSERT(txn->rx_size > 0);

    LOG(DEBUG1, "LARGE_CONFIG_GET (mod %u inst %u large_param_id %u)\n",
                module_id, instance_id, large_param_id);

    txn->request.primary = IPC_PRI(MsgTarget::MODULE_MSG, MsgDir::MSG_REQUEST,
                                   ModuleMsgType::LARGE_CONFIG_GET, instance_id, module_id);
    txn->request.extension = IPC_LARGE_CONFIG_EXT(true, false, large_param_id, data_off_size);

    zx_status_t res = SendIpcWait(txn);
    if (res != ZX_OK) {
        LOG(ERROR, "IPC error (res %d)\n", res);
        return res;
    }

    LOG(DEBUG1, "LARGE_CONFIG_GET (mod %u inst %u large_param_id %u) status %d\n",
                module_id, instance_id, large_param_id, to_underlying(txn->reply.status()));

    return dsp_to_zx_status(txn->reply.status());
}

zx_status_t IntelDspIpc::Bind(uint16_t src_module_id, uint8_t src_instance_id, uint8_t src_queue,
                              uint16_t dst_module_id, uint8_t dst_instance_id, uint8_t dst_queue) {
    LOG(DEBUG1, "BIND (mod %u inst %u -> mod %u inst %u)\n",
                src_module_id, src_instance_id, dst_module_id, dst_instance_id);

    Txn txn(IPC_PRI(MsgTarget::MODULE_MSG, MsgDir::MSG_REQUEST, ModuleMsgType::BIND,
                    src_instance_id, src_module_id),
               IPC_BIND_UNBIND_EXT(dst_module_id, dst_instance_id, dst_queue, src_queue),
               nullptr, 0, nullptr, 0);

    zx_status_t res = SendIpcWait(&txn);
    if (res != ZX_OK) {
        LOG(ERROR, "IPC error (res %d)\n", res);
        return res;
    }

    if (txn.reply.status() != MsgStatus::IPC_SUCCESS) {
        LOG(ERROR, "BIND (mod %u inst %u -> mod %u inst %u) failed (err %d)\n",
                    src_module_id, src_instance_id, dst_module_id, dst_instance_id,
                    to_underlying(txn.reply.status()));
    } else {
        LOG(DEBUG1, "BIND (mod %u inst %u -> mod %u inst %u) success\n",
                    src_module_id, src_instance_id, dst_module_id, dst_instance_id);
    }

    return dsp_to_zx_status(txn.reply.status());
}

zx_status_t IntelDspIpc::CreatePipeline(uint8_t instance_id, uint8_t ppl_priority,
                                        uint16_t ppl_mem_size, bool lp) {
    LOG(DEBUG1, "CREATE_PIPELINE (inst %u)\n", instance_id);

    Txn txn(IPC_CREATE_PIPELINE_PRI(instance_id, ppl_priority, ppl_mem_size),
            IPC_CREATE_PIPELINE_EXT(lp),
            nullptr, 0, nullptr, 0);

    zx_status_t res = SendIpcWait(&txn);
    if (res != ZX_OK) {
        LOG(ERROR, "IPC error (res %d)\n", res);
        return res;
    }

    if (txn.reply.status() != MsgStatus::IPC_SUCCESS) {
        LOG(ERROR, "CREATE_PIPELINE (inst %u) failed (err %d)\n",
                   instance_id, to_underlying(txn.reply.status()));
    } else {
        LOG(DEBUG1, "CREATE_PIPELINE (inst %u) success\n", instance_id);
    }

    return dsp_to_zx_status(txn.reply.status());
}

zx_status_t IntelDspIpc::SetPipelineState(uint8_t ppl_id, PipelineState state,
                                          bool sync_stop_start) {
    LOG(DEBUG1, "SET_PIPELINE_STATE (inst %u)\n", ppl_id);

    Txn txn(IPC_SET_PIPELINE_STATE_PRI(ppl_id, state),
            IPC_SET_PIPELINE_STATE_EXT(false, sync_stop_start),
            nullptr, 0, nullptr, 0);

    zx_status_t res = SendIpcWait(&txn);
    if (res != ZX_OK) {
        LOG(ERROR, "IPC error (res %d)\n", res);
        return res;
    }

    if (txn.reply.status() != MsgStatus::IPC_SUCCESS) {
        LOG(ERROR, "SET_PIPELINE_STATE (inst %u) failed (err %d)\n",
                   ppl_id, to_underlying(txn.reply.status()));
    } else {
        LOG(DEBUG1, "SET_PIPELINE_STATE (inst %u) success\n", ppl_id);
    }

    return dsp_to_zx_status(txn.reply.status());
}

void IntelDspIpc::ProcessIpc(const IpcMessage& message) {
    if (message.is_notif()) {
        ProcessIpcNotification(message);
    } else if (message.is_reply()) {
        ProcessIpcReply(message);
    }
}

void IntelDspIpc::ProcessIpcNotification(const IpcMessage& notif) {
    switch (notif.notif_type()) {
    case NotificationType::FW_READY:
        LOG(INFO, "firmware ready\n");
        break;
    case NotificationType::RESOURCE_EVENT: {
        ResourceEventData data;
        dsp_.IpcMailboxRead(&data, sizeof(data));
#if 0
        LOG(INFO, "resource event type %u id %u event %u\n",
                  data.resource_type, data.resource_id, data.event_type);
#endif
        break;
    }
    default:
        LOG(INFO, "got notification type %u\n", to_underlying(notif.notif_type()));
        break;
    }
}

void IntelDspIpc::ProcessIpcReply(const IpcMessage& reply) {
    fbl::AutoLock ipc_lock(&ipc_lock_);
    if (pending_txn_ == nullptr) {
        LOG(INFO, "got spurious reply message\n");
        return;
    }

    // Check if the reply matches the pending request.
    IpcMessage* pending = &pending_txn_->request;
    if ((pending->msg_tgt() != reply.msg_tgt()) || (pending->type() != reply.type())) {
        LOG(INFO, "reply msg mismatch, got pri 0x%08x ext 0x%08x, expect pri 0x%08x ext 0x%08x\n",
            reply.primary, reply.extension, pending->primary, pending->extension);
        return;
    }

    LOG(INFO, "got reply (status %u) for pending msg, pri 0x%08x ext 0x%08x\n",
              to_underlying(reply.status()), reply.primary, reply.extension);

    pending_txn_->reply = reply;

    if (reply.msg_tgt() == MsgTarget::MODULE_MSG) {
        ModuleMsgType type = static_cast<ModuleMsgType>(reply.type());
        switch (type) {
        case ModuleMsgType::LARGE_CONFIG_GET:
            ProcessLargeConfigGetReply(pending_txn_);
            break;
        default:
            break;
        }
    }

    completion_signal(&pending_txn_->completion);
    pending_txn_ = nullptr;
}

void IntelDspIpc::ProcessLargeConfigGetReply(Txn* txn) {
    ZX_DEBUG_ASSERT_MSG(txn->request.large_param_id() == txn->reply.large_param_id(),
                        "large_param_id mismatch, expected %u got %u\n",
                        txn->request.large_param_id(), txn->reply.large_param_id());

    LOG(INFO, "got LARGE_CONFIG_GET reply, id %u init_block %d final_block %d data_off_size %u\n",
        txn->reply.large_param_id(), txn->reply.init_block(), txn->reply.final_block(),
        txn->reply.data_off_size());

    if (txn->reply.status() == MsgStatus::IPC_SUCCESS) {
        // Only support single reads for now.
        uint32_t size = txn->reply.data_off_size();
        ZX_DEBUG_ASSERT(txn->reply.init_block());
        ZX_DEBUG_ASSERT(txn->reply.final_block());
        ZX_DEBUG_ASSERT(size > 0);
        ZX_DEBUG_ASSERT(size <= txn->rx_size);

        dsp_.IpcMailboxRead(txn->rx_data, size);
        txn->rx_actual = size;
    } else {
        txn->rx_actual = 0;
    }
}

}  // namespace intel_hda
}  // namespace audio
