// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_UNITTEST_INCLUDE_LIB_UNITTEST_USER_MEMORY_H_
#define ZIRCON_KERNEL_LIB_UNITTEST_INCLUDE_LIB_UNITTEST_USER_MEMORY_H_

#include <lib/user_copy/user_ptr.h>

#include <ktl/move.h>
#include <ktl/unique_ptr.h>
#include <vm/pmm.h>
#include <vm/scanner.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object_paged.h>

namespace testing {

// UserMemory facilitates testing code that requires user memory.
//
// Example:
//    unique_ptr<UserMemory> mem = UserMemory::Create(sizeof(thing));
//    auto mem_out = make_user_out_ptr(mem->out());
//    mem_out.copy_array_to_user(&thing, sizeof(thing));
//
class UserMemory {
 public:
  static ktl::unique_ptr<UserMemory> Create(size_t size);
  static ktl::unique_ptr<UserMemory> Create(fbl::RefPtr<VmObject> vmo);
  virtual ~UserMemory();

  vaddr_t base() const { return mapping_->base(); }

  const fbl::RefPtr<VmObject>& vmo() const { return vmo_; }

  const fbl::RefPtr<VmAspace>& aspace() const { return mapping_->aspace(); }

  template <typename T>
  void put(const T& value, size_t i = 0) {
    zx_status_t status = user_out<T>().element_offset(i).copy_to_user(value);
    ASSERT(status == ZX_OK);
  }

  template <typename T>
  T get(size_t i = 0) {
    T value;
    zx_status_t status = user_in<T>().element_offset(i).copy_from_user(&value);
    ASSERT(status == ZX_OK);
    return value;
  }

  template <typename T>
  user_out_ptr<T> user_out() {
    return make_user_out_ptr(reinterpret_cast<T*>(base()));
  }

  template <typename T>
  user_in_ptr<const T> user_in() {
    return make_user_in_ptr(reinterpret_cast<const T*>(base()));
  }

  // Ensures the mapping is committed and mapped such that usages will cause no faults.
  zx_status_t CommitAndMap(size_t size) { return mapping_->MapRange(0, size, true); }

  // Read or write to the underlying VMO directly, bypassing the mapping.
  zx_status_t VmoRead(void* ptr, uint64_t offset, uint64_t len) {
    ASSERT(vmo_);
    return vmo_->Read(ptr, offset, len);
  }
  zx_status_t VmoWrite(const void* ptr, uint64_t offset, uint64_t len) {
    ASSERT(vmo_);
    return vmo_->Write(ptr, offset, len);
  }

 private:
  UserMemory(fbl::RefPtr<VmMapping> mapping, fbl::RefPtr<VmObject> vmo)
      : mapping_(ktl::move(mapping)), vmo_(ktl::move(vmo)) {}

  fbl::RefPtr<VmMapping> mapping_;
  fbl::RefPtr<VmObject> vmo_;

  // User memory here is going to be touched directly by the kernel and will not have the option to
  // fault in memory that should get reclaimed by the scanner. Therefore as long as we are using any
  // UserMemory we should disable the scanner.
  AutoVmScannerDisable scanner_disable_;
};

}  // namespace testing

#endif  // ZIRCON_KERNEL_LIB_UNITTEST_INCLUDE_LIB_UNITTEST_USER_MEMORY_H_
