// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_BUILTIN_DEVICES_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_BUILTIN_DEVICES_H_

#include <lib/async/dispatcher.h>

#include <fbl/ref_ptr.h>

#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "src/lib/storage/vfs/cpp/vnode.h"

constexpr char kNullDevName[] = "null";
constexpr char kZeroDevName[] = "zero";

class BuiltinDevVnode : public fs::Vnode {
 public:
  explicit BuiltinDevVnode(bool null) : null_(null) {}

  zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual) override;

  zx_status_t Write(const void* data, size_t len, size_t off, size_t* out_actual) override;

  zx_status_t Truncate(size_t len) override;

  zx_status_t GetAttributes(fs::VnodeAttributes* a) override;

  fs::VnodeProtocolSet GetProtocols() const override;
  zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights rights,
                                     fs::VnodeRepresentation* info) override;

 private:
  const bool null_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_BUILTIN_DEVICES_H_
