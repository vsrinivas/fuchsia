// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTING_FAKE_MSI_INCLUDE_LIB_FAKE_MSI_MSI_H_
#define SRC_DEVICES_TESTING_FAKE_MSI_INCLUDE_LIB_FAKE_MSI_MSI_H_

#include <lib/fake-object/object.h>
#include <lib/zx/interrupt.h>
#include <limits.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <atomic>

namespace fake_object {

class Msi final : public fake_object::Object {
 public:
  using MsiId = uint32_t;

  explicit Msi(uint32_t irq_cnt) : fake_object::Object(ZX_OBJ_TYPE_MSI), irq_count_(irq_cnt) {}
  ~Msi() override {
    fbl::AutoLock lock(&lock_);
    ClearClosedHandles();
    if (!ids_in_use_.empty()) {
      if (!ids_in_use_assert_disabled_) {
        ZX_ASSERT_MSG(ids_in_use_.empty(),
                      "FakeMsi %p still has %zu reservation(s) during deconstruction", this,
                      ids_in_use_.size());
      }
      out_of_scope_while_holding_reservations_count_.fetch_add(1u);
    }
  }

  zx_status_t get_info(zx_handle_t handle, uint32_t topic, void* buffer, size_t buffer_size,
                       size_t* actual_count, size_t* avail_count) final;

  zx_status_t ReserveId(const zx::unowned_interrupt& interrupt, MsiId msi_id) __TA_EXCLUDES(lock_) {
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

  uint32_t irq_count() const { return irq_count_; }
  static uint64_t out_of_scope_while_holding_reservations_count() {
    return out_of_scope_while_holding_reservations_count_.load();
  }

  static bool disable_ids_in_use_assert(bool disable) {
    ids_in_use_assert_disabled_.store(disable);
    return disable;
  }

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
  // Use an atomic here to track if it dtor'd in a bad state
  static std::atomic_uint64_t out_of_scope_while_holding_reservations_count_;
  static std::atomic_bool ids_in_use_assert_disabled_;
  // A mapping of interrupt handle to msi id is made here. zx_object_get_info is used
  // to verify handles are still valid when reservations are made to free up any child
  // interrupts that were freed in the interim.
  std::unordered_map<zx_handle_t, MsiId> ids_in_use_ __TA_GUARDED(lock_) = {};
  mutable fbl::Mutex lock_;
};  // namespace

}  // namespace fake_object

#endif  // SRC_DEVICES_TESTING_FAKE_MSI_INCLUDE_LIB_FAKE_MSI_MSI_H_
