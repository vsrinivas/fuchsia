// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_PC_EFI_H_
#define ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_PC_EFI_H_

#include <lib/instrumentation/asan.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <ktl/algorithm.h>
#include <vm/vm_aspace.h>

extern "C" {
#include <efi/runtime-services.h>
#include <efi/system-table.h>
}

class EfiServicesActivation;

// Initialize data structures for EFI services.
NO_ASAN zx_status_t InitEfiServices(uint64_t efi_system_table);

// Activate EFI services.
//
// Calling this function will attempt to activate the address space containing
// EFI services, and return a scoped object that provides a pointer. When the
// object is destroyed, the previous address space will be restored.
//
// Returns nullptr if no EFI services are available.
//
// WARNING: Users of the pointer returned by this function  must be tagged
// with the NO_ASAN attribute to avoid crashes when running under KASAN.
NO_ASAN EfiServicesActivation TryActivateEfiServices();

// Manages access to "efi_runtime_services" and restoration of the previous
// address space.
class EfiServicesActivation {
 public:
  EfiServicesActivation() = default;
  ~EfiServicesActivation() { reset(); }
  static EfiServicesActivation Null() { return EfiServicesActivation{}; }

  // Prevent copy, allow move.
  EfiServicesActivation(const EfiServicesActivation&) = delete;
  EfiServicesActivation& operator=(const EfiServicesActivation&) = delete;
  EfiServicesActivation(EfiServicesActivation&& other) noexcept { swap(other); }
  EfiServicesActivation& operator=(EfiServicesActivation&& other) noexcept {
    swap(other);
    return *this;
  }

  // Return true if there is a valid EFI services pointer.
  bool valid() const { return services_ != nullptr; }

  // Destroy this object, and restore the previous address space.
  void reset();

  // Get pointer to EFI services.
  efi_runtime_services* get() const { return services_; }
  efi_runtime_services* operator->() const { return services_; }
  efi_runtime_services& operator*() const { return *services_; }

  // Swap with another element.
  void swap(EfiServicesActivation& other) {
    ktl::swap(previous_aspace_, other.previous_aspace_);
    ktl::swap(services_, other.services_);
  }

 private:
  friend EfiServicesActivation TryActivateEfiServices();
  explicit EfiServicesActivation(VmAspace* previous_aspace, efi_runtime_services* services)
      : previous_aspace_(previous_aspace), services_(services) {}

  VmAspace* previous_aspace_ = nullptr;
  efi_runtime_services* services_ = nullptr;
};

#endif  // ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_PC_EFI_H_
