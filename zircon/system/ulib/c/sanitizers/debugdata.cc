// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/debugdata/c/fidl.h>
#include <lib/fidl/txn_header.h>
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

#include <iterator>
#include <vector>

#include "fuchsia-io-constants.h"

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
  fidl_init_txn_header(&request->hdr, 0, fuchsia_io_DirectoryOpenOrdinal, 0);
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
_fuchsia_debugdata_PublisherPublish(zx_handle_t debug_data_channel, const char* data_sink_data,
                                    size_t data_sink_size, zx_handle_t data,
                                    zx_handle_t vmo_token) {
  if (data_sink_size > fuchsia_io_MAX_NAME_LENGTH) {
    _zx_handle_close(data);
    return ZX_ERR_INVALID_ARGS;
  }
  FIDL_ALIGNDECL char wr_bytes[sizeof(fuchsia_debugdata_PublisherPublishRequestMessage) +
                               fuchsia_io_MAX_NAME_LENGTH] = {};
  fuchsia_debugdata_PublisherPublishRequestMessage* request =
      (fuchsia_debugdata_PublisherPublishRequestMessage*)wr_bytes;
  fidl_init_txn_header(&request->hdr, 0, fuchsia_debugdata_PublisherPublishOrdinal, 0);
  request->data_sink.data = (char*)FIDL_ALLOC_PRESENT;
  request->data_sink.size = data_sink_size;
  request->data = FIDL_HANDLE_PRESENT;
  request->vmo_token = FIDL_HANDLE_PRESENT;

  zx_handle_t handles[2] = {data, vmo_token};

  memcpy(&wr_bytes[sizeof(*request)], data_sink_data, data_sink_size);
  return _zx_channel_write(
      debug_data_channel, 0u, wr_bytes,
      static_cast<uint32_t>(sizeof(fuchsia_debugdata_PublisherPublishRequestMessage) +
                            FIDL_ALIGN(data_sink_size)),
      handles, std::size(handles));
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
      __zircon_namespace_svc,
      fuchsia_io_OpenFlags_RIGHT_READABLE | fuchsia_io_OpenFlags_RIGHT_WRITABLE, 0,
      fuchsia_debugdata_Publisher_Name, strlen(fuchsia_debugdata_Publisher_Name), h0);
  if (status != ZX_OK) {
    constexpr const char kErrorDirectoryOpen[] = "Failed to open service namespace";
    __sanitizer_log_write(kErrorDirectoryOpen, sizeof(kErrorDirectoryOpen) - 1);
    _zx_handle_close(h1);
    return ZX_HANDLE_INVALID;
  }

  return h1;
}

}  // namespace

// Publish VMO and return back event pair handle which controls the lifetime of
// the VMO.
__EXPORT
zx_handle_t __sanitizer_publish_data(const char* sink_name, zx_handle_t vmo) {
  if (__zircon_namespace_svc == ZX_HANDLE_INVALID) {
    _zx_handle_close(vmo);
    return ZX_HANDLE_INVALID;
  }

  zx_handle_t vmo_token_client, vmo_token_server;
  if (_zx_eventpair_create(0, &vmo_token_client, &vmo_token_server) != ZX_OK) {
    constexpr char kErrorEventPairCreate[] = "Failed to create eventpair for debugdata VMO token";
    __sanitizer_log_write(kErrorEventPairCreate, sizeof(kErrorEventPairCreate) - 1);
    return ZX_HANDLE_INVALID;
  }

  zx_handle_t debugdata_channel = sanitizer_debugdata_connect();
  zx_status_t status = _fuchsia_debugdata_PublisherPublish(
      debugdata_channel, sink_name, strlen(sink_name), vmo, vmo_token_server);
  _zx_handle_close(debugdata_channel);

  if (status != ZX_OK) {
    constexpr const char kErrorPublish[] = "Failed to publish data";
    __sanitizer_log_write(kErrorPublish, sizeof(kErrorPublish) - 1);
    _zx_handle_close(vmo_token_client);
    return ZX_HANDLE_INVALID;
  }

  return vmo_token_client;
}
