// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_qcom_device.h"

#include <platform_barriers.h>

#include <magma_util/macros.h>

#include "instructions.h"
#include "magma_qcom_adreno.h"
#include "msd_qcom_connection.h"
#include "msd_qcom_platform_device.h"
#include "registers.h"

std::unique_ptr<MsdQcomDevice> MsdQcomDevice::Create(void* device_handle) {
  auto device = std::make_unique<MsdQcomDevice>();

  if (!device->Init(device_handle, nullptr))
    return DRETP(nullptr, "Device init failed");

  return device;
}

bool MsdQcomDevice::Init(void* device_handle, std::unique_ptr<magma::RegisterIo::Hook> hook) {
  qcom_platform_device_ = MsdQcomPlatformDevice::Create(device_handle);
  if (!qcom_platform_device_)
    return DRETF(false, "Failed to create platform device from handle: %p", device_handle);

  {
    std::unique_ptr<magma::PlatformMmio> mmio(qcom_platform_device_->platform_device()->CpuMapMmio(
        0, magma::PlatformMmio::CACHE_POLICY_UNCACHED_DEVICE));
    if (!mmio)
      return DRETF(false, "Failed to map mmio");

    register_io_ = std::make_unique<magma::RegisterIo>(std::move(mmio));
    if (!register_io_)
      return DRETF(false, "Failed to create register io");
  }

  if (hook) {
    register_io_->InstallHook(std::move(hook));
  }

  bus_mapper_ = magma::PlatformBusMapper::Create(
      qcom_platform_device_->platform_device()->GetBusTransactionInitiator());

  iommu_ = std::shared_ptr<magma::PlatformIommu>(
      magma::PlatformIommu::Create(qcom_platform_device_->platform_device()->GetIommuConnector()));

  address_space_ = std::make_shared<PartialAllocatingAddressSpace>(
      this, kSystemGpuAddrSize + kClientGpuAddrSize, iommu_);
  if (!address_space_->Init(kSystemGpuAddrBase, kSystemGpuAddrSize))
    return DRETF(false, "Failed to initialize address space");

  firmware_ = Firmware::Create(qcom_platform_device_.get());
  if (!firmware_)
    return DRETF(false, "Couldn't create firmware");

  if (!firmware_->Map(address_space_))
    return DRETF(false, "Failed to map firmware");

  qcom_platform_device_->ResetGmu();

  if (!HardwareInit())
    return DRETF(false, "HardwareInit failed");

  if (!InitRingbuffer())
    return DRETF(false, "InitRingbuffer failed");

  if (!InitControlProcessor())
    return DRETF(false, "InitControlProcessor failed");

  return true;
}

void MsdQcomDevice::GetCpInitPacket(std::vector<uint32_t>& packet) {
  packet = {
      0x0000002f,              // Feature bit flags; parameters (one per line):
      0x00000003,              // multiple contexts
      0x20000000,              // error detection
      0x00000000, 0x00000000,  // disable header dump
      0x00000000,              // no workarounds
      0x00000000, 0x00000000,  // padding
  };
}

bool MsdQcomDevice::InitControlProcessor() {
  std::vector<uint32_t> packet;
  GetCpInitPacket(packet);

  DASSERT(ringbuffer_);
  Packet7::write(ringbuffer_.get(), Packet7::OpCode::CpMeInit, packet);

  uint32_t tail = ringbuffer_->tail() / sizeof(uint32_t);

  FlushRingbuffer(tail);
  if (!WaitForIdleRingbuffer(tail))
    return false;

  // Switch to unsecure mode
  registers::A6xxRbbmSecvidTrustControl::CreateFrom(0).WriteTo(register_io_.get());

  return true;
}

void MsdQcomDevice::FlushRingbuffer(uint32_t tail) {
  DASSERT(ringbuffer_);
  DLOG("Flushing ringbuffer to tail %d", tail);

  magma::barriers::Barrier();

  registers::A6xxCpRingbufferWritePointer::CreateFrom(tail).WriteTo(register_io_.get());
}

bool MsdQcomDevice::WaitForIdleRingbuffer(uint32_t tail) {
  DASSERT(ringbuffer_);

  auto read_ptr = registers::A6xxCpRingbufferReadPointer::CreateFrom(register_io_.get());
  auto rbbm_status = registers::A6xxRbbmStatus::CreateFrom(register_io_.get());

  auto start = std::chrono::steady_clock::now();
  while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                               start)
             .count() < 1000) {
    if (read_ptr.reg_value() == tail) {
      if (rbbm_status.gpu_idle()) {
        DLOG("Idle success: read ptr %d tail %d rbbm_status 0x%x", read_ptr.reg_value(), tail,
             rbbm_status.reg_value());
        return true;
      } else {
        rbbm_status.ReadFrom(register_io_.get());
      }
    } else {
      read_ptr.ReadFrom(register_io_.get());
    }
  }

  auto rbbm_status_int0 = registers::A6xxRbbmStatusInt0::CreateFrom(register_io_.get());
  return DRETF(false, "Failed to idle: read ptr %d tail %d rbbm_status 0x%x rbbm_status_int0 0x%x",
               read_ptr.reg_value(), tail, rbbm_status.reg_value(), rbbm_status_int0.reg_value());
}

bool MsdQcomDevice::InitRingbuffer() {
  static constexpr uint64_t kRingbufferSize = 32 * 1024;
  static constexpr uint64_t kRingbufferBlockSize = 32;

  {
    auto buffer = magma::PlatformBuffer::Create(kRingbufferSize, "ringbuffer");
    if (!buffer)
      return DRETF(false, "Failed to create ringbuffer");

    ringbuffer_ = std::make_unique<Ringbuffer>(std::move(buffer));
  }

  DASSERT(address_space_);
  uint64_t gpu_addr;
  if (!ringbuffer_->Map(address_space_, &gpu_addr))
    return DRETF(false, "Failed to map ringbuffer");

  {
    auto reg = registers::A6xxCpRingbufferControl::CreateFrom(0);
    reg.set(kRingbufferSize, kRingbufferBlockSize);
    reg.disable_read_ptr_update();
    reg.WriteTo(register_io_.get());
  }

  registers::A6xxCpRingbufferBase::CreateFrom(gpu_addr).WriteTo(register_io_.get());

  return true;
}

bool MsdQcomDevice::HardwareInit() {
  if (GetGmemSize() > kGmemGpuAddrSize)
    return DRETF(false, "Incompatible GMEM size: %u > %lu", GetGmemSize(), kGmemGpuAddrSize);

  registers::A6xxRbbmSecvidTsbControl::CreateFrom(0).WriteTo(register_io_.get());

  // Disable trusted memory
  registers::A6xxRbbmSecvidTsbTrustedBase::CreateFrom(0).WriteTo(register_io_.get());
  registers::A6xxRbbmSecvidTsbTrustedSize::CreateFrom(0).WriteTo(register_io_.get());

  if (!EnableClockGating(false))
    return DRETF(false, "EnableHardwareClockGating failed");

  registers::A6xxVbifGateOffWrreqEnable::CreateFrom(0x9).WriteTo(register_io_.get());
  registers::A6xxRbbmVbifClientQosControl::CreateFrom(0x3).WriteTo(register_io_.get());

  // Disable l2 bypass
  registers::A6xxRbbmUcheWriteRangeMax::CreateFrom(0x0001ffffffffffc0).WriteTo(register_io_.get());
  registers::A6xxUcheTrapBase::CreateFrom(0x0001fffffffff000).WriteTo(register_io_.get());
  registers::A6xxUcheWriteThroughBase::CreateFrom(0x0001fffffffff000).WriteTo(register_io_.get());

  registers::A6xxUcheGmemRangeMin::CreateFrom(kGmemGpuAddrBase).WriteTo(register_io_.get());
  registers::A6xxUcheGmemRangeMax::CreateFrom(kGmemGpuAddrBase + GetGmemSize() - 1)
      .WriteTo(register_io_.get());

  registers::A6xxUcheFilterControl::CreateFrom(0x804).WriteTo(register_io_.get());
  registers::A6xxUcheCacheWays::CreateFrom(0x4).WriteTo(register_io_.get());

  registers::A6xxCpRoqThresholds2::CreateFrom(0x010000c0).WriteTo(register_io_.get());
  registers::A6xxCpRoqThresholds1::CreateFrom(0x8040362c).WriteTo(register_io_.get());

  registers::A6xxCpMemPoolSize::CreateFrom(128).WriteTo(register_io_.get());

  registers::A6xxPcDbgEcoControl::CreateFrom(0x300 << 11).WriteTo(register_io_.get());

  // Set AHB default slave response to "ERROR"
  registers::A6xxCpAhbControl::CreateFrom(0x1).WriteTo(register_io_.get());

  registers::A6xxRbbmPerfCounterControl::CreateFrom(0x1).WriteTo(register_io_.get());

  // Always count cycles
  registers::A6xxCpPerfCounterCpSel0::CreateFrom(0).WriteTo(register_io_.get());

  registers::A6xxRbNcModeControl::CreateFrom(2 << 1).WriteTo(register_io_.get());
  registers::A6xxTpl1NcModeControl::CreateFrom(2 << 1).WriteTo(register_io_.get());
  registers::A6xxSpNcModeControl::CreateFrom(2 << 1).WriteTo(register_io_.get());
  registers::A6xxUcheModeControl::CreateFrom(2 << 21).WriteTo(register_io_.get());

  registers::A6xxRbbmInterfaceHangInterruptControl::CreateFrom((1 << 30) | 0x1fffff)
      .WriteTo(register_io_.get());

  registers::A6xxUcheClientPf::CreateFrom(1).WriteTo(register_io_.get());

  // Protect registers from CP
  registers::A6xxCpProtectControl::CreateFrom(0x3).WriteTo(register_io_.get());

  registers::A6xxCpProtect<0>::CreateFrom(
      registers::A6xxCpProtectBase::protect_allow_read(0x600, 0x51))
      .WriteTo(register_io_.get());
  registers::A6xxCpProtect<1>::CreateFrom(registers::A6xxCpProtectBase::protect(0xae50, 0x2))
      .WriteTo(register_io_.get());
  registers::A6xxCpProtect<2>::CreateFrom(registers::A6xxCpProtectBase::protect(0x9624, 0x13))
      .WriteTo(register_io_.get());
  registers::A6xxCpProtect<3>::CreateFrom(registers::A6xxCpProtectBase::protect(0x8630, 0x8))
      .WriteTo(register_io_.get());
  registers::A6xxCpProtect<4>::CreateFrom(registers::A6xxCpProtectBase::protect(0x9e70, 0x1))
      .WriteTo(register_io_.get());
  registers::A6xxCpProtect<5>::CreateFrom(registers::A6xxCpProtectBase::protect(0x9e78, 0x187))
      .WriteTo(register_io_.get());
  registers::A6xxCpProtect<6>::CreateFrom(registers::A6xxCpProtectBase::protect(0xf000, 0x810))
      .WriteTo(register_io_.get());
  registers::A6xxCpProtect<7>::CreateFrom(
      registers::A6xxCpProtectBase::protect_allow_read(0xfc00, 0x3))
      .WriteTo(register_io_.get());
  registers::A6xxCpProtect<8>::CreateFrom(registers::A6xxCpProtectBase::protect(0x50e, 0x0))
      .WriteTo(register_io_.get());
  registers::A6xxCpProtect<9>::CreateFrom(
      registers::A6xxCpProtectBase::protect_allow_read(0x50f, 0x0))
      .WriteTo(register_io_.get());
  registers::A6xxCpProtect<10>::CreateFrom(registers::A6xxCpProtectBase::protect(0x510, 0x0))
      .WriteTo(register_io_.get());
  registers::A6xxCpProtect<11>::CreateFrom(
      registers::A6xxCpProtectBase::protect_allow_read(0x0, 0x4f9))
      .WriteTo(register_io_.get());
  registers::A6xxCpProtect<12>::CreateFrom(
      registers::A6xxCpProtectBase::protect_allow_read(0x501, 0xa))
      .WriteTo(register_io_.get());
  registers::A6xxCpProtect<13>::CreateFrom(
      registers::A6xxCpProtectBase::protect_allow_read(0x511, 0x44))
      .WriteTo(register_io_.get());
  registers::A6xxCpProtect<14>::CreateFrom(registers::A6xxCpProtectBase::protect(0xe00, 0xe))
      .WriteTo(register_io_.get());
  registers::A6xxCpProtect<15>::CreateFrom(registers::A6xxCpProtectBase::protect(0x8e00, 0x0))
      .WriteTo(register_io_.get());
  registers::A6xxCpProtect<16>::CreateFrom(registers::A6xxCpProtectBase::protect(0x8e50, 0xf))
      .WriteTo(register_io_.get());
  registers::A6xxCpProtect<17>::CreateFrom(registers::A6xxCpProtectBase::protect(0xbe02, 0x0))
      .WriteTo(register_io_.get());
  registers::A6xxCpProtect<18>::CreateFrom(registers::A6xxCpProtectBase::protect(0xbe20, 0x11f3))
      .WriteTo(register_io_.get());
  registers::A6xxCpProtect<19>::CreateFrom(registers::A6xxCpProtectBase::protect(0x800, 0x82))
      .WriteTo(register_io_.get());
  registers::A6xxCpProtect<20>::CreateFrom(registers::A6xxCpProtectBase::protect(0x8a0, 0x8))
      .WriteTo(register_io_.get());
  registers::A6xxCpProtect<21>::CreateFrom(registers::A6xxCpProtectBase::protect(0x8ab, 0x19))
      .WriteTo(register_io_.get());
  registers::A6xxCpProtect<22>::CreateFrom(registers::A6xxCpProtectBase::protect(0x900, 0x4d))
      .WriteTo(register_io_.get());
  registers::A6xxCpProtect<23>::CreateFrom(registers::A6xxCpProtectBase::protect(0x98d, 0x76))
      .WriteTo(register_io_.get());
  registers::A6xxCpProtect<24>::CreateFrom(
      registers::A6xxCpProtectBase::protect_allow_read(0x980, 0x4))
      .WriteTo(register_io_.get());
  registers::A6xxCpProtect<25>::CreateFrom(registers::A6xxCpProtectBase::protect(0xa630, 0x0))
      .WriteTo(register_io_.get());

  DASSERT(firmware_);
  registers::A6xxCpSqeInstructionBase::CreateFrom(firmware_->gpu_addr())
      .WriteTo(register_io_.get());

  registers::A6xxCpSqeControl::CreateFrom(1).WriteTo(register_io_.get());

  return true;
}

bool MsdQcomDevice::EnableClockGating(bool enable) {
  uint32_t val = registers::A6xxRbbmClockControl::CreateFrom(register_io_.get()).reg_value();
  if (!enable && val == 0)
    return true;

  return DRETF(false, "EnableClockGating: not implemented: enable %d val 0x%x", enable, val);
}

std::unique_ptr<MsdQcomConnection> MsdQcomDevice::Open(msd_client_id_t client_id) {
  auto address_space =
      std::make_unique<AddressSpace>(this, kClientGpuAddrSize + kSystemGpuAddrSize, iommu_);

  // TODO(fxbug.dev/44003): map firmware and ringbuffers into the client address space.
  // Since we currently have one underlying GPU address space, those entities are visible to
  // the GPU because they are mapped at hardware init.

  return std::make_unique<MsdQcomConnection>(this, client_id, std::move(address_space));
}

//////////////////////////////////////////////////////////////////////////////////////////////////

msd_connection_t* msd_device_open(msd_device_t* device, msd_client_id_t client_id) {
  auto connection = MsdQcomDevice::cast(device)->Open(client_id);
  if (!connection)
    return DRETP(nullptr, "MsdQcomDevice::Open failed");
  return new MsdQcomAbiConnection(std::move(connection));
}

void msd_device_destroy(msd_device_t* device) { delete MsdQcomDevice::cast(device); }

magma_status_t msd_device_query(msd_device_t* device, uint64_t id, uint64_t* value_out) {
  return MsdQcomDevice::cast(device)->Query(id, value_out);
}

magma_status_t MsdQcomDevice::Query(uint64_t id, uint64_t* value_out) const {
  switch (id) {
    case MAGMA_QUERY_VENDOR_ID:
      *value_out = MAGMA_VENDOR_ID_QCOM;
      return MAGMA_STATUS_OK;

    case MAGMA_QUERY_DEVICE_ID:
      *value_out = GetChipId();
      return MAGMA_STATUS_OK;

    case MAGMA_QUERY_IS_TOTAL_TIME_SUPPORTED:
      *value_out = 0;
      return MAGMA_STATUS_OK;

    case kMsdQcomQueryClientGpuAddrRange: {
      constexpr uint64_t size_in_mb = kClientGpuAddrSize / 0x1000000;
      static_assert(size_in_mb * 0x1000000 == kClientGpuAddrSize,
                    "kClientGpuAddrSize is not MB aligned");
      constexpr uint64_t base_in_mb = kClientGpuAddrBase / 0x1000000;
      static_assert(base_in_mb * 0x1000000 == kClientGpuAddrBase,
                    "kClientGpuAddrBase is not MB aligned");
      *value_out = static_cast<uint32_t>(base_in_mb) | (size_in_mb << 32);
      return MAGMA_STATUS_OK;
    }
  }
  return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "unhandled id %" PRIu64, id);
}

magma_status_t msd_device_query_returns_buffer(msd_device_t* device, uint64_t id,
                                               uint32_t* buffer_out) {
  return DRET(MAGMA_STATUS_UNIMPLEMENTED);
}

void msd_device_dump_status(msd_device_t* device, uint32_t dump_type) {}
