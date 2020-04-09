// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_USERABI_INCLUDE_LIB_USERABI_RODSO_H_
#define ZIRCON_KERNEL_LIB_USERABI_INCLUDE_LIB_USERABI_RODSO_H_

#include <align.h>
#include <object/handle.h>
#include <object/vm_object_dispatcher.h>

// An EmbeddedVmo object describes a page-aligned file embedded in the kernel.
class EmbeddedVmo {
 public:
  EmbeddedVmo() = delete;

  // The EmbeddedVmo will retain a RefPtr to the created VmObjectDispatcher,
  // but ownership of the wrapping handle is given to the caller.
  EmbeddedVmo(const char* name, const void* image, size_t size,
              KernelHandle<VmObjectDispatcher>* vmo_kernel_handle);

  const auto& vmo() const { return vmo_; }

  size_t size() const { return size_; }

  zx_rights_t vmo_rights() const { return vmo_rights_; }

 protected:
  zx_status_t MapSegment(fbl::RefPtr<VmAddressRegionDispatcher> vmar, bool code, size_t vmar_offset,
                         size_t start_offset, size_t end_offset) const;

 private:
  const char* name_;
  fbl::RefPtr<VmObjectDispatcher> vmo_;
  zx_rights_t vmo_rights_;
  size_t size_;
};

// An RoDso object describes one DSO image built with the rodso.ld layout.
class RoDso : public EmbeddedVmo {
 public:
  bool valid_code_mapping(uint64_t vmo_offset, size_t code_size) const {
    return vmo_offset == code_start_ && code_size == size() - code_start_;
  }

  zx_status_t Map(fbl::RefPtr<VmAddressRegionDispatcher> vmar, size_t offset) const;

 protected:
  RoDso(const char* name, const void* image, size_t size, uintptr_t code_start,
        KernelHandle<VmObjectDispatcher>* vmo_kernel_handle)
      : EmbeddedVmo(name, image, size, vmo_kernel_handle), code_start_(code_start) {
    DEBUG_ASSERT(code_start > 0);
    DEBUG_ASSERT(code_start < size);
    DEBUG_ASSERT(IS_PAGE_ALIGNED(code_start));
  }

 private:
  uintptr_t code_start_;
};

#endif  // ZIRCON_KERNEL_LIB_USERABI_INCLUDE_LIB_USERABI_RODSO_H_
