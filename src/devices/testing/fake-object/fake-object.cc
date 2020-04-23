// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dlfcn.h>
#include <lib/fake-object/object.h>
#include <zircon/compiler.h>
#include <zircon/syscalls.h>

#include <fbl/ref_ptr.h>

__EXPORT
HandleTable& FakeHandleTable() {
  static HandleTable gHandleTable;
  return gHandleTable;
}

__EXPORT void* FindRealSyscall(const char* name) {
  static void* vdso = dlopen("libzircon.so", RTLD_NOLOAD);
  return dlsym(vdso, name);
}

// Closes a fake handle. Real handles are passed through to |zx_handle_close|.
// In the event of ZX_HANDLE_INVALID, that is technically a valid fake handle due
// to fake handles all being even values.
__EXPORT
zx_status_t zx_handle_close(zx_handle_t handle) {
  if (HandleTable::IsValidFakeHandle(handle)) {
    fbl::RefPtr<Object> obj;
    zx_status_t status = FakeHandleTable().Get(handle, &obj);
    ftracef("handle = %#x, obj = %p, status = %d\n", handle, obj.get(), status);
    ZX_ASSERT_MSG(status == ZX_OK, "fake_%s: Bad handle = %#x, status = %d\n", __func__, handle,
                  status);
    return FakeHandleTable().Remove(handle);
  }
  return REAL_SYSCALL(zx_handle_close)(handle);
}

// Calls our |zx_handle_close| on each handle, ensuring that both real and fake handles are
// closed properly when grouped.
__EXPORT
zx_status_t zx_handle_close_many(const zx_handle_t* handles, size_t num_handles) {
  for (size_t i = 0; i < num_handles; i++) {
    zx_handle_close(handles[i]);
  }

  return ZX_OK;
}

// Duplicates a fake handle, or if it is a real handle, calls the real
// zx_handle_duplicate function.
// |rights| is ignored for fake handles.
__EXPORT
zx_status_t zx_handle_duplicate(zx_handle_t handle, zx_rights_t rights, zx_handle_t* out) {
  if (HandleTable::IsValidFakeHandle(handle)) {
    fbl::RefPtr<Object> obj;
    zx_status_t status = FakeHandleTable().Get(handle, &obj);
    ftracef("handle = %#x, obj = %p, status = %d\n", handle, obj.get(), status);
    ZX_ASSERT_MSG(status == ZX_OK, "fake_%s: Bad handle = %#x, status = %d\n", __func__, handle,
                  status);
    return FakeHandleTable().Add(std::move(obj), out);
  }
  return REAL_SYSCALL(zx_handle_duplicate)(handle, rights, out);
}

// Adds an object to the table a second time before removing the first handle.
__EXPORT
zx_status_t zx_handle_replace(zx_handle_t handle, zx_rights_t rights, zx_handle_t* out) {
  if (HandleTable::IsValidFakeHandle(handle)) {
    fbl::RefPtr<Object> obj;
    zx_status_t status = FakeHandleTable().Get(handle, &obj);
    ftracef("handle = %#x, obj = %p, status = %d\n", handle, obj.get(), status);
    ZX_ASSERT_MSG(status == ZX_OK, "fake_%s: Bad handle = %#x, status = %d\n", __func__, handle,
                  status);
    ZX_ASSERT(FakeHandleTable().Add(std::move(obj), out) == ZX_OK);
    ZX_ASSERT(FakeHandleTable().Remove(handle) == ZX_OK);
    return ZX_OK;
  }

  return REAL_SYSCALL(zx_handle_replace)(handle, rights, out);
}

// All object syscalls below will pass valid objects to the real syscalls and fake
// syscalls to the appropriate method on the fake object implemented for that type.
__EXPORT
zx_status_t zx_object_get_child(zx_handle_t handle, uint64_t koid, zx_rights_t rights,
                                zx_handle_t* out) {
  if (!HandleTable::IsValidFakeHandle(handle)) {
    return REAL_SYSCALL(zx_object_get_child)(handle, koid, rights, out);
  }

  fbl::RefPtr<Object> obj;
  zx_status_t status = FakeHandleTable().Get(handle, &obj);
  ZX_ASSERT_MSG(status == ZX_OK, "fake_%s: Bad handle = %#x, status = %d\n", __func__, handle,
                status);
  return obj->get_child(handle, koid, rights, out);
}

__EXPORT
zx_status_t zx_object_get_info(zx_handle_t handle, uint32_t topic, void* buffer, size_t buffer_size,
                               size_t* actual_count, size_t* avail_count) {
  if (!HandleTable::IsValidFakeHandle(handle)) {
    return REAL_SYSCALL(zx_object_get_info)(handle, topic, buffer, buffer_size, actual_count,
                                            avail_count);
  }

  fbl::RefPtr<Object> obj;
  zx_status_t status = FakeHandleTable().Get(handle, &obj);
  ZX_ASSERT_MSG(status == ZX_OK, "fake_%s: Bad handle = %#x, status = %d\n", __func__, handle,
                status);
  return obj->get_info(handle, topic, buffer, buffer_size, actual_count, avail_count);
}

__EXPORT
zx_status_t zx_object_get_property(zx_handle_t handle, uint32_t property, void* value,
                                   size_t value_size) {
  if (!HandleTable::IsValidFakeHandle(handle)) {
    return REAL_SYSCALL(zx_object_get_property)(handle, property, value, value_size);
  }

  fbl::RefPtr<Object> obj;
  zx_status_t status = FakeHandleTable().Get(handle, &obj);
  ZX_ASSERT_MSG(status == ZX_OK, "fake_%s: Bad handle = %#x, status = %d\n", __func__, handle,
                status);
  return obj->get_property(handle, property, value, value_size);
}

__EXPORT
zx_status_t zx_object_set_profile(zx_handle_t handle, zx_handle_t profile, uint32_t options) {
  if (!HandleTable::IsValidFakeHandle(handle)) {
    return REAL_SYSCALL(zx_object_set_profile)(handle, profile, options);
  }

  fbl::RefPtr<Object> obj;
  zx_status_t status = FakeHandleTable().Get(handle, &obj);
  ZX_ASSERT_MSG(status == ZX_OK, "fake_%s: Bad handle = %#x, status = %d\n", __func__, handle,
                status);
  return obj->set_profile(handle, profile, options);
}

__EXPORT
zx_status_t zx_object_set_property(zx_handle_t handle, uint32_t property, const void* value,
                                   size_t value_size) {
  if (!HandleTable::IsValidFakeHandle(handle)) {
    return REAL_SYSCALL(zx_object_set_property)(handle, property, value, value_size);
  }

  fbl::RefPtr<Object> obj;
  zx_status_t status = FakeHandleTable().Get(handle, &obj);
  ZX_ASSERT_MSG(status == ZX_OK, "fake_%s: Bad handle = %#x, status = %d\n", __func__, handle,
                status);
  return obj->set_property(handle, property, value, value_size);
}

__EXPORT
zx_status_t zx_object_signal(zx_handle_t handle, uint32_t clear_mask, uint32_t set_mask) {
  if (!HandleTable::IsValidFakeHandle(handle)) {
    return REAL_SYSCALL(zx_object_signal)(handle, clear_mask, set_mask);
  }

  fbl::RefPtr<Object> obj;
  zx_status_t status = FakeHandleTable().Get(handle, &obj);
  ZX_ASSERT_MSG(status == ZX_OK, "fake_%s: Bad handle = %#x, status = %d\n", __func__, handle,
                status);
  return obj->signal(handle, clear_mask, set_mask);
}

__EXPORT
zx_status_t signal_peer(zx_handle_t handle, uint32_t clear_mask, uint32_t set_mask) {
  if (!HandleTable::IsValidFakeHandle(handle)) {
    return REAL_SYSCALL(zx_object_signal_peer)(handle, clear_mask, set_mask);
  }

  fbl::RefPtr<Object> obj;
  zx_status_t status = FakeHandleTable().Get(handle, &obj);
  ZX_ASSERT_MSG(status == ZX_OK, "fake_%s: Bad handle = %#x, status = %d\n", __func__, handle,
                status);
  return obj->signal_peer(handle, clear_mask, set_mask);
}

__EXPORT
zx_status_t zx_object_wait_one(zx_handle_t handle, zx_signals_t signals, zx_time_t deadline,
                               zx_signals_t* observed) {
  if (!HandleTable::IsValidFakeHandle(handle)) {
    return REAL_SYSCALL(zx_object_wait_one)(handle, signals, deadline, observed);
  }

  fbl::RefPtr<Object> obj;
  zx_status_t status = FakeHandleTable().Get(handle, &obj);
  ZX_ASSERT_MSG(status == ZX_OK, "fake_%s: Bad handle = %#x, status = %d\n", __func__, handle,
                status);
  return obj->wait_one(handle, signals, deadline, observed);
}

__EXPORT
zx_status_t zx_object_wait_async(zx_handle_t handle, zx_handle_t port, uint64_t key,
                                 zx_signals_t signals, uint32_t options) {
  if (!HandleTable::IsValidFakeHandle(handle)) {
    return REAL_SYSCALL(zx_object_wait_async)(handle, port, key, signals, options);
  }

  fbl::RefPtr<Object> obj;
  zx_status_t status = FakeHandleTable().Get(handle, &obj);
  ZX_ASSERT_MSG(status == ZX_OK, "fake_%s: Bad handle = %#x, status = %d\n", __func__, handle,
                status);
  return obj->wait_async(handle, port, key, signals, options);
}

__EXPORT
zx_status_t zx_object_wait_many(zx_wait_item_t* items, size_t count, zx_time_t deadline) {
  for (size_t i = 0; i < count; i++) {
    ZX_ASSERT_MSG(!HandleTable::IsValidFakeHandle(items[i].handle),
                  "Fake handle %u was passed as index %zu to zx_object_wait_many!\n",
                  items[i].handle, i);
  }

  // No fake handles were passed in so it's safe to call the real syscall.
  return REAL_SYSCALL(zx_object_wait_many)(items, count, deadline);
}

__EXPORT
zx_status_t fake_object_create(zx_handle_t* out) {
  fbl::RefPtr<Object> obj = fbl::MakeRefCounted<Object>();
  return FakeHandleTable().Add(std::move(obj), out);
}

__EXPORT
zx_koid_t fake_object_get_koid(zx_handle_t handle) {
  fbl::RefPtr<Object> obj;
  ZX_ASSERT(FakeHandleTable().Get(handle, &obj) == ZX_OK);
  return obj->get_koid();
}
