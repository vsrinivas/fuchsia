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

// Wrapper for BootServices functions which return void.
template <auto func, typename... Args>
EFIAPI void WrapVoid(Args... args) {
  ASSERT_NE(active_stub, nullptr) << "BootServices stub does not exist";
  (active_stub->*func)(args...);
}

}  // namespace

StubBootServices::StubBootServices()
    : services_{.AllocatePages = Wrap<&StubBootServices::AllocatePages>,
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
                .CopyMem = WrapVoid<&StubBootServices::CopyMem>,
                .SetMem = WrapVoid<&StubBootServices::SetMem>} {
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

efi_status StubBootServices::AllocatePages(efi_allocate_type /*type*/, efi_memory_type memory_type,
                                           size_t pages, efi_physical_addr* memory) {
  void* addr = nullptr;
  efi_status result = AllocatePool(memory_type, pages * kMemoryPageSize, &addr);
  *memory = reinterpret_cast<efi_physical_addr>(addr);
  return result;
}

efi_status StubBootServices::FreePages(efi_physical_addr memory, size_t /*pages*/) {
  free(reinterpret_cast<void*>(memory));
  return EFI_SUCCESS;
}

efi_status StubBootServices::AllocatePool(efi_memory_type /*pool_type*/, size_t size, void** buf) {
  *buf = malloc(size);
  if (*buf == nullptr) {
    ADD_FAILURE() << "Failed to allocate " << size << " bytes";
    return EFI_OUT_OF_RESOURCES;
  }

  // Initialize to some garbage to try to catch any code that might be assuming
  // memory will always be 0-initialized.
  memset(*buf, 0x5A, size);
  return EFI_SUCCESS;
}

efi_status StubBootServices::FreePool(void* buf) {
  free(buf);
  return EFI_SUCCESS;
}

void StubBootServices::CopyMem(void* dest, const void* src, size_t len) {
  ASSERT_NE(dest, nullptr) << "CopyMem() should always supply a valid destination buffer";
  ASSERT_NE(src, nullptr) << "CopyMem() should always supply a valid source buffer";
  // Use memmove() rather than memcpy(); the UEFI CopyMem() function supports
  // overlapping buffers.
  memmove(dest, src, len);
}

void StubBootServices::SetMem(void* buf, size_t len, uint8_t val) {
  ASSERT_NE(buf, nullptr) << "SetMem() should always supply a valid buffer";
  memset(buf, val, len);
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
