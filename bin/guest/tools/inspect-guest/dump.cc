// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/tools/inspect-guest/dump.h"

#include <pretty/hexdump.h>
#include <iostream>

#include "garnet/bin/guest/tools/inspect-guest/connect.h"
#include "garnet/lib/machina/fidl/inspect.fidl.h"
#include "lib/fsl/tasks/message_loop.h"

// Inspect service of the guest.
static fidl::InterfacePtr<machina::InspectService> inspect_service;

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
      zx::vmar::root_self().map(0 /* vmar_offset */, vmo, 0 /* vmo_offset */,
                                vmo_size, ZX_VM_FLAG_PERM_READ, &guest_addr);
  if (status != ZX_OK) {
    std::cerr << "Failed to map guest memory\n";
    return;
  }

  std::cout << std::hex << "[0x" << addr << ", 0x" << addr + len << "] of 0x"
            << vmo_size << ":\n";
  hexdump_ex(reinterpret_cast<void*>(guest_addr + addr), len, addr);
}

void handle_dump(zx_vaddr_t addr, size_t len) {
  zx_status_t status = connect(&inspect_service);
  if (status != ZX_OK) {
    return;
  }
  inspect_service->FetchGuestMemory([addr, len](zx::vmo vmo) {
    dump(fbl::move(vmo), addr, len);
    fsl::MessageLoop::GetCurrent()->PostQuitTask();
  });
}
