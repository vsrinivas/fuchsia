// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_EFI_INCLUDE_PHYS_EFI_PROTOCOL_H_
#define ZIRCON_KERNEL_PHYS_EFI_INCLUDE_PHYS_EFI_PROTOCOL_H_

#include <lib/fit/result.h>

#include <memory>
#include <type_traits>

#include <efi/types.h>

// These are convenience wrappers around OpenProtocol and CloseProtocol from
// efi_boot_services.  These functions are not usually used directly, but
// instead the EfiOpenProtocol<Protocol> template should be used and then
// CloseProtocol calls are automatic via RAII.
fit::result<efi_status, efi_handle> EfiOpenProtocol(efi_handle handle, const efi_guid& guid);
void EfiCloseProtocol(const efi_guid& guid, efi_handle protocol);

// This is defined below.
template <class Protocol>
struct EfiProtocolPtrDeleter;

// EfiProtocolPtr<efi_foobar_protocol> is a smart pointer for pointers
// returned by EfiOpenProtocol<efi_foobar_protocol>.  To use this, do
// a specialization like:
// ```
// template<>
// inline constexpr const elf_guid& kEfiProtocolGuid<efi_foobar_protocol> = &FoobarProtocol;
// ```
template <class Protocol>
using EfiProtocolPtr = std::unique_ptr<Protocol, EfiProtocolPtrDeleter<Protocol>>;

// This must be specialized for each protocol appropriately.
template <class Protocol>
inline constexpr efi_guid kEfiProtocolGuid =
    []() { static_assert(!std::is_same_v<Protocol, Protocol>, "missing specialization"); }();

// This does OpenProtocol on the given handle.  It returns a null pointer if
// OpenProtocol fails (with no efi_status details).  The returned move-only
// smart pointer type will automatically call CloseProtocol on destruction.
template <class Protocol>
inline fit::result<efi_status, EfiProtocolPtr<Protocol>> EfiOpenProtocol(efi_handle handle) {
  auto result = EfiOpenProtocol(handle, kEfiProtocolGuid<Protocol>);
  if (result.is_error()) {
    return result.take_error();
  }
  Protocol* ptr = static_cast<Protocol*>(result.value());
  return fit::ok(EfiProtocolPtr<Protocol>(ptr));
}

// Custom unique_ptr deleter instantiated for each efi_*_protocol type.
template <class Protocol>
struct EfiProtocolPtrDeleter {
  void operator()(Protocol* handle) const {
    EfiCloseProtocol(kEfiProtocolGuid<Protocol>, static_cast<efi_handle>(handle));
  }
};

// Checks to see whether a given protocol interface is present on a handle.
bool EfiHasProtocol(efi_handle handle, const efi_guid& guid);

template <class Protocol>
inline bool EfiHasProtocol(efi_handle handle) {
  return EfiHasProtocol(handle, kEfiProtocolGuid<Protocol>);
}

#endif  // ZIRCON_KERNEL_PHYS_EFI_INCLUDE_PHYS_EFI_PROTOCOL_H_
