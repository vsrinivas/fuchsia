// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "page_table_arrays.h"

#include "macros.h"
#include "registers.h"

std::unique_ptr<PageTableArrays> PageTableArrays::Create(magma::PlatformBusMapper* bus_mapper) {
  auto address_manager = std::make_unique<PageTableArrays>();
  if (!address_manager->Init(bus_mapper))
    return DRETP(nullptr, "Failed to initialize");
  return address_manager;
}

bool PageTableArrays::Init(magma::PlatformBusMapper* bus_mapper) {
  page_table_array_ =
      magma::PlatformBuffer::Create(kPageTableArraySizeInPages * PAGE_SIZE, "page_table_array");
  if (!page_table_array_)
    return DRETF(false, "Failed to allocate page_table_array");

  if (!page_table_array_->SetCachePolicy(MAGMA_CACHE_POLICY_UNCACHED))
    return DRETF(false, "Failed to set cache policy");

  if (!page_table_array_->MapCpu(reinterpret_cast<void**>(&master_tlb_bus_addr_)))
    return DRETF(false, "Failed to map page table array");

  bus_mapping_ =
      bus_mapper->MapPageRangeBus(page_table_array_.get(), 0, kPageTableArraySizeInPages);
  if (!bus_mapping_)
    return DRETF(false, "Failed to bus map page table array");

  security_safe_page_ = magma::PlatformBuffer::Create(PAGE_SIZE, "security safe page");
  if (!security_safe_page_)
    return DRETF(false, "Failed to allocated security safe page");

  security_safe_page_bus_mapping_ = bus_mapper->MapPageRangeBus(security_safe_page_.get(), 0, 1);
  if (!security_safe_page_bus_mapping_)
    return DRETF(false, "Failed to bus map security safe page");

  non_security_safe_page_ = magma::PlatformBuffer::Create(PAGE_SIZE, "security safe page");
  if (!non_security_safe_page_)
    return DRETF(false, "Failed to allocated non security safe page");

  non_security_safe_page_bus_mapping_ =
      bus_mapper->MapPageRangeBus(non_security_safe_page_.get(), 0, 1);
  if (!non_security_safe_page_bus_mapping_)
    return DRETF(false, "Failed to bus map non security safe page");

  return true;
}

void PageTableArrays::HardwareInit(magma::RegisterIo* register_io) {
  uint64_t security_safe_page_bus_addr = security_safe_page_bus_mapping_->Get()[0];
  uint64_t non_security_safe_page_bus_addr = non_security_safe_page_bus_mapping_->Get()[0];

  DASSERT(fits_in_40_bits(security_safe_page_bus_addr));
  DASSERT(fits_in_40_bits(non_security_safe_page_bus_addr));

  Enable(register_io, false);

  {
    auto reg = registers::PageTableArrayAddressLow::Get().FromValue(0);
    reg.set_reg_value(magma::lower_32_bits(bus_addr()));
    reg.WriteTo(register_io);
  }
  {
    auto reg = registers::PageTableArrayAddressHigh::Get().FromValue(0);
    reg.set_reg_value(magma::upper_32_bits(bus_addr()));
    reg.WriteTo(register_io);
  }
  {
    auto reg = registers::PageTableArrayControl::Get().FromValue(0);
    reg.enable().set(1);
    reg.WriteTo(register_io);
  }
  {
    auto reg = registers::MmuNonSecuritySafeAddressLow::Get().FromValue(0);
    reg.set_reg_value(magma::lower_32_bits(non_security_safe_page_bus_addr));
    reg.WriteTo(register_io);
  }
  {
    auto reg = registers::MmuSecuritySafeAddressLow::Get().FromValue(0);
    reg.set_reg_value(magma::lower_32_bits(security_safe_page_bus_addr));
    reg.WriteTo(register_io);
  }
  {
    auto reg = registers::MmuSafeAddressConfig::Get().FromValue(0);
    reg.non_security_safe_address_high().set(non_security_safe_page_bus_addr & 0xFF);
    reg.security_safe_address_high().set(security_safe_page_bus_addr & 0xFF);
    reg.WriteTo(register_io);
  }
}

void PageTableArrays::Enable(magma::RegisterIo* register_io, bool enable) {
  auto reg_mmu_sec_ctrl = registers::MmuSecureControl::Get().ReadFrom(register_io);
  reg_mmu_sec_ctrl.enable().set(enable ? 1 : 0);
  reg_mmu_sec_ctrl.WriteTo(register_io);
}

void PageTableArrays::AssignAddressSpace(uint32_t index, AddressSpace* address_space) {
  DASSERT(index <= kPageTableArrayEntries);
  master_tlb_bus_addr_[index] = address_space->bus_addr();
}
