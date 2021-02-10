// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/efi/testing/stub_boot_services.h>
#include <stdio.h>
#include <stdlib.h>

namespace efi {

namespace {

// We need to stash the global StubBootServices object here since there's
// no "self" parameter to any of these functions.
StubBootServices* active_stub = nullptr;

// Wrapper to bounce the EFI C function pointer into our global StubBootServices
// object.
template <auto func, typename... Args>
efi_status Wrap(Args... args) {
  if (!active_stub) {
    // Someone held onto the underlying function table after deleting
    // the parent StubBootServices.
    return EFI_NOT_READY;
  }
  return (active_stub->*func)(args...);
}

}  // namespace

StubBootServices::StubBootServices()
    : services_{
          .AllocatePool = Wrap<&StubBootServices::AllocatePool>,
          .FreePool = Wrap<&StubBootServices::FreePool>,
          .OpenProtocol = Wrap<&StubBootServices::OpenProtocol>,
          .CloseProtocol = Wrap<&StubBootServices::CloseProtocol>,
          .LocateHandleBuffer = Wrap<&StubBootServices::LocateHandleBuffer>,
      } {
  if (active_stub) {
    // We cannot support multiple StubBootServices due to the global singleton
    // nature. Rather than causing hard-to-debug test behavior here, just fail
    // loudly and immediately.
    fprintf(stderr, "ERROR: cannot create multiple StubBootService objects - exiting\n");
    exit(1);
  }
  active_stub = this;
}

StubBootServices::~StubBootServices() { active_stub = nullptr; }

efi_status StubBootServices::AllocatePool(efi_memory_type /*pool_type*/, size_t size, void** buf) {
  *buf = malloc(size);
  return EFI_SUCCESS;
}

efi_status StubBootServices::FreePool(void* buf) {
  free(buf);
  return EFI_SUCCESS;
}

}  // namespace efi
