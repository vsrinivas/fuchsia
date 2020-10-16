// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/debugdata/c/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <zircon/fidl.h>
#include <zircon/process.h>
#include <zircon/sanitizer.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>

__attribute__((visibility("hidden"))) zx_handle_t __zircon_namespace_svc = ZX_HANDLE_INVALID;

namespace {

#if __has_feature(address_sanitizer)
[[clang::no_sanitize("address")]]
#endif
zx_status_t
_fuchsia_io_DirectoryOpen(zx_handle_t channel, uint32_t flags, uint32_t mode, const char* path_data,
                          size_t path_size, zx_handle_t object) {
  if (path_size > fuchsia_io_MAX_PATH) {
    _zx_handle_close(object);
    return ZX_ERR_INVALID_ARGS;
  }
  FIDL_ALIGNDECL char wr_bytes[sizeof(fuchsia_io_DirectoryOpenRequest) + fuchsia_io_MAX_PATH] = {};
  fuchsia_io_DirectoryOpenRequest* request = (fuchsia_io_DirectoryOpenRequest*)wr_bytes;
  // TODO(fxbug.dev/38643) use fidl_init_txn_header once it is inline
  memset(&request->hdr, 0, sizeof(request->hdr));
  request->hdr.magic_number = kFidlWireFormatMagicNumberInitial;
  request->hdr.ordinal = fuchsia_io_DirectoryOpenOrdinal;
  request->flags = flags;
  request->mode = mode;
  request->path.data = (char*)FIDL_ALLOC_PRESENT;
  request->path.size = path_size;
  request->object = FIDL_HANDLE_PRESENT;
  memcpy(&wr_bytes[sizeof(*request)], path_data, path_size);
  return _zx_channel_write(
      channel, 0u, wr_bytes,
      static_cast<uint32_t>(sizeof(fuchsia_io_DirectoryOpenRequest) + FIDL_ALIGN(path_size)),
      &object, 1);
}

#if __has_feature(address_sanitizer)
[[clang::no_sanitize("address")]]
#endif
zx_status_t
_fuchsia_debugdata_DebugDataPublish(zx_handle_t channel, const char* data_sink_data,
                                    size_t data_sink_size, zx_handle_t data) {
  if (data_sink_size > fuchsia_debugdata_MAX_NAME) {
    _zx_handle_close(data);
    return ZX_ERR_INVALID_ARGS;
  }
  FIDL_ALIGNDECL char
      wr_bytes[sizeof(fuchsia_debugdata_DebugDataPublishRequest) + fuchsia_debugdata_MAX_NAME] = {};
  fuchsia_debugdata_DebugDataPublishRequest* request =
      (fuchsia_debugdata_DebugDataPublishRequest*)wr_bytes;
  // TODO(fxbug.dev/38643) use fidl_init_txn_header once it is inline
  memset(&request->hdr, 0, sizeof(request->hdr));
  request->hdr.magic_number = kFidlWireFormatMagicNumberInitial;
  request->hdr.ordinal = fuchsia_debugdata_DebugDataPublishOrdinal;
  request->data_sink.data = (char*)FIDL_ALLOC_PRESENT;
  request->data_sink.size = data_sink_size;
  request->data = FIDL_HANDLE_PRESENT;
  memcpy(&wr_bytes[sizeof(*request)], data_sink_data, data_sink_size);
  return _zx_channel_write(channel, 0u, wr_bytes,
                           static_cast<uint32_t>(sizeof(fuchsia_debugdata_DebugDataPublishRequest) +
                                                 FIDL_ALIGN(data_sink_size)),
                           &data, 1);
}

#if __has_feature(address_sanitizer)
[[clang::no_sanitize("address")]]
#endif
zx_status_t
_fuchsia_debugdata_DebugDataLoadConfig(zx_handle_t channel, const char* config_name_data,
                                       size_t config_name_size, zx_handle_t* out_config) {
  if (config_name_size > fuchsia_debugdata_MAX_NAME) {
    return ZX_ERR_INVALID_ARGS;
  }
  FIDL_ALIGNDECL char wr_bytes[sizeof(fuchsia_debugdata_DebugDataLoadConfigRequest) +
                               fuchsia_debugdata_MAX_NAME] = {};
  fuchsia_debugdata_DebugDataLoadConfigRequest* request =
      (fuchsia_debugdata_DebugDataLoadConfigRequest*)wr_bytes;
  // TODO(fxbug.dev/38643) use fidl_init_txn_header once it is inline
  memset(&request->hdr, 0, sizeof(request->hdr));
  request->hdr.magic_number = kFidlWireFormatMagicNumberInitial;
  request->hdr.ordinal = fuchsia_debugdata_DebugDataLoadConfigOrdinal;
  request->config_name.data = (char*)FIDL_ALLOC_PRESENT;
  request->config_name.size = config_name_size;
  memcpy(&wr_bytes[sizeof(*request)], config_name_data, config_name_size);
  FIDL_ALIGNDECL char rd_bytes[sizeof(fuchsia_debugdata_DebugDataLoadConfigResponse)];
  zx_channel_call_args_t args = {
      .wr_bytes = wr_bytes,
      .wr_handles = nullptr,
      .rd_bytes = rd_bytes,
      .rd_handles = out_config,
      .wr_num_bytes = static_cast<uint32_t>(sizeof(fuchsia_debugdata_DebugDataLoadConfigRequest) +
                                            FIDL_ALIGN(config_name_size)),
      .wr_num_handles = 0,
      .rd_num_bytes = sizeof(fuchsia_debugdata_DebugDataLoadConfigResponse),
      .rd_num_handles = 1,
  };
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  zx_status_t status =
      zx_channel_call(channel, 0u, ZX_TIME_INFINITE, &args, &actual_bytes, &actual_handles);
  if (!actual_handles)
    *out_config = ZX_HANDLE_INVALID;
  return status;
}

zx_handle_t sanitizer_debugdata_connect() {
  zx_handle_t h0, h1;
  zx_status_t status;

  if ((status = _zx_channel_create(0, &h0, &h1)) != ZX_OK) {
    constexpr const char kErrorChannelCreate[] = "Failed to create channel for debugdata service";
    __sanitizer_log_write(kErrorChannelCreate, sizeof(kErrorChannelCreate) - 1);
    return ZX_HANDLE_INVALID;
  }

  status = _fuchsia_io_DirectoryOpen(
      __zircon_namespace_svc, fuchsia_io_OPEN_RIGHT_READABLE | fuchsia_io_OPEN_RIGHT_WRITABLE, 0,
      fuchsia_debugdata_DebugData_Name, strlen(fuchsia_debugdata_DebugData_Name), h0);
  if (status != ZX_OK) {
    constexpr const char kErrorDirectoryOpen[] = "Failed to open service namespace";
    __sanitizer_log_write(kErrorDirectoryOpen, sizeof(kErrorDirectoryOpen) - 1);
    return ZX_HANDLE_INVALID;
  }

  return h1;
}

}  // namespace

__EXPORT
void __sanitizer_publish_data(const char* sink_name, zx_handle_t vmo) {
  if (__zircon_namespace_svc == ZX_HANDLE_INVALID) {
    _zx_handle_close(vmo);
    return;
  }

  zx_handle_t h = sanitizer_debugdata_connect();

  // The handle is always consumed by the callee.
  if (_fuchsia_debugdata_DebugDataPublish(h, sink_name, strlen(sink_name), vmo) != ZX_OK) {
    constexpr const char kErrorPublish[] = "Failed to publish data";
    __sanitizer_log_write(kErrorPublish, sizeof(kErrorPublish) - 1);
  }
}

__EXPORT
zx_status_t __sanitizer_get_configuration(const char* name, zx_handle_t* out_vmo) {
  if (__zircon_namespace_svc == ZX_HANDLE_INVALID) {
    return ZX_ERR_BAD_HANDLE;
  }

  zx_handle_t h = sanitizer_debugdata_connect();

  zx_status_t status = _fuchsia_debugdata_DebugDataLoadConfig(h, name, strlen(name), out_vmo);
  if (status != ZX_OK) {
    constexpr const char kErrorLoadConfig[] = "Failed to get configuration file";
    __sanitizer_log_write(kErrorLoadConfig, sizeof(kErrorLoadConfig) - 1);
  }

  return status;
}
