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

#include <vector>

#include <fbl/auto_lock.h>
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

enum class HandleType : uint32_t {
  BASE,  // A non-derived object, used for tests and assertions.
  BTI,
  MSI_ALLOCATION,
  MSI_INTERRUPT,
  PMT,
  RESOURCE,
};

class Object : public fbl::RefCounted<Object> {
 public:
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
  virtual HandleType type() const { return HandleType::BASE; }
};

class HandleTable {
 public:
  HandleTable() = default;
  ~HandleTable() = default;

  HandleTable(const HandleTable&) = delete;
  HandleTable& operator=(const HandleTable&) = delete;
  HandleTable(HandleTable&&) = delete;
  HandleTable& operator=(HandleTable&&) = delete;

  // A valid fake handle does not have the reserved bits of a real handle set, and
  // has a non-zero value after shifting. This ensures that they will not overlap
  // with real handles, and that ZX_HANDLE_INVALID is not a valid fake handle.
  static bool IsValidFakeHandle(zx_handle_t handle) {
    return ((handle & ZX_HANDLE_FIXED_BITS_MASK) == 0) && (handle & ~ZX_HANDLE_FIXED_BITS_MASK);
  }

  __EXPORT
  zx::status<fbl::RefPtr<Object>> Get(zx_handle_t handle) __TA_EXCLUDES(lock_) {
    fbl::AutoLock guard(&lock_);
    return GetLocked(handle);
  }

  zx::status<> Remove(zx_handle_t handle) __TA_EXCLUDES(lock_);
  zx::status<zx_handle_t> Add(fbl::RefPtr<Object> obj) __TA_EXCLUDES(lock_);
  void Clear() __TA_EXCLUDES(lock_);

  // Walks the handle table and calls |cb| on each handle that matches the
  // provided |type|. Stops walking the table when |cb| returns false.
  //
  // |cb| must NOT attempt to acquire the lock, so this method is not suitable
  // for internal methods.
  template <typename ObjectCallback>
  void ForEach(HandleType type, const ObjectCallback&& cb) __TA_EXCLUDES(lock_) {
    fbl::AutoLock lock(&lock_);
    for (const auto& obj : handles_) {
      if (obj && obj->type() == type) {
        if (!std::forward<const ObjectCallback>(cb)(obj.get())) {
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
    return size_locked();
  }

 private:
  __EXPORT
  zx::status<fbl::RefPtr<Object>> GetLocked(zx_handle_t handle) __TA_REQUIRES(lock_);

  size_t size_locked() __TA_REQUIRES(lock_) {
    size_t s = 0;
    for (auto& h : handles_) {
      if (h) {
        s++;
      }
    }
    return s;
  }

  // The first |ZX_HANDLE_FIXED_BITS_MASK| bits of real handle values are
  // required to be |1|. This requirement is defined both in the public handle
  // documentation and in ProcessDispatcher's implementation.
  static constexpr size_t FakeHandleShiftBitsCount() {
    size_t bits = 0;
    uint32_t val = ZX_HANDLE_FIXED_BITS_MASK;
    while (val) {
      val >>= 1;
      bits++;
    }
    return bits;
  }

  // Fake handle values start at (1 << 2) which maps to 0 in the handle table.
  static zx::status<size_t> HandleToIndex(zx_handle_t handle) {
    if (!IsValidFakeHandle(handle)) {
      return zx::error(ZX_ERR_BAD_HANDLE);
    }
    return zx::ok((handle >> FakeHandleShiftBitsCount()) - 1);
  }

  static zx_handle_t IndexToHandle(size_t idx) {
    return static_cast<zx_handle_t>((idx + 1) << FakeHandleShiftBitsCount());
  }

  fbl::Mutex lock_;
  std::vector<fbl::RefPtr<Object>> handles_ __TA_GUARDED(lock_);
};

HandleTable& FakeHandleTable();

// Creates a base object for testing handle methods.
zx::status<zx_handle_t> fake_object_create();
zx::status<zx_koid_t> fake_object_get_koid(zx_handle_t);

void* FindRealSyscall(const char* name);
#define REAL_SYSCALL(name)                                             \
  ([]() {                                                              \
    static const auto real_syscall =                                   \
        reinterpret_cast<decltype(name)*>(FindRealSyscall("_" #name)); \
    return real_syscall;                                               \
  }())

#endif  // SRC_DEVICES_TESTING_FAKE_OBJECT_INCLUDE_LIB_FAKE_OBJECT_OBJECT_H_
