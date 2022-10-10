// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/builtin_devices.h"

#include "src/lib/storage/vfs/cpp/vfs_types.h"

zx_status_t BuiltinDevVnode::Read(void* data, size_t len, size_t off, size_t* out_actual) {
  // /dev/null implementation.
  if (null_) {
    *out_actual = 0;
    return ZX_OK;
  }
  // /dev/zero implementation.
  memset(data, 0, len);
  *out_actual = len;
  return ZX_OK;
}

zx_status_t BuiltinDevVnode::Write(const void* data, size_t len, size_t off, size_t* out_actual) {
  if (null_) {
    *out_actual = len;
    return ZX_OK;
  }
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t BuiltinDevVnode::Truncate(size_t len) { return ZX_OK; }

zx_status_t BuiltinDevVnode::GetAttributes(fs::VnodeAttributes* a) {
  a->mode = V_TYPE_CDEV | V_IRUSR | V_IWUSR;
  a->content_size = 0;
  a->link_count = 1;
  return ZX_OK;
}

fs::VnodeProtocolSet BuiltinDevVnode::GetProtocols() const { return fs::VnodeProtocol::kFile; }

zx_status_t BuiltinDevVnode::GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights rights,
                                                    fs::VnodeRepresentation* info) {
  switch (protocol) {
    case fs::VnodeProtocol::kConnector:
    case fs::VnodeProtocol::kDirectory:
    case fs::VnodeProtocol::kTty:
      return ZX_ERR_NOT_SUPPORTED;
    case fs::VnodeProtocol::kFile:
      *info = fs::VnodeRepresentation::File{};
      return ZX_OK;
  }
}
