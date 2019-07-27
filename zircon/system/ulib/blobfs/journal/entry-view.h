// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_JOURNAL_ENTRY_VIEW_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_JOURNAL_ENTRY_VIEW_H_

#include <zircon/types.h>

#include <blobfs/block-buffer-view.h>
#include <blobfs/format.h>
#include <blobfs/journal/superblock.h>
#include <blobfs/operation.h>
#include <fbl/macros.h>
#include <fbl/vector.h>

namespace blobfs {

// A view into the filesystem journal entry, including the header and footer.
//
// This class does not have ownership over the underlying buffer, instead, it merely
// provides a basic mechanism to parse a view of the buffer which is owned elsewhere.
class JournalEntryView {
 public:
  // Creates a new entry view without modification.
  explicit JournalEntryView(BlockBufferView view);

  // Creates a new entry view which encodes the operations into the view
  // on construction.
  //
  // Asserts that |operations| is exactly the size of the journal entry.
  JournalEntryView(BlockBufferView view, const fbl::Vector<BufferedOperation>& operations,
                   uint64_t sequence_number);

  const JournalHeaderBlock* header() const {
    return reinterpret_cast<const JournalHeaderBlock*>(view_.Data(0));
  }

  const JournalCommitBlock* footer() const {
    return reinterpret_cast<const JournalCommitBlock*>(
        view_.Data(view_.length() - kJournalEntryCommitBlocks));
  }

  // Iterates through all blocks in the previously set entry, and resets all escaped blocks
  // within the constructor-provided buffer.
  void DecodePayloadBlocks();

  // Calculates the checksum of all blocks excluding the commit block.
  uint32_t CalculateChecksum() const;

 private:
  // Sets all fields of the journal entry.
  //
  // May modify the contents of the payload to "escape" blocks with a prefix
  // that matches |kJournalEntryMagic|.
  //
  // Asserts that |operations| is exactly the size of the journal entry.
  void Encode(const fbl::Vector<BufferedOperation>& operations, uint64_t sequence_number);

  JournalHeaderBlock* header() { return reinterpret_cast<JournalHeaderBlock*>(view_.Data(0)); }

  JournalCommitBlock* footer() {
    return reinterpret_cast<JournalCommitBlock*>(
        view_.Data(view_.length() - kJournalEntryCommitBlocks));
  }

  BlockBufferView view_;
};

}  // namespace blobfs

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_JOURNAL_ENTRY_VIEW_H_
