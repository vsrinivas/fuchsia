// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

#include <cstdint>

#include <fbl/span.h>
#include <intel-hda/utils/intel-audio-dsp-ipc.h>

#include "intel-dsp-ipc.h"

namespace audio {
namespace intel_hda {

// Library & Module Management IPC
zx_status_t DspInitModuleInstance(IntelDspIpc* ipc, uint16_t module_id, uint8_t instance_id,
                                  ProcDomain proc_domain, uint8_t core_id, uint8_t ppl_instance_id,
                                  uint16_t param_block_size, const void* param_data);
zx_status_t DspLargeConfigGet(IntelDspIpc* ipc, uint16_t module_id, uint8_t instance_id,
                              BaseFWParamType large_param_id, fbl::Span<uint8_t> buffer,
                              size_t* bytes_received);
zx_status_t DspBindModules(IntelDspIpc* ipc, uint16_t src_module_id, uint8_t src_instance_id,
                           uint8_t src_queue, uint16_t dst_module_id, uint8_t dst_instance_id,
                           uint8_t dst_queue);

// Pipeline Management IPC
zx_status_t DspCreatePipeline(IntelDspIpc* ipc, uint8_t instance_id, uint8_t ppl_priority,
                              uint16_t ppl_mem_size, bool lp);
zx_status_t DspSetPipelineState(IntelDspIpc* ipc, uint8_t ppl_id, PipelineState state,
                                bool sync_stop_start);

}  // namespace intel_hda
}  // namespace audio
