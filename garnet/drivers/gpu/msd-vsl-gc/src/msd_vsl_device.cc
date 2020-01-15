// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_vsl_device.h"

#include <chrono>
#include <thread>

#include "instructions.h"
#include "magma_util/macros.h"
#include "magma_vendor_queries.h"
#include "msd.h"
#include "platform_barriers.h"
#include "platform_logger.h"
#include "platform_mmio.h"
#include "platform_thread.h"
#include "registers.h"

static constexpr uint32_t kInterruptIndex = 0;

MsdVslDevice::~MsdVslDevice() {
  DisableInterrupts();

  stop_interrupt_thread_ = true;
  if (interrupt_) {
    interrupt_->Signal();
  }
  if (interrupt_thread_.joinable()) {
    interrupt_thread_.join();
    DLOG("Joined interrupt thread");
  }
}

std::unique_ptr<MsdVslDevice> MsdVslDevice::Create(void* device_handle) {
  auto device = std::make_unique<MsdVslDevice>();

  if (!device->Init(device_handle))
    return DRETP(nullptr, "Failed to initialize device");

  return device;
}

bool MsdVslDevice::Init(void* device_handle) {
  platform_device_ = magma::PlatformDevice::Create(device_handle);
  if (!platform_device_)
    return DRETF(false, "Failed to create platform device");

  std::unique_ptr<magma::PlatformMmio> mmio =
      platform_device_->CpuMapMmio(0, magma::PlatformMmio::CACHE_POLICY_UNCACHED_DEVICE);
  if (!mmio)
    return DRETF(false, "failed to map registers");

  register_io_ = std::make_unique<magma::RegisterIo>(std::move(mmio));

  device_id_ = registers::ChipId::Get().ReadFrom(register_io_.get()).chip_id().get();
  DLOG("Detected vsl chip id 0x%x", device_id_);

  if (device_id_ != 0x7000 && device_id_ != 0x8000)
    return DRETF(false, "Unspported gpu model 0x%x\n", device_id_);

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

  bus_mapper_ = magma::PlatformBusMapper::Create(platform_device_->GetBusTransactionInitiator());
  if (!bus_mapper_)
    return DRETF(false, "failed to create bus mapper");

  page_table_arrays_ = PageTableArrays::Create(bus_mapper_.get());
  if (!page_table_arrays_)
    return DRETF(false, "failed to create page table arrays");

  // TODO(fxb/43043): Implement and test ringbuffer wrapping.
  const uint32_t kRingbufferSize = magma::page_size();
  auto buffer = MsdVslBuffer::Create(kRingbufferSize, "ring-buffer");
  buffer->platform_buffer()->SetCachePolicy(MAGMA_CACHE_POLICY_UNCACHED);
  ringbuffer_ = std::make_unique<Ringbuffer>(std::move(buffer), 0 /* start_offset */);

  Reset();
  if (!HardwareInit()) {
    return DRETF(false, "Failed to initialize hardware");
  }

  interrupt_thread_ = std::thread([this] { this->InterruptThreadLoop(); });

  return true;
}

bool MsdVslDevice::HardwareInit() {
  interrupt_ = platform_device_->RegisterInterrupt(kInterruptIndex);
  if (!interrupt_) {
    return DRETF(false, "Failed to register interrupt");
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

  page_table_slot_allocator_ = std::make_unique<PageTableSlotAllocator>(page_table_arrays_->size());
  return true;
}

void MsdVslDevice::DisableInterrupts() {
  if (!register_io_) {
    DLOG("Register io was not initialized, skipping disabling interrupts");
    return;
  }
  auto reg = registers::IrqEnable::Get().FromValue(0);
  reg.WriteTo(register_io_.get());
}

int MsdVslDevice::InterruptThreadLoop() {
  magma::PlatformThreadHelper::SetCurrentThreadName("VSL InterruptThread");
  DLOG("VSL Interrupt thread started");

  std::unique_ptr<magma::PlatformHandle> profile = platform_device_->GetSchedulerProfile(
      magma::PlatformDevice::kPriorityHigher, "msd-vsl-gc/vsl-interrupt-thread");
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

    auto irq_status = registers::IrqAck::Get().ReadFrom(register_io_.get());
    auto mmu_exception = irq_status.mmu_exception().get();
    auto bus_error = irq_status.bus_error().get();
    auto value = irq_status.value().get();
    if (mmu_exception) {
      DMESSAGE("Interrupt thread received mmu_exception");
    }
    if (bus_error) {
      DMESSAGE("Interrupt thread received bus error");
    }
    // Check which bits are set and complete the corresponding event.
    for (unsigned int i = 0; i < kNumEvents; i++) {
      if (value & (1 << i)) {
        // TODO(fxb/43235): this should be processed on the driver device thread once it exists.
        if (!CompleteInterruptEvent(i)) {
          DLOG("Failed to complete event %u\n", i);
        }
      }
    }
    interrupt_->Complete();
  }
  DLOG("VSL Interrupt thread exiting");
  return 0;
}

bool MsdVslDevice::AllocInterruptEvent(uint32_t* out_event_id) {
  std::lock_guard<std::mutex> lock(events_mutex_);

  for (uint32_t i = 0; i < kNumEvents; i++) {
    if (!events_[i].allocated) {
      events_[i].allocated = true;
      *out_event_id = i;
      return true;
    }
  }
  return DRETF(false, "No events are currently available");
}

bool MsdVslDevice::FreeInterruptEvent(uint32_t event_id) {
  std::lock_guard<std::mutex> lock(events_mutex_);

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
bool MsdVslDevice::WriteInterruptEvent(uint32_t event_id,
                                       std::shared_ptr<magma::PlatformSemaphore> signal) {
  std::lock_guard<std::mutex> lock(events_mutex_);

  if (event_id >= kNumEvents) {
    return DRETF(false, "Invalid event id %u", event_id);
  }
  if (!events_[event_id].allocated) {
    return DRETF(false, "Event id %u was not allocated", event_id);
  }
  if (events_[event_id].submitted) {
    return DRETF(false, "Event id %u was already submitted", event_id);
  }
  events_[event_id].submitted = true;
  events_[event_id].signal = signal;
  MiEvent::write(ringbuffer_.get(), event_id);
  return true;
}

bool MsdVslDevice::CompleteInterruptEvent(uint32_t event_id) {
  std::lock_guard<std::mutex> lock(events_mutex_);

  if (event_id >= kNumEvents) {
    return DRETF(false, "Invalid event id %u", event_id);
  }
  if (!events_[event_id].allocated || !events_[event_id].submitted) {
    return DRETF(false, "Cannot complete event %u, allocated %u submitted %u",
                 event_id, events_[event_id].allocated, events_[event_id].submitted);
  }
  if (events_[event_id].signal) {
    events_[event_id].signal->Signal();
  }
  events_[event_id].submitted = false;
  events_[event_id].signal = nullptr;
  return true;
}

void MsdVslDevice::Reset() {
  DLOG("Reset start");

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

  if (!IsIdle() || !clock_control.idle_3d().get()) {
    MAGMA_LOG(WARNING, "Gpu reset: failed to idle");
  }

  DLOG("Reset complete");
}

bool MsdVslDevice::IsIdle() {
  return registers::IdleState::Get().ReadFrom(register_io_.get()).IsIdle();
}

bool MsdVslDevice::StopRingbuffer() {
  if (IsIdle()) {
    return true;
  }
  // Overwrite the last WAIT with an END.
  bool res =
      ringbuffer_->Overwrite32(kWaitLinkDwords /* dwords_before_tail */, MiEnd::kCommandType);
  if (!res) {
    return DRETF(false, "Failed to overwrite WAIT in ringbuffer");
  }
  return true;
}

bool MsdVslDevice::WaitUntilIdle(uint32_t timeout_ms) {
  auto start = std::chrono::high_resolution_clock::now();
  while (!IsIdle() && std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::high_resolution_clock::now() - start)
                              .count() < timeout_ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return IsIdle();
}

bool MsdVslDevice::LoadInitialAddressSpace(std::shared_ptr<AddressSpace> address_space,
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

  return true;
}

bool MsdVslDevice::SubmitCommandBufferNoMmu(uint64_t bus_addr, uint32_t length,
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

bool MsdVslDevice::StartRingbuffer(std::shared_ptr<AddressSpace> address_space) {
  if (!IsIdle()) {
    return true;  // Already running and looping on WAIT-LINK.
  }
  bool res = ringbuffer_->Map(address_space);
  if (!res) {
    return DRETF(res, "Could not map ringbuffer");
  }
  uint64_t rb_gpu_addr;
  res = ringbuffer_->GetGpuAddress(&rb_gpu_addr);
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

bool MsdVslDevice::AddRingbufferWaitLink() {
  uint64_t rb_gpu_addr;
  bool res = ringbuffer_->GetGpuAddress(&rb_gpu_addr);
  if (!res) {
    return DRETF(false, "Failed to get ringbuffer gpu address");
  }
  uint32_t wait_gpu_addr = rb_gpu_addr + ringbuffer_->tail();
  MiWait::write(ringbuffer_.get());
  MiLink::write(ringbuffer_.get(), 2 /* prefetch */, wait_gpu_addr);
  return true;
}

bool MsdVslDevice::LinkRingbuffer(uint32_t num_new_rb_instructions, uint32_t gpu_addr,
                                  uint32_t dest_prefetch) {
  // Replace the penultimate WAIT (before the newly added one) with a LINK to the command buffer.
  // We need to calculate the offset from the current tail, skipping past the new commands
  // we wrote into the ringbuffer and also the WAIT-LINK that we are modifying.
  uint32_t prev_wait_offset_dwords =
     (num_new_rb_instructions * kInstructionDwords) + kWaitLinkDwords;
  DASSERT(prev_wait_offset_dwords > 0);

  // prev_wait_offset_dwords is pointing to the beginning of the WAIT instruction.
  // We will first modify the second dword which specifies the address,
  // as the hardware may be executing at the address of the current WAIT.
  ringbuffer_->Overwrite32(prev_wait_offset_dwords - 1 /* dwords_before_tail */, gpu_addr);
  magma::barriers::Barrier();
  ringbuffer_->Overwrite32(prev_wait_offset_dwords, MiLink::kCommandType | dest_prefetch);
  magma::barriers::Barrier();
  return true;
}

bool MsdVslDevice::WriteLinkCommand(magma::PlatformBuffer* buf, uint32_t length,
                                    uint16_t link_prefetch, uint32_t link_addr) {
  // Check if we have enough space for the LINK command.
  uint32_t link_instr_size = kInstructionDwords * sizeof(uint32_t);

  if (buf->size() < length + link_instr_size) {
    return DRETF(false, "Buffer does not have %d free bytes for ringbuffer LINK", link_instr_size);
  }

  uint32_t* buf_cpu_addr;
  bool res = buf->MapCpu(reinterpret_cast<void**>(&buf_cpu_addr));
  if (!res) {
    return DRETF(false, "Failed to map command buffer");
  }

  BufferWriter buf_writer(buf_cpu_addr, buf->size(), length);
  MiLink::write(&buf_writer, link_prefetch, link_addr);
  if (!buf->UnmapCpu()) {
    return DRETF(false, "Failed to unmap command buffer");
  }
  return true;
}

// When submitting a command buffer, we modify the following:
//  1) add a LINK from the command buffer to the end of the ringbuffer
//  2) add an EVENT and WAIT-LINK pair to the end of the ringbuffer
//  3) modify the penultimate WAIT in the ringbuffer to LINK to the command buffer
bool MsdVslDevice::SubmitCommandBuffer(std::shared_ptr<AddressSpace> address_space,
                                       uint32_t address_space_index,
                                       magma::PlatformBuffer* buf, uint32_t gpu_addr,
                                       uint32_t length, uint32_t event_id,
                                       std::shared_ptr<magma::PlatformSemaphore> signal,
                                       uint16_t* prefetch_out) {
  // Check if we have loaded an address space and enabled the MMU.
  if (!page_table_arrays_->IsEnabled(register_io())) {
    if (!LoadInitialAddressSpace(address_space, address_space_index)) {
      return DRETF(false, "Failed to load initial address space");
    }
  }
  // Check if we have started the ringbuffer WAIT-LINK loop.
  if (IsIdle()) {
    if (!StartRingbuffer(address_space)) {
      return DRETF(false, "Failed to start ringbuffer");
    }
  }
  // Check if we need to switch address spaces.
  auto mapped_address_space = ringbuffer_->GetMappedAddressSpace().lock();
  // TODO(fxb/43718): support switching address spaces.
  // We will need to keep the previous address space alive until the switch is completed
  // by the hardware.
  if (!mapped_address_space || (mapped_address_space.get() != address_space.get())) {
    return DRETF(false, "Switching ringbuffer contexts not yet supported");
  }
  uint64_t rb_gpu_addr;
  bool res = ringbuffer_->GetGpuAddress(&rb_gpu_addr);
  if (!res) {
    return DRETF(false, "Failed to get ringbuffer gpu address");
  }
  length = magma::round_up(length, sizeof(uint64_t));

  // Number of new commands to be added to the ringbuffer - EVENT WAIT LINK.
  const uint16_t kRbPrefetch = 3;

  // Write a LINK at the end of the command buffer that links back to the ringbuffer.
  if (!WriteLinkCommand(buf, length, kRbPrefetch,
                        static_cast<uint32_t>(rb_gpu_addr + ringbuffer_->tail()))) {
    return DRETF(false, "Failed to write LINK from command buffer to ringbuffer");
  }
  // Increment the command buffer length to account for the LINK command size.
  length += (kInstructionDwords * sizeof(uint32_t));

  uint32_t prefetch = magma::round_up(length, sizeof(uint64_t)) / sizeof(uint64_t);
  if (prefetch & 0xFFFF0000)
    return DRETF(false, "Can't submit length %u (prefetch 0x%x)", length, prefetch);

  *prefetch_out = prefetch & 0xFFFF;

  // Write the new commands to the end of the ringbuffer.
  // Add an EVENT to the end to the ringbuffer.
  if (!WriteInterruptEvent(event_id, signal)) {
    return DRETF(false, "Failed to write interrupt event %u\n", event_id);
  }
  // Add a new WAIT-LINK to the end of the ringbuffer.
  if (!AddRingbufferWaitLink()) {
    return DRETF(false, "Failed to add WAIT-LINK to ringbuffer");
  }

  DLOG("Submitting buffer at gpu addr 0x%x", gpu_addr);

  if (!LinkRingbuffer(kRbPrefetch, gpu_addr, *prefetch_out)) {
    return DRETF(false, "Failed to link ringbuffer");
  }
  return true;
}

std::unique_ptr<MsdVslConnection> MsdVslDevice::Open(msd_client_id_t client_id) {
  uint32_t page_table_array_slot;
  if (!page_table_slot_allocator_->Alloc(&page_table_array_slot))
    return DRETP(nullptr, "couldn't allocate page table slot");

  auto address_space = AddressSpace::Create(this);
  if (!address_space)
    return DRETP(nullptr, "failed to create address space");

  page_table_arrays_->AssignAddressSpace(page_table_array_slot, address_space.get());

  return std::make_unique<MsdVslConnection>(this, page_table_array_slot, std::move(address_space),
                                            client_id);
}

magma_status_t MsdVslDevice::ChipIdentity(magma_vsl_gc_chip_identity* out_identity) {
  if (device_id() != 0x8000) {
    // TODO(fxb/37962): Read hardcoded values from features database instead.
    return DRET_MSG(MAGMA_STATUS_UNIMPLEMENTED, "unhandled device id 0x%x", device_id());
  }
  memset(out_identity, 0, sizeof(*out_identity));
  out_identity->chip_model = device_id();
  out_identity->chip_revision =
      registers::Revision::Get().ReadFrom(register_io_.get()).chip_revision().get();
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

magma_status_t MsdVslDevice::ChipOption(magma_vsl_gc_chip_option* out_option) {
  if (device_id() != 0x8000) {
    // TODO(fxb/37962): Read hardcoded values from features database instead.
    return DRET_MSG(MAGMA_STATUS_UNIMPLEMENTED, "unhandled device id 0x%x", device_id());
  }
  memset(out_option, 0, sizeof(*out_option));
  out_option->gpu_profiler = false;
  out_option->allow_fast_clear = false;
  out_option->power_management = false;
  out_option->enable_mmu = true;
  out_option->compression = kVslGcCompressionOptionNone;
  out_option->usc_l1_cache_ratio = 0;
  out_option->secure_mode = kVslGcSecureModeNormal;
  return MAGMA_STATUS_OK;
}

//////////////////////////////////////////////////////////////////////////////////////////////////

msd_connection_t* msd_device_open(msd_device_t* device, msd_client_id_t client_id) {
  auto connection = MsdVslDevice::cast(device)->Open(client_id);
  if (!connection)
    return DRETP(nullptr, "failed to create connection");
  return new MsdVslAbiConnection(std::move(connection));
}

void msd_device_destroy(msd_device_t* device) { delete MsdVslDevice::cast(device); }

magma_status_t msd_device_query(msd_device_t* device, uint64_t id, uint64_t* value_out) {
  switch (id) {
    case MAGMA_QUERY_VENDOR_ID:
      // VK_VENDOR_ID_VIV
      *value_out = 0x10001;
      return MAGMA_STATUS_OK;

    case MAGMA_QUERY_DEVICE_ID:
      *value_out = MsdVslDevice::cast(device)->device_id();
      return MAGMA_STATUS_OK;

    case MAGMA_QUERY_IS_TOTAL_TIME_SUPPORTED:
      *value_out = 0;
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
    case kMsdVslVendorQueryChipIdentity: {
      magma_vsl_gc_chip_identity result;
      magma_status_t status = MsdVslDevice::cast(device)->ChipIdentity(&result);
      if (status != MAGMA_STATUS_OK) {
        return status;
      }
      return DataToBuffer("chip_identity", &result, sizeof(result), buffer_out);
    }
    case kMsdVslVendorQueryChipOption: {
      magma_vsl_gc_chip_option result;
      magma_status_t status = MsdVslDevice::cast(device)->ChipOption(&result);
      if (status != MAGMA_STATUS_OK) {
        return status;
      }
      return DataToBuffer("chip_option", &result, sizeof(result), buffer_out);
    }
    default:
      return DRET_MSG(MAGMA_STATUS_UNIMPLEMENTED, "unhandled id %" PRIu64, id);
  }
}

void msd_device_dump_status(msd_device_t* device, uint32_t dump_type) {}
