// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_FILE_INDEX_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_FILE_INDEX_H_

#include "src/ledger/bin/storage/impl/file_index_generated.h"
#include "src/ledger/bin/storage/public/data_source.h"
#include "src/ledger/bin/storage/public/types.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

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
  static bool CheckValidFileIndexSerialization(absl::string_view data);

  // Parses a |FileIndex| from |content|.
  static Status ParseFileIndex(absl::string_view content, const FileIndex** file_index);

  // Builds the |FileIndex| representing the given children.
  static void BuildFileIndex(const std::vector<ObjectIdentifierAndSize>& children,
                             std::unique_ptr<DataSource::DataChunk>* output, size_t* total_size);

 private:
  FileIndexSerialization() = default;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_FILE_INDEX_H_
