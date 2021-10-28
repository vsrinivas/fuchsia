// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_RUNTIME_HANDLE_H_
#define SRC_DEVICES_BIN_DRIVER_RUNTIME_HANDLE_H_

#include <lib/fdf/types.h>
#include <lib/fit/nullable.h>
#include <stdint.h>

#include <array>

#include <fbl/auto_lock.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/ref_ptr.h>

#include "src/devices/bin/driver_runtime/object.h"

namespace driver_runtime {

class Handle;

// Callable object for destroying uniquely owned handles.
struct HandleDestroyer {
  inline void operator()(Handle* handle);
};

// HandleOwner wraps a Handle in a unique_ptr that has single
// ownership of the Handle and deletes it whenever it falls out of scope.
using HandleOwner = std::unique_ptr<Handle, HandleDestroyer>;

// A handle is how a process refers to runtime objects such as fdf_channels.
class Handle : public fbl::SinglyLinkedListable<Handle*> {
 public:
  // Returns a unique reference to a newly created handle.
  // Takes ownership of |object|.
  static HandleOwner Create(fbl::RefPtr<Object> object);

  // This needs to be public for |HandleTableArena|.
  explicit Handle(fbl::RefPtr<Object> object = nullptr, fdf_handle_t value = FDF_HANDLE_INVALID)
      : object_(std::move(object)), value_(value) {}

  // Clears handle state specific to this lifetime.
  // The handle |value| is preserved, as it is used to generate a new handle value
  // referring to the same handle object.
  void Reset() { object_ = nullptr; }

  // Returns whether the handle exists in the handle table.
  static bool HandleExists(fdf_handle_t value) { return MapValueToHandle(value); }

  // Maps |value| to the runtime's Handle object.
  // The Handle must have previously been created with |Create|.
  // This does not provide ownership to the Handle. To destroy the Handle,
  // the caller should use |TakeOwnership|.
  static Handle* MapValueToHandle(fdf_handle_t value);

  // Returns whether |handle_value| is of type fdf_handle_t.
  // Does not do any validation on whether it is a valid fdf handle.
  static bool IsFdfHandle(zx_handle_t handle_value);

  // Returns the object corresponding to |value_|.
  template <typename T>
  zx_status_t GetObject(fbl::RefPtr<T>* out_object) {
    // TODO(fxbug.dev/86542): we should add some type checking once we support more object types.
    *out_object = fbl::RefPtr<T>::Downcast(object());
    if (!*out_object) {
      return ZX_ERR_WRONG_TYPE;
    }
    return ZX_OK;
  }

  // Returns the object corresponding to |handle_value|.
  template <typename T>
  static zx_status_t GetObject(fdf_handle_t handle_value, fbl::RefPtr<T>* out_object) {
    Handle* handle = Handle::MapValueToHandle(handle_value);
    if (!handle) {
      return ZX_ERR_BAD_HANDLE;
    }
    return handle->GetObject<T>(out_object);
  }

  HandleOwner TakeOwnership() { return HandleOwner(this); }

  // Returns the object this handle refers to.
  fbl::RefPtr<Object> object() { return object_; }

  // Returns the handle value which refers to this object.
  fdf_handle_t handle_value() const { return value_; }

 private:
  fbl::RefPtr<Object> object_;
  fdf_handle_t value_;
};

// HandleTableArena provides the memory backing the Handle objects.
// This class is thread-safe.
class HandleTableArena {
 public:
  // TODO(fxbug.dev/86594): fine-tune this numbers, they were randomly selected.
  static constexpr size_t kMaxNumHandles = 64ull * 1024;
  // The number of tables stored in |handle_table_dir_|.
  static constexpr size_t kNumTables = 64;
  // The number of handles per table.
  static constexpr size_t kHandlesPerTable = kMaxNumHandles / kNumTables;

  HandleTableArena() : handle_table_dir_{&initial_handles_} {}

  ~HandleTableArena() {
    // fbl::SinglyLinkedList does not like deleting non-empty lists containing
    // unmanaged pointers. We don't need to free the handle pointers,
    // as the tables backing them will be deleted.
    free_handles_.clear();
    for (const auto& table : handle_table_dir_) {
      // Make sure we don't accidentally delete the |handles_| array that was
      // allocated as part of the class.
      if (table && table != &initial_handles_) {
        delete table;
      }
    }
  }

  // Alloc returns storage for a handle.
  // |out_handle_value| is the generated handle value referring to the returned Handle object.
  Handle* Alloc(fdf_handle_t* out_handle_value);

  // Clears handle state specific to this lifetime and adds the handle to the free
  // list for re-use.
  void Delete(Handle* handle) {
    handle->Reset();

    fbl::AutoLock lock(&lock_);
    free_handles_.push_front(handle);
    num_allocated_--;
  }

  // Returns the handle located in the handle table pointed to by |table|, at |index|.
  // Returns nullptr if the indexes are invalid, or do not point to an allocated handle.
  fit::nullable<Handle*> GetExistingHandle(uint32_t table, uint32_t index);

  // Returns the number of handles currently allocated (does not include freed handles).
  uint32_t num_allocated() {
    fbl::AutoLock lock(&lock_);
    return num_allocated_;
  }

 private:
  // Returns a pointer to memory that can be used to construct a Handle object.
  // |out_table| and |out_index| describes the location of that memory.
  Handle* AllocHandleMemoryLocked(uint32_t* out_table, uint32_t* out_index) __TA_REQUIRES(&lock_);

  fbl::Mutex lock_;
  // The first handle table allocated as part of the class.
  std::array<Handle, kHandlesPerTable> initial_handles_ __TA_GUARDED(&lock_);
  // Directory which holds all the handle tables, including |initial_handles_|.
  // More handle tables are allocated and added to this as needed.
  std::array<std::array<Handle, kHandlesPerTable>*, kNumTables> handle_table_dir_
      __TA_GUARDED(&lock_);

  // Indexes pointing to the next available memory that can be used for handle allocation.
  // Index into |handle_table_dir_|.
  uint32_t dir_index_ __TA_GUARDED(&lock_) = 0;
  // Index into the handle table referred to by|dir_index_|.
  uint32_t handles_index_ __TA_GUARDED(&lock_) = 0;

  // Handles that have been freed are recycled into |free_handles_|.
  fbl::SinglyLinkedList<Handle*> free_handles_ __TA_GUARDED(&lock_);

  // Number of handles currently allocated (does not include freed handles).
  uint32_t num_allocated_ __TA_GUARDED(&lock_) = 0;
};

extern HandleTableArena gHandleTableArena;

// This can't be defined directly in the HandleDestroyer struct definition
// because Handle is an incomplete type at that point.
inline void HandleDestroyer::operator()(Handle* handle) { gHandleTableArena.Delete(handle); }

}  // namespace driver_runtime

#endif  //  SRC_DEVICES_BIN_DRIVER_RUNTIME_HANDLE_H_
