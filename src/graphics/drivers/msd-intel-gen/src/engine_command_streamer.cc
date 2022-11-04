// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "engine_command_streamer.h"

#include <thread>

#include "cache_config.h"
#include "instructions.h"
#include "magma_util/macros.h"
#include "msd_intel_buffer.h"
#include "msd_intel_connection.h"
#include "platform_logger.h"
#include "platform_trace.h"
#include "register_state_helper.h"
#include "registers.h"
#include "render_init_batch.h"
#include "ringbuffer.h"
#include "workarounds.h"

EngineCommandStreamer::EngineCommandStreamer(Owner* owner, EngineCommandStreamerId id,
                                             uint32_t mmio_base,
                                             std::unique_ptr<GpuMapping> hw_status_page_mapping,
                                             std::unique_ptr<Scheduler> scheduler)
    : owner_(owner),
      id_(id),
      mmio_base_(mmio_base),
      hw_status_page_(id, std::move(hw_status_page_mapping)),
      scheduler_(std::move(scheduler)) {}

const char* EngineCommandStreamer::Name() const {
  switch (id()) {
    case RENDER_COMMAND_STREAMER:
      return "RCS";
    case VIDEO_COMMAND_STREAMER:
      return "VCS";
    default:
      DASSERT(false);
      return "Unknown";
  }
}

bool EngineCommandStreamer::InitContext(MsdIntelContext* context) const {
  DASSERT(context);

  uint32_t context_size = GetContextSize();
  DASSERT(context_size > 0 && magma::is_page_aligned(context_size));

  std::unique_ptr<MsdIntelBuffer> context_buffer(
      MsdIntelBuffer::Create(context_size, "context-buffer"));
  if (!context_buffer)
    return DRETF(false, "couldn't create context buffer");

  const uint32_t kRingbufferSize = 32 * magma::page_size();
  auto ringbuffer =
      std::make_unique<Ringbuffer>(MsdIntelBuffer::Create(kRingbufferSize, "ring-buffer"));
  ringbuffer->Reset(kRingbufferSize - magma::page_size());

  if (!InitContextBuffer(context_buffer.get(), ringbuffer.get(),
                         context->exec_address_space().get()))
    return DRETF(false, "InitContextBuffer failed");

  // Transfer ownership of context_buffer
  context->SetEngineState(id(), std::move(context_buffer), std::move(ringbuffer));

  return true;
}

bool EngineCommandStreamer::InitContextWorkarounds(MsdIntelContext* context) {
  auto ringbuffer = context->get_ringbuffer(id());

  if (!ringbuffer->HasSpace(Workarounds::InstructionBytesRequired()))
    return DRETF(false, "insufficient ringbuffer space for workarounds");

  if (!Workarounds::Init(ringbuffer, id()))
    return DRETF(false, "failed to init workarounds");

  return true;
}

bool EngineCommandStreamer::InitContextCacheConfig(MsdIntelContext* context) {
  auto ringbuffer = context->get_ringbuffer(id());

  if (!ringbuffer->HasSpace(CacheConfig::InstructionBytesRequired()))
    return DRETF(false, "insufficient ringbuffer space for cache config");

  if (!CacheConfig::InitCacheConfig(ringbuffer, id()))
    return DRETF(false, "failed to init cache config buffer");

  return true;
}

void EngineCommandStreamer::InitHardware() {
  auto forcewake = ForceWakeRequest();

  Reset();

  if (DeviceId::is_gen12(owner_->device_id())) {
    // Delay after reset needed for the graphics mode write to take.
    std::this_thread::sleep_for(std::chrono::microseconds(50));

    // Disabling legacy gives us the 12 CSB count (see hardware status page), and seems necessary
    // at least for the video engine.
    registers::GraphicsMode::write(register_io(), mmio_base_,
                                   registers::GraphicsMode::kExeclistDisableLegacyGen11,
                                   registers::GraphicsMode::kExeclistDisableLegacyGen11);

    uint32_t val = registers::GraphicsMode::read(register_io(), mmio_base_);
    DASSERT(val & registers::GraphicsMode::kExeclistDisableLegacyGen11);

    hardware_status_page()->InitContextStatusGen12();
    context_status_read_index_ = GlobalHardwareStatusPage::kStatusQwordsGen12 - 1;

  } else {
    registers::GraphicsMode::write(register_io(), mmio_base_,
                                   registers::GraphicsMode::kExeclistEnableGen9,
                                   registers::GraphicsMode::kExeclistEnableGen9);

    context_status_read_index_ = 0;
  }

  uint32_t gtt_addr = magma::to_uint32(hardware_status_page()->gpu_addr());
  registers::HardwareStatusPageAddress::write(register_io(), mmio_base_, gtt_addr);

  // TODO(fxbug.dev/80908) - switch to engine specific sequence numbers?
  uint32_t initial_sequence_number = sequencer()->next_sequence_number();
  hardware_status_page()->write_sequence_number(initial_sequence_number);

  DLOG("initialized engine sequence number: 0x%x", initial_sequence_number);

  registers::HardwareStatusMask::write(register_io(), mmio_base_,
                                       registers::InterruptRegisterBase::UNMASK,
                                       registers::InterruptRegisterBase::kUserBit |
                                           registers::InterruptRegisterBase::kContextSwitchBit);

  context_switch_pending_ = false;
}

void EngineCommandStreamer::InvalidateTlbs() {
  // Should only be called when gpu is idle.
  switch (id()) {
    case RENDER_COMMAND_STREAMER: {
      auto reg = registers::RenderEngineTlbControl::Get().FromValue(0);
      reg.set_invalidate(1);
      reg.WriteTo(register_io());
      break;
    }
    case VIDEO_COMMAND_STREAMER: {
      auto reg = registers::VideoEngineTlbControl::Get().FromValue(0);
      reg.set_invalidate(1);
      reg.WriteTo(register_io());
      break;
    }
    default:
      DASSERT(false);
      break;
  }
}

void EngineCommandStreamer::InitRegisterState(RegisterStateHelper& helper, Ringbuffer* ringbuffer,
                                              uint64_t ppgtt_pml4_addr) const {
  helper.write_load_register_immediate_headers();
  helper.write_context_save_restore_control();
  helper.write_ring_head_pointer(ringbuffer->head());
  // Ring buffer tail and start is patched in later (see UpdateContext).
  helper.write_ring_tail_pointer(0);
  helper.write_ring_buffer_start(0);
  helper.write_ring_buffer_control(ringbuffer->size());
  helper.write_batch_buffer_upper_head_pointer();
  helper.write_batch_buffer_head_pointer();
  helper.write_batch_buffer_state();
  helper.write_second_level_batch_buffer_upper_head_pointer();
  helper.write_second_level_batch_buffer_head_pointer();
  helper.write_second_level_batch_buffer_state();
  helper.write_batch_buffer_per_context_pointer();
  helper.write_indirect_context_pointer(0, 0);
  helper.write_indirect_context_offset(0);
  helper.write_ccid();
  helper.write_semaphore_token();
  helper.write_context_timestamp();
  helper.write_pdp3_upper(0);
  helper.write_pdp3_lower(0);
  helper.write_pdp2_upper(0);
  helper.write_pdp2_lower(0);
  helper.write_pdp1_upper(0);
  helper.write_pdp1_lower(0);
  helper.write_pdp0_upper(ppgtt_pml4_addr);
  helper.write_pdp0_lower(ppgtt_pml4_addr);

  if (id() == RENDER_COMMAND_STREAMER) {
    helper.write_render_power_clock_state();
  }
}

bool EngineCommandStreamer::InitContextBuffer(MsdIntelBuffer* buffer, Ringbuffer* ringbuffer,
                                              AddressSpace* address_space) const {
  void* addr;
  if (!buffer->platform_buffer()->MapCpu(&addr))
    return DRETF(false, "Couldn't map context buffer");

  uint64_t ppgtt_pml4_addr = address_space->type() == ADDRESS_SPACE_PPGTT
                                 ? static_cast<PerProcessGtt*>(address_space)->get_pml4_bus_addr()
                                 : 0;

  if (DeviceId::is_gen12(owner_->device_id())) {
    RegisterStateHelperGen12 helper(
        id(), mmio_base_, static_cast<uint32_t*>(RegisterStateHelper::register_context_base(addr)));

    InitRegisterState(helper, ringbuffer, ppgtt_pml4_addr);
  } else {
    DASSERT(DeviceId::is_gen9(owner_->device_id()));

    RegisterStateHelperGen9 helper(
        id(), mmio_base_, static_cast<uint32_t*>(RegisterStateHelper::register_context_base(addr)));

    InitRegisterState(helper, ringbuffer, ppgtt_pml4_addr);
  }

  if (!buffer->platform_buffer()->UnmapCpu())
    return DRETF(false, "Couldn't unmap context buffer");

  return true;
}

void EngineCommandStreamer::InitIndirectContext(MsdIntelContext* context,
                                                std::shared_ptr<IndirectContextBatch> batch) {
  uint32_t gtt_addr = magma::to_uint32(batch->GetBatchMapping()->gpu_addr());

  auto register_state = static_cast<uint32_t*>(
      RegisterStateHelper::register_context_base(context->GetCachedContextBufferCpuAddr(id())));

  if (DeviceId::is_gen12(owner_->device_id())) {
    RegisterStateHelperGen12 helper(id(), mmio_base(), register_state);

    helper.write_indirect_context_pointer(gtt_addr, batch->length());
    helper.write_indirect_context_offset(RegisterStateHelperGen12::kIndirectContextOffsetGen12);
  } else {
    DASSERT(DeviceId::is_gen9(owner_->device_id()));
    RegisterStateHelperGen9 helper(id(), mmio_base(), register_state);

    helper.write_indirect_context_pointer(gtt_addr, batch->length());
    helper.write_indirect_context_offset(RegisterStateHelperGen9::kIndirectContextOffsetGen9);
  }

  context->SetIndirectContextBatch(std::move(batch));
}

bool EngineCommandStreamer::SubmitContext(MsdIntelContext* context, uint32_t tail) {
  TRACE_DURATION("magma", "SubmitContext");
  if (!UpdateContext(context, tail))
    return DRETF(false, "UpdateContext failed");

  SubmitExeclists(context);
  return true;
}

bool EngineCommandStreamer::UpdateContext(MsdIntelContext* context, uint32_t tail) {
  gpu_addr_t gpu_addr;
  if (!context->GetRingbufferGpuAddress(id(), &gpu_addr))
    return DRETF(false, "failed to get ringbuffer gpu address");

  uint32_t gtt_addr = magma::to_uint32(gpu_addr);

  RegisterStateHelper helper(id(), mmio_base(),
                             static_cast<uint32_t*>(RegisterStateHelper::register_context_base(
                                 context->GetCachedContextBufferCpuAddr(id()))));

  DLOG("UpdateContext ringbuffer gpu_addr 0x%lx tail 0x%x", gpu_addr, tail);

  helper.write_ring_buffer_start(gtt_addr);
  helper.write_ring_tail_pointer(tail);

  return true;
}

void EngineCommandStreamer::SubmitExeclists(MsdIntelContext* context) {
  TRACE_DURATION("magma", "SubmitExeclists");
  gpu_addr_t gpu_addr;
  if (!context->GetGpuAddress(id(), &gpu_addr)) {
    // Shouldn't happen.
    DASSERT(false);
    gpu_addr = kInvalidGpuAddr;
  }

  auto start = std::chrono::high_resolution_clock::now();

  auto forcewake = ForceWakeRequest();

  for (bool busy = true; busy;) {
    if (DeviceId::is_gen12(owner_->device_id())) {
      auto reg = registers::ExeclistStatusGen12::GetAddr(mmio_base_).ReadFrom(register_io());
      busy = !reg.exec_queue_invalid();
    } else {
      uint64_t status = registers::ExeclistStatusGen9::read(register_io(), mmio_base());

      busy = registers::ExeclistStatusGen9::execlist_write_pointer(status) ==
                 registers::ExeclistStatusGen9::execlist_current_pointer(status) &&
             registers::ExeclistStatusGen9::execlist_queue_full(status);
    }

    if (busy) {
      constexpr uint32_t kTimeoutUs = 100;
      if (std::chrono::duration<double, std::micro>(std::chrono::high_resolution_clock::now() -
                                                    start)
              .count() > kTimeoutUs) {
        MAGMA_LOG(WARNING, "%s: Timeout waiting for execlist port", Name());
        break;
      }
    }
  }

  DLOG("%s: SubmitExeclists context gpu_addr 0x%lx", Name(), gpu_addr);

  if (DeviceId::is_gen12(owner_->device_id())) {
    // We don't have a globally unique context id that fits in 11 bits, so just use an
    // incrementing counter.
    uint32_t context_id = hw_context_id_counter_++;
    if (context_id == 0x7FF) {
      hw_context_id_counter_ = 1;
      context_id = hw_context_id_counter_++;
    }

    registers::ExeclistSubmitQueue::EngineType type;
    switch (id()) {
      case RENDER_COMMAND_STREAMER:
        type = registers::ExeclistSubmitQueue::RENDER;
        break;
      case VIDEO_COMMAND_STREAMER:
        type = registers::ExeclistSubmitQueue::VIDEO;
        break;
    }
    uint64_t descriptor = registers::ExeclistSubmitQueue::context_descriptor(type, /*instance=*/0,
                                                                             context_id, gpu_addr);

    registers::ExeclistSubmitQueue::write(register_io(), mmio_base_, descriptor);
    registers::ExeclistControl::load(register_io(), mmio_base_);

    DLOG("%s: SubmitExeclists loaded gen12 descriptor 0x%016lx context_id 0x%x gpu_addr 0x%lx",
         Name(), descriptor, context_id, gpu_addr);
  } else {
    // Use most significant bits of context gpu_addr as globally unique context id
    uint32_t context_id = magma::to_uint32(gpu_addr >> 12);

    uint64_t descriptor0 = registers::ExeclistSubmitPort::context_descriptor(
        gpu_addr, context_id, context->exec_address_space()->type() == ADDRESS_SPACE_PPGTT);
    uint64_t descriptor1 = 0;

    registers::ExeclistSubmitPort::write(register_io(), mmio_base_, descriptor1, descriptor0);

    DLOG("%s: SubmitExeclists submitted descriptor 0x%016lx context_id 0x%x", Name(), descriptor0,
         context_id);
  }
}

uint64_t EngineCommandStreamer::GetActiveHeadPointer() {
  auto forcewake = ForceWakeRequest();
  return registers::ActiveHeadPointer::read(register_io(), mmio_base_);
}

uint32_t EngineCommandStreamer::GetRingbufferHeadPointer() {
  auto forcewake = ForceWakeRequest();
  return registers::RingbufferHead::read(register_io(), mmio_base_);
}

bool EngineCommandStreamer::Reset() {
  auto forcewake = ForceWakeRequest();

  uint8_t bit;
  switch (id()) {
    case RENDER_COMMAND_STREAMER:
      bit = registers::GraphicsDeviceResetControl::kRcsResetBit;
      break;
    case VIDEO_COMMAND_STREAMER:
      if (DeviceId::is_gen12(owner_->device_id())) {
        bit = registers::GraphicsDeviceResetControl::kVcs0ResetBitGen12;
      } else {
        bit = registers::GraphicsDeviceResetControl::kVcsResetBit;
      }
      break;
  }

  registers::ResetControl::request(register_io(), mmio_base());

  constexpr uint32_t kRetryMs = 10;
  constexpr uint32_t kRetryTimeoutMs = 100;

  auto start = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> elapsed;

  bool ready_for_reset = false;
  do {
    ready_for_reset = registers::ResetControl::ready_for_reset(register_io(), mmio_base());
    if (ready_for_reset) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kRetryMs));
    elapsed = std::chrono::high_resolution_clock::now() - start;
  } while (elapsed.count() < kRetryTimeoutMs);

  bool reset_complete = false;
  if (ready_for_reset) {
    registers::GraphicsDeviceResetControl::initiate_reset(register_io(), bit);
    start = std::chrono::high_resolution_clock::now();

    do {
      reset_complete = registers::GraphicsDeviceResetControl::is_reset_complete(register_io(), bit);
      if (reset_complete) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(kRetryMs));
      elapsed = std::chrono::high_resolution_clock::now() - start;
    } while (elapsed.count() < kRetryTimeoutMs);
  }

  // Always invalidate tlbs, otherwise risk memory corruption.
  InvalidateTlbs();

  DLOG("%s ready_for_reset %d reset_complete %d", Name(), ready_for_reset, reset_complete);

  return DRETF(reset_complete, "Reset did not complete");
}

bool EngineCommandStreamer::StartBatchBuffer(MsdIntelContext* context, gpu_addr_t gpu_addr,
                                             AddressSpaceType address_space_type) {
  auto ringbuffer = context->get_ringbuffer(id());

  uint32_t dword_count = MiBatchBufferStart::kDwordCount + MiNoop::kDwordCount;

  if (!ringbuffer->HasSpace(dword_count * sizeof(uint32_t)))
    return DRETF(false, "ringbuffer has insufficient space");

  MiBatchBufferStart::write(ringbuffer, gpu_addr, address_space_type);
  MiNoop::write(ringbuffer);

  DLOG("started batch buffer 0x%lx address_space_type %d", gpu_addr, address_space_type);

  return true;
}

bool EngineCommandStreamer::ExecBatch(std::unique_ptr<MappedBatch> mapped_batch) {
  TRACE_DURATION("magma", "ExecBatch");
  auto context = mapped_batch->GetContext().lock();
  DASSERT(context);

  if (!MoveBatchToInflight(std::move(mapped_batch)))
    return DRETF(false, "WriteBatchToRingbuffer failed");

  SubmitContext(context.get(), context->get_ringbuffer(id())->tail());
  return true;
}

void EngineCommandStreamer::SubmitBatch(std::unique_ptr<MappedBatch> batch) {
  auto context = batch->GetContext().lock();
  if (!context)
    return;

  context->pending_batch_queue(id_).emplace(std::move(batch));

  scheduler_->CommandBufferQueued(context);

  // It should be possible to submit additional work for the current context without waiting,
  // but I ran into a problem where an execlist submission can be missed leading to a false GPU
  // hang detection; so for now we only submit work when the command streamer is idle.
  if (!context_switch_pending_)
    ScheduleContext();
}

void EngineCommandStreamer::ContextSwitched() {
  std::optional<bool> idle;
  if (DeviceId::is_gen12(owner_->device_id())) {
    hardware_status_page()->ReadContextStatusGen12(context_status_read_index_, &idle);
  } else {
    hardware_status_page()->ReadContextStatus(context_status_read_index_, &idle);
  }

  if (idle) {
    DLOG("%s: idle %d", Name(), *idle);

    if (*idle)
      context_switch_pending_ = false;
  }

  // Because of delays in processing context switch interrupts, we often handle multiple
  // context status events in one shot; however the command completion interrupts may be handled
  // after we process an idle event, so always attempt scheduling here when possible.
  if (!context_switch_pending_)
    ScheduleContext();
}

void EngineCommandStreamer::ScheduleContext() {
  auto context = scheduler_->ScheduleContext();
  if (!context)
    return;

  while (true) {
    auto mapped_batch = std::move(context->pending_batch_queue(id_).front());
    mapped_batch->scheduled();
    context->pending_batch_queue(id_).pop();

    // TODO(fxbug.dev/12764) - MoveBatchToInflight should not fail.  Scheduler should verify there
    // is sufficient room in the ringbuffer before selecting a context. For now, drop the command
    // buffer and try another context.
    if (!MoveBatchToInflight(std::move(mapped_batch))) {
      MAGMA_LOG(WARNING, "MoveBatchToInflight failed");
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

bool EngineCommandStreamer::MoveBatchToInflight(std::unique_ptr<MappedBatch> mapped_batch) {
  auto context = mapped_batch->GetContext().lock();
  DASSERT(context);

  uint32_t sequence_number;
  if (!WriteBatchToRingBuffer(mapped_batch.get(), &sequence_number))
    return DRETF(false, "WriteBatchToRingBuffer failed");

  mapped_batch->SetSequenceNumber(sequence_number);

  uint32_t ringbuffer_offset = context->get_ringbuffer(id())->tail();
  inflight_command_sequences_.emplace(sequence_number, ringbuffer_offset, std::move(mapped_batch));

  progress()->Submitted(sequence_number, std::chrono::steady_clock::now());

  return true;
}

void EngineCommandStreamer::ProcessCompletedCommandBuffers(uint32_t last_completed_sequence) {
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

void EngineCommandStreamer::ResetCurrentContext() {
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

std::vector<MappedBatch*> EngineCommandStreamer::GetInflightBatches() {
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
