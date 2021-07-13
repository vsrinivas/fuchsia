// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/syslog/logger.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/syscalls/log.h>
#include <zircon/types.h>

namespace no_ddk {

fx_log_severity_t kMinLogSeverity = FX_LOG_INFO;
size_t kFakeFWSize = 0x1000;

}  // namespace no_ddk

__EXPORT
zx_status_t device_add_from_driver(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                                   zx_device_t** out) {
  return ZX_OK;
}

__EXPORT
void device_async_remove(zx_device_t* device) {}

__EXPORT
void device_init_reply(zx_device_t* device, zx_status_t status,
                       const device_init_reply_args_t* args) {}

__EXPORT
void device_unbind_reply(zx_device_t* device) {}

__EXPORT void device_suspend_reply(zx_device_t* dev, zx_status_t status, uint8_t out_state) {}

__EXPORT void device_resume_reply(zx_device_t* dev, zx_status_t status, uint8_t out_power_state,
                                  uint32_t out_perf_state) {}

__EXPORT
zx_status_t device_add_metadata(zx_device_t* device, uint32_t type, const void* data,
                                size_t length) {
  return ZX_OK;
}

__EXPORT
zx_status_t device_get_protocol(const zx_device_t* device, uint32_t proto_id, void* protocol) {
  return ZX_ERR_NOT_SUPPORTED;
}
__EXPORT
zx_status_t device_open_protocol_session_multibindable(const zx_device_t* dev, uint32_t proto_id,
                                                       void* protocol) {
  return ZX_ERR_NOT_SUPPORTED;
}

__EXPORT
const char* device_get_name(zx_device_t* device) { return nullptr; }

__EXPORT
zx_off_t device_get_size(zx_device_t* device) { return 0; }

__EXPORT
zx_status_t device_get_metadata(zx_device_t* device, uint32_t type, void* buf, size_t buflen,
                                size_t* actual) {
  return ZX_ERR_NOT_SUPPORTED;
}

__EXPORT
zx_status_t device_get_metadata_size(zx_device_t* device, uint32_t type, size_t* out_size) {
  return ZX_ERR_NOT_SUPPORTED;
}

__EXPORT zx_status_t device_get_fragment_protocol(zx_device_t* device, const char* name,
                                                  uint32_t proto_id, void* protocol) {
  return ZX_ERR_NOT_SUPPORTED;
}

__EXPORT
zx_status_t device_get_fragment_metadata(zx_device_t* device, const char* name, uint32_t type,
                                         void* buf, size_t buflen, size_t* actual) {
  return ZX_ERR_NOT_SUPPORTED;
}

__EXPORT
void device_state_clr_set(zx_device_t* dev, zx_signals_t clearflag, zx_signals_t setflag) {
  // This is currently a no-op.
}

__EXPORT
zx_status_t device_get_profile(zx_device_t* device, uint32_t priority, const char* name,
                               zx_handle_t* out_profile) {
  // This is currently a no-op.
  *out_profile = ZX_HANDLE_INVALID;
  return ZX_OK;
}

__EXPORT
zx_status_t device_get_deadline_profile(zx_device_t* device, uint64_t capacity, uint64_t deadline,
                                        uint64_t period, const char* name,
                                        zx_handle_t* out_profile) {
  // This is currently a no-op.
  *out_profile = ZX_HANDLE_INVALID;
  return ZX_OK;
}

__EXPORT
zx_status_t device_set_profile_by_role(zx_device_t* device, zx_handle_t thread, const char* role,
                                       size_t role_size) {
  // This is currently a no-op.
  return ZX_OK;
}

__EXPORT __WEAK zx_status_t load_firmware_from_driver(zx_driver_t* drv, zx_device_t* dev,
                                                      const char* path, zx_handle_t* fw,
                                                      size_t* size) {
  // This is currently a no-op.
  *fw = ZX_HANDLE_INVALID;
  *size = no_ddk::kFakeFWSize;
  return ZX_OK;
}

__EXPORT
zx_status_t device_rebind(zx_device_t* device) { return ZX_OK; }

__EXPORT uint32_t device_get_fragment_count(zx_device_t* dev) { return 0; }

__EXPORT void device_get_fragments(zx_device_t* dev, composite_device_fragment_t* comp_list,
                                   size_t comp_count, size_t* comp_actual) {}

__EXPORT
void device_fidl_transaction_take_ownership(fidl_txn_t* txn, device_fidl_txn_t* new_txn) {}

// Please do not use get_root_resource() in new code. See ZX-1467.
__EXPORT
zx_handle_t get_root_resource() { return ZX_HANDLE_INVALID; }

extern "C" bool driver_log_severity_enabled_internal(const zx_driver_t* drv,
                                                     fx_log_severity_t flag) {
  return flag >= no_ddk::kMinLogSeverity;
}

extern "C" void driver_logvf_internal(const zx_driver_t* drv, fx_log_severity_t flag,
                                      const char* file, int line, const char* msg, va_list args) {
  vfprintf(stdout, msg, args);
  putchar('\n');
}

extern "C" void driver_logf_internal(const zx_driver_t* drv, fx_log_severity_t flag,
                                     const char* file, int line, const char* msg, ...) {
  va_list args;
  va_start(args, msg);
  driver_logvf_internal(drv, flag, file, line, msg, args);
  va_end(args);
}

__EXPORT
__WEAK zx_driver_rec __zircon_driver_rec__ = {
    .ops = {},
    .driver = {},
    .log_flags = 0,
};
