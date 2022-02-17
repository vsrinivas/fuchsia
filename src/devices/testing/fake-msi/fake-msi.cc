// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake-msi/msi.h>
#include <lib/fake-object/object.h>
#include <lib/stdcompat/bit.h>
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

#include <atomic>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

namespace fake_object {
std::atomic_uint64_t Msi::out_of_scope_while_holding_reservations_count_ = 0;
std::atomic_bool Msi::ids_in_use_assert_disabled_ = false;

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

}  // namespace fake_object

// TODO(fxbug.dev/32978): Pull some of these structures out of their parent headers so that
// both the tests and the real implementations can use the same information.
constexpr size_t MsiCapabilitySize = 24u;

// Fake syscall implementations
__EXPORT
zx_status_t zx_msi_allocate(zx_handle_t /*root*/, uint32_t count, zx_handle_t* msi_out) {
  if (!count || !msi_out || !cpp20::has_single_bit(count)) {
    return ZX_ERR_INVALID_ARGS;
  }
  auto new_msi = fbl::AdoptRef(new fake_object::Msi(count));
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

  if (get_res->type() != ZX_OBJ_TYPE_MSI) {
    return ZX_ERR_WRONG_TYPE;
  }
  auto msi = fbl::RefPtr<fake_object::Msi>::Downcast(std::move(get_res.value()));
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
