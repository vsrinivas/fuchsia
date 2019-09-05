// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-dsp-ipc.h"

#include <lib/zx/time.h>
#include <string.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <intel-hda/utils/intel-hda-registers.h>
#include <intel-hda/utils/utils.h>

#include "debug-logging.h"

namespace audio {
namespace intel_hda {

IntelDspIpc::IntelDspIpc(fbl::String log_prefix, adsp_registers_t* regs)
    : log_prefix_(std::move(log_prefix)), regs_(regs) {
  uint8_t* mapped_base = reinterpret_cast<uint8_t*>(regs);
  mailbox_in_.Initialize(
      static_cast<void*>(mapped_base + SKL_ADSP_SRAM0_OFFSET + ADSP_MAILBOX_IN_OFFSET),
      MAILBOX_SIZE);
  mailbox_out_.Initialize(static_cast<void*>(mapped_base + SKL_ADSP_SRAM1_OFFSET), MAILBOX_SIZE);
}

void IntelDspIpc::Shutdown() {
  fbl::AutoLock ipc_lock(&ipc_lock_);
  // Fail all pending IPCs
  while (!ipc_queue_.is_empty()) {
    sync_completion_signal(&ipc_queue_.pop_front()->completion);
  }
}

void IntelDspIpc::SendIpc(const Txn& txn) {
  // Copy tx data to outbox
  if (txn.tx_size > 0) {
    IpcMailboxWrite(txn.tx_data, txn.tx_size);
  }
  SendIpcMessage(txn.request);
}

void IntelDspIpc::SendIpcMessage(const IpcMessage& message) {
  REG_WR(&regs_->hipcie, message.extension);
  REG_WR(&regs_->hipci, message.primary | ADSP_REG_HIPCI_BUSY);
}

zx_status_t IntelDspIpc::SendIpcWait(Txn* txn) {
  {
    // Add to the pending queue and start the ipc if necessary
    fbl::AutoLock ipc_lock(&ipc_lock_);
    bool needs_start = ipc_queue_.is_empty();
    ipc_queue_.push_back(txn);
    if (needs_start) {
      SendIpc(ipc_queue_.front());
    }
  }

  // Wait for completion
  zx_status_t res = sync_completion_wait(&txn->completion, ZX_MSEC(300));
  if (res != ZX_OK) {
    return res;
  }

  // TODO(yky): ZX-2261: Figure out why this is needed and eliminate it.
  zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
  return res;
}

void IntelDspIpc::ProcessIrq() {
  uint32_t adspis = REG_RD(&regs_->adspis);
  if (!(adspis & ADSP_REG_ADSPIC_IPC)) {
    return;
  }

  IpcMessage message(REG_RD(&regs_->hipct), REG_RD(&regs_->hipcte));
  if (message.primary & ADSP_REG_HIPCT_BUSY) {
    // Process the incoming message
    ProcessIpc(message);

    // Ack the IRQ after reading mailboxes.
    REG_SET_BITS(&regs_->hipct, ADSP_REG_HIPCT_BUSY);
  }

  // Ack the IPC target done IRQ
  uint32_t val = REG_RD(&regs_->hipcie);
  if (val & ADSP_REG_HIPCIE_DONE) {
    REG_WR(&regs_->hipcie, val);
  }
}

zx_status_t IntelDspIpc::InitInstance(uint16_t module_id, uint8_t instance_id,
                                      ProcDomain proc_domain, uint8_t core_id,
                                      uint8_t ppl_instance_id, uint16_t param_block_size,
                                      const void* param_data) {
  LOG(DEBUG1, "INIT_INSTANCE (mod %u inst %u)\n", module_id, instance_id);

  Txn txn(IPC_PRI(MsgTarget::MODULE_MSG, MsgDir::MSG_REQUEST, ModuleMsgType::INIT_INSTANCE,
                  instance_id, module_id),
          IPC_INIT_INSTANCE_EXT(proc_domain, core_id, ppl_instance_id, param_block_size),
          param_data, param_block_size, nullptr, 0);

  zx_status_t res = SendIpcWait(&txn);
  if (res != ZX_OK) {
    LOG(ERROR, "IPC error (res %d)\n", res);
    return res;
  }

  if (txn.reply.status() != MsgStatus::IPC_SUCCESS) {
    LOG(ERROR, "INIT_INSTANCE (mod %u inst %u) failed (err %d)\n", module_id, instance_id,
        to_underlying(txn.reply.status()));
  } else {
    LOG(DEBUG1, "INIT_INSTANCE (mod %u inst %u) success\n", module_id, instance_id);
  }

  return dsp_to_zx_status(txn.reply.status());
}

zx_status_t IntelDspIpc::LargeConfigGet(Txn* txn, uint16_t module_id, uint8_t instance_id,
                                        uint8_t large_param_id, uint32_t data_off_size) {
  ZX_DEBUG_ASSERT(txn->rx_data != nullptr);
  ZX_DEBUG_ASSERT(txn->rx_size > 0);

  LOG(DEBUG1, "LARGE_CONFIG_GET (mod %u inst %u large_param_id %u)\n", module_id, instance_id,
      large_param_id);

  txn->request.primary = IPC_PRI(MsgTarget::MODULE_MSG, MsgDir::MSG_REQUEST,
                                 ModuleMsgType::LARGE_CONFIG_GET, instance_id, module_id);
  txn->request.extension = IPC_LARGE_CONFIG_EXT(true, false, large_param_id, data_off_size);

  zx_status_t res = SendIpcWait(txn);
  if (res != ZX_OK) {
    LOG(ERROR, "IPC error (res %d)\n", res);
    return res;
  }

  LOG(DEBUG1, "LARGE_CONFIG_GET (mod %u inst %u large_param_id %u) status %d\n", module_id,
      instance_id, large_param_id, to_underlying(txn->reply.status()));

  return dsp_to_zx_status(txn->reply.status());
}

zx_status_t IntelDspIpc::Bind(uint16_t src_module_id, uint8_t src_instance_id, uint8_t src_queue,
                              uint16_t dst_module_id, uint8_t dst_instance_id, uint8_t dst_queue) {
  LOG(DEBUG1, "BIND (mod %u inst %u -> mod %u inst %u)\n", src_module_id, src_instance_id,
      dst_module_id, dst_instance_id);

  Txn txn(IPC_PRI(MsgTarget::MODULE_MSG, MsgDir::MSG_REQUEST, ModuleMsgType::BIND, src_instance_id,
                  src_module_id),
          IPC_BIND_UNBIND_EXT(dst_module_id, dst_instance_id, dst_queue, src_queue), nullptr, 0,
          nullptr, 0);

  zx_status_t res = SendIpcWait(&txn);
  if (res != ZX_OK) {
    LOG(ERROR, "IPC error (res %d)\n", res);
    return res;
  }

  if (txn.reply.status() != MsgStatus::IPC_SUCCESS) {
    LOG(ERROR, "BIND (mod %u inst %u -> mod %u inst %u) failed (err %d)\n", src_module_id,
        src_instance_id, dst_module_id, dst_instance_id, to_underlying(txn.reply.status()));
  } else {
    LOG(DEBUG1, "BIND (mod %u inst %u -> mod %u inst %u) success\n", src_module_id, src_instance_id,
        dst_module_id, dst_instance_id);
  }

  return dsp_to_zx_status(txn.reply.status());
}

zx_status_t IntelDspIpc::CreatePipeline(uint8_t instance_id, uint8_t ppl_priority,
                                        uint16_t ppl_mem_size, bool lp) {
  LOG(DEBUG1, "CREATE_PIPELINE (inst %u)\n", instance_id);

  Txn txn(IPC_CREATE_PIPELINE_PRI(instance_id, ppl_priority, ppl_mem_size),
          IPC_CREATE_PIPELINE_EXT(lp), nullptr, 0, nullptr, 0);

  zx_status_t res = SendIpcWait(&txn);
  if (res != ZX_OK) {
    LOG(ERROR, "IPC error (res %d)\n", res);
    return res;
  }

  if (txn.reply.status() != MsgStatus::IPC_SUCCESS) {
    LOG(ERROR, "CREATE_PIPELINE (inst %u) failed (err %d)\n", instance_id,
        to_underlying(txn.reply.status()));
  } else {
    LOG(DEBUG1, "CREATE_PIPELINE (inst %u) success\n", instance_id);
  }

  return dsp_to_zx_status(txn.reply.status());
}

zx_status_t IntelDspIpc::SetPipelineState(uint8_t ppl_id, PipelineState state,
                                          bool sync_stop_start) {
  LOG(DEBUG1, "SET_PIPELINE_STATE (inst %u)\n", ppl_id);

  Txn txn(IPC_SET_PIPELINE_STATE_PRI(ppl_id, state),
          IPC_SET_PIPELINE_STATE_EXT(false, sync_stop_start), nullptr, 0, nullptr, 0);

  zx_status_t res = SendIpcWait(&txn);
  if (res != ZX_OK) {
    LOG(ERROR, "IPC error (res %d)\n", res);
    return res;
  }

  if (txn.reply.status() != MsgStatus::IPC_SUCCESS) {
    LOG(ERROR, "SET_PIPELINE_STATE (inst %u) failed (err %d)\n", ppl_id,
        to_underlying(txn.reply.status()));
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
      LOG(TRACE, "firmware ready\n");
      sync_completion_signal(&fw_ready_completion_);
      break;
    case NotificationType::RESOURCE_EVENT: {
      ResourceEventData data;
      IpcMailboxRead(&data, sizeof(data));
      break;
    }
    default:
      LOG(INFO, "got notification type %u\n", to_underlying(notif.notif_type()));
      break;
  }
}

void IntelDspIpc::ProcessIpcReply(const IpcMessage& reply) {
  fbl::AutoLock ipc_lock(&ipc_lock_);
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

  LOG(DEBUG1, "got reply (status %u) for pending msg, pri 0x%08x ext 0x%08x\n",
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

void IntelDspIpc::ProcessLargeConfigGetReply(Txn* txn) {
  ZX_DEBUG_ASSERT_MSG(txn->request.large_param_id() == txn->reply.large_param_id(),
                      "large_param_id mismatch, expected %u got %u\n",
                      txn->request.large_param_id(), txn->reply.large_param_id());

  LOG(DEBUG1, "got LARGE_CONFIG_GET reply, id %u init_block %d final_block %d data_off_size %u\n",
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

}  // namespace intel_hda
}  // namespace audio
