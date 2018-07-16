// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/cli/dump.h"

#include <fuchsia/guest/cpp/fidl.h>
#include <pretty/hexdump.h>
#include <iostream>

#include "lib/app/cpp/environment_services.h"

static void dump(zx::vmo vmo, zx_vaddr_t addr, size_t len) {
  uint64_t vmo_size;
  zx_status_t status = vmo.get_size(&vmo_size);
  if (status != ZX_OK) {
    std::cerr << "Failed to get guest memory size\n";
    return;
  } else if (addr > vmo_size || addr > vmo_size - len) {
    std::cerr << "Range exceeds guest memory\n";
    return;
  }
  uintptr_t guest_addr;
  status =
      zx::vmar::root_self()->map(0 /* vmar_offset */, vmo, 0 /* vmo_offset */,
                                 vmo_size, ZX_VM_FLAG_PERM_READ, &guest_addr);
  if (status != ZX_OK) {
    std::cerr << "Failed to map guest memory\n";
    return;
  }

  std::cout << std::hex << "[0x" << addr << ", 0x" << addr + len << "] of 0x"
            << vmo_size << ":\n";
  hexdump_ex(reinterpret_cast<void*>(guest_addr + addr), len, addr);
}

void handle_dump(uint32_t env_id, uint32_t cid, zx_vaddr_t addr, size_t len) {
  // Connect to environment.
  fuchsia::guest::GuestManagerSyncPtr guestmgr;
  fuchsia::sys::ConnectToEnvironmentService(guestmgr.NewRequest());
  fuchsia::guest::GuestEnvironmentSyncPtr env_ptr;
  guestmgr->ConnectToEnvironment(env_id, env_ptr.NewRequest());

  fuchsia::guest::GuestControllerSyncPtr guest_controller;
  env_ptr->ConnectToGuest(cid, guest_controller.NewRequest());

  // Fetch the VMO and dump.
  zx::vmo vmo;
  guest_controller->GetPhysicalMemory(&vmo);
  if (vmo) {
    dump(std::move(vmo), addr, len);
  }
}
