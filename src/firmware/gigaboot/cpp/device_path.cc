// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_path.h"

#include <zircon/assert.h>

namespace gigaboot {

EfiDevicePathNode::EfiDevicePathNode(const efi_device_path_protocol *node) : node_(node) {
  ZX_ASSERT(node);
}

std::optional<EfiDevicePathNode> EfiDevicePathNode::Next() const {
  if (type() == DEVICE_PATH_END) {
    return std::nullopt;
  }

  // The length field includes both path data length + `efi_device_path_protocol` struct size.
  // (UEFI specification chapter 10).
  const uint8_t *start = reinterpret_cast<const uint8_t *>(node_);
  return EfiDevicePathNode(reinterpret_cast<const efi_device_path_protocol *>(start + length()));
}

bool EfiDevicePathNode::operator==(const EfiDevicePathNode &rhs) const {
  return length() == rhs.length() && memcmp(node_, rhs.node_, length()) == 0;
}

bool EfiDevicePathNode::StartsWith(const efi_device_path_protocol *path,
                                   const efi_device_path_protocol *prefix) {
  std::optional<EfiDevicePathNode> this_node = EfiDevicePathNode(path);
  std::optional<EfiDevicePathNode> prefix_node = EfiDevicePathNode(prefix);
  while (true) {
    if (prefix_node->type() == DEVICE_PATH_END) {
      return true;
    }

    if (!this_node || (!(*this_node == *prefix_node))) {
      return false;
    }

    this_node = this_node->Next();
    prefix_node = prefix_node->Next();
  }
}

}  // namespace gigaboot
