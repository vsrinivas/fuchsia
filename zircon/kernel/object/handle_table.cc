// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/handle_table.h"

#include <lib/crypto/global_prng.h>

#include <object/job_dispatcher.h>

constexpr uint32_t kHandleMustBeOneMask = ((0x1u << kHandleReservedBits) - 1);
static_assert(kHandleMustBeOneMask == ZX_HANDLE_FIXED_BITS_MASK,
              "kHandleMustBeOneMask must match ZX_HANDLE_FIXED_BITS_MASK!");

static zx_handle_t map_handle_to_value(const Handle* handle, uint32_t mixer) {
  // Ensure that the last two bits of the result is not zero, and make sure we
  // don't lose any base_value bits when shifting.
  constexpr uint32_t kBaseValueMustBeZeroMask =
      (kHandleMustBeOneMask << ((sizeof(handle->base_value()) * 8) - kHandleReservedBits));

  DEBUG_ASSERT((mixer & kHandleMustBeOneMask) == 0);
  DEBUG_ASSERT((handle->base_value() & kBaseValueMustBeZeroMask) == 0);

  auto handle_id = (handle->base_value() << kHandleReservedBits) | kHandleMustBeOneMask;

  return static_cast<zx_handle_t>(mixer ^ handle_id);
}

static Handle* map_value_to_handle(zx_handle_t value, uint32_t mixer) {
  // Validate that the "must be one" bits are actually one.
  if ((value & kHandleMustBeOneMask) != kHandleMustBeOneMask) {
    return nullptr;
  }

  uint32_t handle_id = (static_cast<uint32_t>(value) ^ mixer) >> kHandleReservedBits;
  return Handle::FromU32(handle_id);
}

HandleTable::HandleTable(ProcessDispatcher* process) : process_(process) {
  // Generate handle XOR mask with top bit and bottom two bits cleared
  uint32_t secret;
  auto prng = crypto::GlobalPRNG::GetInstance();
  prng->Draw(&secret, sizeof(secret));

  // Handle values must always have the low kHandleReservedBits set.  Do not
  // ever attempt to toggle these bits using the random_value_ xor mask.
  random_value_ = secret << kHandleReservedBits;
}

HandleTable::~HandleTable() {
  DEBUG_ASSERT(handles_.is_empty());
  DEBUG_ASSERT(count_ == 0);
  DEBUG_ASSERT(cursors_.is_empty());
}

void HandleTable::Clean() {
  HandleList to_clean;
  {
    Guard<BrwLockPi, BrwLockPi::Writer> guard{&lock_};
    for (auto& cursor : cursors_) {
      cursor.Invalidate();
    }
    for (auto& handle : handles_) {
      handle.set_process_id(ZX_KOID_INVALID);
    }
    count_ = 0;
    to_clean.swap(handles_);
  }

  // This needs to be done outside of the critical section above.
  while (!to_clean.is_empty()) {
    // Delete handle via HandleOwner dtor.
    HandleOwner ho(to_clean.pop_front());
  }
}

zx_handle_t HandleTable::MapHandleToValue(const Handle* handle) const {
  return map_handle_to_value(handle, random_value_);
}

zx_handle_t HandleTable::MapHandleToValue(const HandleOwner& handle) const {
  return map_handle_to_value(handle.get(), random_value_);
}

Handle* HandleTable::GetHandleLocked(zx_handle_t handle_value, bool skip_policy) {
  auto handle = map_value_to_handle(handle_value, random_value_);
  if (handle && handle->process_id() == process_->get_koid())
    return handle;

  if (likely(!skip_policy)) {
    // Handle lookup failed.  We potentially generate an exception or kill the process,
    // depending on the job policy. Note that we don't use the return value from
    // EnforceBasicPolicy() here: ZX_POL_ACTION_ALLOW and ZX_POL_ACTION_DENY are equivalent for
    // ZX_POL_BAD_HANDLE.
    __UNUSED auto result = process_->EnforceBasicPolicy(ZX_POL_BAD_HANDLE);
  }

  return nullptr;
}

uint32_t HandleTable::HandleCount() const {
  Guard<BrwLockPi, BrwLockPi::Reader> guard{&lock_};
  return count_;
}

void HandleTable::AddHandle(HandleOwner handle) {
  Guard<BrwLockPi, BrwLockPi::Writer> guard{&lock_};
  AddHandleLocked(ktl::move(handle));
}

void HandleTable::AddHandleLocked(HandleOwner handle) {
  handle->set_process_id(process_->get_koid());
  handles_.push_front(handle.release());
  count_++;
}

HandleOwner HandleTable::RemoveHandleLocked(Handle* handle) {
  DEBUG_ASSERT(count_ > 0);
  handle->set_process_id(ZX_KOID_INVALID);
  // Make sure we don't leave any dangling cursors.
  for (auto& cursor : cursors_) {
    // If it points to |handle|, skip over it.
    cursor.AdvanceIf(handle);
  }
  handles_.erase(*handle);
  count_--;
  return HandleOwner(handle);
}

HandleOwner HandleTable::RemoveHandle(zx_handle_t handle_value) {
  Guard<BrwLockPi, BrwLockPi::Writer> guard{&lock_};
  return RemoveHandleLocked(handle_value);
}

HandleOwner HandleTable::RemoveHandleLocked(zx_handle_t handle_value) {
  auto handle = GetHandleLocked(handle_value);
  if (!handle)
    return nullptr;
  return RemoveHandleLocked(handle);
}

zx_status_t HandleTable::RemoveHandles(ktl::span<const zx_handle_t> handles) {
  zx_status_t status = ZX_OK;
  Guard<BrwLockPi, BrwLockPi::Writer> guard{get_lock()};

  for (zx_handle_t handle_value : handles) {
    if (handle_value == ZX_HANDLE_INVALID)
      continue;
    auto handle = RemoveHandleLocked(handle_value);
    if (!handle)
      status = ZX_ERR_BAD_HANDLE;
  }
  return status;
}

zx_koid_t HandleTable::GetKoidForHandle(zx_handle_t handle_value) {
  Guard<BrwLockPi, BrwLockPi::Reader> guard{&lock_};
  Handle* handle = GetHandleLocked(handle_value);
  if (!handle)
    return ZX_KOID_INVALID;
  return handle->dispatcher()->get_koid();
}

zx_status_t HandleTable::GetDispatcherInternal(zx_handle_t handle_value,
                                               fbl::RefPtr<Dispatcher>* dispatcher,
                                               zx_rights_t* rights) {
  Guard<BrwLockPi, BrwLockPi::Reader> guard{&lock_};
  Handle* handle = GetHandleLocked(handle_value);
  if (!handle)
    return ZX_ERR_BAD_HANDLE;

  *dispatcher = handle->dispatcher();
  if (rights)
    *rights = handle->rights();
  return ZX_OK;
}

zx_status_t HandleTable::GetHandleInfo(fbl::Array<zx_info_handle_extended_t>* handles) const {
  for (;;) {
    size_t count = HandleCount();
    // TODO: Bug 45685. This memory allocation should come from a different pool since it
    // can be larger than one page.
    fbl::AllocChecker ac;
    handles->reset(new (&ac) zx_info_handle_extended_t[count], count);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }

    {
      Guard<BrwLockPi, BrwLockPi::Reader> guard{&lock_};
      if (count != count_) {
        continue;
      }

      size_t index = 0;
      ForEachHandleLocked([&](zx_handle_t handle, zx_rights_t rights, const Dispatcher* disp) {
        auto& entry = (*handles)[index++];
        entry = {disp->get_type(),         handle, rights, 0u, disp->get_koid(),
                 disp->get_related_koid(), 0u};
        return ZX_OK;
      });
    }
    return ZX_OK;
  }
}

bool HandleTable::IsHandleValid(zx_handle_t handle_value) {
  Guard<BrwLockPi, BrwLockPi::Reader> guard{&lock_};
  return (GetHandleLocked(handle_value) != nullptr);
}

HandleTable::HandleCursor::HandleCursor(HandleTable* handle_table) : handle_table_(handle_table) {
  Guard<BrwLockPi, BrwLockPi::Writer> guard{&handle_table_->lock_};
  if (!handle_table_->handles_.is_empty()) {
    iter_ = handle_table_->handles_.begin();
  } else {
    iter_ = handle_table_->handles_.end();
  }

  // Register so this cursor can be invalidated or advanced if the handle it points to is removed.
  handle_table_->cursors_.push_front(this);
}

HandleTable::HandleCursor::~HandleCursor() {
  Guard<BrwLockPi, BrwLockPi::Writer> guard{&handle_table_->lock_};
  handle_table_->cursors_.erase(*this);
}

void HandleTable::HandleCursor::Invalidate() { iter_ = handle_table_->handles_.end(); }

Handle* HandleTable::HandleCursor::Next() {
  if (iter_ == handle_table_->handles_.end()) {
    return nullptr;
  }

  Handle* result = &*iter_;
  iter_++;
  return result;
}

void HandleTable::HandleCursor::AdvanceIf(const Handle* h) {
  if (iter_ != handle_table_->handles_.end()) {
    if (&*iter_ == h) {
      iter_++;
    }
  }
}
