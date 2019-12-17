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
#include "registers.h"

std::unique_ptr<MsdVslDevice> MsdVslDevice::Create(void* device_handle, bool enable_mmu) {
  auto device = std::make_unique<MsdVslDevice>();

  if (!device->Init(device_handle, enable_mmu))
    return DRETP(nullptr, "Failed to initialize device");

  return device;
}

bool MsdVslDevice::Init(void* device_handle, bool enable_mmu) {
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
  HardwareInit(enable_mmu);

  return true;
}

void MsdVslDevice::HardwareInit(bool enable_mmu) {
  {
    auto reg = registers::SecureAhbControl::Get().ReadFrom(register_io_.get());
    reg.non_secure_access().set(1);
    reg.WriteTo(register_io_.get());
  }

  page_table_arrays_->HardwareInit(register_io_.get());
  page_table_arrays_->Enable(register_io(), enable_mmu);

  page_table_slot_allocator_ = std::make_unique<PageTableSlotAllocator>(page_table_arrays_->size());
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

bool MsdVslDevice::SubmitCommandBufferNoMmu(uint64_t bus_addr, uint32_t length,
                                            uint16_t* prefetch_out) {
  if (bus_addr & 0xFFFFFFFF00000000ul)
    return DRETF(false, "Can't submit address > 32 bits without mmu: 0x%08lx", bus_addr);

  uint32_t prefetch = magma::round_up(length, sizeof(uint64_t)) / sizeof(uint64_t);
  if (prefetch & 0xFFFF0000)
    return DRETF(false, "Can't submit length %u (prefetch 0x%x)", length, prefetch);

  *prefetch_out = prefetch & 0xFFFF;

  DLOG("Submitting buffer at bus addr 0x%lx", bus_addr);

  auto reg_cmd_addr = registers::FetchEngineCommandAddress::Get().FromValue(0);
  reg_cmd_addr.addr().set(bus_addr & 0xFFFFFFFF);

  auto reg_cmd_ctrl = registers::FetchEngineCommandControl::Get().FromValue(0);
  reg_cmd_ctrl.enable().set(1);
  reg_cmd_ctrl.prefetch().set(*prefetch_out);

  auto reg_sec_cmd_ctrl = registers::SecureCommandControl::Get().FromValue(0);
  reg_sec_cmd_ctrl.enable().set(1);
  reg_sec_cmd_ctrl.prefetch().set(*prefetch_out);

  reg_cmd_addr.WriteTo(register_io_.get());
  reg_cmd_ctrl.WriteTo(register_io_.get());
  reg_sec_cmd_ctrl.WriteTo(register_io_.get());

  return true;
}

bool MsdVslDevice::InitRingbuffer(std::shared_ptr<AddressSpace> address_space) {
  auto mapped_address_space = ringbuffer_->GetMappedAddressSpace().lock();
  if (mapped_address_space) {
    if (mapped_address_space.get() != address_space.get()) {
      return DRETF(false, "Switching ringbuffer contexts not yet supported");
    }
    // Ringbuffer is already mapped and initialized.
    return true;
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
  reg_cmd_addr.addr().set(static_cast<uint32_t>(rb_gpu_addr) /* WAIT gpu addr */);

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
//  2) add a WAIT-LINK pair to the end of the ringbuffer
//  3) modify the penultimate WAIT in the ringbuffer to LINK to the command buffer
bool MsdVslDevice::SubmitCommandBuffer(std::shared_ptr<AddressSpace> address_space,
                                       magma::PlatformBuffer* buf, uint32_t gpu_addr,
                                       uint32_t length, uint16_t* prefetch_out) {
  if (!InitRingbuffer(address_space)) {
    return DRETF(false, "Error initializing ringbuffer");
  }
  uint64_t rb_gpu_addr;
  bool res = ringbuffer_->GetGpuAddress(&rb_gpu_addr);
  if (!res) {
    return DRETF(false, "Failed to get ringbuffer gpu address");
  }
  length = magma::round_up(length, sizeof(uint64_t));

  // Number of new commands to be added to the ringbuffer. This should be changed once we add
  // more than just WAIT-LINK.
  const uint16_t kRbPrefetch = 2;

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

  // Add a new WAIT-LINK to the end of the ringbuffer.
  uint32_t wait_gpu_addr = rb_gpu_addr + ringbuffer_->tail();
  MiWait::write(ringbuffer_.get());
  MiLink::write(ringbuffer_.get(), 2 /* prefetch */, wait_gpu_addr);

  DLOG("Submitting buffer at gpu addr 0x%x", gpu_addr);

  // Replace the penultimate WAIT (before the newly added one) with a LINK to the command buffer.
  // We need to calculate the offset from the current tail, skipping past the new commands
  // we wrote into the ringbuffer and also the WAIT-LINK that we are modifying.
  uint32_t prev_wait_offset_dwords = (kRbPrefetch * 2) + kWaitLinkDwords;
  DASSERT(prev_wait_offset_dwords > 0);

  // prev_wait_offset_dwords is pointing to the beginning of the WAIT instruction.
  // We will first modify the second dword which specifies the address,
  // as the hardware may be executing at the address of the current WAIT.
  ringbuffer_->Overwrite32(prev_wait_offset_dwords - 1 /* dwords_before_tail */, gpu_addr);
  magma::barriers::Barrier();
  ringbuffer_->Overwrite32(prev_wait_offset_dwords, MiLink::kCommandType | (*prefetch_out));
  magma::barriers::Barrier();

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
