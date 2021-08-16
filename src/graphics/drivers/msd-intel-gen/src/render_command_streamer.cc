// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "render_command_streamer.h"

#include "device_id.h"
#include "instructions.h"
#include "registers.h"

std::unique_ptr<RenderInitBatch> RenderEngineCommandStreamer::CreateRenderInitBatch(
    uint32_t device_id) {
  std::unique_ptr<RenderInitBatch> batch;
  if (DeviceId::is_gen9(device_id)) {
    return std::unique_ptr<RenderInitBatch>(new RenderInitBatchGen9());
  }
  return DRETP(nullptr, "unhandled device id");
}

RenderEngineCommandStreamer::RenderEngineCommandStreamer(EngineCommandStreamer::Owner* owner,
                                                         std::unique_ptr<GpuMapping> hw_status_page)
    : EngineCommandStreamer(owner, RENDER_COMMAND_STREAMER, kRenderEngineMmioBase,
                            std::move(hw_status_page), Scheduler::CreateFifoScheduler()) {}

bool RenderEngineCommandStreamer::RenderInit(std::shared_ptr<MsdIntelContext> context,
                                             std::unique_ptr<RenderInitBatch> init_batch,
                                             std::shared_ptr<AddressSpace> address_space) {
  DASSERT(context);
  DASSERT(init_batch);
  DASSERT(address_space);

  auto buffer = std::unique_ptr<MsdIntelBuffer>(
      MsdIntelBuffer::Create(init_batch->size(), "render-init-batch"));
  if (!buffer)
    return DRETF(false, "failed to allocate render init buffer");

  auto mapping = init_batch->Init(std::move(buffer), address_space);
  if (!mapping)
    return DRETF(false, "batch init failed");

  std::unique_ptr<SimpleMappedBatch> mapped_batch(
      new SimpleMappedBatch(context, std::move(mapping)));

  return ExecBatch(std::move(mapped_batch));
}

bool RenderEngineCommandStreamer::WriteBatchToRingBuffer(MappedBatch* mapped_batch,
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

  uint32_t sequence_number;
  if (!PipeControl(context.get(), mapped_batch->GetPipeControlFlags(), &sequence_number))
    return DRETF(false, "PipeControl failed");

  auto ringbuffer = context->get_ringbuffer(id());

  // TODO(fxbug.dev/12764): don't allocate a sequence number if no space for the user interrupt
  if (!ringbuffer->HasSpace(MiUserInterrupt::kDwordCount * sizeof(uint32_t)))
    return DRETF(false, "ringbuffer has insufficient space");

  MiUserInterrupt::write(ringbuffer);

  *sequence_number_out = sequence_number;

  return true;
}

bool RenderEngineCommandStreamer::PipeControl(MsdIntelContext* context, uint32_t flags,
                                              uint32_t* sequence_number_out) {
  auto ringbuffer = context->get_ringbuffer(id());

  uint32_t dword_count = MiPipeControl::kDwordCount + MiNoop::kDwordCount;

  if (!ringbuffer->HasSpace(dword_count * sizeof(uint32_t)))
    return DRETF(false, "ringbuffer has insufficient space");

  gpu_addr_t gpu_addr =
      hardware_status_page_mapping()->gpu_addr() + GlobalHardwareStatusPage::kSequenceNumberOffset;

  uint32_t sequence_number = sequencer()->next_sequence_number();
  DLOG("writing sequence number update to 0x%x", sequence_number);

  MiPipeControl::write(ringbuffer, sequence_number, gpu_addr, flags);
  MiNoop::write(ringbuffer);

  *sequence_number_out = sequence_number;

  return true;
}
