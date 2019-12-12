// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "iommu_impl.h"

#include <err.h>
#include <platform.h>
#include <trace.h>
#include <zircon/time.h>

#include <new>

#include <dev/interrupt.h>
#include <fbl/algorithm.h>
#include <fbl/ref_ptr.h>
#include <ktl/move.h>
#include <ktl/unique_ptr.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object_paged.h>
#include <vm/vm_object_physical.h>

#include "context_table_state.h"
#include "device_context.h"
#include "hw.h"

#define LOCAL_TRACE 0

namespace intel_iommu {

IommuImpl::IommuImpl(volatile void* register_base, ktl::unique_ptr<const uint8_t[]> desc,
                     size_t desc_len)
    : desc_(ktl::move(desc)), desc_len_(desc_len), mmio_(register_base) {
  memset(&irq_block_, 0, sizeof(irq_block_));
  // desc_len_ is currently unused, but we stash it so we can use the length
  // of it later in case we need it.  This silences a warning in Clang.
  desc_len_ = desc_len;
}

zx_status_t IommuImpl::Create(ktl::unique_ptr<const uint8_t[]> desc_bytes, size_t desc_len,
                              fbl::RefPtr<Iommu>* out) {
  zx_status_t status = ValidateIommuDesc(desc_bytes, desc_len);
  if (status != ZX_OK) {
    return status;
  }

  auto desc = reinterpret_cast<const zx_iommu_desc_intel_t*>(desc_bytes.get());
  const uint64_t register_base = desc->register_base;

  auto kernel_aspace = VmAspace::kernel_aspace();
  void* vaddr;
  status = kernel_aspace->AllocPhysical(
      "iommu", PAGE_SIZE, &vaddr, PAGE_SIZE_SHIFT, register_base, 0,
      ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE | ARCH_MMU_FLAG_UNCACHED);
  if (status != ZX_OK) {
    return status;
  }

  fbl::AllocChecker ac;
  auto instance =
      fbl::AdoptRef<IommuImpl>(new (&ac) IommuImpl(vaddr, ktl::move(desc_bytes), desc_len));
  if (!ac.check()) {
    kernel_aspace->FreeRegion(reinterpret_cast<vaddr_t>(vaddr));
    return ZX_ERR_NO_MEMORY;
  }

  status = instance->Initialize();
  if (status != ZX_OK) {
    return status;
  }

  *out = ktl::move(instance);
  return ZX_OK;
}

IommuImpl::~IommuImpl() {
  Guard<Mutex> guard{&lock_};

  // We cannot unpin memory until translation is disabled
  zx_status_t status = SetTranslationEnableLocked(false, ZX_TIME_INFINITE);
  ASSERT(status == ZX_OK);

  DisableFaultsLocked();
  if (irq_block_.allocated) {
    msi_register_handler(&irq_block_, 0, nullptr, nullptr);
    msi_free_block(&irq_block_);
  }

  // Need to free any context tables before mmio_ is unmapped (and before this destructor
  // concludes) as the context_tables_ hold raw pointers back into us. As the destructors of the
  // tables will call operations that acquire the lock_ we drop them with the lock temporarily
  // released.
  fbl::DoublyLinkedList<ktl::unique_ptr<ContextTableState>> tables = ktl::move(context_tables_);
  guard.CallUnlocked([&tables] { tables.clear(); });

  VmAspace::kernel_aspace()->FreeRegion(mmio_.base());
}

// Validate the IOMMU descriptor from userspace.
//
// The IOMMU descriptor identifies either a whitelist (if whole_segment is false)
// or a blacklist (if whole_segment is true) of devices that are decoded by this
// IOMMU. An entry in the list is described by a "scope" below. A scope
// identifies a single PCIe device. If the device is behind a bridge, it will be
// described using multiple "hops", one for each bridge in the way and one for
// the device itself. A hop identifies the address of a bridge on the path to
// the device, or (in the final entry) the address of the device itself.
//
// The descriptor also contains a list of "Reserved Memory Regions", which
// describes regions of physical address space that must be identity-mapped for
// specific devices to function correctly.  There is typically one region for
// the i915 gpu (initial framebuffer) and one for the XHCI controller
// (scratch space for the BIOS before the OS takes ownership of the controller).
zx_status_t IommuImpl::ValidateIommuDesc(const ktl::unique_ptr<const uint8_t[]>& desc_bytes,
                                         size_t desc_len) {
  auto desc = reinterpret_cast<const zx_iommu_desc_intel_t*>(desc_bytes.get());

  // Validate the size
  if (desc_len < sizeof(*desc)) {
    LTRACEF("desc too short: %zu < %zu\n", desc_len, sizeof(*desc));
    return ZX_ERR_INVALID_ARGS;
  }
  static_assert(sizeof(desc->scope_bytes) < sizeof(size_t),
                "if this changes, need to check for overflow");
  size_t actual_size = sizeof(*desc);
  if (add_overflow(actual_size, desc->scope_bytes, &actual_size) ||
      add_overflow(actual_size, desc->reserved_memory_bytes, &actual_size) ||
      actual_size != desc_len) {
    LTRACEF("desc size mismatch: %zu != %zu\n", desc_len, actual_size);
    return ZX_ERR_INVALID_ARGS;
  }

  // Validate scopes
  if (desc->scope_bytes == 0 && !desc->whole_segment) {
    LTRACEF("desc has no scopes\n");
    return ZX_ERR_INVALID_ARGS;
  }
  const size_t num_scopes = desc->scope_bytes / sizeof(zx_iommu_desc_intel_scope_t);
  size_t scope_bytes = num_scopes;
  if (mul_overflow(scope_bytes, sizeof(zx_iommu_desc_intel_scope_t), &scope_bytes) ||
      scope_bytes != desc->scope_bytes) {
    LTRACEF("desc has invalid scope_bytes field\n");
    return ZX_ERR_INVALID_ARGS;
  }

  auto scopes = reinterpret_cast<zx_iommu_desc_intel_scope_t*>(reinterpret_cast<uintptr_t>(desc) +
                                                               sizeof(*desc));
  for (size_t i = 0; i < num_scopes; ++i) {
    if (scopes[i].num_hops == 0) {
      LTRACEF("desc scope %zu has no hops\n", i);
      return ZX_ERR_INVALID_ARGS;
    }
    if (scopes[i].num_hops > fbl::count_of(scopes[0].dev_func)) {
      LTRACEF("desc scope %zu has too many hops\n", i);
      return ZX_ERR_INVALID_ARGS;
    }
  }

  // Validate reserved memory regions
  size_t cursor_bytes = sizeof(*desc) + desc->scope_bytes;
  while (cursor_bytes + sizeof(zx_iommu_desc_intel_reserved_memory_t) < desc_len) {
    auto mem = reinterpret_cast<zx_iommu_desc_intel_reserved_memory_t*>(
        reinterpret_cast<uintptr_t>(desc) + cursor_bytes);

    size_t next_entry = cursor_bytes;
    if (add_overflow(next_entry, sizeof(zx_iommu_desc_intel_reserved_memory_t), &next_entry) ||
        add_overflow(next_entry, mem->scope_bytes, &next_entry) || next_entry > desc_len) {
      LTRACEF("desc reserved memory entry has invalid scope_bytes\n");
      return ZX_ERR_INVALID_ARGS;
    }

    // TODO(teisenbe): Make sure that the reserved memory regions are not in our
    // allocatable RAM pools

    // Validate scopes
    if (mem->scope_bytes == 0) {
      LTRACEF("desc reserved memory entry has no scopes\n");
      return ZX_ERR_INVALID_ARGS;
    }
    const size_t num_scopes = mem->scope_bytes / sizeof(zx_iommu_desc_intel_scope_t);
    size_t scope_bytes = num_scopes;
    if (mul_overflow(scope_bytes, sizeof(zx_iommu_desc_intel_scope_t), &scope_bytes) ||
        scope_bytes != desc->scope_bytes) {
      LTRACEF("desc reserved memory entry has invalid scope_bytes field\n");
      return ZX_ERR_INVALID_ARGS;
    }

    auto scopes = reinterpret_cast<zx_iommu_desc_intel_scope_t*>(reinterpret_cast<uintptr_t>(mem) +
                                                                 sizeof(*mem));
    for (size_t i = 0; i < num_scopes; ++i) {
      if (scopes[i].num_hops == 0) {
        LTRACEF("desc reserved memory entry scope %zu has no hops\n", i);
        return ZX_ERR_INVALID_ARGS;
      }
      if (scopes[i].num_hops > fbl::count_of(scopes[0].dev_func)) {
        LTRACEF("desc reserved memory entry scope %zu has too many hops\n", i);
        return ZX_ERR_INVALID_ARGS;
      }
    }

    cursor_bytes = next_entry;
  }
  if (cursor_bytes != desc_len) {
    LTRACEF("desc has invalid reserved_memory_bytes field\n");
    return ZX_ERR_INVALID_ARGS;
  }

  LTRACEF("validated desc\n");
  return ZX_OK;
}

bool IommuImpl::IsValidBusTxnId(uint64_t bus_txn_id) const {
  if (bus_txn_id > UINT16_MAX) {
    return false;
  }

  ds::Bdf bdf = decode_bus_txn_id(bus_txn_id);

  auto desc = reinterpret_cast<const zx_iommu_desc_intel_t*>(desc_.get());
  const size_t num_scopes = desc->scope_bytes / sizeof(zx_iommu_desc_intel_scope_t);
  auto scopes = reinterpret_cast<zx_iommu_desc_intel_scope_t*>(reinterpret_cast<uintptr_t>(desc) +
                                                               sizeof(*desc));

  // Search for this BDF in the scopes we have
  for (size_t i = 0; i < num_scopes; ++i) {
    if (scopes[i].num_hops != 1) {
      // TODO(teisenbe): Implement
      continue;
    }

    if (scopes[i].start_bus == bdf.bus() && scopes[i].dev_func[0] == bdf.packed_dev_and_func()) {
      return !desc->whole_segment;
    }
  }

  if (desc->whole_segment) {
    // Since we only support single segment currently, just return true
    // here.  To support more segments, we need to make sure the segment
    // matches, too.
    return true;
  }

  return false;
}

zx_status_t IommuImpl::Map(uint64_t bus_txn_id, const fbl::RefPtr<VmObject>& vmo, uint64_t offset,
                           size_t size, uint32_t perms, dev_vaddr_t* vaddr, size_t* mapped_len) {
  DEBUG_ASSERT(vaddr);
  if (!IS_PAGE_ALIGNED(offset) || size == 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (perms & ~(IOMMU_FLAG_PERM_READ | IOMMU_FLAG_PERM_WRITE | IOMMU_FLAG_PERM_EXECUTE)) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (perms == 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (!IsValidBusTxnId(bus_txn_id)) {
    return ZX_ERR_NOT_FOUND;
  }

  ds::Bdf bdf = decode_bus_txn_id(bus_txn_id);

  Guard<Mutex> guard{&lock_};
  DeviceContext* dev;
  zx_status_t status = GetOrCreateDeviceContextLocked(bdf, &dev);
  if (status != ZX_OK) {
    return status;
  }
  return dev->SecondLevelMap(vmo, offset, size, perms, false /* map_contiguous */, vaddr,
                             mapped_len);
}

zx_status_t IommuImpl::MapContiguous(uint64_t bus_txn_id, const fbl::RefPtr<VmObject>& vmo,
                                     uint64_t offset, size_t size, uint32_t perms,
                                     dev_vaddr_t* vaddr, size_t* mapped_len) {
  DEBUG_ASSERT(vaddr);
  if (!IS_PAGE_ALIGNED(offset) || size == 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (perms & ~(IOMMU_FLAG_PERM_READ | IOMMU_FLAG_PERM_WRITE | IOMMU_FLAG_PERM_EXECUTE)) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (perms == 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (!IsValidBusTxnId(bus_txn_id)) {
    return ZX_ERR_NOT_FOUND;
  }

  ds::Bdf bdf = decode_bus_txn_id(bus_txn_id);

  Guard<Mutex> guard{&lock_};
  DeviceContext* dev;
  zx_status_t status = GetOrCreateDeviceContextLocked(bdf, &dev);
  if (status != ZX_OK) {
    return status;
  }
  return dev->SecondLevelMap(vmo, offset, size, perms, true /* map_contiguous */, vaddr,
                             mapped_len);
}

zx_status_t IommuImpl::Unmap(uint64_t bus_txn_id, dev_vaddr_t vaddr, size_t size) {
  if (!IS_PAGE_ALIGNED(vaddr) || !IS_PAGE_ALIGNED(size)) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (!IsValidBusTxnId(bus_txn_id)) {
    return ZX_ERR_NOT_FOUND;
  }

  ds::Bdf bdf = decode_bus_txn_id(bus_txn_id);

  Guard<Mutex> guard{&lock_};
  DeviceContext* dev;
  zx_status_t status = GetOrCreateDeviceContextLocked(bdf, &dev);
  if (status != ZX_OK) {
    return status;
  }
  status = dev->SecondLevelUnmap(vaddr, size);
  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

zx_status_t IommuImpl::ClearMappingsForBusTxnId(uint64_t bus_txn_id) {
  PANIC_UNIMPLEMENTED;
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t IommuImpl::Initialize() {
  Guard<Mutex> guard{&lock_};

  // Ensure we support this device version
  auto version = reg::Version::Get().ReadFrom(&mmio_);
  if (version.major() != 1 && version.minor() != 0) {
    LTRACEF("Unsupported IOMMU version: %u.%u\n", version.major(), version.minor());
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Cache useful capability info
  caps_ = reg::Capability::Get().ReadFrom(&mmio_);
  extended_caps_ = reg::ExtendedCapability::Get().ReadFrom(&mmio_);

  max_guest_addr_mask_ = (1ULL << (caps_.max_guest_addr_width() + 1)) - 1;
  fault_recording_reg_offset_ = static_cast<uint32_t>(caps_.fault_recording_register_offset() * 16);
  num_fault_recording_reg_ = static_cast<uint32_t>(caps_.num_fault_recording_reg() + 1);
  iotlb_reg_offset_ = static_cast<uint32_t>(extended_caps_.iotlb_register_offset() * 16);

  constexpr size_t kIoTlbRegisterBankSize = 16;
  if (iotlb_reg_offset_ > PAGE_SIZE - kIoTlbRegisterBankSize) {
    LTRACEF("Unsupported IOMMU: IOTLB offset runs past the register page\n");
    return ZX_ERR_NOT_SUPPORTED;
  }
  supports_extended_context_ = extended_caps_.supports_extended_context();
  if (extended_caps_.supports_pasid()) {
    valid_pasid_mask_ = static_cast<uint32_t>((1ULL << (extended_caps_.pasid_size() + 1)) - 1);
  }

  const uint64_t num_domains_raw = caps_.num_domains();
  if (num_domains_raw > 0x6) {
    LTRACEF("Unknown num_domains value\n");
    return ZX_ERR_NOT_SUPPORTED;
  }
  const uint32_t num_supported_domains = static_cast<uint32_t>(1ul << (4 + 2 * num_domains_raw));
  domain_allocator_.set_num_domains(num_supported_domains);

  // Sanity check initial configuration
  auto global_ctl = reg::GlobalControl::Get().ReadFrom(&mmio_);
  if (global_ctl.translation_enable()) {
    LTRACEF("DMA remapping already enabled?!\n");
    return ZX_ERR_BAD_STATE;
  }
  if (global_ctl.interrupt_remap_enable()) {
    LTRACEF("IRQ remapping already enabled?!\n");
    return ZX_ERR_BAD_STATE;
  }

  // Allocate and setup the root table
  zx_status_t status = IommuPage::AllocatePage(&root_table_page_);
  if (status != ZX_OK) {
    LTRACEF("alloc root table failed\n");
    return status;
  }
  status = SetRootTablePointerLocked(root_table_page_.paddr());
  if (status != ZX_OK) {
    LTRACEF("set root table failed\n");
    return status;
  }

  // Enable interrupts before we enable translation
  status = ConfigureFaultEventInterruptLocked();
  if (status != ZX_OK) {
    LTRACEF("configuring fault event irq failed\n");
    return status;
  }

  status = EnableBiosReservedMappingsLocked();
  if (status != ZX_OK) {
    LTRACEF("enable bios reserved mappings failed\n");
    return status;
  }

  status = SetTranslationEnableLocked(true, zx_time_add_duration(current_time(), ZX_SEC(1)));
  if (status != ZX_OK) {
    LTRACEF("set translation enable failed\n");
    return status;
  }

  return ZX_OK;
}

zx_status_t IommuImpl::EnableBiosReservedMappingsLocked() {
  auto desc = reinterpret_cast<const zx_iommu_desc_intel_t*>(desc_.get());

  size_t cursor_bytes = 0;
  while (cursor_bytes + sizeof(zx_iommu_desc_intel_reserved_memory_t) <
         desc->reserved_memory_bytes) {
    // The descriptor has already been validated, so no need to check again.
    auto mem = reinterpret_cast<zx_iommu_desc_intel_reserved_memory_t*>(
        reinterpret_cast<uintptr_t>(desc) + sizeof(*desc) + desc->scope_bytes + cursor_bytes);

    const size_t num_scopes = mem->scope_bytes / sizeof(zx_iommu_desc_intel_scope_t);
    auto scopes = reinterpret_cast<zx_iommu_desc_intel_scope_t*>(reinterpret_cast<uintptr_t>(mem) +
                                                                 sizeof(*mem));
    for (size_t i = 0; i < num_scopes; ++i) {
      if (scopes[i].num_hops != 1) {
        // TODO(teisenbe): Implement
        return ZX_ERR_NOT_SUPPORTED;
      }

      ds::Bdf bdf;
      bdf.set_bus(scopes[i].start_bus);
      bdf.set_dev(static_cast<uint8_t>(scopes[i].dev_func[0] >> 3));
      bdf.set_func(static_cast<uint8_t>(scopes[i].dev_func[0] & 0x7));

      DeviceContext* dev;
      zx_status_t status = GetOrCreateDeviceContextLocked(bdf, &dev);
      if (status != ZX_OK) {
        return status;
      }

      LTRACEF("Enabling region [%lx, %lx) for %02x:%02x.%02x\n", mem->base_addr,
              mem->base_addr + mem->len, bdf.bus(), bdf.dev(), bdf.func());
      size_t size = ROUNDUP(mem->len, PAGE_SIZE);
      const uint32_t perms = IOMMU_FLAG_PERM_READ | IOMMU_FLAG_PERM_WRITE;
      status = dev->SecondLevelMapIdentity(mem->base_addr, size, perms);
      if (status != ZX_OK) {
        return status;
      }
    }

    cursor_bytes += sizeof(*mem) + mem->scope_bytes;
  }

  return ZX_OK;
}

// Sets the root table pointer and invalidates the context-cache and IOTLB.
zx_status_t IommuImpl::SetRootTablePointerLocked(paddr_t pa) {
  DEBUG_ASSERT(IS_PAGE_ALIGNED(pa));

  auto root_table_addr = reg::RootTableAddress::Get().FromValue(0);
  // If we support extended contexts, use it.
  root_table_addr.set_root_table_type(supports_extended_context_);
  root_table_addr.set_root_table_address(pa >> PAGE_SIZE_SHIFT);
  root_table_addr.WriteTo(&mmio_);

  auto global_ctl = reg::GlobalControl::Get().ReadFrom(&mmio_);
  DEBUG_ASSERT(!global_ctl.translation_enable());
  global_ctl.set_root_table_ptr(1);
  global_ctl.WriteTo(&mmio_);
  zx_status_t status = WaitForValueLocked(&global_ctl, &decltype(global_ctl)::root_table_ptr, 1,
                                          zx_time_add_duration(current_time(), ZX_SEC(1)));
  if (status != ZX_OK) {
    LTRACEF("Timed out waiting for root_table_ptr bit to take\n");
    return status;
  }

  InvalidateContextCacheGlobalLocked();
  InvalidateIotlbGlobalLocked();

  return ZX_OK;
}

zx_status_t IommuImpl::SetTranslationEnableLocked(bool enabled, zx_time_t deadline) {
  auto global_ctl = reg::GlobalControl::Get().ReadFrom(&mmio_);
  global_ctl.set_translation_enable(enabled);
  global_ctl.WriteTo(&mmio_);

  return WaitForValueLocked(&global_ctl, &decltype(global_ctl)::translation_enable, enabled,
                            deadline);
}

void IommuImpl::InvalidateContextCacheGlobalLocked() {
  DEBUG_ASSERT(lock_.lock().IsHeld());

  auto context_cmd = reg::ContextCommand::Get().FromValue(0);
  context_cmd.set_invld_context_cache(1);
  context_cmd.set_invld_request_granularity(reg::ContextCommand::kGlobalInvld);
  context_cmd.WriteTo(&mmio_);

  WaitForValueLocked(&context_cmd, &decltype(context_cmd)::invld_context_cache, 0,
                     ZX_TIME_INFINITE);
}

void IommuImpl::InvalidateContextCacheDomainLocked(uint32_t domain_id) {
  DEBUG_ASSERT(lock_.lock().IsHeld());

  auto context_cmd = reg::ContextCommand::Get().FromValue(0);
  context_cmd.set_invld_context_cache(1);
  context_cmd.set_invld_request_granularity(reg::ContextCommand::kDomainInvld);
  context_cmd.set_domain_id(domain_id);
  context_cmd.WriteTo(&mmio_);

  WaitForValueLocked(&context_cmd, &decltype(context_cmd)::invld_context_cache, 0,
                     ZX_TIME_INFINITE);
}

void IommuImpl::InvalidateContextCacheGlobal() {
  Guard<Mutex> guard{&lock_};
  InvalidateContextCacheGlobalLocked();
}

void IommuImpl::InvalidateContextCacheDomain(uint32_t domain_id) {
  Guard<Mutex> guard{&lock_};
  InvalidateContextCacheDomainLocked(domain_id);
}

void IommuImpl::InvalidateIotlbGlobalLocked() {
  DEBUG_ASSERT(lock_.lock().IsHeld());
  ASSERT(!caps_.required_write_buf_flushing());

  // TODO(teisenbe): Read/write draining?
  auto iotlb_invld = reg::IotlbInvalidate::Get(iotlb_reg_offset_).ReadFrom(&mmio_);
  iotlb_invld.set_invld_iotlb(1);
  iotlb_invld.set_invld_request_granularity(reg::IotlbInvalidate::kGlobalInvld);
  iotlb_invld.WriteTo(&mmio_);

  WaitForValueLocked(&iotlb_invld, &decltype(iotlb_invld)::invld_iotlb, 0, ZX_TIME_INFINITE);
}

void IommuImpl::InvalidateIotlbDomainAllLocked(uint32_t domain_id) {
  DEBUG_ASSERT(lock_.lock().IsHeld());
  ASSERT(!caps_.required_write_buf_flushing());

  // TODO(teisenbe): Read/write draining?
  auto iotlb_invld = reg::IotlbInvalidate::Get(iotlb_reg_offset_).ReadFrom(&mmio_);
  iotlb_invld.set_invld_iotlb(1);
  iotlb_invld.set_invld_request_granularity(reg::IotlbInvalidate::kDomainAllInvld);
  iotlb_invld.set_domain_id(domain_id);
  iotlb_invld.WriteTo(&mmio_);

  WaitForValueLocked(&iotlb_invld, &decltype(iotlb_invld)::invld_iotlb, 0, ZX_TIME_INFINITE);
}

void IommuImpl::InvalidateIotlbPageLocked(uint32_t domain_id, dev_vaddr_t vaddr, uint pages_pow2) {
  DEBUG_ASSERT(lock_.lock().IsHeld());
  DEBUG_ASSERT(IS_PAGE_ALIGNED(vaddr));
  DEBUG_ASSERT(pages_pow2 < 64);
  DEBUG_ASSERT(pages_pow2 <= caps_.max_addr_mask_value());
  ASSERT(!caps_.required_write_buf_flushing());

  auto invld_addr = reg::InvalidateAddress::Get(iotlb_reg_offset_).FromValue(0);
  invld_addr.set_address(vaddr >> 12);
  invld_addr.set_invld_hint(0);
  invld_addr.set_address_mask(pages_pow2);
  invld_addr.WriteTo(&mmio_);

  // TODO(teisenbe): Read/write draining?
  auto iotlb_invld = reg::IotlbInvalidate::Get(iotlb_reg_offset_).ReadFrom(&mmio_);
  iotlb_invld.set_invld_iotlb(1);
  iotlb_invld.set_invld_request_granularity(reg::IotlbInvalidate::kDomainPageInvld);
  iotlb_invld.set_domain_id(domain_id);
  iotlb_invld.WriteTo(&mmio_);

  WaitForValueLocked(&iotlb_invld, &decltype(iotlb_invld)::invld_iotlb, 0, ZX_TIME_INFINITE);
}

void IommuImpl::InvalidateIotlbGlobal() {
  Guard<Mutex> guard{&lock_};
  InvalidateIotlbGlobalLocked();
}

void IommuImpl::InvalidateIotlbDomainAll(uint32_t domain_id) {
  Guard<Mutex> guard{&lock_};
  InvalidateIotlbDomainAllLocked(domain_id);
}

template <class RegType>
zx_status_t IommuImpl::WaitForValueLocked(RegType* reg,
                                          typename RegType::ValueType (RegType::*getter)() const,
                                          typename RegType::ValueType value, zx_time_t deadline) {
  DEBUG_ASSERT(lock_.lock().IsHeld());

  const zx_time_t kMaxSleepDuration = ZX_USEC(10);

  while (true) {
    // Read the register and check if it matches the expected value.  If
    // not, sleep for a bit and try again.
    reg->ReadFrom(&mmio_);
    if ((reg->*getter)() == value) {
      return ZX_OK;
    }

    const zx_time_t now = current_time();
    if (now > deadline) {
      break;
    }

    zx_time_t sleep_deadline = fbl::min(zx_time_add_duration(now, kMaxSleepDuration), deadline);
    thread_sleep(sleep_deadline);
  }
  return ZX_ERR_TIMED_OUT;
}

interrupt_eoi IommuImpl::FaultHandler(void* ctx) {
  auto self = static_cast<IommuImpl*>(ctx);
  auto status = reg::FaultStatus::Get().ReadFrom(&self->mmio_);

  if (!status.primary_pending_fault()) {
    TRACEF("Non primary fault\n");
    return IRQ_EOI_DEACTIVATE;
  }

  auto caps = reg::Capability::Get().ReadFrom(&self->mmio_);
  const uint32_t num_regs = static_cast<uint32_t>(caps.num_fault_recording_reg() + 1);
  const uint32_t reg_offset = static_cast<uint32_t>(caps.fault_recording_register_offset() * 16);

  uint32_t index = status.fault_record_index();
  while (1) {
    auto rec_high = reg::FaultRecordHigh::Get(reg_offset, index).ReadFrom(&self->mmio_);
    if (!rec_high.fault()) {
      break;
    }
    auto rec_low = reg::FaultRecordLow::Get(reg_offset, index).ReadFrom(&self->mmio_);
    uint64_t source = rec_high.source_id();
    TRACEF(
        "IOMMU Fault: access %c, PASID (%c) %#04lx, reason %#02lx, source %02lx:%02lx.%lx, info: "
        "%lx\n",
        rec_high.request_type() ? 'R' : 'W', rec_high.pasid_present() ? 'V' : '-',
        rec_high.pasid_value(), rec_high.fault_reason(), source >> 8, (source >> 3) & 0x1f,
        source & 0x7, rec_low.fault_info() << 12);

    // Clear this fault (RW1CS)
    rec_high.WriteTo(&self->mmio_);

    ++index;
    if (index >= num_regs) {
      index -= num_regs;
    }
  }

  status.set_reg_value(0);
  // Clear the primary fault overflow condition (RW1CS)
  // TODO(teisenbe): How do we guarantee we get an interrupt on the next fault/if we left a fault
  // unprocessed?
  status.set_primary_fault_overflow(1);
  status.WriteTo(&self->mmio_);
  return IRQ_EOI_DEACTIVATE;
}

zx_status_t IommuImpl::ConfigureFaultEventInterruptLocked() {
  DEBUG_ASSERT(lock_.lock().IsHeld());

  if (!msi_is_supported()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  DEBUG_ASSERT(!irq_block_.allocated);
  zx_status_t status =
      msi_alloc_block(1, false /* can_target_64bit */, false /* msi x */, &irq_block_);
  if (status != ZX_OK) {
    return status;
  }

  auto event_data = reg::FaultEventData::Get().FromValue(irq_block_.tgt_data);
  auto event_addr =
      reg::FaultEventAddress::Get().FromValue(static_cast<uint32_t>(irq_block_.tgt_addr));
  auto event_upper_addr = reg::FaultEventUpperAddress::Get().FromValue(
      static_cast<uint32_t>(irq_block_.tgt_addr >> 32));

  event_data.WriteTo(&mmio_);
  event_addr.WriteTo(&mmio_);
  event_upper_addr.WriteTo(&mmio_);

  // Clear all primary fault records
  for (uint32_t i = 0; i < num_fault_recording_reg_; ++i) {
    const uint32_t offset = fault_recording_reg_offset_;
    auto record_high = reg::FaultRecordHigh::Get(offset, i).ReadFrom(&mmio_);
    record_high.WriteTo(&mmio_);
  }

  // Clear all pending faults
  auto fault_status_ctl = reg::FaultStatus::Get().ReadFrom(&mmio_);
  fault_status_ctl.WriteTo(&mmio_);

  msi_register_handler(&irq_block_, 0, FaultHandler, this);

  // Unmask interrupts
  auto fault_event_ctl = reg::FaultEventControl::Get().ReadFrom(&mmio_);
  fault_event_ctl.set_interrupt_mask(0);
  fault_event_ctl.WriteTo(&mmio_);

  return ZX_OK;
}

void IommuImpl::DisableFaultsLocked() {
  auto fault_event_ctl = reg::FaultEventControl::Get().ReadFrom(&mmio_);
  fault_event_ctl.set_interrupt_mask(1);
  fault_event_ctl.WriteTo(&mmio_);
}

zx_status_t IommuImpl::GetOrCreateContextTableLocked(ds::Bdf bdf, ContextTableState** tbl) {
  DEBUG_ASSERT(lock_.lock().IsHeld());

  volatile ds::RootTable* root_table = this->root_table();
  DEBUG_ASSERT(root_table);

  volatile ds::RootEntrySubentry* target_entry = &root_table->entry[bdf.bus()].lower;
  if (supports_extended_context_ && bdf.dev() >= 16) {
    // If this is an extended root table and the device is in the upper half
    // of the bus address space, use the upper pointer.
    target_entry = &root_table->entry[bdf.bus()].upper;
  }

  ds::RootEntrySubentry entry;
  entry.ReadFrom(target_entry);
  if (entry.present()) {
    // We know the entry exists, so search our list of tables for it.
    for (ContextTableState& context_table : context_tables_) {
      if (context_table.includes_bdf(bdf)) {
        *tbl = &context_table;
        return ZX_OK;
      }
    }
  }

  // Couldn't find the ContextTable, so create it.
  ktl::unique_ptr<ContextTableState> table;
  zx_status_t status =
      ContextTableState::Create(static_cast<uint8_t>(bdf.bus()), supports_extended_context_,
                                bdf.dev() >= 16 /* upper */, this, target_entry, &table);
  if (status != ZX_OK) {
    return status;
  }

  *tbl = table.get();
  context_tables_.push_back(ktl::move(table));

  return ZX_OK;
}

zx_status_t IommuImpl::GetOrCreateDeviceContextLocked(ds::Bdf bdf, DeviceContext** context) {
  DEBUG_ASSERT(lock_.lock().IsHeld());

  ContextTableState* ctx_table_state;
  zx_status_t status = GetOrCreateContextTableLocked(bdf, &ctx_table_state);
  if (status != ZX_OK) {
    return status;
  }

  status = ctx_table_state->GetDeviceContext(bdf, context);
  if (status != ZX_ERR_NOT_FOUND) {
    // Either status was ZX_OK and we're done, or some error occurred.
    return status;
  }

  uint32_t domain_id;
  status = domain_allocator_.Allocate(&domain_id);
  if (status != ZX_OK) {
    return status;
  }
  return ctx_table_state->CreateDeviceContext(bdf, domain_id, context);
}

uint64_t IommuImpl::minimum_contiguity(uint64_t bus_txn_id) {
  if (!IsValidBusTxnId(bus_txn_id)) {
    return 0;
  }

  ds::Bdf bdf = decode_bus_txn_id(bus_txn_id);

  Guard<Mutex> guard{&lock_};
  DeviceContext* dev;
  zx_status_t status = GetOrCreateDeviceContextLocked(bdf, &dev);
  if (status != ZX_OK) {
    return status;
  }

  return dev->minimum_contiguity();
}

uint64_t IommuImpl::aspace_size(uint64_t bus_txn_id) {
  if (!IsValidBusTxnId(bus_txn_id)) {
    return 0;
  }

  ds::Bdf bdf = decode_bus_txn_id(bus_txn_id);

  Guard<Mutex> guard{&lock_};
  DeviceContext* dev;
  zx_status_t status = GetOrCreateDeviceContextLocked(bdf, &dev);
  if (status != ZX_OK) {
    return status;
  }

  return dev->aspace_size();
}

}  // namespace intel_iommu
