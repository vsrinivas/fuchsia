// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <align.h>
#include <zircon/syscalls/hypervisor.h>

#include <arch/hypervisor.h>
#include <hypervisor/guest_physical_address_space.h>
#include <vm/pmm.h>

// static
zx_status_t Guest::Create(ktl::unique_ptr<Guest>* out) {
  return ZX_OK;
}

Guest::Guest(uint8_t vmid) : vmid_(vmid) {}

Guest::~Guest() { }

zx_status_t Guest::SetTrap(uint32_t kind, zx_gpaddr_t addr, size_t len,
                           fbl::RefPtr<PortDispatcher> port, uint64_t key) {
  return ZX_OK;
}

zx_status_t Guest::AllocVpid(uint8_t* vpid) {
  return ZX_OK;
}

zx_status_t Guest::FreeVpid(uint8_t vpid) {
  return ZX_OK;
}
