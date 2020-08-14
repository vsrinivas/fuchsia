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

#include <unordered_map>
#include <utility>
#include <vector>

#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

namespace {
class Msi final : public fake_object::Object {
 public:
  using MsiId = uint32_t;

  explicit Msi(uint32_t irq_cnt) : irq_count_(irq_cnt) {}
  ~Msi() {
    fbl::AutoLock lock(&lock_);
    ClearClosedHandles();
    ZX_ASSERT_MSG(ids_in_use_.empty(),
                  "FakeMsi %p still has %zu reservation(s) during deconstruction", this,
                  ids_in_use_.size());
  }
  fake_object::HandleType type() const final { return fake_object::HandleType::MSI; }
  zx_status_t get_info(zx_handle_t handle, uint32_t topic, void* buffer, size_t buffer_size,
                       size_t* actual_count, size_t* avail_count) final;

  zx_status_t ReserveId(zx::unowned_interrupt interrupt, MsiId msi_id) __TA_EXCLUDES(lock_) {
    fbl::AutoLock lock(&lock_);
    ClearClosedHandles();
    if (msi_id >= irq_count_) {
      return ZX_ERR_INVALID_ARGS;
    }

    for (const auto& [handle, stored_msi_id] : ids_in_use_) {
      if (stored_msi_id == msi_id) {
        return ZX_ERR_ALREADY_BOUND;
      }
    }

    ftracef("Add: handle %#x = %u\n", interrupt->get(), msi_id);
    zx_handle_t local_handle;
    zx_status_t status = zx_handle_duplicate(interrupt->get(), ZX_RIGHT_SAME_RIGHTS, &local_handle);
    if (status == ZX_OK) {
      ids_in_use_[local_handle] = msi_id;
    }
    return status;
  }

  uint32_t irq_count() { return irq_count_; }

 private:
  void ClearClosedHandles() __TA_REQUIRES(lock_) {
    std::unordered_map<zx_handle_t, MsiId>::iterator elem = ids_in_use_.begin();
    while (elem != ids_in_use_.end()) {
      zx_info_handle_count_t info;
      zx_status_t status = zx_object_get_info(elem->first, ZX_INFO_HANDLE_COUNT, &info,
                                              sizeof(info), nullptr, nullptr);
      if (status != ZX_OK || info.handle_count == 1) {
        ftracef("Remove: handle %#x = %u (info status = %d)\n", elem->first, elem->second, status);
        zx_handle_close(elem->first);
        elem = ids_in_use_.erase(elem);
      } else {
        elem++;
      }
    }
  }

  const uint32_t irq_count_;
  // A mapping of interrupt handle to msi id is made here. zx_object_get_info is used
  // to verify handles are still valid when reservations are made to free up any child
  // interrupts that were freed in the interim.
  std::unordered_map<zx_handle_t, MsiId> ids_in_use_ __TA_GUARDED(lock_) = {};
  mutable fbl::Mutex lock_;
};  // namespace

// Implements fake-msi's version of |zx_object_get_info|.
zx_status_t Msi::get_info(zx_handle_t /*handle*/, uint32_t topic, void* buffer, size_t buffer_size,
                          size_t* /*actual_count*/, size_t* /*avail_count*/) {
  if (buffer_size != sizeof(zx_info_msi_t) || !buffer || topic != ZX_INFO_MSI) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AutoLock lock(&lock_);
  ClearClosedHandles();
  auto* info = static_cast<zx_info_msi_t*>(buffer);
  info->target_addr = 0xCAFE;
  info->target_data = 0xC0FE;
  info->base_irq_id = 1024;
  info->num_irq = irq_count_;
  info->interrupt_count = ids_in_use_.size();
  return ZX_OK;
}

}  // namespace

// TODO(fxbug.dev/32978): Pull some of these structures out of their parent headers so that
// both the tests and the real implementations can use the same information.
constexpr size_t MsiCapabilitySize = 24u;

// Fake syscall implementations
__EXPORT
zx_status_t zx_msi_allocate(zx_handle_t /*root*/, uint32_t count, zx_handle_t* msi_out) {
  if (!count || !msi_out || !fbl::is_pow2(count)) {
    return ZX_ERR_INVALID_ARGS;
  }
  auto new_msi = fbl::AdoptRef(new Msi(count));
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

  if (get_res->type() != fake_object::HandleType::MSI) {
    return ZX_ERR_WRONG_TYPE;
  }
  auto msi = fbl::RefPtr<Msi>::Downcast(std::move(get_res.value()));
  if (msi_id >= msi->irq_count()) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_info_vmo_t vmo_info;
  zx::unowned_vmo vmo(vmo_hnd);
  ZX_ASSERT(vmo->get_info(ZX_INFO_VMO, &vmo_info, sizeof(vmo_info), nullptr, nullptr) == ZX_OK);

  if (cap_offset > vmo_info.size_bytes - MsiCapabilitySize ||
      vmo_info.cache_policy != ZX_CACHE_POLICY_UNCACHED_DEVICE || options & ~ZX_MSI_MODE_MSI_X) {
    return ZX_ERR_INVALID_ARGS;
  }

  // After creation here, this handle is only used by the caller. We want no ownership of it,
  // it's only stored so we can check if it remains unclosed.
  zx::interrupt interrupt = {};
  ZX_ASSERT(zx::interrupt::create(*zx::unowned_resource(ZX_HANDLE_INVALID), 0, ZX_INTERRUPT_VIRTUAL,
                                  &interrupt) == ZX_OK);
  zx_status_t status = msi->ReserveId(interrupt.borrow(), msi_id);
  if (status == ZX_OK) {
    *out = interrupt.release();
  }

  return status;
}
