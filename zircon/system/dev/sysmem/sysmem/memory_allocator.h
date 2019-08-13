// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/fit/function.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmo.h>

#include <map>

class MemoryAllocator {
 public:
  // Some sub-classes take this interface as a constructor param, which
  // enables a fake in tests where we don't have a real zx::bti etc.
  class Owner {
  public:
    virtual const zx::bti& bti() = 0;
    // TODO(ZX-4809): This shouldn't exist, but will be needed for VDEC unless we resolve ZX-4809.
    virtual zx_status_t CreatePhysicalVmo(uint64_t base, uint64_t size, zx::vmo* vmo_out) = 0;
  };

  virtual ~MemoryAllocator();

  virtual zx_status_t Allocate(uint64_t size, zx::vmo* parent_vmo) = 0;
  // The callee must not create long-lived duplicate handles to child_vmo, as
  // that would prevent ZX_VMO_ZERO_CHILDREN from being signaled on parent_vmo
  // which would prevent Delete() from ever getting called even if all sysmem
  // participants have closed their handles to child_vmo.  A transient
  // short-lived duplicate handle to child_vmo is fine.
  //
  // The parent_vmo's handle value is guaranteed to remain valid (and a unique
  // handle value) until Delete().
  //
  // The child_vmo's handle value is not guaranteed to remain valid, nor is it
  // guaranteed to remain unique.  However, the child_vmo's koid is unique per
  // boot, and can be used to identify whether an arbitrary VMO handle refers to
  // the same VMO as child_vmo.  Any such tracking by koid should be cleaned up
  // during Delete().
  virtual zx_status_t SetupChildVmo(const zx::vmo& parent_vmo, const zx::vmo& child_vmo) = 0;
  // This also should clean up any tracking of child_vmo by child_vmo's koid.
  // The child_vmo object itself, and all handles to it, are completely gone by
  // this point.  Any child_vmo handle values are no longer guaranteed unique,
  // so should not be retained beyond SetupChildVmo() above.
  //
  // This call takes ownership of parent_vmo, and should close parent_vmo so
  // that the memory used by parent_vmo can be freed/reclaimed/recycled.
  virtual void Delete(zx::vmo parent_vmo) = 0;

  virtual bool CoherencyDomainIsInaccessible() = 0;
  virtual zx_status_t GetPhysicalMemoryInfo(uint64_t* base, uint64_t* size) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  void AddDestroyCallback(intptr_t key, fit::callback<void()> callback);
  void RemoveDestroyCallback(intptr_t key);

 public:
  std::map<intptr_t, fit::callback<void()>> destroy_callbacks_;
};
