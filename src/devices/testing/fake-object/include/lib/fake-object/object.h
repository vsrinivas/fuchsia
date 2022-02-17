// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTING_FAKE_OBJECT_INCLUDE_LIB_FAKE_OBJECT_OBJECT_H_
#define SRC_DEVICES_TESTING_FAKE_OBJECT_INCLUDE_LIB_FAKE_OBJECT_OBJECT_H_

#include <lib/zx/status.h>
#include <limits.h>
#include <stdio.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <unordered_map>

#include <fbl/auto_lock.h>
#include <fbl/canary.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#define FAKE_OBJECT_TRACE 0
#if FAKE_OBJECT_TRACE
#define ftracef(...)                           \
  {                                            \
    printf("fake-object %16.16s: ", __func__); \
    printf(__VA_ARGS__);                       \
  }
#else
#define ftracef(...) ;
#endif

namespace fake_object {

class Object : public fbl::RefCounted<Object> {
 public:
  Object() = delete;
  explicit Object(zx_obj_type_t type) : type_(type) {}
  // For each object-related syscall we stub out a fake-specific version that
  // can be implemented within the derived fake objects. syscall symbols defined
  // in the fake-object source will route to the fake impl or real impl
  // depending on the handle's validity.
  virtual zx_status_t get_child(zx_handle_t /* handle */, uint64_t /* koid */,
                                zx_rights_t /* rights */, zx_handle_t* /* out */) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  virtual zx_status_t get_info(zx_handle_t /* handle */, uint32_t /* topic */, void* /* buffer */,
                               size_t /* buffer_size */, size_t* /* actual_count */,
                               size_t* /* aval_count */) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  virtual zx_status_t get_property(zx_handle_t /* handle */, uint32_t /* property */,
                                   void* /* value */, size_t /* value_size */) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  virtual zx_status_t set_profile(zx_handle_t /* handle */, zx_handle_t /* profile */,
                                  uint32_t /* options */) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  virtual zx_status_t set_property(zx_handle_t /* handle */, uint32_t /* property */,
                                   const void* /* value */, size_t /* value_size */) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  virtual zx_status_t signal(zx_handle_t /* handle */, uint32_t /* clear_mask */,
                             uint32_t /* set_mask */) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  virtual zx_status_t signal_peer(zx_handle_t /* handle */, uint32_t /* clear_mask */,
                                  uint32_t /* set_mask */) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  virtual zx_status_t wait_one(zx_handle_t /* handle */, zx_signals_t /* signals */,
                               zx_time_t /* deadline */, zx_signals_t* /* observed */) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // |zx_object_wait_many| is omitted because we would need to define what it means to wait on
  // both real objects and fake objects at the same time due to it taking a handle table parameter.

  virtual zx_status_t wait_async(zx_handle_t /* handle */, zx_handle_t /* port */,
                                 uint64_t /* key */, zx_signals_t /* signals */,
                                 uint32_t /* options */) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  virtual ~Object() = default;

  // For the purposes of tests we only need to ensure the koid is unique to the object.
  zx_koid_t get_koid() const { return reinterpret_cast<zx_koid_t>(this); }
  zx_obj_type_t type() const { return type_; }

 private:
  zx_obj_type_t type_;
};

class HandleTable {
 public:
  HandleTable() = default;
  ~HandleTable() = default;

  HandleTable(const HandleTable&) = delete;
  HandleTable& operator=(const HandleTable&) = delete;
  HandleTable(HandleTable&&) = delete;
  HandleTable& operator=(HandleTable&&) = delete;

  static bool IsValidFakeHandle(zx_handle_t handle);

  zx::status<fbl::RefPtr<Object>> Get(zx_handle_t handle) __TA_EXCLUDES(lock_);
  zx::status<> Remove(zx_handle_t handle) __TA_EXCLUDES(lock_);
  zx::status<zx_handle_t> Add(fbl::RefPtr<Object> obj) __TA_EXCLUDES(lock_);
  void Clear() __TA_EXCLUDES(lock_);

  // Walks the handle table and calls |cb| on each handle that matches the
  // provided |type|. Stops walking the table when |cb| returns false.
  //
  // |cb| must NOT attempt to acquire the lock, so this method is not suitable
  // for internal methods.
  template <typename ObjectCallback>
  void ForEach(zx_obj_type_t type, const ObjectCallback cb) __TA_EXCLUDES(lock_) {
    fbl::AutoLock lock(&lock_);
    for (const auto& e : handles_) {
      if (e.second->type() == type) {
        if (!std::forward<const ObjectCallback>(cb)(e.second.get())) {
          break;
        }
      }
    }
  }

  void Dump() __TA_EXCLUDES(lock_);
  // We use the overall size of the vector to calculate new indices
  // so to determine the occupied size we have to verify each element.
  size_t size() __TA_EXCLUDES(lock_) {
    fbl::AutoLock lock(&lock_);
    return handles_.size();
  }

 private:
  static constexpr const char kFakeObjectPropName[ZX_MAX_NAME_LEN] = "FAKEOBJECT";

  fbl::Mutex lock_;
  std::unordered_map<zx_handle_t, fbl::RefPtr<Object>> handles_ __TA_GUARDED(lock_);
  fbl::Canary<fbl::magic("FAKE")> canary_;
};

// Singleton accessor for tests and any derived fake object type.
HandleTable& FakeHandleTable();

// Creates a base object for testing handle methods.
zx::status<zx_handle_t> fake_object_create();
zx::status<zx_handle_t> fake_object_create_typed(zx_obj_type_t type);
zx::status<zx_koid_t> fake_object_get_koid(zx_handle_t);

void* FindRealSyscall(const char* name);

}  // namespace fake_object

#define REAL_SYSCALL(name)                                                          \
  ([]() {                                                                           \
    static const auto real_syscall =                                                \
        reinterpret_cast<decltype(name)*>(fake_object::FindRealSyscall("_" #name)); \
    return real_syscall;                                                            \
  }())

#endif  // SRC_DEVICES_TESTING_FAKE_OBJECT_INCLUDE_LIB_FAKE_OBJECT_OBJECT_H_
