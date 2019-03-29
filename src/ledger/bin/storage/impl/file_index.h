// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_FILE_INDEX_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_FILE_INDEX_H_

#include <src/lib/fxl/strings/string_view.h>

#include "src/ledger/bin/storage/impl/file_index_generated.h"
#include "src/ledger/bin/storage/public/data_source.h"
#include "src/ledger/bin/storage/public/types.h"

namespace storage {

// Wrappers over flatbuffer serialization of FileIndex that ensures additional
// validation.
class FileIndexSerialization {
 public:
  struct ObjectIdentifierAndSize {
    ObjectIdentifier identifier;
    uint64_t size;
  };

  // Checks that |data| is a correct encoding for a |FileIndex|.
  static bool CheckValidFileIndexSerialization(fxl::StringView data);

  // Parses a |FileIndex| from |content|.
  static Status ParseFileIndex(fxl::StringView content,
                               const FileIndex** file_index);

  // Builds the |FileIndex| representing the given children.
  static void BuildFileIndex(
      const std::vector<ObjectIdentifierAndSize>& children,
      std::unique_ptr<DataSource::DataChunk>* output, size_t* total_size);

 private:
  FileIndexSerialization() {}
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_FILE_INDEX_H_
