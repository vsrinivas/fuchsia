// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake-bti/bti.h>
#include <lib/fake-object/object.h>
#include <lib/stdcompat/span.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/status.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <cstdint>
#include <utility>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>

// Normally just defined in the kernel:
#define PAGE_SIZE_SHIFT 12

namespace {
class Bti final : public fake_object::Object {
 public:
  ~Bti() final = default;

  static zx_status_t Create(cpp20::span<const zx_paddr_t> paddrs,
                            fbl::RefPtr<fake_object::Object>* out) {
    *out = fbl::AdoptRef(new Bti(paddrs));
    return ZX_OK;
  }

  zx_status_t get_info(zx_handle_t handle, uint32_t topic, void* buffer, size_t buffer_size,
                       size_t* actual_count, size_t* avail_count) override;

  uint64_t& pmo_count() { return pmo_count_; }

  bool PopulatePaddrs(zx_paddr_t* paddrs, size_t paddrs_count) {
    for (size_t i = 0; i < paddrs_count; i++) {
      if (paddrs_.empty()) {
        paddrs[i] = FAKE_BTI_PHYS_ADDR;
      } else {
        if (paddrs_index_ >= paddrs_.size()) {
          return false;
        }
        paddrs[i] = paddrs_[paddrs_index_++];
      }
    }
    return true;
  }

  zx_status_t PinVmo(const zx::unowned_vmo& vmo, uint64_t size, uint64_t offset) {
    zx_info_handle_basic_t info;
    zx_status_t status = vmo->get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
    if (status != ZX_OK) {
      return status;
    }

    zx::vmo vmo_dup;
    status = vmo->duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_dup);
    if (status != ZX_OK) {
      return status;
    }

    fbl::AutoLock lock(&lock_);
    pinned_vmos_.push_back({std::move(vmo_dup), size, offset, info.koid});
    return ZX_OK;
  }

  void RemovePinnedVmo(const zx::vmo& vmo, uint64_t size, uint64_t offset) {
    zx_info_handle_basic_t info;
    zx_status_t status = vmo.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
    ZX_DEBUG_ASSERT(status == ZX_OK);

    fbl::AutoLock lock(&lock_);
    for (size_t i = 0; i < pinned_vmos_.size(); ++i) {
      if (pinned_vmos_[i].size == size && pinned_vmos_[i].offset == offset &&
          pinned_vmos_[i].koid == info.koid) {
        pinned_vmos_.erase(i);
        return;
      }
    }
    ZX_PANIC("%s: pinned vmo (koid=%lu, offset=%lu, size=%lu) not found", __func__, info.koid,
             offset, size);
  }

  const auto& pinned_vmos() const { return pinned_vmos_; }

 private:
  Bti() : Object(ZX_OBJ_TYPE_BTI) {}
  explicit Bti(cpp20::span<const zx_paddr_t> paddrs) : Object(ZX_OBJ_TYPE_BTI), paddrs_(paddrs) {}

  struct PinnedVmoInfo {
    zx::vmo vmo;
    uint64_t size;
    uint64_t offset;
    uint64_t koid;
    PinnedVmoInfo(zx::vmo vmo_in, uint64_t size_in, uint64_t offset_in, uint64_t koid_in)
        : vmo(std::move(vmo_in)), size(size_in), offset(offset_in), koid(koid_in) {}
  };

  fbl::Mutex lock_;
  fbl::Vector<PinnedVmoInfo> pinned_vmos_ TA_GUARDED(lock_);
  cpp20::span<const zx_paddr_t> paddrs_ = {};
  size_t paddrs_index_ = 0;
  uint64_t pmo_count_ = 0;
};

class Pmt final : public fake_object::Object {
 public:
  virtual ~Pmt() = default;

  static zx_status_t Create(zx::vmo vmo, uint64_t offset, uint64_t size, fbl::RefPtr<Bti> bti,
                            fbl::RefPtr<fake_object::Object>* out) {
    fbl::RefPtr<Pmt> pmt(fbl::AdoptRef(new Pmt(std::move(vmo), offset, size, std::move(bti))));
    // These lines exist because currently offset_ and size_ are unused, and
    // GCC and Clang disagree about whether or not marking them as unused is acceptable.
    (void)pmt->offset_;
    (void)pmt->size_;
    *out = std::move(pmt);
    return ZX_OK;
  }

  void Unpin() { bti().RemovePinnedVmo(vmo_, size_, offset_); }

  Bti& bti() { return *bti_; }

 private:
  Pmt(zx::vmo vmo, uint64_t offset, uint64_t size, fbl::RefPtr<Bti> bti)
      : Object(ZX_OBJ_TYPE_PMT),
        vmo_(std::move(vmo)),
        offset_(offset),
        size_(size),
        bti_(std::move(bti)) {}

  zx::vmo vmo_;
  uint64_t offset_;
  uint64_t size_;
  fbl::RefPtr<Bti> bti_;
};
}  // namespace
// Fake BTI API

// Implements fake-bti's version of |zx_object_get_info|.
zx_status_t Bti::get_info(zx_handle_t handle, uint32_t topic, void* buffer, size_t buffer_size,
                          size_t* actual_count, size_t* avail_count) {
  zx::status status = fake_object::FakeHandleTable().Get(handle);
  if (!status.is_ok()) {
    printf("fake_bti_get_info: Failed to find handle %u\n", handle);
    return status.status_value();
  }
  fbl::RefPtr<fake_object::Object> obj = std::move(status.value());
  if (obj->type() == ZX_OBJ_TYPE_BTI) {
    auto bti_obj = fbl::RefPtr<Bti>::Downcast(std::move(obj));
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
            .pmo_count = bti_obj->pmo_count(),
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

zx_status_t fake_bti_create(zx_handle_t* out) {
  return fake_bti_create_with_paddrs(nullptr, 0, out);
}

zx_status_t fake_bti_create_with_paddrs(const zx_paddr_t* paddrs, size_t paddr_count,
                                        zx_handle_t* out) {
  fbl::RefPtr<fake_object::Object> new_bti;
  zx_status_t status = Bti::Create(cpp20::span(paddrs, paddr_count), &new_bti);
  if (status != ZX_OK) {
    return status;
  }

  zx::status add_status = fake_object::FakeHandleTable().Add(std::move(new_bti));
  if (add_status.is_ok()) {
    *out = add_status.value();
  }

  return add_status.status_value();
}

zx_status_t fake_bti_get_pinned_vmos(zx_handle_t bti, fake_bti_pinned_vmo_info_t* out_vmo_info,
                                     size_t out_num_vmos, size_t* actual_num_vmos) {
  // Make sure this is a valid fake bti:
  zx::status get_status = fake_object::FakeHandleTable().Get(bti);
  ZX_ASSERT_MSG(get_status.is_ok() && get_status.value()->type() == ZX_OBJ_TYPE_BTI,
                "fake_bti_get_pinned_vmos: Bad handle %u\n", bti);
  fbl::RefPtr<Bti> bti_obj = fbl::RefPtr<Bti>::Downcast(std::move(get_status.value()));

  const auto& vmos = bti_obj->pinned_vmos();
  if (actual_num_vmos != nullptr) {
    *actual_num_vmos = vmos.size();
  }

  for (size_t i = 0; i < vmos.size() && i < out_num_vmos; i++) {
    const auto& vmo_info = vmos[i];
    zx::vmo vmo_dup;
    zx_status_t status = vmo_info.vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_dup);
    if (status != ZX_OK) {
      return status;
    }

    *(out_vmo_info++) = {
        .vmo = vmo_dup.release(),
        .size = vmo_info.size,
        .offset = vmo_info.offset,
    };
  }
  return ZX_OK;
}

// Fake syscall implementations
__EXPORT
zx_status_t zx_bti_pin(zx_handle_t bti_handle, uint32_t options, zx_handle_t vmo, uint64_t offset,
                       uint64_t size, zx_paddr_t* addrs, size_t addrs_count, zx_handle_t* out) {
  zx::status status = fake_object::FakeHandleTable().Get(bti_handle);
  ZX_ASSERT_MSG(status.is_ok() && status.value()->type() == ZX_OBJ_TYPE_BTI,
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
  ZX_ASSERT_MSG(vmo_status == ZX_OK, "fake bti_pin: Failed to get VMO info: %s\n",
                zx_status_get_string(vmo_status));
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
  if (!bti_obj->PopulatePaddrs(addrs, addrs_count)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  fbl::RefPtr<fake_object::Object> new_pmt;
  zx_status_t pmt_status = Pmt::Create(std::move(vmo_clone), offset, size, bti_obj, &new_pmt);
  if (pmt_status != ZX_OK) {
    return pmt_status;
  }

  zx::status add_status = fake_object::FakeHandleTable().Add(std::move(new_pmt));
  if (add_status.is_ok()) {
    *out = add_status.value();
    ++bti_obj->pmo_count();
  }

  if (add_status.is_error()) {
    return add_status.status_value();
  }

  return bti_obj->PinVmo(zx::unowned_vmo(vmo), size, offset);
}

__EXPORT
zx_status_t zx_bti_release_quarantine(zx_handle_t handle) {
  zx::status status = fake_object::FakeHandleTable().Get(handle);
  ZX_ASSERT_MSG(status.is_ok() && status.value()->type() == ZX_OBJ_TYPE_BTI,
                "fake bti_release_quarantine: Bad handle %u\n", handle);
  return ZX_OK;
}

__EXPORT
zx_status_t zx_pmt_unpin(zx_handle_t handle) {
  zx::status get_status = fake_object::FakeHandleTable().Get(handle);
  ZX_ASSERT_MSG(get_status.is_ok() && get_status.value()->type() == ZX_OBJ_TYPE_PMT,
                "fake pmt_unpin: Bad handle %u\n", handle);
  fbl::RefPtr<Pmt> pmt = fbl::RefPtr<Pmt>::Downcast(std::move(get_status.value()));
  pmt->Unpin();
  --pmt->bti().pmo_count();
  zx::status remove_status = fake_object::FakeHandleTable().Remove(handle);
  ZX_ASSERT_MSG(remove_status.is_ok(), "fake pmt_unpin: Failed to remove handle %u: %s\n", handle,
                zx_status_get_string(remove_status.status_value()));
  return ZX_OK;
}

// A fake version of zx_vmo_create_contiguous.  This version just creates a normal vmo.
// The vmo will be always pinned at offset 0 with its full size.
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
  zx::status get_status = fake_object::FakeHandleTable().Get(bti_handle);
  ZX_ASSERT_MSG(get_status.is_ok() && get_status.value()->type() == ZX_OBJ_TYPE_BTI,
                "fake bti_pin: Bad handle %u\n", bti_handle);

  // For this fake implementation, just create a normal vmo:
  return zx_vmo_create(size, 0, out);
}
