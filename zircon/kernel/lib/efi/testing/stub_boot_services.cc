// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/efi/testing/stub_boot_services.h>
#include <stdio.h>
#include <stdlib.h>

namespace efi {

namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;

// Defined by the EFI standard.
constexpr size_t kMemoryPageSize = 4096;

// We need to stash the global StubBootServices object here since there's
// no "self" parameter to any of these functions.
StubBootServices* active_stub = nullptr;

// Wrapper to bounce the EFI C function pointer into our global StubBootServices
// object.
template <auto func, typename... Args>
EFIAPI efi_status Wrap(Args... args) {
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
          .AllocatePages = Wrap<&StubBootServices::AllocatePages>,
          .FreePages = Wrap<&StubBootServices::FreePages>,
          .GetMemoryMap = Wrap<&StubBootServices::GetMemoryMap>,
          .AllocatePool = Wrap<&StubBootServices::AllocatePool>,
          .FreePool = Wrap<&StubBootServices::FreePool>,
          .CreateEvent = Wrap<&StubBootServices::CreateEvent>,
          .SetTimer = Wrap<&StubBootServices::SetTimer>,
          .CloseEvent = Wrap<&StubBootServices::CloseEvent>,
          .CheckEvent = Wrap<&StubBootServices::CheckEvent>,
          .LocateHandle = Wrap<&StubBootServices::LocateHandle>,
          .OpenProtocol = Wrap<&StubBootServices::OpenProtocol>,
          .CloseProtocol = Wrap<&StubBootServices::CloseProtocol>,
          .LocateHandleBuffer = Wrap<&StubBootServices::LocateHandleBuffer>,
          .LocateProtocol = Wrap<&StubBootServices::LocateProtocol>,
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

efi_status StubBootServices::AllocatePages(efi_allocate_type /*type*/,
                                           efi_memory_type /*memory_type*/, size_t pages,
                                           efi_physical_addr* memory) {
  *memory = reinterpret_cast<efi_physical_addr>(malloc(pages * kMemoryPageSize));
  return EFI_SUCCESS;
}

efi_status StubBootServices::FreePages(efi_physical_addr memory, size_t /*pages*/) {
  free(reinterpret_cast<void*>(memory));
  return EFI_SUCCESS;
}

efi_status StubBootServices::AllocatePool(efi_memory_type /*pool_type*/, size_t size, void** buf) {
  *buf = malloc(size);
  return EFI_SUCCESS;
}

efi_status StubBootServices::FreePool(void* buf) {
  free(buf);
  return EFI_SUCCESS;
}

void MockBootServices::ExpectOpenProtocol(efi_handle handle, efi_guid guid, void* protocol) {
  EXPECT_CALL(*this, OpenProtocol(handle, MatchGuid(guid), _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(protocol), Return(EFI_SUCCESS)));
}

void MockBootServices::ExpectCloseProtocol(efi_handle handle, efi_guid guid) {
  EXPECT_CALL(*this, CloseProtocol(handle, MatchGuid(guid), _, _)).WillOnce(Return(EFI_SUCCESS));
}

void MockBootServices::SetDefaultProtocol(efi_handle handle, efi_guid guid, void* protocol) {
  ON_CALL(*this, OpenProtocol(handle, MatchGuid(guid), _, _, _, _))
      .WillByDefault(DoAll(SetArgPointee<2>(protocol), Return(EFI_SUCCESS)));
  ON_CALL(*this, CloseProtocol(handle, MatchGuid(guid), _, _)).WillByDefault(Return(EFI_SUCCESS));
}

}  // namespace efi
