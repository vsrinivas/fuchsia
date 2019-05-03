// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <string_view>

#include <fbl/string.h>
#include <fbl/vector.h>
#include <fs/vfs.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>

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
using ItemMap = std::map<ItemKey, ItemValue>;

// Retrieve boot image |vmo| from the startup handle table, and add boot items
// to |map|.
zx_status_t RetrieveBootImage(zx::vmo* out_vmo, ItemMap* out_map);

// Parse boot arguments in |str|, and add them to |buf|. |buf| is a series of
// null-separated "key" or "key=value" pairs.
zx_status_t ParseBootArgs(std::string_view str, fbl::Vector<char>* buf);

// Create a connection to a |vnode| in a |vfs|.
zx_status_t CreateVnodeConnection(fs::Vfs* vfs, fbl::RefPtr<fs::Vnode> vnode, zx::channel* out);

// Path relative to /boot used for crashlogs.
extern const char* const kLastPanicFilePath;

fbl::Vector<fbl::String> SplitString(fbl::String input, char delimiter);

} // namespace bootsvc
