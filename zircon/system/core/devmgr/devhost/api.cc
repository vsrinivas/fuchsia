// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>

__EXPORT zx_status_t device_add_from_driver(zx_driver_t* drv, zx_device_t* parent,
                                            device_add_args_t* args, zx_device_t** out) {
  __builtin_abort();
}

__EXPORT zx_status_t device_remove_deprecated(zx_device_t* dev) { __builtin_abort(); }

__EXPORT zx_status_t device_remove(zx_device_t* dev) { __builtin_abort(); }

__EXPORT zx_status_t device_rebind(zx_device_t* dev) { __builtin_abort(); }

__EXPORT void device_make_visible(zx_device_t* dev, const device_init_reply_args_t* args) {
  __builtin_abort();
}

__EXPORT void device_async_remove(zx_device_t* dev) { __builtin_abort(); }

__EXPORT void device_init_reply(zx_device_t* dev, zx_status_t status,
                                const device_make_visible_args_t* args) { __builtin_abort(); }

__EXPORT void device_unbind_reply(zx_device_t* dev) { __builtin_abort(); }

__EXPORT void device_suspend_reply(zx_device_t* dev, zx_status_t status, uint8_t out_state) {
  __builtin_abort();
}

__EXPORT zx_status_t device_get_profile(zx_device_t* dev, uint32_t priority, const char* name,
                                        zx_handle_t* out_profile) {
  __builtin_abort();
}

__EXPORT zx_status_t device_get_deadline_profile(zx_device_t* device, uint64_t capacity,
                                                 uint64_t deadline, uint64_t period,
                                                 const char* name, zx_handle_t* out_profile) {
  __builtin_abort();
}

__EXPORT const char* device_get_name(zx_device_t* dev) { __builtin_abort(); }

__EXPORT zx_device_t* device_get_parent(zx_device_t* dev) { __builtin_abort(); }

__EXPORT zx_status_t device_get_protocol(const zx_device_t* dev, uint32_t proto_id, void* out) {
  __builtin_abort();
}

__EXPORT void device_state_clr_set(zx_device_t* dev, zx_signals_t clearflag, zx_signals_t setflag) {
  __builtin_abort();
}

__EXPORT zx_off_t device_get_size(zx_device_t* dev) { __builtin_abort(); }

__EXPORT zx_handle_t get_root_resource() { return -1; }

__EXPORT zx_status_t load_firmware(zx_device_t* dev, const char* path, zx_handle_t* fw,
                                   size_t* size) {
  __builtin_abort();
}

__EXPORT zx_status_t device_get_metadata(zx_device_t* dev, uint32_t type, void* buf, size_t buflen,
                                         size_t* actual) {
  __builtin_abort();
}

__EXPORT zx_status_t device_get_metadata_size(zx_device_t* dev, uint32_t type, size_t* out_size) {
  __builtin_abort();
}

__EXPORT zx_status_t device_add_metadata(zx_device_t* dev, uint32_t type, const void* data,
                                         size_t length) {
  __builtin_abort();
}

__EXPORT zx_status_t device_publish_metadata(zx_device_t* dev, const char* path, uint32_t type,
                                             const void* data, size_t length) {
  __builtin_abort();
}

__EXPORT zx_status_t device_add_composite(zx_device_t* dev, const char* name,
                                          const composite_device_desc_t* comp_desc) {
  __builtin_abort();
}

__EXPORT zx_status_t device_schedule_work(zx_device_t* dev, void (*callback)(void*), void* cookie) {
  __builtin_abort();
}
__EXPORT void driver_printf(uint32_t flags, const char* fmt, ...) { /* no abort here */
}
