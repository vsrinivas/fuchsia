// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake-msi/msi.h>
#include <lib/fake-object/object.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/msi.h>
#include <lib/zx/status.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <utility>
#include <vector>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

namespace {
class MsiAllocation final : public fake_object::Object {
 public:
  using MsiId = uint32_t;

  explicit MsiAllocation(uint32_t irq_cnt) : irq_count_(irq_cnt) {}
  ~MsiAllocation() {
    fbl::AutoLock lock(&lock_);
    ZX_ASSERT_MSG(!ids_in_use_, "FakeMsi %p still has reservations during deconstruction: %#x",
                  this, ids_in_use_);
  }
  fake_object::HandleType type() const final { return fake_object::HandleType::MSI_ALLOCATION; }
  zx_status_t get_info(zx_handle_t handle, uint32_t topic, void* buffer, size_t buffer_size,
                       size_t* actual_count, size_t* avail_count) final;

  zx_status_t ReserveId(MsiId msi_id) __TA_EXCLUDES(lock_) {
    fbl::AutoLock lock(&lock_);
    if (msi_id >= irq_count_) {
      return ZX_ERR_INVALID_ARGS;
    }
    uint32_t mask = (1 << msi_id);
    if (ids_in_use_ & mask) {
      return ZX_ERR_ALREADY_BOUND;
    }

    ids_in_use_ |= mask;
    return ZX_OK;
  }

  zx_status_t ReleaseId(MsiId msi_id) __TA_EXCLUDES(lock_) {
    fbl::AutoLock lock(&lock_);
    if (msi_id >= irq_count_) {
      return ZX_ERR_INVALID_ARGS;
    }
    uint32_t mask = (1 << msi_id);

    if (!(ids_in_use_ & mask)) {
      return ZX_ERR_BAD_STATE;
    }

    ids_in_use_ &= ~mask;
    return ZX_OK;
  }

  uint32_t irq_count() { return irq_count_; }

 private:
  const uint32_t irq_count_;
  uint32_t ids_in_use_ __TA_GUARDED(lock_) = 0;
  mutable fbl::Mutex lock_;
};  // namespace

class MsiInterrupt : public fake_object::Object {
 public:
  explicit MsiInterrupt(zx::interrupt interrupt, fbl::RefPtr<MsiAllocation> allocation,
                        MsiAllocation::MsiId msi_id)
      : interrupt_(std::move(interrupt)), allocation_(std::move(allocation)), msi_id_(msi_id) {
    allocation_->ReserveId(msi_id_);
  }
  ~MsiInterrupt() { allocation_->ReleaseId(msi_id_); }
  fake_object::HandleType type() const final { return fake_object::HandleType::MSI_INTERRUPT; }
  zx_handle_t interrupt() { return interrupt_.get(); }

 private:
  zx::interrupt interrupt_;
  fbl::RefPtr<MsiAllocation> allocation_;
  MsiAllocation::MsiId msi_id_;
};

// Implements fake-msi's version of |zx_object_get_info|.
zx_status_t MsiAllocation::get_info(zx_handle_t /*handle*/, uint32_t topic, void* buffer,
                                    size_t buffer_size, size_t* /*actual_count*/,
                                    size_t* /*avail_count*/) {
  if (buffer_size != sizeof(zx_info_msi_t) || !buffer || topic != ZX_INFO_MSI) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AutoLock lock(&lock_);
  auto* info = static_cast<zx_info_msi_t*>(buffer);
  info->target_addr = 0xCAFE;
  info->target_data = 0xC0FE;
  info->base_irq_id = 1024;
  info->num_irq = irq_count_;
  info->interrupt_count = __builtin_popcount(ids_in_use_);
  return ZX_OK;
}

}  // namespace

// TODO(fxb/32978): Pull some of these structures out of their parent headers so that
// both the tests and the real implementations can use the same information.
constexpr size_t MsiCapabilitySize = 24u;

// Fake syscall implementations
__EXPORT
zx_status_t zx_msi_allocate(zx_handle_t root, uint32_t count, zx_handle_t* msi_out) {
  fbl::RefPtr<fake_object::Object> new_msi = fbl::AdoptRef(new MsiAllocation(count));
  if (auto res = fake_object::FakeHandleTable().Add(std::move(new_msi)); res.is_ok()) {
    *msi_out = res.value();
    return ZX_OK;
  } else {
    return res.status_value();
  }
}

__EXPORT
zx_status_t zx_msi_create(zx_handle_t msi_handle, uint32_t options, uint32_t msi_id,
                          zx_handle_t vmo_hnd, size_t cap_offset, zx_handle_t* out) {
  zx::status get_res = fake_object::FakeHandleTable().Get(msi_handle);
  if (!get_res.is_ok()) {
    return ZX_ERR_BAD_HANDLE;
  }

  if (get_res->type() != fake_object::HandleType::MSI_ALLOCATION) {
    return ZX_ERR_WRONG_TYPE;
  }
  auto msi = fbl::RefPtr<MsiAllocation>::Downcast(std::move(get_res.value()));

  if (msi_id >= msi->irq_count()) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_info_vmo_t vmo_info;
  zx::unowned_vmo vmo(vmo_hnd);
  ZX_ASSERT(vmo->get_info(ZX_INFO_VMO, &vmo_info, sizeof(vmo_info), nullptr, nullptr) == ZX_OK);

  if (vmo_info.size_bytes != ZX_PAGE_SIZE || cap_offset > vmo_info.size_bytes - MsiCapabilitySize ||
      vmo_info.cache_policy != ZX_CACHE_POLICY_UNCACHED_DEVICE || options) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status = msi->ReserveId(msi_id);
  if (status != ZX_OK) {
    return status;
  }

  zx::interrupt interrupt;
  ZX_ASSERT(zx::interrupt::create(*zx::unowned_resource(ZX_HANDLE_INVALID), /*src_num=*/0,
                                  ZX_INTERRUPT_VIRTUAL, &interrupt) == ZX_OK);
  fbl::RefPtr<fake_object::Object> new_interrupt =
      fbl::AdoptRef(new MsiInterrupt(std::move(interrupt), std::move(msi), msi_id));
  zx::status<zx_handle_t> add_res = fake_object::FakeHandleTable().Add(std::move(new_interrupt));
  if (add_res.is_ok()) {
    *out = add_res.value();
  }
  return add_res.status_value();
}

zx_status_t zx_interrupt_wait(zx_handle_t handle, zx_time_t* out_timestamp) {
  if (auto res = fake_object::FakeHandleTable().Get(handle); res.is_ok()) {
    auto interrupt = fbl::RefPtr<MsiInterrupt>::Downcast(res.value());
    return REAL_SYSCALL(zx_interrupt_wait)(interrupt->interrupt(), out_timestamp);
  } else {
    return res.status_value();
  }
}
