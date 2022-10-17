// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/guest_dispatcher.h"

#include <lib/counters.h>
#include <zircon/rights.h>

#include <arch/hypervisor.h>
#include <fbl/alloc_checker.h>
#include <object/vm_address_region_dispatcher.h>

KCOUNTER(dispatcher_guest_create_count, "dispatcher.guest.create")
KCOUNTER(dispatcher_guest_destroy_count, "dispatcher.guest.destroy")

namespace {

zx::result<ktl::unique_ptr<Guest>> CreateGuest(uint32_t options) {
  switch (options) {
    case ZX_GUEST_OPT_NORMAL:
      return NormalGuest::Create();
    case ZX_GUEST_OPT_DIRECT:
#if ARCH_X86
      return DirectGuest::Create();
#else
      return zx::error(ZX_ERR_NOT_SUPPORTED);
#endif  // ARCH_X86
    default:
      return zx::error(ZX_ERR_INVALID_ARGS);
  }
}

}  // namespace

// static
zx_status_t GuestDispatcher::Create(uint32_t options, KernelHandle<GuestDispatcher>* guest_handle,
                                    zx_rights_t* guest_rights,
                                    KernelHandle<VmAddressRegionDispatcher>* vmar_handle,
                                    zx_rights_t* vmar_rights) {
  auto guest = CreateGuest(options);
  if (guest.is_error()) {
    return guest.status_value();
  }
  fbl::RefPtr<VmAddressRegion> vmar = (*guest)->RootVmar();

  fbl::AllocChecker ac;
  KernelHandle new_guest_handle(
      fbl::AdoptRef(new (&ac) GuestDispatcher(options, ktl::move(*guest))));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  uint mmu_flags = options == ZX_GUEST_OPT_DIRECT ? ARCH_MMU_FLAG_PERM_USER : 0;
  zx_status_t status =
      VmAddressRegionDispatcher::Create(std::move(vmar), mmu_flags, vmar_handle, vmar_rights);
  if (status != ZX_OK) {
    return status;
  }

  *guest_rights = default_rights();
  *guest_handle = ktl::move(new_guest_handle);
  return ZX_OK;
}

GuestDispatcher::GuestDispatcher(uint32_t options, ktl::unique_ptr<Guest> guest)
    : options_(options), guest_(ktl::move(guest)) {
  kcounter_add(dispatcher_guest_create_count, 1);
}

GuestDispatcher::~GuestDispatcher() { kcounter_add(dispatcher_guest_destroy_count, 1); }

zx_status_t GuestDispatcher::SetTrap(uint32_t kind, zx_vaddr_t addr, size_t len,
                                     fbl::RefPtr<PortDispatcher> port, uint64_t key) {
  canary_.Assert();
  if (options_ == ZX_GUEST_OPT_NORMAL) {
    return static_cast<NormalGuest*>(guest_.get())->SetTrap(kind, addr, len, ktl::move(port), key);
  }
  return ZX_ERR_NOT_SUPPORTED;
}
