// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake-bti/bti.h>
#include <lib/fake-object/object.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/status.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <utility>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>

#include "zircon/errors.h"

// Normally just defined in the kernel:
#define PAGE_SIZE_SHIFT 12

namespace {
class Bti final : public Object {
 public:
  virtual ~Bti() = default;

  static zx_status_t Create(fbl::RefPtr<Object>* out) {
    *out = fbl::AdoptRef(new Bti());
    return ZX_OK;
  }

  zx_status_t get_info(zx_handle_t handle, uint32_t topic, void* buffer, size_t buffer_size,
                       size_t* actual_count, size_t* avail_count) override;

  HandleType type() const final { return HandleType::BTI; }

 private:
  Bti() = default;
};

class Pmt final : public Object {
 public:
  virtual ~Pmt() = default;

  static zx_status_t Create(zx::vmo vmo, uint64_t offset, uint64_t size, fbl::RefPtr<Object>* out) {
    fbl::RefPtr<Pmt> pmt(fbl::AdoptRef(new Pmt(std::move(vmo), offset, size)));
    // These lines exist because currently offset_ and size_ are unused, and
    // GCC and Clang disagree about whether or not marking them as unused is acceptable.
    (void)pmt->offset_;
    (void)pmt->size_;
    *out = std::move(pmt);
    return ZX_OK;
  }

  HandleType type() const final { return HandleType::PMT; }

 private:
  Pmt(zx::vmo vmo, uint64_t offset, uint64_t size)
      : vmo_(std::move(vmo)), offset_(offset), size_(size) {}

  zx::vmo vmo_;
  uint64_t offset_;
  uint64_t size_;
};
}  // namespace
// Fake BTI API

// Implements fake-bti's version of |zx_object_get_info|.
zx_status_t Bti::get_info(zx_handle_t handle, uint32_t topic, void* buffer, size_t buffer_size,
                          size_t* actual_count, size_t* avail_count) {
  zx::status status = FakeHandleTable().Get(handle);
  if (!status.is_ok()) {
    printf("fake_bti_get_info: Failed to find handle %u\n", handle);
    return status.status_value();
  }
  fbl::RefPtr<Object> obj = std::move(status.value());
  if (obj->type() == HandleType::BTI) {
    switch (topic) {
      case ZX_INFO_BTI: {
        if (avail_count) {
          *avail_count = 1;
        }
        if (actual_count) {
          *actual_count = 0;
        }
        if (buffer_size < sizeof(zx_info_bti_t)) {
          return ZX_ERR_BUFFER_TOO_SMALL;
        }
        zx_info_bti_t info = {
            .minimum_contiguity = ZX_PAGE_SIZE,
            .aspace_size = UINT64_MAX,
            .pmo_count = 0,
            .quarantine_count = 0,
        };
        memcpy(buffer, &info, sizeof(info));
        if (actual_count) {
          *actual_count = 1;
        }
        return ZX_OK;
      }
      default:
        ZX_ASSERT_MSG(false, "fake object_get_info: Unsupported BTI topic %u\n", topic);
    }
  }
  ZX_ASSERT_MSG(false, "fake object_get_info: Unsupported PMT topic %u\n", topic);
}

__EXPORT
zx_status_t fake_bti_create(zx_handle_t* out) {
  fbl::RefPtr<Object> new_bti;
  zx_status_t status = Bti::Create(&new_bti);
  if (status != ZX_OK) {
    return status;
  }

  zx::status add_status = FakeHandleTable().Add(std::move(new_bti));
  if (add_status.is_ok()) {
    *out = add_status.value();
  }

  return add_status.status_value();
}

// Fake syscall implementations
__EXPORT
zx_status_t zx_bti_pin(zx_handle_t bti_handle, uint32_t options, zx_handle_t vmo, uint64_t offset,
                       uint64_t size, zx_paddr_t* addrs, size_t addrs_count, zx_handle_t* out) {
  zx::status status = FakeHandleTable().Get(bti_handle);
  ZX_ASSERT_MSG(status.is_ok() && status.value()->type() == HandleType::BTI,
                "fake bti_pin: Bad handle %u\n", bti_handle);
  fbl::RefPtr<Bti> bti_obj = fbl::RefPtr<Bti>::Downcast(std::move(status.value()));

  zx::vmo vmo_clone;
  zx_status_t vmo_status = zx::unowned_vmo(vmo)->duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_clone);
  if (vmo_status != ZX_OK) {
    return vmo_status;
  }

  zx_info_handle_basic_t handle_info;
  vmo_status =
      vmo_clone.get_info(ZX_INFO_HANDLE_BASIC, &handle_info, sizeof(handle_info), nullptr, nullptr);
  ZX_ASSERT_MSG(vmo_status == ZX_OK, "fake bti_pin: Failed to get VMO info\n");
  const zx_rights_t vmo_rights = handle_info.rights;
  if (!(vmo_rights & ZX_RIGHT_MAP)) {
    return ZX_ERR_ACCESS_DENIED;
  }

  // Check argument validity
  if (offset % ZX_PAGE_SIZE != 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (size % ZX_PAGE_SIZE != 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Validate options
  bool compress_results = false;
  bool contiguous = false;
  if (options & ZX_BTI_PERM_READ) {
    if (!(vmo_rights & ZX_RIGHT_READ)) {
      return ZX_ERR_ACCESS_DENIED;
    }
    options &= ~ZX_BTI_PERM_READ;
  }
  if (options & ZX_BTI_PERM_WRITE) {
    if (!(vmo_rights & ZX_RIGHT_WRITE)) {
      return ZX_ERR_ACCESS_DENIED;
    }
    options &= ~ZX_BTI_PERM_WRITE;
  }
  if (options & ZX_BTI_PERM_EXECUTE) {
    // Note: We check ZX_RIGHT_READ instead of ZX_RIGHT_EXECUTE
    // here because the latter applies to execute permission of
    // the host CPU, whereas ZX_BTI_PERM_EXECUTE applies to
    // transactions initiated by the bus device.
    if (!(vmo_rights & ZX_RIGHT_READ)) {
      return ZX_ERR_ACCESS_DENIED;
    }
    options &= ~ZX_BTI_PERM_EXECUTE;
  }
  if (!((options & ZX_BTI_COMPRESS) && (options & ZX_BTI_CONTIGUOUS))) {
    if (options & ZX_BTI_COMPRESS) {
      compress_results = true;
      options &= ~ZX_BTI_COMPRESS;
    }
    if (options & ZX_BTI_CONTIGUOUS) {
      contiguous = true;
      options &= ~ZX_BTI_CONTIGUOUS;
    }
  }
  if (options) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (compress_results || !contiguous) {
    if (addrs_count != size / ZX_PAGE_SIZE) {
      return ZX_ERR_INVALID_ARGS;
    }
  } else {
    if (addrs_count != 1) {
      return ZX_ERR_INVALID_ARGS;
    }
  }
  // Fill |addrs| with the fake physical address.
  for (size_t i = 0; i != addrs_count; ++i) {
    addrs[i] = FAKE_BTI_PHYS_ADDR;
  }

  fbl::RefPtr<Object> new_pmt;
  zx_status_t pmt_status = Pmt::Create(std::move(vmo_clone), offset, size, &new_pmt);
  if (pmt_status != ZX_OK) {
    return pmt_status;
  }

  zx::status add_status = FakeHandleTable().Add(std::move(new_pmt));
  if (add_status.is_ok()) {
    *out = add_status.value();
  }

  return add_status.status_value();
}

__EXPORT
zx_status_t zx_bti_release_quarantine(zx_handle_t handle) {
  zx::status status = FakeHandleTable().Get(handle);
  ZX_ASSERT_MSG(status.is_ok() && status.value()->type() == HandleType::BTI,
                "fake bti_release_quarantine: Bad handle %u\n", handle);
  return ZX_OK;
}

__EXPORT
zx_status_t zx_pmt_unpin(zx_handle_t handle) {
  zx::status get_status = FakeHandleTable().Get(handle);
  ZX_ASSERT_MSG(get_status.is_ok() && get_status.value()->type() == HandleType::PMT,
                "fake pmt_unpin: Bad handle %u\n", handle);
  zx::status remove_status = FakeHandleTable().Remove(handle);
  ZX_ASSERT_MSG(remove_status.is_ok(), "fake pmt_unpin: Failed to remove handle %u: %s\n", handle,
                zx_status_get_string(remove_status.status_value()));
  return ZX_OK;
}

// A fake version of zx_vmo_create_contiguous.  This version just creates a normal vmo.
__EXPORT
zx_status_t zx_vmo_create_contiguous(zx_handle_t bti_handle, size_t size, uint32_t alignment_log2,
                                     zx_handle_t* out) {
  if (size == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (alignment_log2 == 0) {
    alignment_log2 = PAGE_SIZE_SHIFT;
  }
  // catch obviously wrong values
  if (alignment_log2 < PAGE_SIZE_SHIFT || alignment_log2 >= (8 * sizeof(uint64_t))) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Make sure this is a valid fake bti:
  fbl::RefPtr<Object> bti_obj;
  zx::status get_status = FakeHandleTable().Get(bti_handle);
  ZX_ASSERT_MSG(get_status.is_ok() && get_status.value()->type() == HandleType::BTI,
                "fake bti_pin: Bad handle %u\n", bti_handle);

  // For this fake implementation, just create a normal vmo:
  return zx_vmo_create(size, 0, out);
}
