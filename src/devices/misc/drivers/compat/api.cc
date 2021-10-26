// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>

#include "src/devices/misc/drivers/compat/device.h"
#include "src/devices/misc/drivers/compat/driver.h"

extern "C" {
__EXPORT zx_status_t device_add_from_driver(zx_driver_t* drv, zx_device_t* parent,
                                            device_add_args_t* args, zx_device_t** out) {
  return parent->Add(args, out);
}

__EXPORT void device_init_reply(zx_device_t* dev, zx_status_t status,
                                const device_init_reply_args_t* args) {}

__EXPORT zx_status_t device_rebind(zx_device_t* dev) { return ZX_ERR_NOT_SUPPORTED; }

__EXPORT void device_async_remove(zx_device_t* dev) { dev->Remove(); }

__EXPORT void device_unbind_reply(zx_device_t* dev) {}

__EXPORT void device_suspend_reply(zx_device_t* dev, zx_status_t status, uint8_t out_state) {}

__EXPORT void device_resume_reply(zx_device_t* dev, zx_status_t status, uint8_t out_power_state,
                                  uint32_t out_perf_state) {}

__EXPORT zx_status_t device_get_profile(zx_device_t* dev, uint32_t priority, const char* name,
                                        zx_handle_t* out_profile) {
  return ZX_ERR_NOT_SUPPORTED;
}

__EXPORT zx_status_t device_get_deadline_profile(zx_device_t* device, uint64_t capacity,
                                                 uint64_t deadline, uint64_t period,
                                                 const char* name, zx_handle_t* out_profile) {
  return ZX_ERR_NOT_SUPPORTED;
}

__EXPORT zx_status_t device_set_profile_by_role(zx_device_t* device, zx_handle_t thread,
                                                const char* role, size_t role_size) {
  return ZX_ERR_NOT_SUPPORTED;
}

__EXPORT const char* device_get_name(zx_device_t* dev) { return dev->Name(); }

__EXPORT zx_status_t device_get_protocol(const zx_device_t* dev, uint32_t proto_id, void* out) {
  return dev->GetProtocol(proto_id, out);
}

__EXPORT zx_status_t device_open_protocol_session_multibindable(zx_device_t* dev, uint32_t proto_id,
                                                                void* out) {
  return ZX_ERR_NOT_SUPPORTED;
}

__EXPORT zx_status_t device_close_protocol_session_multibindable(zx_device_t* dev, void* proto) {
  return ZX_ERR_NOT_SUPPORTED;
}

__EXPORT zx_off_t device_get_size(zx_device_t* dev) { return 0; }

// LibDriver Misc Interfaces

__EXPORT zx_handle_t get_root_resource() { return kRootResource.get(); }

__EXPORT zx_status_t load_firmware_from_driver(zx_driver_t* drv, zx_device_t* dev, const char* path,
                                               zx_handle_t* fw, size_t* size) {
  return ZX_ERR_NOT_SUPPORTED;
}

__EXPORT void load_firmware_async_from_driver(zx_driver_t* drv, zx_device_t* dev, const char* path,
                                              load_firmware_callback_t callback, void* ctx) {}

__EXPORT zx_status_t device_get_metadata(zx_device_t* dev, uint32_t type, void* buf, size_t buflen,
                                         size_t* actual) {
  return dev->GetMetadata(type, buf, buflen, actual);
}

__EXPORT zx_status_t device_get_metadata_size(zx_device_t* dev, uint32_t type, size_t* out_size) {
  return dev->GetMetadataSize(type, out_size);
}

__EXPORT zx_status_t device_add_metadata(zx_device_t* dev, uint32_t type, const void* data,
                                         size_t size) {
  return dev->AddMetadata(type, data, size);
}

__EXPORT zx_status_t device_publish_metadata(zx_device_t* dev, const char* path, uint32_t type,
                                             const void* data, size_t size) {
  return ZX_ERR_NOT_SUPPORTED;
}

__EXPORT zx_status_t device_add_composite(zx_device_t* dev, const char* name,
                                          const composite_device_desc_t* comp_desc) {
  return ZX_ERR_NOT_SUPPORTED;
}

__EXPORT bool driver_log_severity_enabled_internal(const zx_driver_t* drv,
                                                   fx_log_severity_t severity) {
  return severity >= FX_LOG_SEVERITY_DEFAULT;
}

__EXPORT void driver_logvf_internal(const zx_driver_t* drv, fx_log_severity_t severity,
                                    const char* tag, const char* file, int line, const char* msg,
                                    va_list args) {
  const_cast<zx_driver_t*>(drv)->Log(static_cast<FuchsiaLogSeverity>(severity), tag, file, line,
                                     msg, args);
}

__EXPORT void driver_logf_internal(const zx_driver_t* drv, fx_log_severity_t severity,
                                   const char* tag, const char* file, int line, const char* msg,
                                   ...) {
  va_list args;
  va_start(args, msg);
  const_cast<zx_driver_t*>(drv)->Log(static_cast<FuchsiaLogSeverity>(severity), tag, file, line,
                                     msg, args);
  va_end(args);
}

__EXPORT void device_fidl_transaction_take_ownership(fidl_txn_t* txn, device_fidl_txn_t* new_txn) {}

__EXPORT uint32_t device_get_fragment_count(zx_device_t* dev) { return 0; }

__EXPORT void device_get_fragments(zx_device_t* dev, composite_device_fragment_t* comp_list,
                                   size_t comp_count, size_t* comp_actual) {}

__EXPORT zx_status_t device_get_fragment_protocol(zx_device_t* dev, const char* name,
                                                  uint32_t proto_id, void* out) {
  return ZX_ERR_NOT_SUPPORTED;
}

__EXPORT zx_status_t device_get_fragment_metadata(zx_device_t* dev, const char* name, uint32_t type,
                                                  void* buf, size_t buflen, size_t* actual) {
  return ZX_ERR_NOT_SUPPORTED;
}

__EXPORT zx_status_t device_get_variable(zx_device_t* device, const char* name, char* out,
                                         size_t out_size, size_t* size_actual) {
  return ZX_ERR_NOT_SUPPORTED;
}
}
