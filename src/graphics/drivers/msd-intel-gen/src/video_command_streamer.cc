// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "video_command_streamer.h"

#include "instructions.h"

VideoCommandStreamer::VideoCommandStreamer(EngineCommandStreamer::Owner* owner,
                                           std::unique_ptr<GpuMapping> hw_status_page)
    : EngineCommandStreamer(
          owner, VIDEO_COMMAND_STREAMER,
          DeviceId::is_gen12(owner->device_id()) ? kVideoEngineMmioBaseGen12 : kVideoEngineMmioBase,
          std::move(hw_status_page), Scheduler::CreateFifoScheduler()) {
  if (DeviceId::is_gen12(owner->device_id())) {
    set_forcewake_domain(ForceWakeDomain::GEN12_VDBOX0);
  } else {
    set_forcewake_domain(ForceWakeDomain::GEN9_MEDIA);
  }
}

bool VideoCommandStreamer::WriteBatchToRingBuffer(MappedBatch* mapped_batch,
                                                  uint32_t* sequence_number_out) {
  auto context = mapped_batch->GetContext().lock();
  DASSERT(context);

  {
    gpu_addr_t gpu_addr;
    // Some "mapped batches" have no batch
    if (mapped_batch->GetGpuAddress(&gpu_addr)) {
      if (!StartBatchBuffer(context.get(), gpu_addr, context->exec_address_space()->type()))
        return DRETF(false, "failed to emit batch");
    }
  }

  auto ringbuffer = context->get_ringbuffer(id());

  if (!ringbuffer->HasSpace((MiFlush::kDwordCount + MiUserInterrupt::kDwordCount) *
                            sizeof(uint32_t)))
    return DRETF(false, "ringbuffer has insufficient space");

  uint32_t sequence_number = sequencer()->next_sequence_number();

  {
    gpu_addr_t gpu_addr =
        hardware_status_page()->gpu_addr() + GlobalHardwareStatusPage::kSequenceNumberOffset;

    MiFlush::write(ringbuffer, sequence_number, ADDRESS_SPACE_GGTT, gpu_addr);
  }

  MiUserInterrupt::write(ringbuffer);

  *sequence_number_out = sequence_number;

  return true;
}
