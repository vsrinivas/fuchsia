// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "address_manager.h"

#include <chrono>
#include <thread>

#include "address_space.h"
#include "msd_arm_connection.h"
#include "platform_barriers.h"
#include "platform_logger.h"

// Normal memory, outer non-cacheable, inner cacheable with
// implementation-defined allocation. The definition of this is similar to
// normal LPAE memory attributes, but is undocumented.
constexpr uint8_t kMmuNormalMemoryAttr = 0x48;
// Memory with this attribute is also outer cacheable with
// implementation-defined allocation.
constexpr uint8_t kMmuOuterCacheableMemoryAttr = 0x88;

// The memory attribute register has 8 8-bit slots.
static constexpr uint64_t SlotAttribute(int slot, uint8_t attributes) {
  return static_cast<uint64_t>(attributes) << (slot * 8);
}

constexpr uint64_t kMemoryAttributes =
    SlotAttribute(AddressSpace::kNormalMemoryAttributeSlot, kMmuNormalMemoryAttr) |
    SlotAttribute(AddressSpace::kOuterCacheableAttributeSlot, kMmuOuterCacheableMemoryAttr);

AddressManager::AddressManager(Owner* owner, uint32_t address_slot_count) : owner_(owner) {
  address_slots_.resize(address_slot_count);
  for (uint32_t i = 0; i < address_slot_count; i++) {
    registers_.push_back(std::make_unique<HardwareSlot>(i));
  }
}

bool AddressManager::AssignAddressSpace(MsdArmAtom* atom) {
  DASSERT(!atom->address_slot_mapping());
  auto connection = atom->connection().lock();
  if (!connection)
    return false;
  if (connection->address_space_lost())
    return false;

  std::shared_ptr<AddressSlotMapping> mapping = AllocateMappingForAddressSpace(connection);
  atom->set_address_slot_mapping(mapping);

  return mapping ? true : false;
}

void AddressManager::NotifySlotPotentiallyFree() {
  std::lock_guard<std::mutex> lock(address_slot_lock_);
  // This needs to be done after |address_slot_lock_| is acquired to ensure that there can't be a
  // AllocateMappingForAddressSpace that's currently between checking expired mappings and waiting
  // for a notify, which would cause it to miss a notification.
  address_slot_free_.notify_one();
}

void AddressManager::AtomFinished(MsdArmAtom* atom) {
  if (!atom->address_slot_mapping())
    return;
  atom->set_address_slot_mapping(nullptr);
  NotifySlotPotentiallyFree();
}

void AddressManager::ClearAddressMappings(bool force_expire) {
  std::lock_guard<std::mutex> lock(address_slot_lock_);
  for (uint32_t i = 0; i < address_slots_.size(); i++) {
    auto& slot = address_slots_[i];
    HardwareSlot& hardware_slot = *registers_[i];
    std::lock_guard<std::mutex> lock(hardware_slot.lock);

    if (slot.address_space) {
      // Invalidate the hardware slot to ensure the relevant regions of the L2 cache are
      // flushed, because any AddressSpace invalidations afterwards (before the L2 is shut
      // down) will be ignored and could otherwise allow flushing the L2 to write to
      // deallocated memory.
      hardware_slot.InvalidateSlot(owner_->register_io());
      slot.address_space = nullptr;
    }
    if (force_expire) {
      slot.mapping.reset();
    } else {
      // Do this check while HardwareSlot is locked, to ensure that any previous
      // FlushAddressMappingRange has finished and released its mapping.
      DASSERT(slot.mapping.expired());
    }
  }
}

std::shared_ptr<AddressSlotMapping> AddressManager::GetMappingForSlot(uint32_t slot_number) {
  std::lock_guard<std::mutex> lock(address_slot_lock_);
  auto& slot = address_slots_[slot_number];
  return slot.mapping.lock();
}

std::shared_ptr<AddressSlotMapping> AddressManager::GetMappingForAddressSpaceUnlocked(
    const AddressSpace* address_space) {
  DASSERT(address_space);
  for (size_t i = 0; i < address_slots_.size(); ++i) {
    AddressSlot& slot = address_slots_[i];
    if (slot.address_space != address_space)
      continue;
    auto mapping = slot.mapping.lock();
    if (!mapping) {
      mapping = std::make_shared<AddressSlotMapping>(
          i, std::static_pointer_cast<MsdArmConnection>(address_space->owner()));
      slot.mapping = mapping;
    }
    return mapping;
  }

  return nullptr;
}

void AddressManager::FlushAddressMappingRange(AddressSpace* address_space, uint64_t start,
                                              uint64_t length, bool synchronous) {
  HardwareSlot* slot;
  std::shared_ptr<AddressSlotMapping> mapping;
  {
    std::lock_guard<std::mutex> lock(address_slot_lock_);
    mapping = AddressManager::GetMappingForAddressSpaceUnlocked(address_space);
    if (!mapping)
      return;
    slot = registers_[mapping->slot_number()].get();
    // Grab the hardware lock inside the address slot lock so we can be
    // sure the address slot still maps to the same address space.
    // std::unique_lock can't be used because it interacts poorly with
    // thread-safety analysis
    slot->lock.lock();
  }
  slot->FlushMmuRange(owner_->register_io(), start, length, synchronous);

  // The mapping will be released before the hardware lock, so that we
  // can be sure that the mapping will be expired after ReleaseSpaceMappings
  // acquires the hardware lock.
  mapping.reset();
  slot->lock.unlock();
  NotifySlotPotentiallyFree();
}

void AddressManager::UnlockAddressSpace(AddressSpace* address_space) {
  HardwareSlot* slot;
  std::shared_ptr<AddressSlotMapping> mapping;
  {
    std::lock_guard<std::mutex> lock(address_slot_lock_);
    mapping = AddressManager::GetMappingForAddressSpaceUnlocked(address_space);
    if (!mapping)
      return;
    slot = registers_[mapping->slot_number()].get();
    // Grab the hardware lock inside the address slot lock so we can be
    // sure the address slot still maps to the same address space.
    // std::unique_lock can't be used because it interacts poorly with
    // thread-safety analysis
    slot->lock.lock();
  }
  slot->UnlockMmu(owner_->register_io());

  // The mapping will be released before the hardware lock, so that we
  // can be sure that the mapping will be expired after ReleaseSpaceMappings
  // acquires the hardware lock.
  mapping.reset();
  slot->lock.unlock();
  NotifySlotPotentiallyFree();
}

// Disable thread safety analysis because it doesn't understand unique_lock.
std::shared_ptr<AddressSlotMapping> AddressManager::AllocateMappingForAddressSpace(
    std::shared_ptr<MsdArmConnection> connection) __TA_NO_THREAD_SAFETY_ANALYSIS {
  std::unique_lock<std::mutex> lock(address_slot_lock_);
  while (true) {
    std::shared_ptr<AddressSlotMapping> mapping =
        GetMappingForAddressSpaceUnlocked(connection->const_address_space());
    if (mapping)
      return mapping;

    // Allocate new mapping (trying to avoid evicting).
    for (uint32_t i = 0; i < address_slots_.size(); ++i) {
      if (!address_slots_[i].address_space)
        return AssignToSlot(connection, i);
    }

    // TODO(fxbug.dev/12997): Evict the LRU slot.
    for (uint32_t i = 0; i < address_slots_.size(); ++i) {
      if (address_slots_[i].mapping.expired())
        return AssignToSlot(connection, i);
    }

    if (increase_notify_race_window_) {
      constexpr auto kRaceDelay = std::chrono::milliseconds(100);
      std::this_thread::sleep_for(kRaceDelay);
    }

    // There are normally 8 hardware address slots but only 6 jobs can be running in hardware at
    // a time (and also the profiler can use an address slot). So the only way we can be
    // completely out of address slots is that a connection thread is flushing the MMU. Because
    // of that there's no deadlock if we block the device thread, because the connection can
    // finish flushing and release its mapping without the device thread. Starvation is still
    // possible, though.
    if (address_slot_free_.wait_for(lock, std::chrono::seconds(acquire_slot_timeout_seconds_)) ==
        std::cv_status::timeout) {
      MAGMA_LOG(WARNING, "Timeout waiting for address slot");
      return DRETP(nullptr, "Timeout waiting for address slot");
    }
  }
}

std::shared_ptr<AddressSlotMapping> AddressManager::AssignToSlot(
    std::shared_ptr<MsdArmConnection> connection, uint32_t slot_number) {
  DLOG("Assigning connection %p to slot %d", connection.get(), slot_number);
  AddressSlot& slot = address_slots_[slot_number];
  HardwareSlot& hardware_slot = *registers_[slot_number];
  std::lock_guard<std::mutex> lock(hardware_slot.lock);

  auto old_address_space = slot.address_space;
  magma::RegisterIo* io = owner_->register_io();
  if (old_address_space)
    hardware_slot.InvalidateSlot(io);
  auto mapping = std::make_shared<AddressSlotMapping>(slot_number, connection);
  slot.mapping = mapping;
  slot.address_space = connection->const_address_space();

  registers::AsRegisters& as_reg = hardware_slot.registers;
  hardware_slot.WaitForMmuIdle(io);

  uint64_t translation_table_entry = connection->const_address_space()->translation_table_entry();
  as_reg.TranslationTable().FromValue(translation_table_entry).WriteTo(io);

  as_reg.MemoryAttributes().FromValue(kMemoryAttributes).WriteTo(io);

  as_reg.Command().FromValue(registers::AsCommand::kCmdUpdate).WriteTo(io);
  return mapping;
}

void AddressManager::ReleaseSpaceMappings(const AddressSpace* address_space) {
  std::lock_guard<std::mutex> lock(address_slot_lock_);
  for (size_t i = 0; i < address_slots_.size(); ++i) {
    AddressSlot& slot = address_slots_[i];
    if (slot.address_space != address_space)
      continue;
    // Grab lock to ensure the registers aren't being modified during
    // invalidate.
    HardwareSlot& hardware_slot = *registers_[i];
    std::lock_guard<std::mutex> lock(hardware_slot.lock);
    DASSERT(slot.mapping.expired());
    hardware_slot.InvalidateSlot(owner_->register_io());
    slot.address_space = nullptr;
  }
}

void AddressManager::HardwareSlot::InvalidateSlot(magma::RegisterIo* io) {
  WaitForMmuIdle(io);
  constexpr uint64_t kFullAddressSpaceSize = 1ul << AddressSpace::kVirtualAddressSize;
  FlushMmuRange(io, 0, kFullAddressSpaceSize, true);

  registers.TranslationTable().FromValue(0).WriteTo(io);
  registers.MemoryAttributes().FromValue(kMemoryAttributes).WriteTo(io);

  registers.Command().FromValue(registers::AsCommand::kCmdUpdate).WriteTo(io);

  // Ensure CPU reads and writes to buffers in the address space don't happen
  // until after the hardware got the command to finish using the buffer.
  magma::barriers::Barrier();
}

void AddressManager::HardwareSlot::WaitForMmuIdle(magma::RegisterIo* io) {
  auto status_reg = registers.Status();
  if (!status_reg.ReadFrom(io).reg_value())
    return;

  auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (status_reg.ReadFrom(io).reg_value() && std::chrono::steady_clock::now() < timeout)
    ;

  uint32_t status = status_reg.ReadFrom(io).reg_value();
  if (status)
    MAGMA_LOG(WARNING, "Wait for MMU %d to idle timed out with status 0x%x",
              registers.address_space(), status);
}

void AddressManager::HardwareSlot::FlushMmuRange(magma::RegisterIo* io, uint64_t start,
                                                 uint64_t length, bool synchronous) {
  DASSERT(magma::is_page_aligned(start));
  uint64_t region = start;
  uint64_t num_pages = length >> PAGE_SHIFT;
  uint8_t log2_num_pages = 0;
  if (num_pages > 0) {
    log2_num_pages = static_cast<uint8_t>(63 - __builtin_clzl(num_pages));
    if ((1ul << log2_num_pages) < num_pages)
      log2_num_pages++;
  }

  // Ensure page table writes are completed before the hardware tries to
  // access the buffer.
  magma::barriers::WriteBarrier();
  constexpr uint32_t kRegionLengthOffset = 11;

  // The low 12 bits are used to specify how many pages are to be locked in
  // this operation.
  static_assert(kRegionLengthOffset + 64 < PAGE_SIZE, "maximum region length is too large");

  uint8_t region_width = log2_num_pages + kRegionLengthOffset;

  region |= region_width;
  registers.LockAddress().FromValue(region).WriteTo(io);
  registers.Command().FromValue(registers::AsCommand::kCmdLock).WriteTo(io);
  WaitForMmuIdle(io);
  if (synchronous) {
    // Both invalidate the TLB entries and throw away data in the L2 cache
    // corresponding to them, or otherwise the cache may be written back to
    // memory after the memory's started being used for something else.
    registers.Command().FromValue(registers::AsCommand::kCmdFlushMem).WriteTo(io);
  } else {
    registers.Command().FromValue(registers::AsCommand::kCmdFlushPageTable).WriteTo(io);
  }
  WaitForMmuIdle(io);
  // If a page range was unmapped, ensure the hardware is no longer accessing
  // it before any CPU reads or writes to the memory.
  magma::barriers::Barrier();
}

void AddressManager::HardwareSlot::UnlockMmu(magma::RegisterIo* io) {
  WaitForMmuIdle(io);
  registers.Command().FromValue(registers::AsCommand::kCmdUnlock).WriteTo(io);
}
