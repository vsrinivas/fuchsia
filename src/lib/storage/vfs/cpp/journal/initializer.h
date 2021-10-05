// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_JOURNAL_INITIALIZER_H_
#define SRC_LIB_STORAGE_VFS_CPP_JOURNAL_INITIALIZER_H_

#include <inttypes.h>
#include <lib/stdcompat/span.h>
#include <zircon/types.h>

#include <functional>

namespace fs {

// Writes |block_count| blocks worth of data in |buffer| at |block_offset| to backing data
// store.
using WriteBlocksFn = std::function<zx_status_t(cpp20::span<const uint8_t> buffer,
                                                uint64_t block_offset, uint64_t block_count)>;

// Makes a journal that fits in |journal_blocks| by writing journal metadata using user supplied
// write function, |WriteBlocks|. MakeJournal is called from host and from target while creating
// different FSes. There isn't a common writer trait among the users to write to the backing data
// store. WriteBlockFn is a work-around until then.
zx_status_t MakeJournal(uint64_t journal_blocks, const WriteBlocksFn& WriteBlocks);

}  // namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_JOURNAL_INITIALIZER_H_
