// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_BOOTSVC_UTIL_H_
#define SRC_BRINGUP_BIN_BOOTSVC_UTIL_H_

#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "src/lib/storage/vfs/cpp/fuchsia_vfs.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"

namespace bootsvc {

// Identifier of a boot item.
struct ItemKey {
  uint32_t type;
  uint32_t extra;

  bool operator<(const ItemKey& ik) const {
    uint64_t a = (static_cast<uint64_t>(type) << 32) | extra;
    uint64_t b = (static_cast<uint64_t>(ik.type) << 32) | ik.extra;
    return a < b;
  }
};

// Location of a boot item within a boot image.
struct ItemValue {
  uint32_t offset;
  uint32_t length;
};

// Map for boot items.
using ItemMap = std::map<ItemKey, std::vector<ItemValue>>;
using BootloaderFileMap = std::map<std::string, ItemValue>;

// Retrieve boot image |vmo| from the startup handle table, and add boot items
// to |out_map|.
zx_status_t RetrieveBootImage(zx::vmo vmo, zx::vmo* out_vmo, ItemMap* out_map,
                              BootloaderFileMap* out_bootloader_file_map);

// Parses ' '-separated boot arguments to try and find kBootsvcNextArg.
std::optional<std::string> GetBootsvcNext(std::string_view str);

// Create a connection to a |vnode| in a |vfs|.
zx_status_t CreateVnodeConnection(fs::FuchsiaVfs* vfs, fbl::RefPtr<fs::Vnode> vnode,
                                  fs::Rights rights, zx::channel* out);

// Path relative to /boot used for crashlogs.
extern const char* const kLastPanicFilePath;

std::vector<std::string> SplitString(std::string_view input, char delimiter);

}  // namespace bootsvc

#endif  // SRC_BRINGUP_BIN_BOOTSVC_UTIL_H_
