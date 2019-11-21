// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake-bti/bti.h>

#include <utility>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <lib/zircon-internal/thread_annotations.h>

// Normally just defined in the kernel:
#define PAGE_SIZE_SHIFT 12

namespace {

enum class HandleType {
  BTI,
  PMT,
};

class Object : public fbl::RefCounted<Object> {
 public:
  virtual ~Object() = default;
  virtual HandleType type() const = 0;
};

class Bti final : public Object {
 public:
  virtual ~Bti() = default;

  static zx_status_t Create(fbl::RefPtr<Object>* out) {
    *out = fbl::AdoptRef(new Bti());
    return ZX_OK;
  }

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

// Thread-safe implementation of a handle table for the fake BTI/PMT handles
class HandleTable {
 public:
  HandleTable() = default;
  ~HandleTable() = default;

  HandleTable(const HandleTable&) = delete;
  HandleTable& operator=(const HandleTable&) = delete;
  HandleTable(HandleTable&&) = delete;
  HandleTable& operator=(HandleTable&&) = delete;

  // Handle values are always odd, so we can use even numbers to identify
  // fake BTI and PMT objects.
  // TODO(ZX-3131): This guarantee should be documented or we should change
  // this code to do something else.
  static bool IsValidFakeHandle(zx_handle_t handle) { return (handle & 1) == 0; }

  zx_status_t Get(zx_handle_t handle, fbl::RefPtr<Object>* out) {
    fbl::AutoLock guard(&lock_);

    size_t idx = HandleToIndex(handle);
    if (idx >= handles_.size()) {
      return ZX_ERR_NOT_FOUND;
    }
    const fbl::RefPtr<Object>& h = handles_[idx];
    if (!h) {
      return ZX_ERR_NOT_FOUND;
    }
    *out = h;
    return ZX_OK;
  }

  zx_status_t Remove(zx_handle_t handle) {
    fbl::AutoLock guard(&lock_);

    size_t idx = HandleToIndex(handle);
    if (idx >= handles_.size()) {
      return ZX_ERR_NOT_FOUND;
    }
    fbl::RefPtr<Object>* h = &handles_[idx];
    if (!*h) {
      return ZX_ERR_NOT_FOUND;
    }
    h->reset();
    return ZX_OK;
  }

  zx_status_t Add(fbl::RefPtr<Object> obj, zx_handle_t* out) {
    fbl::AutoLock guard(&lock_);

    for (size_t i = 0; i < handles_.size(); ++i) {
      if (!handles_[i]) {
        handles_[i] = std::move(obj);
        *out = IndexToHandle(i);
        return ZX_OK;
      }
    }

    handles_.push_back(std::move(obj));
    *out = IndexToHandle(handles_.size() - 1);
    return ZX_OK;
  }

 private:
  static size_t HandleToIndex(zx_handle_t handle) {
    ZX_ASSERT(IsValidFakeHandle(handle));
    return (handle >> 1) - 1;
  }

  static zx_handle_t IndexToHandle(size_t idx) { return static_cast<zx_handle_t>((idx + 1) << 1); }

  fbl::Mutex lock_;
  fbl::Vector<fbl::RefPtr<Object>> handles_ TA_GUARDED(lock_);
};

HandleTable gHandleTable;

}  // namespace

// Fake BTI API

__EXPORT
zx_status_t fake_bti_create(zx_handle_t* out) {
  fbl::RefPtr<Object> new_bti;
  zx_status_t status = Bti::Create(&new_bti);
  if (status != ZX_OK) {
    return status;
  }
  return gHandleTable.Add(std::move(new_bti), out);
}

__EXPORT
void fake_bti_destroy(zx_handle_t h) {
  fbl::RefPtr<Object> obj;
  zx_status_t status = gHandleTable.Get(h, &obj);
  ZX_ASSERT_MSG(status == ZX_OK, "fake_bti_destroy: Failed to find handle %u\n", h);
  ZX_ASSERT_MSG(obj->type() == HandleType::BTI, "fake_bti_destroy: Wrong handle type: %u\n",
                static_cast<uint32_t>(obj->type()));
  status = gHandleTable.Remove(h);
  ZX_ASSERT_MSG(status == ZX_OK, "fake_bti_destroy: Failed to destroy handle %u: %s\n", h,
                zx_status_get_string(status));
}

// Fake syscall implementations

__EXPORT
zx_status_t zx_bti_pin(zx_handle_t bti_handle, uint32_t options, zx_handle_t vmo, uint64_t offset,
                       uint64_t size, zx_paddr_t* addrs, size_t addrs_count, zx_handle_t* out) {
  fbl::RefPtr<Object> bti_obj;
  zx_status_t status = gHandleTable.Get(bti_handle, &bti_obj);
  ZX_ASSERT_MSG(status == ZX_OK && bti_obj->type() == HandleType::BTI,
                "fake bti_pin: Bad handle %u\n", bti_handle);

  fbl::RefPtr<Bti> bti(static_cast<Bti*>(bti_obj.get()));

  zx::vmo vmo_clone;
  status = zx::unowned_vmo(vmo)->duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_clone);
  if (status != ZX_OK) {
    return status;
  }
  zx_info_handle_basic_t handle_info;
  status =
      vmo_clone.get_info(ZX_INFO_HANDLE_BASIC, &handle_info, sizeof(handle_info), nullptr, nullptr);
  ZX_ASSERT_MSG(status == ZX_OK, "fake bti_pin: Failed to get VMO info\n");
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
  status = Pmt::Create(std::move(vmo_clone), offset, size, &new_pmt);
  if (status != ZX_OK) {
    return status;
  }
  return gHandleTable.Add(std::move(new_pmt), out);
}

__EXPORT
zx_status_t zx_bti_release_quarantine(zx_handle_t handle) {
  fbl::RefPtr<Object> obj;
  zx_status_t status = gHandleTable.Get(handle, &obj);
  ZX_ASSERT_MSG(status == ZX_OK && obj->type() == HandleType::BTI,
                "fake bti_release_quarantine: Bad handle %u\n", handle);
  return ZX_OK;
}

__EXPORT
zx_status_t zx_pmt_unpin(zx_handle_t handle) {
  fbl::RefPtr<Object> obj;
  zx_status_t status = gHandleTable.Get(handle, &obj);
  ZX_ASSERT_MSG(status == ZX_OK && obj->type() == HandleType::PMT,
                "fake pmt_unpin: Bad handle %u\n", handle);
  status = gHandleTable.Remove(handle);
  ZX_ASSERT_MSG(status == ZX_OK, "fake pmt_unpin: Failed to remove handle %u: %s\n", handle,
                zx_status_get_string(status));
  return ZX_OK;
}

__EXPORT
zx_status_t zx_object_get_info(zx_handle_t handle, uint32_t topic, void* buffer, size_t buffer_size,
                               size_t* actual_count, size_t* avail_count) {
  if (!HandleTable::IsValidFakeHandle(handle)) {
    return _zx_object_get_info(handle, topic, buffer, buffer_size, actual_count, avail_count);
  }

  fbl::RefPtr<Object> obj;
  zx_status_t status = gHandleTable.Get(handle, &obj);
  ZX_ASSERT_MSG(status == ZX_OK, "fake object_get_info: Bad handle %u\n", handle);

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
  zx_status_t status = gHandleTable.Get(bti_handle, &bti_obj);
  ZX_ASSERT_MSG(status == ZX_OK && bti_obj->type() == HandleType::BTI,
                "fake bti_pin: Bad handle %u\n", bti_handle);

  // For this fake implementation, just create a normal vmo:
  return zx_vmo_create(size, 0, out);
}

// Duplicates a fake handle, or if it is a real handle, calls the real
// zx_handle_duplicate function.
// |rights| is ignored for fake handles.
__EXPORT
zx_status_t zx_handle_duplicate(zx_handle_t handle_value, zx_rights_t rights, zx_handle_t* out) {
  if (HandleTable::IsValidFakeHandle(handle_value)) {
    fbl::RefPtr<Object> obj;
    zx_status_t status = gHandleTable.Get(handle_value, &obj);
    ZX_ASSERT_MSG(status == ZX_OK, "fake object_get_info: Bad handle %u\n", handle_value);
    return gHandleTable.Add(std::move(obj), out);
  }
  return _zx_handle_duplicate(handle_value, rights, out);
}
