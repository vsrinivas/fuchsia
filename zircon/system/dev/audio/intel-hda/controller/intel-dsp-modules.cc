// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-dsp-modules.h"

#include <cstdint>

#include <fbl/span.h>
#include <intel-hda/utils/intel-audio-dsp-ipc.h>
#include <intel-hda/utils/intel-hda-registers.h>
#include <intel-hda/utils/utils.h>

#include "debug-logging.h"
#include "intel-dsp-ipc.h"

namespace audio {
namespace intel_hda {

namespace {
zx_status_t dsp_to_zx_status(MsgStatus status) {
  return (status == MsgStatus::IPC_SUCCESS) ? ZX_OK : ZX_ERR_INTERNAL;
}
}  // namespace

zx_status_t DspInitModuleInstance(IntelDspIpc* ipc, uint16_t module_id, uint8_t instance_id,
                                  ProcDomain proc_domain, uint8_t core_id, uint8_t ppl_instance_id,
                                  uint16_t param_block_size, const void* param_data) {
  GLOBAL_LOG(DEBUG1, "INIT_INSTANCE (mod %u inst %u)\n", module_id, instance_id);

  Status result = ipc->SendWithData(
      IPC_PRI(MsgTarget::MODULE_MSG, MsgDir::MSG_REQUEST, ModuleMsgType::INIT_INSTANCE, instance_id,
              module_id),
      IPC_INIT_INSTANCE_EXT(proc_domain, core_id, ppl_instance_id, param_block_size),
      fbl::Span(static_cast<const uint8_t*>(param_data), param_block_size), fbl::Span<uint8_t>(),
      nullptr);
  if (!result.ok()) {
    GLOBAL_LOG(ERROR, "INIT_INSTANCE (mod %u inst %u) failed: %s\n", module_id, instance_id,
               result.ToString().c_str());
    return result.code();
  }

  GLOBAL_LOG(DEBUG1, "INIT_INSTANCE (mod %u inst %u) success\n", module_id, instance_id);
  return ZX_OK;
}

zx_status_t DspLargeConfigGet(IntelDspIpc* ipc, uint16_t module_id, uint8_t instance_id,
                              BaseFWParamType large_param_id, fbl::Span<uint8_t> buffer,
                              size_t* bytes_received) {
  GLOBAL_LOG(DEBUG1, "LARGE_CONFIG_GET (mod %u inst %u large_param_id %u)\n", module_id,
             instance_id, to_underlying(large_param_id));

  if (buffer.size_bytes() > IPC_EXT_DATA_OFF_MAX_SIZE) {
    buffer = buffer.subspan(0, IPC_EXT_DATA_OFF_MAX_SIZE);
  }

  size_t bytes_received_local;
  Status result =
      ipc->SendWithData(IPC_PRI(MsgTarget::MODULE_MSG, MsgDir::MSG_REQUEST,
                                ModuleMsgType::LARGE_CONFIG_GET, instance_id, module_id),
                        IPC_LARGE_CONFIG_EXT(true, false, to_underlying(large_param_id),
                                             static_cast<uint32_t>(buffer.size())),
                        fbl::Span<const uint8_t>(), buffer, &bytes_received_local);
  if (!result.ok()) {
    GLOBAL_LOG(ERROR, "LARGE_CONFIG_GET (mod %u inst %u large_param_id %u) failed: %s\n", module_id,
               instance_id, to_underlying(large_param_id), result.ToString().c_str());
    return result.code();
  }

  GLOBAL_LOG(DEBUG1,
             "LARGE_CONFIG_GET (mod %u inst %u large_param_id %u) success: received %ld byte(s).\n",
             module_id, instance_id, to_underlying(large_param_id), bytes_received_local);
  if (bytes_received != nullptr) {
    *bytes_received = bytes_received_local;
  }
  return ZX_OK;
}

zx_status_t DspBindModules(IntelDspIpc* ipc, uint16_t src_module_id, uint8_t src_instance_id,
                           uint8_t src_queue, uint16_t dst_module_id, uint8_t dst_instance_id,
                           uint8_t dst_queue) {
  GLOBAL_LOG(DEBUG1, "BIND (mod %u inst %u -> mod %u inst %u)\n", src_module_id, src_instance_id,
             dst_module_id, dst_instance_id);

  Status result =
      ipc->Send(IPC_PRI(MsgTarget::MODULE_MSG, MsgDir::MSG_REQUEST, ModuleMsgType::BIND,
                        src_instance_id, src_module_id),
                IPC_BIND_UNBIND_EXT(dst_module_id, dst_instance_id, dst_queue, src_queue));
  if (!result.ok()) {
    GLOBAL_LOG(ERROR, "BIND (mod %u inst %u -> mod %u inst %u) failed: %s\n", src_module_id,
               src_instance_id, dst_module_id, dst_instance_id, result.ToString().c_str());
    return result.code();
  }

  GLOBAL_LOG(DEBUG1, "BIND (mod %u inst %u -> mod %u inst %u) success\n", src_module_id,
             src_instance_id, dst_module_id, dst_instance_id);
  return ZX_OK;
}

zx_status_t DspCreatePipeline(IntelDspIpc* ipc, uint8_t instance_id, uint8_t ppl_priority,
                              uint16_t ppl_mem_size, bool lp) {
  GLOBAL_LOG(DEBUG1, "CREATE_PIPELINE (inst %u)\n", instance_id);

  Status result = ipc->Send(IPC_CREATE_PIPELINE_PRI(instance_id, ppl_priority, ppl_mem_size),
                            IPC_CREATE_PIPELINE_EXT(lp));
  if (!result.ok()) {
    GLOBAL_LOG(ERROR, "CREATE_PIPELINE (inst %u) failed: %s\n", instance_id,
               result.ToString().c_str());
    return result.code();
  }

  GLOBAL_LOG(DEBUG1, "CREATE_PIPELINE (inst %u) success\n", instance_id);
  return ZX_OK;
}

zx_status_t DspSetPipelineState(IntelDspIpc* ipc, uint8_t ppl_id, PipelineState state,
                                bool sync_stop_start) {
  GLOBAL_LOG(DEBUG1, "SET_PIPELINE_STATE (inst %u)\n", ppl_id);

  Status result = ipc->Send(IPC_SET_PIPELINE_STATE_PRI(ppl_id, state),
                            IPC_SET_PIPELINE_STATE_EXT(false, sync_stop_start));
  if (!result.ok()) {
    GLOBAL_LOG(ERROR, "SET_PIPELINE_STATE (inst %u) failed: %s\n", ppl_id,
               result.ToString().c_str());
    return result.code();
  }

  GLOBAL_LOG(DEBUG1, "SET_PIPELINE_STATE (inst %u) success\n", ppl_id);
  return ZX_OK;
}

}  // namespace intel_hda
}  // namespace audio
