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
                            std::move(hw_status_page)) {
  scheduler_ = Scheduler::CreateFifoScheduler();
}

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

bool RenderEngineCommandStreamer::ExecBatch(std::unique_ptr<MappedBatch> mapped_batch) {
  TRACE_DURATION("magma", "ExecBatch");
  auto context = mapped_batch->GetContext().lock();
  DASSERT(context);

  if (!MoveBatchToInflight(std::move(mapped_batch)))
    return DRETF(false, "WriteBatchToRingbuffer failed");

  SubmitContext(context.get(), inflight_command_sequences_.back().ringbuffer_offset());
  return true;
}

bool RenderEngineCommandStreamer::MoveBatchToInflight(std::unique_ptr<MappedBatch> mapped_batch) {
  auto context = mapped_batch->GetContext().lock();
  DASSERT(context);

  gpu_addr_t gpu_addr;
  if (mapped_batch->GetGpuAddress(&gpu_addr)) {
    if (!StartBatchBuffer(context.get(), gpu_addr, context->exec_address_space()->type()))
      return DRETF(false, "failed to emit batch");
  }

  uint32_t sequence_number;
  if (!PipeControl(context.get(), mapped_batch->GetPipeControlFlags(), &sequence_number))
    return DRETF(false, "PipeControl failed");

  auto ringbuffer = context->get_ringbuffer(id());

  // TODO: don't allocate a sequence number if we don't have space for the user interrupt
  if (!ringbuffer->HasSpace(MiUserInterrupt::kDwordCount * sizeof(uint32_t)))
    return DRETF(false, "ringbuffer has insufficient space");

  MiUserInterrupt::write(ringbuffer);

  mapped_batch->SetSequenceNumber(sequence_number);

  uint32_t ringbuffer_offset = context->get_ringbuffer(id())->tail();
  inflight_command_sequences_.emplace(sequence_number, ringbuffer_offset, std::move(mapped_batch));

  progress()->Submitted(sequence_number, std::chrono::steady_clock::now());

  return true;
}

void RenderEngineCommandStreamer::ContextSwitched() {
  context_switch_pending_ = false;
  ScheduleContext();
}

void RenderEngineCommandStreamer::ScheduleContext() {
  auto context = scheduler_->ScheduleContext();
  if (!context)
    return;

  while (true) {
    auto mapped_batch = std::move(context->pending_batch_queue().front());
    mapped_batch->scheduled();
    context->pending_batch_queue().pop();

    // TODO(fxbug.dev/12764) - MoveBatchToInflight should not fail.  Scheduler should verify there
    // is sufficient room in the ringbuffer before selecting a context. For now, drop the command
    // buffer and try another context.
    if (!MoveBatchToInflight(std::move(mapped_batch))) {
      MAGMA_LOG(WARNING, "ExecBatch failed");
      break;
    }

    // Scheduler returns nullptr when its time to switch contexts
    auto next_context = scheduler_->ScheduleContext();
    if (next_context == nullptr)
      break;
    DASSERT(context == next_context);
  }

  SubmitContext(context.get(), inflight_command_sequences_.back().ringbuffer_offset());
  context_switch_pending_ = true;
}

void RenderEngineCommandStreamer::SubmitBatch(std::unique_ptr<MappedBatch> batch) {
  auto context = batch->GetContext().lock();
  if (!context)
    return;

  context->pending_batch_queue().emplace(std::move(batch));

  scheduler_->CommandBufferQueued(context);

  if (!context_switch_pending_)
    ScheduleContext();
}

void RenderEngineCommandStreamer::ProcessCompletedCommandBuffers(uint32_t last_completed_sequence) {
  // pop all completed command buffers
  while (!inflight_command_sequences_.empty() &&
         inflight_command_sequences_.front().sequence_number() <= last_completed_sequence) {
    InflightCommandSequence& sequence = inflight_command_sequences_.front();

    DLOG(
        "ProcessCompletedCommandBuffers popping inflight command sequence with "
        "sequence_number 0x%x "
        "ringbuffer_start_offset 0x%x",
        sequence.sequence_number(), sequence.ringbuffer_offset());

    auto context = sequence.GetContext().lock();
    DASSERT(context);
    context->get_ringbuffer(id())->update_head(sequence.ringbuffer_offset());

    // NOTE: The order of the following lines matter.
    //
    // We need to pop() before telling the scheduler we're done so that the
    // flow events in the Command Buffer destructor happens before the
    // Context Exec virtual duration event is over.
    bool was_scheduled = sequence.mapped_batch()->was_scheduled();
    inflight_command_sequences_.pop();

    if (was_scheduled) {
      scheduler_->CommandBufferCompleted(context);
    }
  }

  progress()->Completed(last_completed_sequence, std::chrono::steady_clock::now());
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

void RenderEngineCommandStreamer::ResetCurrentContext() {
  DLOG("ResetCurrentContext");

  if (!inflight_command_sequences_.empty()) {
    auto context = inflight_command_sequences_.front().GetContext().lock();
    DASSERT(context);

    // Cleanup resources for any inflight command sequences on this context
    while (!inflight_command_sequences_.empty()) {
      auto& sequence = inflight_command_sequences_.front();
      if (sequence.mapped_batch()->was_scheduled())
        scheduler_->CommandBufferCompleted(inflight_command_sequences_.front().GetContext().lock());
      inflight_command_sequences_.pop();
    }

    progress()->Reset();

    context->Kill();
  }
}

std::vector<MappedBatch*> RenderEngineCommandStreamer::GetInflightBatches() {
  size_t num_sequences = inflight_command_sequences_.size();
  std::vector<MappedBatch*> inflight_batches;
  inflight_batches.reserve(num_sequences);
  for (uint32_t i = 0; i < num_sequences; i++) {
    auto sequence = std::move(inflight_command_sequences_.front());
    inflight_batches.push_back(sequence.mapped_batch());

    // Pop off the front and push to the back
    inflight_command_sequences_.pop();
    inflight_command_sequences_.push(std::move(sequence));
  }
  return inflight_batches;
}
