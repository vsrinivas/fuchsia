// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_CPP_DEVICE_PATH_H_
#define SRC_FIRMWARE_GIGABOOT_CPP_DEVICE_PATH_H_

#include <optional>

#include <efi/protocol/device-path.h>

namespace gigaboot {

// A helper class for efi_device_path_protocol path node related operations.
class EfiDevicePathNode {
 public:
  explicit EfiDevicePathNode(const efi_device_path_protocol *node);

  uint8_t type() const { return node_->Type; }
  size_t length() const { return node_->Length[0] | (node_->Length[1] << 8); }

  // Get the next path node. Return std::nullopt if current node is a device path end node. Thus
  // it doesn't support multi-instance device path.
  // TODO(b/): Add support for multi-instance device path.
  std::optional<EfiDevicePathNode> Next() const;

  bool operator==(const EfiDevicePathNode &rhs) const;

  // Check if the device path starts with another device path.
  static bool StartsWith(const efi_device_path_protocol *path,
                         const efi_device_path_protocol *prefix);

 private:
  const efi_device_path_protocol *node_ = nullptr;
};
}  // namespace gigaboot

#endif  // SRC_FIRMWARE_GIGABOOT_CPP_DEVICE_PATH_H_
