// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "performance_counters.h"

#include "platform_barriers.h"
#include "platform_logger.h"
#include "registers.h"

namespace {
constexpr uint32_t kPerfBufferSize = PAGE_SIZE * 4;

// Start of the buffer in the GPU address space.
constexpr uint32_t kPerfBufferStartOffset = PAGE_SIZE;
}  // namespace

bool PerformanceCounters::Enable() {
  if (counter_state_ == PerformanceCounterState::kTriggeredWillBeDisabled) {
    // Signal that the counters should be left enabled.
    counter_state_ = PerformanceCounterState::kTriggered;
    return true;
  }
  if (counter_state_ != PerformanceCounterState::kDisabled) {
    MAGMA_LOG(WARNING, "Can't enable performance counters from state %d",
              static_cast<int>(counter_state_));
    return false;
  }
  if (!connection_) {
    auto connection = MsdArmConnection::Create(0xffffffff, owner_->connection_owner());
    if (!connection) {
      return DRETF(false, "Unable to create perf counter connection");
    }
    std::shared_ptr<MsdArmBuffer> buffer(
        MsdArmBuffer::Create(kPerfBufferSize, "performance_counter_buffer"));
    if (!buffer) {
      return DRETF(false, "Unable to create perf counter buffer");
    }
    auto gpu_mapping = std::make_unique<GpuMapping>(
        kPerfBufferStartOffset, 0, kPerfBufferSize,
        MAGMA_GPU_MAP_FLAG_WRITE | MAGMA_GPU_MAP_FLAG_READ | kMagmaArmMaliGpuMapFlagInnerShareable,
        connection.get(), buffer);
    bool result = connection->AddMapping(std::move(gpu_mapping));
    if (!result) {
      return DRETF(false, "Unable to map perf counter buffer");
    }
    result = buffer->SetCommittedPages(0, kPerfBufferSize / PAGE_SIZE);
    if (!connection) {
      return DRETF(false, "Unable to commit pages for perf counter buffer");
    }
    // Keep mapped on the CPU forever.
    void* cpu_map;
    if (!buffer->platform_buffer()->MapCpu(&cpu_map)) {
      return DRETF(false, "Failed to map perf counter buffer");
    }
    buffer->platform_buffer()->CleanCache(0, kPerfBufferSize, true);
    connection_ = connection;
    buffer_ = buffer;
  }

  if (!address_mapping_) {
    auto mapping = owner_->address_manager()->AllocateMappingForAddressSpace(connection_);
    if (!mapping) {
      return DRETF(false, "Unable to map perf counter address space to GPU");
    }
    address_mapping_ = mapping;
  }
  // Ensure the cache flush or any previous read completes before signaling the hardware to
  // write into the buffer.
  magma::barriers::Barrier();

  auto base = registers::PerformanceCounterBase::Get().FromValue(kPerfBufferStartOffset);
  base.WriteTo(owner_->register_io());
  last_perf_base_ =
      registers::PerformanceCounterBase::Get().ReadFrom(owner_->register_io()).reg_value();

  // Enable every performance counter
  registers::PerformanceCounterJmEnable::Get()
      .FromValue(0xffffffffu)
      .WriteTo(owner_->register_io());
  registers::PerformanceCounterTilerEnable::Get()
      .FromValue(0xffffffffu)
      .WriteTo(owner_->register_io());
  registers::PerformanceCounterShaderEnable::Get()
      .FromValue(0xffffffffu)
      .WriteTo(owner_->register_io());
  registers::PerformanceCounterMmuL2Enable::Get()
      .FromValue(0xffffffffu)
      .WriteTo(owner_->register_io());

  auto config = registers::PerformanceCounterConfig::Get().FromValue(0);
  config.address_space().set(address_mapping_->slot_number());
  config.mode().set(registers::PerformanceCounterConfig::kModeManual);
  config.WriteTo(owner_->register_io());
  counter_state_ = PerformanceCounterState::kEnabled;
  enable_time_ = std::chrono::steady_clock::now();
  return true;
}

bool PerformanceCounters::TriggerRead() {
  if (counter_state_ != PerformanceCounterState::kEnabled) {
    MAGMA_LOG(WARNING, "Can't trigger performance counters from state %d",
              static_cast<int>(counter_state_));
    return false;
  }
  last_perf_base_ =
      registers::PerformanceCounterBase::Get().ReadFrom(owner_->register_io()).reg_value();
  owner_->register_io()->Write32(registers::GpuCommand::kOffset,
                                 registers::GpuCommand::kCmdSamplePerformanceCounters);
  counter_state_ = PerformanceCounterState::kTriggered;
  return true;
}

void PerformanceCounters::ReadCompleted() {
  std::vector<uint32_t> output;
  if (counter_state_ == PerformanceCounterState::kTriggeredWillBeDisabled) {
    auto config = registers::PerformanceCounterConfig::Get().FromValue(0);
    config.address_space().set(address_mapping_->slot_number());
    config.mode().set(registers::PerformanceCounterConfig::kModeDisabled);
    config.WriteTo(owner_->register_io());
    counter_state_ = PerformanceCounterState::kDisabled;
    return;
  }

  if (counter_state_ != PerformanceCounterState::kTriggered) {
    DLOG("Can't trigger performance counters from state %d", static_cast<int>(counter_state_));
    return;
  }
  uint64_t new_base =
      registers::PerformanceCounterBase::Get().ReadFrom(owner_->register_io()).reg_value();

  DASSERT(new_base >= last_perf_base_);
  DASSERT(new_base <= kPerfBufferSize + kPerfBufferStartOffset);

  void* mapped_data;
  uint64_t base = last_perf_base_ - kPerfBufferStartOffset;
  // A memory barrier is unnecessary since this was triggered by an interrupt
  // which can't be reordered-past.
  buffer_->platform_buffer()->CleanCache(base, kPerfBufferSize, true);
  bool success = buffer_->platform_buffer()->MapCpu(&mapped_data);
  DASSERT(success);
  auto perf_registers = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(mapped_data) + base);

  for (uint32_t i = 0; i < (new_base - last_perf_base_) / sizeof(uint32_t); ++i) {
    output.push_back(perf_registers[i]);
  }

  buffer_->platform_buffer()->UnmapCpu();
  auto config = registers::PerformanceCounterConfig::Get().FromValue(0);
  config.address_space().set(address_mapping_->slot_number());
  config.mode().set(registers::PerformanceCounterConfig::kModeDisabled);
  config.WriteTo(owner_->register_io());
  counter_state_ = PerformanceCounterState::kDisabled;

  // Reading from the performance counters clears them but leaves them
  // enabled, so just setting the state to enabled would normally work. However,
  // the base register address changes every time a read happens, so we
  // need to temporarily disable them to reset that address so we don't
  // overflow the buffer.
  Enable();

  for (Client* client : clients_)
    client->OnPerfCountDump(output);
}

bool PerformanceCounters::Disable() {
  switch (counter_state_) {
    case PerformanceCounterState::kTriggered:
    case PerformanceCounterState::kTriggeredWillBeDisabled:
      counter_state_ = PerformanceCounterState::kTriggeredWillBeDisabled;
      return true;
    case PerformanceCounterState::kEnabled: {
      auto config = registers::PerformanceCounterConfig::Get().FromValue(0);
      config.address_space().set(address_mapping_->slot_number());
      config.mode().set(registers::PerformanceCounterConfig::kModeDisabled);
      config.WriteTo(owner_->register_io());
      counter_state_ = PerformanceCounterState::kDisabled;
      return true;
    }
    case PerformanceCounterState::kDisabled: {
      return true;
    }
  }
}

void PerformanceCounters::AddClient(Client* client) { clients_.insert(client); }

void PerformanceCounters::RemoveClient(Client* client) { clients_.erase(client); }

bool PerformanceCounters::AddManager(PerformanceCountersManager* manager) {
  if (manager_)
    return false;
  manager_ = manager;
  return true;
}

void PerformanceCounters::RemoveManager(PerformanceCountersManager* manager) {
  if (manager_ == manager)
    manager_ = nullptr;
}

bool PerformanceCounters::ShouldBeEnabled() {
  return !force_disabled_ && manager_ && manager_->EnabledPerfCountFlags().size() > 0;
}

void PerformanceCounters::Update() {
  bool should_be_enabled = ShouldBeEnabled();
  if (should_be_enabled && (counter_state_ == PerformanceCounterState::kDisabled ||
                            counter_state_ == PerformanceCounterState::kTriggeredWillBeDisabled)) {
    Enable();
  } else if (!should_be_enabled && (counter_state_ == PerformanceCounterState::kEnabled ||
                                    counter_state_ == PerformanceCounterState::kTriggered)) {
    Disable();
  }
}
