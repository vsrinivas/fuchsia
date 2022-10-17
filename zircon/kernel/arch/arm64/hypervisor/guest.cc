// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <align.h>
#include <zircon/syscalls/hypervisor.h>

#include <arch/hypervisor.h>
#include <dev/interrupt/arm_gic_hw_interface.h>
#include <hypervisor/aspace.h>
#include <vm/pmm.h>

#include "el2_cpu_state_priv.h"

static constexpr zx_gpaddr_t kGicvAddress = 0x800001000;
static constexpr size_t kGicvSize = 0x2000;

// static
zx::result<ktl::unique_ptr<Guest>> Guest::Create() {
  if (arm64_get_boot_el() < 2) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  auto vmid = alloc_vmid();
  if (vmid.is_error()) {
    return vmid.take_error();
  }

  fbl::AllocChecker ac;
  ktl::unique_ptr<Guest> guest(new (&ac) Guest(*vmid));
  if (!ac.check()) {
    auto result = free_vmid(*vmid);
    ZX_ASSERT(result.is_ok());
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  auto gpa = hypervisor::GuestPhysicalAspace::Create();
  if (gpa.is_error()) {
    return gpa.take_error();
  }
  guest->gpa_ = ktl::move(*gpa);
  guest->gpa_.arch_aspace().arch_set_asid(*vmid);

  zx_paddr_t gicv_paddr;
  zx_status_t status = gic_get_gicv(&gicv_paddr);

  // If `status` is ZX_OK, we are running GICv2. We then need to map GICV.
  // If `status is ZX_ERR_NOT_FOUND, we are running GICv3.
  // Otherwise, return `status`.
  if (status == ZX_OK) {
    if (auto result = guest->gpa_.MapInterruptController(kGicvAddress, gicv_paddr, kGicvSize);
        result.is_error()) {
      return result.take_error();
    }
  } else if (status != ZX_ERR_NOT_FOUND) {
    return zx::error(status);
  }

  return zx::ok(ktl::move(guest));
}

Guest::Guest(uint16_t vmid) : vmid_(vmid) {}

Guest::~Guest() {
  auto result = free_vmid(vmid_);
  ZX_ASSERT(result.is_ok());
}

zx_status_t Guest::SetTrap(uint32_t kind, zx_gpaddr_t addr, size_t len,
                           fbl::RefPtr<PortDispatcher> port, uint64_t key) {
  switch (kind) {
    case ZX_GUEST_TRAP_MEM:
      if (port) {
        return ZX_ERR_INVALID_ARGS;
      }
      break;
    case ZX_GUEST_TRAP_BELL:
      if (!port) {
        return ZX_ERR_INVALID_ARGS;
      }
      break;
    case ZX_GUEST_TRAP_IO:
      return ZX_ERR_NOT_SUPPORTED;
    default:
      return ZX_ERR_INVALID_ARGS;
  }

  if (!IS_PAGE_ALIGNED(addr) || !IS_PAGE_ALIGNED(len)) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (auto result = gpa_.UnmapRange(addr, len); result.is_error()) {
    return result.status_value();
  }
  return traps_.InsertTrap(kind, addr, len, ktl::move(port), key).status_value();
}
