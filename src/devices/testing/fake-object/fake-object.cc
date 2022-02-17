// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dlfcn.h>
#include <lib/fake-object/object.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <vector>

#include <fbl/ref_ptr.h>

namespace fake_object {

__EXPORT
HandleTable& FakeHandleTable() {
  static HandleTable gHandleTable;
  return gHandleTable;
}

__EXPORT void* FindRealSyscall(const char* name) {
  static void* vdso = dlopen("libzircon.so", RTLD_NOLOAD);
  return dlsym(vdso, name);
}

__EXPORT
zx::status<zx_handle_t> fake_object_create_typed(zx_obj_type_t type) {
  auto obj = fbl::MakeRefCounted<Object>(type);
  if (auto res = FakeHandleTable().Add(std::move(obj)); res.is_ok()) {
    return zx::success(res.value());
  } else {
    return res.take_error();
  }
}

__EXPORT
zx::status<zx_handle_t> fake_object_create() { return fake_object_create_typed(ZX_OBJ_TYPE_NONE); }

__EXPORT
zx::status<zx_koid_t> fake_object_get_koid(zx_handle_t handle) {
  if (auto res = FakeHandleTable().Get(handle); res.is_ok()) {
    return zx::success(res->get_koid());
  } else {
    return res.take_error();
  }
}

}  // namespace fake_object

// Closes a fake handle. Real handles are passed through to |zx_handle_close|.
// In the event of ZX_HANDLE_INVALID, that is technically a valid fake handle due
// to fake handles all being even values.
__EXPORT
zx_status_t zx_handle_close(zx_handle_t handle) {
  if (!fake_object::HandleTable::IsValidFakeHandle(handle)) {
    return REAL_SYSCALL(zx_handle_close)(handle);
  }
  return fake_object::FakeHandleTable().Remove(handle).status_value();
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
  if (!fake_object::HandleTable::IsValidFakeHandle(handle)) {
    return REAL_SYSCALL(zx_handle_duplicate)(handle, rights, out);
  }

  auto get_res = fake_object::FakeHandleTable().Get(handle);
  if (!get_res.is_ok()) {
    return get_res.status_value();
  }
  auto add_res = fake_object::FakeHandleTable().Add(std::move(get_res.value()));
  if (add_res.is_ok()) {
    *out = add_res.value();
  }
  return add_res.status_value();
}

// Adds an object to the table a second time before removing the first handle.
__EXPORT
zx_status_t zx_handle_replace(zx_handle_t handle, zx_rights_t rights, zx_handle_t* out) {
  if (!fake_object::HandleTable::IsValidFakeHandle(handle)) {
    return REAL_SYSCALL(zx_handle_replace)(handle, rights, out);
  }

  zx::status get_res = fake_object::FakeHandleTable().Get(handle);
  if (!get_res.is_ok()) {
    return get_res.status_value();
  }
  zx::status add_res = fake_object::FakeHandleTable().Add(std::move(get_res.value()));
  ZX_ASSERT(add_res.is_ok());
  *out = add_res.value();
  ZX_ASSERT(fake_object::FakeHandleTable().Remove(handle).is_ok());
  return ZX_OK;
}

// All object syscalls below will pass valid objects to the real syscalls and fake
// syscalls to the appropriate method on the fake object implemented for that type.
__EXPORT
zx_status_t zx_object_get_child(zx_handle_t handle, uint64_t koid, zx_rights_t rights,
                                zx_handle_t* out) {
  if (!fake_object::HandleTable::IsValidFakeHandle(handle)) {
    return REAL_SYSCALL(zx_object_get_child)(handle, koid, rights, out);
  }

  zx::status get_res = fake_object::FakeHandleTable().Get(handle);
  if (!get_res.is_ok()) {
    printf("%s: Bad handle = %#x, status = %d\n", __func__, handle, get_res.status_value());
    return get_res.status_value();
  }
  return get_res.value()->get_child(handle, koid, rights, out);
}

__EXPORT
zx_status_t zx_object_get_info(zx_handle_t handle, uint32_t topic, void* buffer, size_t buffer_size,
                               size_t* actual_count, size_t* avail_count) {
  if (!fake_object::HandleTable::IsValidFakeHandle(handle)) {
    return REAL_SYSCALL(zx_object_get_info)(handle, topic, buffer, buffer_size, actual_count,
                                            avail_count);
  }

  zx::status get_res = fake_object::FakeHandleTable().Get(handle);
  if (!get_res.is_ok()) {
    printf("%s: Bad handle = %#x, status = %d\n", __func__, handle, get_res.status_value());
    return get_res.status_value();
  }
  return get_res.value()->get_info(handle, topic, buffer, buffer_size, actual_count, avail_count);
}

__EXPORT
zx_status_t zx_object_get_property(zx_handle_t handle, uint32_t property, void* value,
                                   size_t value_size) {
  if (!fake_object::HandleTable::IsValidFakeHandle(handle)) {
    return REAL_SYSCALL(zx_object_get_property)(handle, property, value, value_size);
  }

  zx::status get_res = fake_object::FakeHandleTable().Get(handle);
  if (!get_res.is_ok()) {
    printf("%s: Bad handle = %#x, status = %d\n", __func__, handle, get_res.status_value());
    return get_res.status_value();
  }
  return get_res.value()->get_property(handle, property, value, value_size);
}

__EXPORT
zx_status_t zx_object_set_profile(zx_handle_t handle, zx_handle_t profile, uint32_t options) {
  if (!fake_object::HandleTable::IsValidFakeHandle(handle)) {
    return REAL_SYSCALL(zx_object_set_profile)(handle, profile, options);
  }

  zx::status get_res = fake_object::FakeHandleTable().Get(handle);
  if (!get_res.is_ok()) {
    printf("%s: Bad handle = %#x, status = %d\n", __func__, handle, get_res.status_value());
    return get_res.status_value();
  }
  return get_res.value()->set_profile(handle, profile, options);
}

__EXPORT
zx_status_t zx_object_set_property(zx_handle_t handle, uint32_t property, const void* value,
                                   size_t value_size) {
  if (!fake_object::HandleTable::IsValidFakeHandle(handle)) {
    return REAL_SYSCALL(zx_object_set_property)(handle, property, value, value_size);
  }

  zx::status get_res = fake_object::FakeHandleTable().Get(handle);
  if (!get_res.is_ok()) {
    printf("%s: Bad handle = %#x, status = %d\n", __func__, handle, get_res.status_value());
    return get_res.status_value();
  }
  return get_res.value()->set_property(handle, property, value, value_size);
}

__EXPORT
zx_status_t zx_object_signal(zx_handle_t handle, uint32_t clear_mask, uint32_t set_mask) {
  if (!fake_object::HandleTable::IsValidFakeHandle(handle)) {
    return REAL_SYSCALL(zx_object_signal)(handle, clear_mask, set_mask);
  }

  if (auto get_res = fake_object::FakeHandleTable().Get(handle); get_res.is_ok()) {
    return get_res->signal(handle, clear_mask, set_mask);
  } else {
    printf("%s: Bad handle = %#x, status = %d\n", __func__, handle, get_res.status_value());
    return get_res.status_value();
  }
}

__EXPORT
zx_status_t zx_object_signal_peer(zx_handle_t handle, uint32_t clear_mask, uint32_t set_mask) {
  if (!fake_object::HandleTable::IsValidFakeHandle(handle)) {
    return REAL_SYSCALL(zx_object_signal_peer)(handle, clear_mask, set_mask);
  }

  zx::status get_res = fake_object::FakeHandleTable().Get(handle);
  if (!get_res.is_ok()) {
    printf("%s: Bad handle = %#x, status = %d\n", __func__, handle, get_res.status_value());
    return get_res.status_value();
  }
  return get_res.value()->signal_peer(handle, clear_mask, set_mask);
}

__EXPORT
zx_status_t zx_object_wait_one(zx_handle_t handle, zx_signals_t signals, zx_time_t deadline,
                               zx_signals_t* observed) {
  if (!fake_object::HandleTable::IsValidFakeHandle(handle)) {
    return REAL_SYSCALL(zx_object_wait_one)(handle, signals, deadline, observed);
  }

  zx::status get_res = fake_object::FakeHandleTable().Get(handle);
  if (!get_res.is_ok()) {
    printf("%s: Bad handle = %#x, status = %d\n", __func__, handle, get_res.status_value());
    return get_res.status_value();
  }
  return get_res.value()->wait_one(handle, signals, deadline, observed);
}

__EXPORT
zx_status_t zx_object_wait_async(zx_handle_t handle, zx_handle_t port, uint64_t key,
                                 zx_signals_t signals, uint32_t options) {
  if (!fake_object::HandleTable::IsValidFakeHandle(handle)) {
    return REAL_SYSCALL(zx_object_wait_async)(handle, port, key, signals, options);
  }

  zx::status get_res = fake_object::FakeHandleTable().Get(handle);
  if (!get_res.is_ok()) {
    printf("%s: Bad handle = %#x, status = %d\n", __func__, handle, get_res.status_value());
    return get_res.status_value();
  }
  return get_res.value()->wait_async(handle, port, key, signals, options);
}

__EXPORT
zx_status_t zx_object_wait_many(zx_wait_item_t* items, size_t count, zx_time_t deadline) {
  for (size_t i = 0; i < count; i++) {
    ZX_ASSERT_MSG(!fake_object::HandleTable::IsValidFakeHandle(items[i].handle),
                  "Fake handle %u was passed as index %zu to zx_object_wait_many!\n",
                  items[i].handle, i);
  }

  // No fake handles were passed in so it's safe to call the real syscall.
  return REAL_SYSCALL(zx_object_wait_many)(items, count, deadline);
}

std::vector<zx_handle_disposition_t> FixHandleDisposition(zx_handle_disposition_t* handles,
                                                          uint32_t num_handles) {
  // Fake objects all have type VMO so they will fail any write_etc checks
  // around type. We can work around this by modifying the disposition array to
  // not check type, then update the returned results so they look like the
  // client would expect. We need to copy this because the client may intend to
  // check that the types and results match in tests.
  std::vector<zx_handle_disposition_t> filtered_handles;
  for (uint32_t i = 0; i < num_handles; i++) {
    filtered_handles.push_back(handles[i]);
    if (fake_object::HandleTable::IsValidFakeHandle(handles[i].handle)) {
      filtered_handles[i].type = ZX_OBJ_TYPE_NONE;
    }
  }
  return filtered_handles;
}

// Fake handles coming from the other side of a channel write will be of type ZX_OBJ_TYPE_VMO
// and must be adjusted back into their correct fake type before being handed to the caller.
void FixIncomingHandleTypes(zx_handle_info_t* handles, uint32_t num_handles) {
  for (uint32_t i = 0; i < num_handles; i++) {
    if (fake_object::HandleTable::IsValidFakeHandle(handles[i].handle)) {
      auto object = fake_object::FakeHandleTable().Get(handles[i].handle);
      handles[i].type = static_cast<zx_obj_type_t>(object->type());
    }
  }
}

__EXPORT
zx_status_t zx_channel_write_etc(zx_handle_t handle, uint32_t options, const void* bytes,
                                 uint32_t num_bytes, zx_handle_disposition_t* handles,
                                 uint32_t num_handles) {
  auto filtered_handles = FixHandleDisposition(handles, num_handles);
  zx_status_t status = REAL_SYSCALL(zx_channel_write_etc)(handle, options, bytes, num_bytes,
                                                          filtered_handles.data(), num_handles);
  // Copy the results back from the real syscall's results since the client
  // still expects real results from valid handles.
  for (uint32_t i = 0; i < num_handles; i++) {
    handles[i].result = filtered_handles[i].result;
  }
  return status;
}

__EXPORT
zx_status_t zx_channel_call_etc(zx_handle_t handle, uint32_t options, zx_time_t deadline,
                                zx_channel_call_etc_args_t* args, uint32_t* actual_bytes,
                                uint32_t* actual_handles) {
  zx_channel_call_etc_args_t real_args = *args;
  auto filtered_handles = FixHandleDisposition(real_args.wr_handles, real_args.wr_num_handles);
  real_args.wr_handles = filtered_handles.data();
  zx_status_t status = REAL_SYSCALL(zx_channel_call_etc)(handle, options, deadline, &real_args,
                                                         actual_bytes, actual_handles);
  // Copy the results back from the real syscall's results since the client
  // still expects real results from valid handles.
  for (uint32_t i = 0; i < real_args.wr_num_handles; i++) {
    args->wr_handles[i].result = real_args.wr_handles[i].result;
  }

  if (status != ZX_OK) {
    return status;
  }

  FixIncomingHandleTypes(args->rd_handles, args->rd_num_handles);
  return status;
}

__EXPORT
zx_status_t zx_channel_read_etc(zx_handle_t handle, uint32_t options, void* bytes,
                                zx_handle_info_t* handles, uint32_t num_bytes, uint32_t num_handles,
                                uint32_t* actual_bytes, uint32_t* actual_handles) {
  zx_status_t status = REAL_SYSCALL(zx_channel_read_etc)(handle, options, bytes, handles, num_bytes,
                                                         num_handles, actual_bytes, actual_handles);
  if (status != ZX_OK) {
    return status;
  }

  FixIncomingHandleTypes(handles, std::min(num_handles, *actual_handles));
  return status;
}
