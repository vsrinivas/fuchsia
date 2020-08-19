// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_vsi_device.h"

#include <chrono>
#include <thread>

#include <fbl/auto_call.h>

#include "address_space_layout.h"
#include "command_buffer.h"
#include "instructions.h"
#include "magma_util/macros.h"
#include "magma_vendor_queries.h"
#include "msd.h"
#include "msd_vsi_context.h"
#include "platform_barriers.h"
#include "platform_logger.h"
#include "platform_mmio.h"
#include "platform_thread.h"
#include "registers.h"

static constexpr uint32_t kInterruptIndex = 0;

class MsdVsiDevice::BatchRequest : public DeviceRequest {
 public:
  BatchRequest(std::unique_ptr<MappedBatch> batch, bool do_flush)
      : batch_(std::move(batch)), do_flush_(do_flush) {}

 protected:
  magma::Status Process(MsdVsiDevice* device) override {
    return device->ProcessBatch(std::move(batch_), do_flush_);
  }

 private:
  std::unique_ptr<MappedBatch> batch_;
  bool do_flush_;
};

class MsdVsiDevice::InterruptRequest : public DeviceRequest {
 public:
  InterruptRequest() {}

 protected:
  magma::Status Process(MsdVsiDevice* device) override { return device->ProcessInterrupt(); }
};

class MsdVsiDevice::DumpRequest : public DeviceRequest {
 public:
  DumpRequest() {}

 protected:
  magma::Status Process(MsdVsiDevice* device) override { return device->ProcessDumpStatusToLog(); }
};

MsdVsiDevice::~MsdVsiDevice() { Shutdown(); }

bool MsdVsiDevice::Shutdown() {
  CHECK_THREAD_NOT_CURRENT(device_thread_id_);

  DisableInterrupts();

  stop_interrupt_thread_ = true;
  if (interrupt_) {
    interrupt_->Signal();
  }
  if (interrupt_thread_.joinable()) {
    interrupt_thread_.join();
    DLOG("Joined interrupt thread");
  }

  stop_device_thread_ = true;

  if (device_request_semaphore_) {
    device_request_semaphore_->Signal();
  }

  if (device_thread_.joinable()) {
    DLOG("joining device thread");
    device_thread_.join();
    DLOG("joined");
  }

  // Ensure hardware is idle.
  if (register_io_) {
    return HardwareReset();
  }

  return true;
}

std::unique_ptr<MsdVsiDevice> MsdVsiDevice::Create(void* device_handle, bool start_device_thread) {
  auto device = std::make_unique<MsdVsiDevice>();

  if (!device->Init(device_handle))
    return DRETP(nullptr, "Failed to initialize device");

  if (start_device_thread)
    device->StartDeviceThread();

  return device;
}

bool MsdVsiDevice::Init(void* device_handle) {
  platform_device_ = MsdVsiPlatformDevice::Create(device_handle);
  if (!platform_device_)
    return DRETF(false, "Failed to create platform device");

  uint32_t mmio_count = platform_device_->platform_device()->GetMmioCount();
  DASSERT(mmio_count > 0);

  std::unique_ptr<magma::PlatformMmio> mmio = platform_device_->platform_device()->CpuMapMmio(
      0, magma::PlatformMmio::CACHE_POLICY_UNCACHED_DEVICE);
  if (!mmio)
    return DRETF(false, "failed to map registers");

  register_io_ = std::make_unique<magma::RegisterIo>(std::move(mmio));

  DASSERT(mmio_count > 1);
  external_sram_ = platform_device_->platform_device()->GetMmioBuffer(mmio_count - 1);
  if (!external_sram_)
    return DRETF(false, "GetMmioBuffer(%d) failed", mmio_count - 1);

  if (!external_sram_->SetCachePolicy(MAGMA_CACHE_POLICY_WRITE_COMBINING))
    return DRETF(false, "Failed setting cache policy on external SRAM");

  device_id_ = registers::ChipId::Get().ReadFrom(register_io_.get()).chip_id().get();
  DLOG("Detected vsi chip id 0x%x", device_id_);

  if (device_id_ != 0x7000 && device_id_ != 0x8000)
    return DRETF(false, "Unspported gpu model 0x%x\n", device_id_);

  revision_ = registers::Revision::Get().ReadFrom(register_io_.get()).chip_revision().get();

  gpu_features_ = std::make_unique<GpuFeatures>(register_io_.get());
  DLOG("gpu features: 0x%x minor features 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
       gpu_features_->features().reg_value(), gpu_features_->minor_features(0),
       gpu_features_->minor_features(1), gpu_features_->minor_features(2),
       gpu_features_->minor_features(3), gpu_features_->minor_features(4),
       gpu_features_->minor_features(5));
  DLOG("halti5: %d mmu: %d", gpu_features_->halti5(), gpu_features_->has_mmu());

  DLOG(
      "stream count %u register_max %u thread_count %u vertex_cache_size %u shader_core_count "
      "%u pixel_pipes %u vertex_output_buffer_size %u\n",
      gpu_features_->stream_count(), gpu_features_->register_max(), gpu_features_->thread_count(),
      gpu_features_->vertex_cache_size(), gpu_features_->shader_core_count(),
      gpu_features_->pixel_pipes(), gpu_features_->vertex_output_buffer_size());
  DLOG("instruction count %u buffer_size %u num_constants %u varyings_count %u\n",
       gpu_features_->instruction_count(), gpu_features_->buffer_size(),
       gpu_features_->num_constants(), gpu_features_->varyings_count());

  if (!gpu_features_->features().pipe_3d().get())
    return DRETF(false, "Gpu has no 3d pipe: features 0x%x\n",
                 gpu_features_->features().reg_value());

  bus_mapper_ = magma::PlatformBusMapper::Create(
      platform_device_->platform_device()->GetBusTransactionInitiator());
  if (!bus_mapper_)
    return DRETF(false, "failed to create bus mapper");

  page_table_arrays_ = PageTableArrays::Create(bus_mapper_.get());
  if (!page_table_arrays_)
    return DRETF(false, "failed to create page table arrays");

  // Add a page to account for ringbuffer overfetch
  uint32_t ringbuffer_size = AddressSpaceLayout::ringbuffer_size() + magma::page_size();
  DASSERT(ringbuffer_size <= AddressSpaceLayout::system_gpu_addr_size());

  auto buffer = MsdVsiBuffer::Create(ringbuffer_size, "ring-buffer");
  buffer->platform_buffer()->SetCachePolicy(MAGMA_CACHE_POLICY_UNCACHED);

  ringbuffer_ =
      std::make_unique<Ringbuffer>(std::move(buffer), AddressSpaceLayout::ringbuffer_size());

  progress_ = std::make_unique<GpuProgress>();

  constexpr uint32_t kFirstSequenceNumber = 0x1;
  sequencer_ = std::make_unique<Sequencer>(kFirstSequenceNumber);

  device_request_semaphore_ = magma::PlatformSemaphore::Create();

  interrupt_ = platform_device_->platform_device()->RegisterInterrupt(kInterruptIndex);
  if (!interrupt_) {
    return DRETF(false, "Failed to register interrupt");
  }

  page_table_slot_allocator_ = std::make_unique<PageTableSlotAllocator>(page_table_arrays_->size());

  HardwareReset();
  HardwareInit();

  return true;
}

void MsdVsiDevice::HardwareInit() {
  {
    auto reg = registers::PulseEater::Get().ReadFrom(register_io_.get());
    reg.disable_internal_dfs().set(1);
    reg.WriteTo(register_io_.get());
  }

  {
    auto reg = registers::IrqEnable::Get().FromValue(~0);
    reg.WriteTo(register_io_.get());
  }

  {
    auto reg = registers::SecureAhbControl::Get().ReadFrom(register_io_.get());
    reg.non_secure_access().set(1);
    reg.WriteTo(register_io_.get());
  }

  page_table_arrays_->HardwareInit(register_io_.get());
}

void MsdVsiDevice::KillCurrentContext() {
  // Get the context of the batch with the lowest sequence number.
  uint32_t min_seq = UINT_MAX;
  std::shared_ptr<MsdVsiContext> context_to_kill;
  for (unsigned int i = 0; i < kNumEvents; i++) {
    if (events_[i].allocated) {
      uint32_t seq_num = events_[i].mapped_batch->GetSequenceNumber();
      if (seq_num < min_seq) {
        min_seq = seq_num;
        context_to_kill = events_[i].mapped_batch->GetContext().lock();
      }
    }
  }
  if (context_to_kill) {
    context_to_kill->Kill();
  }
}

void MsdVsiDevice::Reset() {
  HardwareReset();

  // Save the pending batches that have been posted to the ringbuffer.
  std::vector<DeferredRequest> pending_batches;
  for (unsigned int i = 0; i < kNumEvents; i++) {
    if (events_[i].allocated) {
      auto context = events_[i].mapped_batch->GetContext().lock();
      if (context && !context->killed()) {
        // Since we are going to reset the hardware state, the TLB should be invalidated.
        // |SubmitCommandBuffer| will determine if flushing is required when switching address
        // spaces.
        pending_batches.emplace_back(
            DeferredRequest{std::move(events_[i].mapped_batch), false /* do_flush */});
      }
      CompleteInterruptEvent(i);
    }
  }

  // Ensure the batches will be requeued in the same order.
  std::sort(pending_batches.begin(), pending_batches.end(),
            [](const DeferredRequest& a, const DeferredRequest& b) {
              return a.batch->GetSequenceNumber() < b.batch->GetSequenceNumber();
            });

  // Prepend these batches to the backlog, which is processed before the device request list.
  request_backlog_.insert(request_backlog_.begin(),
                          std::make_move_iterator(pending_batches.begin()),
                          std::make_move_iterator(pending_batches.end()));

  ringbuffer_->Reset(0);
  configured_address_space_ = nullptr;
  progress_ = std::make_unique<GpuProgress>();

  HardwareInit();
}

void MsdVsiDevice::DisableInterrupts() {
  if (!register_io_) {
    DLOG("Register io was not initialized, skipping disabling interrupts");
    return;
  }
  auto reg = registers::IrqEnable::Get().FromValue(0);
  reg.WriteTo(register_io_.get());
}

void MsdVsiDevice::HangCheckTimeout() {
  std::vector<std::string> dump;
  DumpToString(&dump, false /* fault_present */);

  MAGMA_LOG(WARNING, "Suspected GPU hang:");
  MAGMA_LOG(WARNING, "last_interrupt_timestamp %lu", last_interrupt_timestamp_.load());
  for (auto& str : dump) {
    MAGMA_LOG(WARNING, "%s", str.c_str());
  }
  KillCurrentContext();
  Reset();
}

void MsdVsiDevice::StartDeviceThread() {
  DASSERT(!device_thread_.joinable());
  device_thread_ = std::thread([this] { this->DeviceThreadLoop(); });
  interrupt_thread_ = std::thread([this] { this->InterruptThreadLoop(); });
}

int MsdVsiDevice::DeviceThreadLoop() {
  magma::PlatformThreadHelper::SetCurrentThreadName("DeviceThread");

  device_thread_id_ = std::make_unique<magma::PlatformThreadId>();
  CHECK_THREAD_IS_CURRENT(device_thread_id_);

  DLOG("DeviceThreadLoop starting thread 0x%lx", device_thread_id_->id());

  std::unique_ptr<magma::PlatformHandle> profile =
      platform_device_->platform_device()->GetSchedulerProfile(
          magma::PlatformDevice::kPriorityHigher, "msd-vsi-vip/device-thread");
  if (!profile) {
    return DRETF(false, "Failed to get higher priority");
  }
  if (!magma::PlatformThreadHelper::SetProfile(profile.get())) {
    return DRETF(false, "Failed to set priority");
  }

  std::unique_lock<std::mutex> lock(device_request_mutex_, std::defer_lock);

  while (!stop_device_thread_) {
    constexpr uint32_t kTimeoutMs = 5000;

    auto timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
        progress_->GetHangcheckTimeout(kTimeoutMs, std::chrono::steady_clock::now()));
    magma::Status status = device_request_semaphore_->Wait(timeout.count());
    switch (status.get()) {
      case MAGMA_STATUS_OK:
        break;
      case MAGMA_STATUS_TIMED_OUT: {
        // Check that there are no pending device requests.
        lock.lock();
        bool empty = device_request_list_.empty();
        lock.unlock();
        if (empty) {
          HangCheckTimeout();
        }
      } break;
      default:
        MAGMA_LOG(WARNING, "device_request_semaphore_ Wait failed: %d", status.get());
        DASSERT(false);
        // TODO(fxbug.dev/44475): handle wait errors.
    }

    while (true) {
      lock.lock();
      if (!device_request_list_.size()) {
        lock.unlock();
        break;
      }
      auto request = std::move(device_request_list_.front());
      device_request_list_.pop_front();
      lock.unlock();
      request->ProcessAndReply(this);
    }
  }

  DLOG("DeviceThreadLoop exit");
  return 0;
}

void MsdVsiDevice::EnqueueDeviceRequest(std::unique_ptr<DeviceRequest> request) {
  std::unique_lock<std::mutex> lock(device_request_mutex_);
  device_request_list_.emplace_back(std::move(request));
  device_request_semaphore_->Signal();
}

int MsdVsiDevice::InterruptThreadLoop() {
  magma::PlatformThreadHelper::SetCurrentThreadName("VSI InterruptThread");
  DLOG("VSI Interrupt thread started");

  std::unique_ptr<magma::PlatformHandle> profile =
      platform_device_->platform_device()->GetSchedulerProfile(
          magma::PlatformDevice::kPriorityHigher, "msd-vsi-vip/vsi-interrupt-thread");
  if (!profile) {
    return DRETF(0, "Failed to get higher priority");
  }
  if (!magma::PlatformThreadHelper::SetProfile(profile.get())) {
    return DRETF(0, "Failed to set priority");
  }

  while (!stop_interrupt_thread_) {
    interrupt_->Wait();

    if (stop_interrupt_thread_) {
      break;
    }

    last_interrupt_timestamp_ = magma::get_monotonic_ns();

    auto request = std::make_unique<InterruptRequest>();
    auto reply = request->GetReply();
    EnqueueDeviceRequest(std::move(request));
    reply->Wait();
  }
  DLOG("VSI Interrupt thread exiting");
  return 0;
}

magma::Status MsdVsiDevice::ProcessInterrupt() {
  CHECK_THREAD_IS_CURRENT(device_thread_id_);

  auto irq_status = registers::IrqAck::Get().ReadFrom(register_io_.get());
  auto mmu_exception = irq_status.mmu_exception().get();
  auto bus_error = irq_status.bus_error().get();
  auto value = irq_status.value().get();
  bool do_dump = false;
  if (mmu_exception) {
    DMESSAGE("Interrupt thread received mmu_exception");
    do_dump = true;
  }
  if (bus_error) {
    DMESSAGE("Interrupt thread received bus error");
  }
  // Though events complete in order, we may receive a single interrupt for multiple events
  // simultaneously. We should update the ringbuffer head following the event with the
  // highest sequence number.
  uint32_t max_seq_num = 0;
  uint32_t rb_new_head = kInvalidRingbufferOffset;
  // Check which bits are set and complete the corresponding event.
  for (unsigned int i = 0; i < kNumEvents; i++) {
    if (value & (1 << i)) {
      const auto& batch = events_[i].mapped_batch;
      // This should never be null as |WriteInterruptEvent| does not allow it.
      // Ignore it in case it's a spurious interrupt.
      if (!batch) {
        DMESSAGE(
            "Ignoring interrupt, event %u did not have an associated mapped batch, allocated: %d "
            "submitted: %d",
            i, events_[i].allocated, events_[i].submitted);
        do_dump = true;
        continue;
      }

      if (batch->IsCommandBuffer()) {
        auto* buffer = static_cast<CommandBuffer*>(batch.get())->GetBatchBuffer();
        TRACE_VTHREAD_DURATION_END("magma", "Command Buffer", "GPU", buffer->id(),
                                   magma::PlatformTrace::GetCurrentTicks(), "id", buffer->id());
      }

      if (batch->GetSequenceNumber() > max_seq_num) {
        max_seq_num = batch->GetSequenceNumber();
        rb_new_head = events_[i].ringbuffer_offset;
      }
      if (!CompleteInterruptEvent(i)) {
        DMESSAGE("Failed to complete event %u", i);
      }
    }
  }
  if (max_seq_num) {
    DASSERT(rb_new_head != kInvalidRingbufferOffset);
    ringbuffer_->update_head(rb_new_head);
    progress_->Completed(max_seq_num, std::chrono::steady_clock::now());
  } else {
    DMESSAGE("Interrupt thread did not find any interrupt events");
    do_dump = true;
  }
  if (do_dump) {
    std::vector<std::string> dump;
    DumpToString(&dump, mmu_exception /* fault_present */);
    for (auto& str : dump) {
      MAGMA_LOG(WARNING, "%s", str.c_str());
    }
  }
  interrupt_->Complete();

  if (mmu_exception) {
    KillCurrentContext();
    Reset();
  }

  ProcessRequestBacklog();

  return MAGMA_STATUS_OK;
}

magma::Status MsdVsiDevice::ProcessDumpStatusToLog() {
  std::vector<std::string> dump;
  // Faults are detected on the interrupt thread.
  DumpToString(&dump, false /* fault_present */);
  for (auto& str : dump) {
    MAGMA_LOG(INFO, "%s", str.c_str());
  }
  return MAGMA_STATUS_OK;
}

void MsdVsiDevice::ProcessRequestBacklog() {
  CHECK_THREAD_IS_CURRENT(device_thread_id_);

  while (!request_backlog_.empty()) {
    uint32_t event_id;
    if (!AllocInterruptEvent(true /* free_on_complete */, &event_id)) {
      // No more events available, we will continue processing after the next interrupt.
      return;
    }
    // Free the interrupt event if submitting fails.
    auto free_event = fbl::MakeAutoCall([this, event_id]() { FreeInterruptEvent(event_id); });

    auto request = std::move(request_backlog_.front());
    request_backlog_.pop_front();

    auto context = request.batch->GetContext().lock();
    if (!context) {
      DMESSAGE("No context for batch %lu, IsCommandBuffer=%d", request.batch->GetBatchBufferId(),
               request.batch->IsCommandBuffer());
      // If a batch fails, we will drop it and try the next one.
      continue;
    }
    auto address_space = context->exec_address_space();

    if (!SubmitCommandBuffer(context, address_space->page_table_array_slot(), request.do_flush,
                             std::move(request.batch), event_id)) {
      DMESSAGE("Failed to submit command buffer");
      continue;
    }
    free_event.cancel();
  }
}

bool MsdVsiDevice::AllocInterruptEvent(bool free_on_complete, uint32_t* out_event_id) {
  CHECK_THREAD_IS_CURRENT(device_thread_id_);

  for (uint32_t i = 0; i < kNumEvents; i++) {
    if (!events_[i].allocated) {
      events_[i].allocated = true;
      events_[i].free_on_complete = free_on_complete;
      *out_event_id = i;
      return true;
    }
  }
  return DRETF(false, "No events are currently available");
}

bool MsdVsiDevice::FreeInterruptEvent(uint32_t event_id) {
  CHECK_THREAD_IS_CURRENT(device_thread_id_);

  if (event_id >= kNumEvents) {
    return DRETF(false, "Invalid event id %u", event_id);
  }
  if (!events_[event_id].allocated) {
    return DRETF(false, "Event id %u was not allocated", event_id);
  }
  events_[event_id] = {};
  return true;
}

// Writes an event into the end of the ringbuffer.
bool MsdVsiDevice::WriteInterruptEvent(uint32_t event_id, std::unique_ptr<MappedBatch> mapped_batch,
                                       std::shared_ptr<AddressSpace> prev_address_space) {
  CHECK_THREAD_IS_CURRENT(device_thread_id_);

  if (event_id >= kNumEvents) {
    return DRETF(false, "Invalid event id %u", event_id);
  }
  if (!events_[event_id].allocated) {
    return DRETF(false, "Event id %u was not allocated", event_id);
  }
  if (events_[event_id].submitted) {
    return DRETF(false, "Event id %u was already submitted", event_id);
  }
  if (!mapped_batch) {
    return DRETF(false, "No mapped batch was provided");
  }
  events_[event_id].submitted = true;
  events_[event_id].mapped_batch = std::move(mapped_batch);
  events_[event_id].prev_address_space = prev_address_space;
  MiEvent::write(ringbuffer_.get(), event_id);

  // Save the ringbuffer offset immediately after this event.
  events_[event_id].ringbuffer_offset = ringbuffer_->tail();
  return true;
}

bool MsdVsiDevice::CompleteInterruptEvent(uint32_t event_id) {
  CHECK_THREAD_IS_CURRENT(device_thread_id_);

  if (event_id >= kNumEvents) {
    return DRETF(false, "Invalid event id %u", event_id);
  }
  if (!events_[event_id].allocated || !events_[event_id].submitted) {
    return DRETF(false, "Cannot complete event %u, allocated %u submitted %u", event_id,
                 events_[event_id].allocated, events_[event_id].submitted);
  }
  num_events_completed_++;

  bool free_on_complete = events_[event_id].free_on_complete;
  events_[event_id] = {};
  events_[event_id].allocated = !free_on_complete;
  return true;
}

bool MsdVsiDevice::HardwareReset() {
  DLOG("HardwareReset start");

  constexpr uint32_t kResetTimeoutMs = 100;

  auto start = std::chrono::steady_clock::now();

  bool is_idle, is_idle_3d;

  while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                               start)
             .count() < kResetTimeoutMs) {
    auto clock_control = registers::ClockControl::Get().FromValue(0);
    clock_control.isolate_gpu().set(1);
    clock_control.WriteTo(register_io());

    {
      auto reg = registers::SecureAhbControl::Get().FromValue(0);
      reg.reset().set(1);
      reg.WriteTo(register_io_.get());
    }

    std::this_thread::sleep_for(std::chrono::microseconds(100));

    clock_control.soft_reset().set(0);
    clock_control.WriteTo(register_io());

    clock_control.isolate_gpu().set(0);
    clock_control.WriteTo(register_io());

    clock_control = registers::ClockControl::Get().ReadFrom(register_io_.get());

    is_idle = IsIdle();
    is_idle_3d = clock_control.idle_3d().get();

    if (is_idle && is_idle_3d) {
      DLOG("HardwareReset complete");
      return true;
    }
  }

  MAGMA_LOG(WARNING, "Hardware reset failed: is_idle %d is_idle_3d %d", is_idle, is_idle_3d);
  return false;
}

bool MsdVsiDevice::IsIdle() {
  return registers::IdleState::Get().ReadFrom(register_io_.get()).IsIdle();
}

bool MsdVsiDevice::StopRingbuffer() {
  if (IsIdle()) {
    return true;
  }
  // Overwrite the last WAIT with an END.
  uint32_t prev_wait_link = ringbuffer_->SubtractOffset(kWaitLinkDwords * sizeof(uint32_t));
  if (!ringbuffer_->Overwrite32(prev_wait_link, MiEnd::kCommandType)) {
    return DRETF(false, "Failed to overwrite WAIT in ringbuffer");
  }
  return true;
}

bool MsdVsiDevice::WaitUntilIdle(uint32_t timeout_ms) {
  auto start = std::chrono::high_resolution_clock::now();
  while (std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::high_resolution_clock::now() - start)
             .count() < timeout_ms) {
    if (IsIdle()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return false;
}

bool MsdVsiDevice::LoadInitialAddressSpace(std::shared_ptr<MsdVsiContext> context,
                                           uint32_t address_space_index) {
  // Check if we have already configured an address space and enabled the MMU.
  if (page_table_arrays_->IsEnabled(register_io())) {
    return DRETF(false, "MMU already enabled");
  }
  static constexpr uint32_t kPageCount = 1;

  std::unique_ptr<magma::PlatformBuffer> buffer =
      magma::PlatformBuffer::Create(PAGE_SIZE * kPageCount, "address space config");
  if (!buffer) {
    return DRETF(false, "failed to create buffer");
  }

  auto bus_mapping = GetBusMapper()->MapPageRangeBus(buffer.get(), 0, kPageCount);
  if (!bus_mapping) {
    return DRETF(false, "failed to create bus mapping");
  }

  uint32_t* cmd_ptr;
  if (!buffer->MapCpu(reinterpret_cast<void**>(&cmd_ptr))) {
    return DRETF(false, "failed to map command buffer");
  }

  BufferWriter buf_writer(cmd_ptr, buffer->size(), 0);
  auto reg = registers::MmuPageTableArrayConfig::Get().addr();
  MiLoadState::write(&buf_writer, reg, address_space_index);
  MiEnd::write(&buf_writer);

  if (!buffer->UnmapCpu()) {
    return DRETF(false, "failed to unmap cpu");
  }
  if (!buffer->CleanCache(0, PAGE_SIZE * kPageCount, false)) {
    return DRETF(false, "failed to clean buffer cache");
  }

  auto res = SubmitCommandBufferNoMmu(bus_mapping->Get()[0], buf_writer.bytes_written());
  if (!res) {
    return DRETF(false, "failed to submit command buffer");
  }
  constexpr uint32_t kTimeoutMs = 100;
  if (!WaitUntilIdle(kTimeoutMs)) {
    return DRETF(false, "failed to wait for device to be idle");
  }

  page_table_arrays_->Enable(register_io(), true);

  DLOG("Address space loaded, index %u", address_space_index);

  configured_address_space_ = context->exec_address_space();

  return true;
}

bool MsdVsiDevice::SubmitCommandBufferNoMmu(uint64_t bus_addr, uint32_t length,
                                            uint16_t* prefetch_out) {
  if (bus_addr & 0xFFFFFFFF00000000ul)
    return DRETF(false, "Can't submit address > 32 bits without mmu: 0x%08lx", bus_addr);

  uint32_t prefetch = magma::round_up(length, sizeof(uint64_t)) / sizeof(uint64_t);
  if (prefetch & 0xFFFF0000)
    return DRETF(false, "Can't submit length %u (prefetch 0x%x)", length, prefetch);

  prefetch &= 0xFFFF;
  if (prefetch_out) {
    *prefetch_out = prefetch;
  }

  DLOG("Submitting buffer at bus addr 0x%lx", bus_addr);

  auto reg_cmd_addr = registers::FetchEngineCommandAddress::Get().FromValue(0);
  reg_cmd_addr.addr().set(bus_addr & 0xFFFFFFFF);

  auto reg_cmd_ctrl = registers::FetchEngineCommandControl::Get().FromValue(0);
  reg_cmd_ctrl.enable().set(1);
  reg_cmd_ctrl.prefetch().set(prefetch);

  auto reg_sec_cmd_ctrl = registers::SecureCommandControl::Get().FromValue(0);
  reg_sec_cmd_ctrl.enable().set(1);
  reg_sec_cmd_ctrl.prefetch().set(prefetch);

  reg_cmd_addr.WriteTo(register_io_.get());
  reg_cmd_ctrl.WriteTo(register_io_.get());
  reg_sec_cmd_ctrl.WriteTo(register_io_.get());

  return true;
}

bool MsdVsiDevice::StartRingbuffer(std::shared_ptr<MsdVsiContext> context) {
  if (!IsIdle()) {
    return true;  // Already running and looping on WAIT-LINK.
  }
  uint64_t rb_gpu_addr;
  bool res = context->exec_address_space()->GetRingbufferGpuAddress(&rb_gpu_addr);
  if (!res) {
    return DRETF(res, "Could not get ringbuffer gpu address");
  }

  const uint16_t kRbPrefetch = 2;
  // Write the initial WAIT-LINK to the ringbuffer. The LINK points back to the WAIT,
  // and will keep looping until the WAIT is replaced with a LINK on command buffer submission.
  uint32_t wait_gpu_addr = rb_gpu_addr + ringbuffer_->tail();
  MiWait::write(ringbuffer_.get());
  MiLink::write(ringbuffer_.get(), kRbPrefetch, wait_gpu_addr);

  auto reg_cmd_addr = registers::FetchEngineCommandAddress::Get().FromValue(0);
  reg_cmd_addr.addr().set(static_cast<uint32_t>(wait_gpu_addr));

  auto reg_cmd_ctrl = registers::FetchEngineCommandControl::Get().FromValue(0);
  reg_cmd_ctrl.enable().set(1);
  reg_cmd_ctrl.prefetch().set(kRbPrefetch);

  auto reg_sec_cmd_ctrl = registers::SecureCommandControl::Get().FromValue(0);
  reg_sec_cmd_ctrl.enable().set(1);
  reg_sec_cmd_ctrl.prefetch().set(kRbPrefetch);

  reg_cmd_addr.WriteTo(register_io_.get());
  reg_cmd_ctrl.WriteTo(register_io_.get());
  reg_sec_cmd_ctrl.WriteTo(register_io_.get());

  return true;
}

bool MsdVsiDevice::AddRingbufferWaitLink() {
  uint64_t rb_gpu_addr;
  bool res = configured_address_space_->GetRingbufferGpuAddress(&rb_gpu_addr);
  if (!res) {
    return DRETF(false, "Failed to get ringbuffer gpu address");
  }
  uint32_t wait_gpu_addr = rb_gpu_addr + ringbuffer_->tail();
  MiWait::write(ringbuffer_.get());
  MiLink::write(ringbuffer_.get(), 2 /* prefetch */, wait_gpu_addr);
  return true;
}

void MsdVsiDevice::LinkRingbuffer(uint32_t wait_link_offset, uint32_t gpu_addr,
                                  uint32_t dest_prefetch) {
  DASSERT(ringbuffer_->IsOffsetPopulated(wait_link_offset));
  // We can assume the instruction was written as 8 contiguous bytes.
  DASSERT(ringbuffer_->IsOffsetPopulated(wait_link_offset + sizeof(uint32_t)));

  // Replace the penultimate WAIT (before the newly added one) with a LINK to the command buffer.
  // We will first modify the second dword which specifies the address,
  // as the hardware may be executing at the address of the current WAIT.
  ringbuffer_->Overwrite32(wait_link_offset + sizeof(uint32_t), gpu_addr);
  magma::barriers::Barrier();
  ringbuffer_->Overwrite32(wait_link_offset, MiLink::kCommandType | dest_prefetch);
  magma::barriers::Barrier();
}

bool MsdVsiDevice::WriteLinkCommand(magma::PlatformBuffer* buf, uint32_t write_offset,
                                    uint16_t link_prefetch, uint32_t link_addr) {
  // Check if we have enough space for the LINK command.
  uint32_t link_instr_size = kInstructionDwords * sizeof(uint32_t);

  if (buf->size() < write_offset + link_instr_size) {
    return DRETF(false, "Buffer does not have %d free bytes for ringbuffer LINK", link_instr_size);
  }

  uint32_t* buf_cpu_addr;
  bool res = buf->MapCpu(reinterpret_cast<void**>(&buf_cpu_addr));
  if (!res) {
    return DRETF(false, "Failed to map command buffer");
  }

  BufferWriter buf_writer(buf_cpu_addr, buf->size(), write_offset);
  MiLink::write(&buf_writer, link_prefetch, link_addr);
  if (!buf->UnmapCpu()) {
    return DRETF(false, "Failed to unmap command buffer");
  }
  return true;
}

bool MsdVsiDevice::SubmitFlushTlb(std::shared_ptr<MsdVsiContext> context) {
  // It's possible we may need to switch to the address space of |context|. We will use the
  // currently configured address space until the switch occurs. The ringbuffer should already be
  // mapped.
  DASSERT(configured_address_space_);
  uint64_t rb_gpu_addr;
  bool res = configured_address_space_->GetRingbufferGpuAddress(&rb_gpu_addr);
  if (!res) {
    return DRETF(false, "Failed to get ringbuffer gpu address");
  }

  // Save the previous WAIT LINK which will be replaced with a LINK jumping to the new commands.
  uint32_t prev_wait_link = ringbuffer_->SubtractOffset(kWaitLinkDwords * sizeof(uint32_t));

  uint32_t prefetch = kRbInstructionsPerFlush;
  bool switch_address_space =
      configured_address_space_.get() != context->exec_address_space().get();
  if (switch_address_space) {
    // Need to add an additional instruction to load the address space.
    prefetch++;
  }
  // We need to write the new block of ringbuffer instructions contiguously.
  // Since only 30 concurrent events are supported, it should not be possible to run out
  // of space in the ringbuffer.
  bool reserved = ringbuffer_->ReserveContiguous(prefetch * sizeof(uint64_t));
  DASSERT(reserved);

  // Save the gpu address pointing to the new instructions so we can link to it.
  uint32_t new_rb_instructions_start_offset = ringbuffer_->tail();
  uint32_t gpu_addr = rb_gpu_addr + new_rb_instructions_start_offset;

  if (switch_address_space) {
    auto reg = registers::MmuPageTableArrayConfig::Get().addr();
    MiLoadState::write(ringbuffer_.get(), reg,
                       context->exec_address_space()->page_table_array_slot());
    configured_address_space_ = context->exec_address_space();
  }
  auto reg = registers::MmuConfig::Get().addr();
  // The MmuConfig register can also be used to change modes.
  // Instruct the hardware to ignore mode change bits.
  constexpr uint32_t kModeMask = 0x8;
  constexpr uint32_t kFlushAllTlbs = 0x10;
  constexpr uint32_t flush_command = kModeMask | kFlushAllTlbs;
  MiLoadState::write(ringbuffer_.get(), reg, flush_command);
  // These additional bits appear to be needed to ensure the fetch engine waits for any
  // address space change to complete.
  constexpr uint32_t kWaitAddressSpaceChange = 0x3 << 28;
  MiSemaphore::write(ringbuffer_.get(), MiRecipient::FetchEngine, MiRecipient::PixelEngine,
                     kWaitAddressSpaceChange);
  MiStall::write(ringbuffer_.get(), MiRecipient::FetchEngine, MiRecipient::PixelEngine,
                 kWaitAddressSpaceChange);

  if (!AddRingbufferWaitLink()) {
    return DRETF(false, "Failed to add WAIT-LINK to ringbuffer");
  }

  // Verify the number of instructions we just wrote matches the prefetch value
  // of the user buffer's LINK command.
  DASSERT(new_rb_instructions_start_offset ==
          ringbuffer_->SubtractOffset(prefetch * sizeof(uint64_t)));

  DLOG("Submitting flush TLB command");

  LinkRingbuffer(prev_wait_link, gpu_addr, prefetch);

  return true;
}

// When submitting a command buffer, we modify the following:
//  1) add a LINK from the command buffer to the end of the ringbuffer
//  2) add an EVENT and WAIT-LINK pair to the end of the ringbuffer
//  3) modify the penultimate WAIT in the ringbuffer to LINK to the command buffer
bool MsdVsiDevice::SubmitCommandBuffer(std::shared_ptr<MsdVsiContext> context,
                                       uint32_t address_space_index, bool do_flush,
                                       std::unique_ptr<MappedBatch> mapped_batch,
                                       uint32_t event_id) {
  if (context->killed()) {
    return DRETF(false, "Context killed");
  }
  // Check if we have loaded an address space and enabled the MMU.
  bool initial_address_space_loaded = page_table_arrays_->IsEnabled(register_io());
  if (!initial_address_space_loaded) {
    if (!LoadInitialAddressSpace(context, address_space_index)) {
      return DRETF(false, "Failed to load initial address space");
    }
  }
  // Check if we have started the ringbuffer WAIT-LINK loop.
  if (IsIdle()) {
    if (!StartRingbuffer(context)) {
      return DRETF(false, "Failed to start ringbuffer");
    }
  }
  // Check if we need to switch address spaces. We should also save this copy before
  // any possible address space switch happens in |SubmitFlushTlb|.
  auto prev_address_space = configured_address_space_;
  // We always save the last address space the ringbuffer was mapped to, as we need
  // to keep the previous address space alive until the switch is completed by the hardware.
  DASSERT(prev_address_space);
  bool switch_address_space = prev_address_space.get() != context->exec_address_space().get();
  do_flush |= switch_address_space;
  if (do_flush && !SubmitFlushTlb(context)) {
    return DRETF(false, "Failed to submit flush tlb command");
  }
  uint64_t rb_gpu_addr;
  bool res = context->exec_address_space()->GetRingbufferGpuAddress(&rb_gpu_addr);
  if (!res) {
    return DRETF(false, "Failed to get ringbuffer gpu address");
  }
  uint32_t gpu_addr = mapped_batch->GetGpuAddress();
  uint32_t length = magma::round_up(mapped_batch->GetLength(), sizeof(uint64_t));

  // Number of new commands to be added to the ringbuffer - EVENT WAIT LINK.
  const uint16_t kRbPrefetch = kRbInstructionsPerBatch;
  uint32_t prev_wait_link = ringbuffer_->SubtractOffset(kWaitLinkDwords * sizeof(uint32_t));

  // We need to write the new block of ringbuffer instructions contiguously.
  // Since only 30 concurrent events are supported, it should not be possible to run out
  // of space in the ringbuffer.
  bool reserved = ringbuffer_->ReserveContiguous(kRbPrefetch * sizeof(uint64_t));
  DASSERT(reserved);

  // Calculate where to jump to after completion of the command buffer.
  // This will point to EVENT WAIT LINK.
  uint32_t rb_complete_addr = rb_gpu_addr + ringbuffer_->tail();

  bool is_cmd_buf = mapped_batch->IsCommandBuffer();
  if (is_cmd_buf) {
    auto* command_buf = static_cast<CommandBuffer*>(mapped_batch.get());
    magma::PlatformBuffer* buf = command_buf->GetBatchBuffer();

    TRACE_VTHREAD_DURATION_BEGIN("magma", "Command Buffer", "GPU", buf->id(),
                                 magma::PlatformTrace::GetCurrentTicks(), "id", buf->id());

    uint32_t write_offset = command_buf->GetBatchBufferWriteOffset();

    // Write a LINK at the end of the command buffer that links back to the ringbuffer.
    if (!WriteLinkCommand(buf, write_offset, kRbPrefetch, rb_complete_addr)) {
      return DRETF(false, "Failed to write LINK from command buffer to ringbuffer");
    }
    // Increment the command buffer length to account for the LINK command size.
    length += (kInstructionDwords * sizeof(uint32_t));

    auto prev_executed_context = prev_executed_context_.lock();
    if (!prev_executed_context || (prev_executed_context != context)) {
      auto csb = command_buf->GetContextStateBufferResource();
      if (csb) {
        auto csb_mapping = command_buf->GetContextStateBufferMapping();
        DASSERT(csb_mapping);
        // |gpu_addr| and |length| currently point to the command buffer which the ringbuffer
        // will be linking to at the end of this function. We want the ringbuffer to link
        // to the CSB instead, and the CSB to link to the command buffer.
        uint32_t cmd_buf_prefetch = magma::round_up(length, sizeof(uint64_t)) / sizeof(uint64_t);
        if (cmd_buf_prefetch & 0xFFFF0000) {
          return DRETF(false, "Can't submit length %u (prefetch 0x%x)", length, cmd_buf_prefetch);
        }
        // Write a LINK at the end of the context state buffer that links to the command buffer.
        uint32_t csb_length = magma::round_up(csb->length, sizeof(uint64_t));
        bool res = WriteLinkCommand(csb->buffer->platform_buffer(),
                                    csb_length + csb->offset /* write_offset */, cmd_buf_prefetch,
                                    gpu_addr);
        if (!res) {
          return DRETF(false, "Failed to write LINK from context state buffer to command buffer");
        }
        // Update the address the ringbuffer will link to.
        gpu_addr = csb_mapping->gpu_addr();
        length = csb_length + (kInstructionDwords * sizeof(uint32_t));  // Additional LINK size.
      }
    }
  } else {
    // If there is no command buffer, we link directly to the new ringbuffer commands.
    gpu_addr = rb_complete_addr;
    length = kRbPrefetch * sizeof(uint64_t);
  }

  uint32_t prefetch = magma::round_up(length, sizeof(uint64_t)) / sizeof(uint64_t);
  if (prefetch & 0xFFFF0000)
    return DRETF(false, "Can't submit length %u (prefetch 0x%x)", length, prefetch);

  // Write the new commands to the end of the ringbuffer.
  // When adding new instructions, make sure to modify |kRbInstructionsPerBatch| accordingly.
  // Add an EVENT to the end to the ringbuffer.
  uint32_t new_rb_instructions_start = ringbuffer_->tail();
  if (!WriteInterruptEvent(event_id, std::move(mapped_batch), prev_address_space)) {
    return DRETF(false, "Failed to write interrupt event %u\n", event_id);
  }
  // Add a new WAIT-LINK to the end of the ringbuffer.
  if (!AddRingbufferWaitLink()) {
    return DRETF(false, "Failed to add WAIT-LINK to ringbuffer");
  }
  // Verify the number of instructions we just wrote matches the prefetch value
  // of the user buffer's LINK command.
  DASSERT(new_rb_instructions_start ==
          ringbuffer_->SubtractOffset(kRbInstructionsPerBatch * sizeof(uint64_t)));

  DLOG("Submitting buffer at gpu addr 0x%x", gpu_addr);

  LinkRingbuffer(prev_wait_link, gpu_addr, prefetch);

  // Save the context of the last executed command buffer. Since any command buffer may modify
  // hardware state, we should update this even if no command state buffer was provided.
  if (is_cmd_buf) {
    prev_executed_context_ = context;
  }

  return true;
}

std::vector<MappedBatch*> MsdVsiDevice::GetInflightBatches() {
  std::vector<MappedBatch*> inflight_batches;
  inflight_batches.reserve(kNumEvents);
  for (unsigned i = 0; i < kNumEvents; i++) {
    if (events_[i].submitted) {
      DASSERT(events_[i].mapped_batch != nullptr);
      inflight_batches.push_back(events_[i].mapped_batch.get());
    }
  }
  // Sort the batches by sequence number, as the event ids may not correspond to the actual
  // ordering.
  std::sort(inflight_batches.begin(), inflight_batches.end(),
            [](const MappedBatch* a, const MappedBatch* b) {
              return a->GetSequenceNumber() < b->GetSequenceNumber();
            });

  return inflight_batches;
}

void MsdVsiDevice::DumpStatusToLog() { EnqueueDeviceRequest(std::make_unique<DumpRequest>()); }

magma::Status MsdVsiDevice::SubmitBatch(std::unique_ptr<MappedBatch> batch, bool do_flush) {
  DLOG("SubmitBatch");
  CHECK_THREAD_NOT_CURRENT(device_thread_id_);

  EnqueueDeviceRequest(std::make_unique<BatchRequest>(std::move(batch), do_flush));
  return MAGMA_STATUS_OK;
}

magma::Status MsdVsiDevice::ProcessBatch(std::unique_ptr<MappedBatch> batch, bool do_flush) {
  CHECK_THREAD_IS_CURRENT(device_thread_id_);

  auto context = batch->GetContext().lock();
  if (!context) {
    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "No context for batch %lu, IsCommandBuffer=%d",
                    batch->GetBatchBufferId(), batch->IsCommandBuffer());
  }

  uint32_t sequence_number = sequencer_->next_sequence_number();
  batch->SetSequenceNumber(sequence_number);
  progress_->Submitted(sequence_number, std::chrono::steady_clock::now());

  uint32_t event_id;
  if (!AllocInterruptEvent(true /* free_on_complete */, &event_id)) {
    DLOG("No events remaining, deferring execution of command buffer until next interrupt");
    // Not an error, just need to wait for a pending command buffer to complete.
    request_backlog_.emplace_back(DeferredRequest{std::move(batch), do_flush});
    return MAGMA_STATUS_OK;
  }
  if (!SubmitCommandBuffer(context, context->exec_address_space()->page_table_array_slot(),
                           do_flush, std::move(batch), event_id)) {
    FreeInterruptEvent(event_id);
    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Failed to submit command buffer");
  }

  return MAGMA_STATUS_OK;
}

std::unique_ptr<MsdVsiConnection> MsdVsiDevice::Open(msd_client_id_t client_id) {
  uint32_t page_table_array_slot;
  if (!page_table_slot_allocator_->Alloc(&page_table_array_slot))
    return DRETP(nullptr, "couldn't allocate page table slot");

  auto address_space = AddressSpace::Create(this, page_table_array_slot);
  if (!address_space)
    return DRETP(nullptr, "failed to create address space");

  page_table_arrays_->AssignAddressSpace(page_table_array_slot, address_space.get());

  return std::make_unique<MsdVsiConnection>(this, std::move(address_space), client_id);
}

magma_status_t MsdVsiDevice::ChipIdentity(magma_vsi_vip_chip_identity* out_identity) {
  if (device_id() != 0x8000) {
    // TODO(fxbug.dev/37962): Read hardcoded values from features database instead.
    return DRET_MSG(MAGMA_STATUS_UNIMPLEMENTED, "unhandled device id 0x%x", device_id());
  }
  memset(out_identity, 0, sizeof(*out_identity));
  out_identity->chip_model = device_id();
  out_identity->chip_revision = revision();
  out_identity->chip_date =
      registers::ChipDate::Get().ReadFrom(register_io_.get()).chip_date().get();

  out_identity->stream_count = gpu_features_->stream_count();
  out_identity->pixel_pipes = gpu_features_->pixel_pipes();
  out_identity->resolve_pipes = 0x0;
  out_identity->instruction_count = gpu_features_->instruction_count();
  out_identity->num_constants = gpu_features_->num_constants();
  out_identity->varyings_count = gpu_features_->varyings_count();
  out_identity->gpu_core_count = 0x1;

  out_identity->product_id =
      registers::ProductId::Get().ReadFrom(register_io_.get()).product_id().get();
  out_identity->chip_flags = 0x4;
  out_identity->eco_id = registers::EcoId::Get().ReadFrom(register_io_.get()).eco_id().get();
  out_identity->customer_id =
      registers::CustomerId::Get().ReadFrom(register_io_.get()).customer_id().get();
  return MAGMA_STATUS_OK;
}

magma_status_t MsdVsiDevice::ChipOption(magma_vsi_vip_chip_option* out_option) {
  if (device_id() != 0x8000) {
    // TODO(fxbug.dev/37962): Read hardcoded values from features database instead.
    return DRET_MSG(MAGMA_STATUS_UNIMPLEMENTED, "unhandled device id 0x%x", device_id());
  }
  memset(out_option, 0, sizeof(*out_option));
  out_option->gpu_profiler = false;
  out_option->allow_fast_clear = false;
  out_option->power_management = false;
  out_option->enable_mmu = true;
  out_option->compression = kVsiVipCompressionOptionNone;
  out_option->usc_l1_cache_ratio = 0;
  out_option->secure_mode = kVsiVipSecureModeNormal;
  return MAGMA_STATUS_OK;
}

magma_status_t MsdVsiDevice::QuerySram(uint32_t* handle_out) {
  DASSERT(external_sram_);

  if (external_sram_->HasChildren())
    return DRET_MSG(MAGMA_STATUS_ACCESS_DENIED, "External SRAM has children");

  void* ptr;
  if (!external_sram_->MapCpu(&ptr))
    return MAGMA_STATUS_INTERNAL_ERROR;

  // Wipe any previous content
  memset(ptr, 0, external_sram_->size());

  // Client looks for phys addr in the first few bytes
  *reinterpret_cast<uint64_t*>(ptr) = platform_device_->GetExternalSramPhysicalBase();

  external_sram_->UnmapCpu();

  if (!external_sram_->CreateChild(handle_out))
    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "CreateChild failed");

  return MAGMA_STATUS_OK;
}

//////////////////////////////////////////////////////////////////////////////////////////////////

msd_connection_t* msd_device_open(msd_device_t* device, msd_client_id_t client_id) {
  auto connection = MsdVsiDevice::cast(device)->Open(client_id);
  if (!connection)
    return DRETP(nullptr, "failed to create connection");
  return new MsdVsiAbiConnection(std::move(connection));
}

void msd_device_destroy(msd_device_t* device) { delete MsdVsiDevice::cast(device); }

magma_status_t msd_device_query(msd_device_t* device, uint64_t id, uint64_t* value_out) {
  switch (id) {
    case MAGMA_QUERY_VENDOR_ID:
      *value_out = MAGMA_VENDOR_ID_VSI;
      return MAGMA_STATUS_OK;

    case MAGMA_QUERY_DEVICE_ID:
      *value_out = MsdVsiDevice::cast(device)->device_id();
      return MAGMA_STATUS_OK;

    case MAGMA_QUERY_IS_TOTAL_TIME_SUPPORTED:
      *value_out = 0;
      return MAGMA_STATUS_OK;

    case kMsdVsiVendorQueryClientGpuAddrRange:
      uint32_t size_in_pages = AddressSpaceLayout::client_gpu_addr_size() / magma::page_size();
      DASSERT(size_in_pages * magma::page_size() == AddressSpaceLayout::client_gpu_addr_size());
      uint32_t base_in_pages = AddressSpaceLayout::client_gpu_addr_base() / magma::page_size();
      DASSERT(base_in_pages * magma::page_size() == AddressSpaceLayout::client_gpu_addr_base());
      *value_out =
          static_cast<uint64_t>(base_in_pages) | (static_cast<uint64_t>(size_in_pages) << 32);
      return MAGMA_STATUS_OK;
  }
  return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "unhandled id %" PRIu64, id);
}

static magma_status_t DataToBuffer(const char* name, void* data, uint64_t size,
                                   uint32_t* buffer_out) {
  std::unique_ptr<magma::PlatformBuffer> buffer = magma::PlatformBuffer::Create(size, name);
  if (!buffer) {
    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Failed to allocate buffer");
  }
  if (!buffer->Write(data, 0, size)) {
    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Failed to write result to buffer");
  }
  if (!buffer->duplicate_handle(buffer_out)) {
    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Failed to duplicate handle");
  }
  return MAGMA_STATUS_OK;
}

magma_status_t msd_device_query_returns_buffer(msd_device_t* device, uint64_t id,
                                               uint32_t* buffer_out) {
  switch (id) {
    case kMsdVsiVendorQueryChipIdentity: {
      magma_vsi_vip_chip_identity result;
      magma_status_t status = MsdVsiDevice::cast(device)->ChipIdentity(&result);
      if (status != MAGMA_STATUS_OK) {
        return status;
      }
      return DataToBuffer("chip_identity", &result, sizeof(result), buffer_out);
    }

    case kMsdVsiVendorQueryChipOption: {
      magma_vsi_vip_chip_option result;
      magma_status_t status = MsdVsiDevice::cast(device)->ChipOption(&result);
      if (status != MAGMA_STATUS_OK) {
        return status;
      }
      return DataToBuffer("chip_option", &result, sizeof(result), buffer_out);
    }

    case kMsdVsiVendorQueryExternalSram:
      return MsdVsiDevice::cast(device)->QuerySram(buffer_out);

    default:
      return DRET_MSG(MAGMA_STATUS_UNIMPLEMENTED, "unhandled id %" PRIu64, id);
  }
}

void msd_device_dump_status(msd_device_t* device, uint32_t dump_type) {
  MsdVsiDevice::cast(device)->DumpStatusToLog();
}
