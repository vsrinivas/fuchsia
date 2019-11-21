// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/file_index.h"

#include "src/ledger/bin/storage/impl/object_identifier_encoding.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {

bool FileIndexSerialization::CheckValidFileIndexSerialization(absl::string_view data) {
  flatbuffers::Verifier verifier(reinterpret_cast<const unsigned char*>(data.data()), data.size());
  return VerifyFileIndexBuffer(verifier);
}

Status FileIndexSerialization::ParseFileIndex(absl::string_view content,
                                              const FileIndex** file_index) {
  if (!CheckValidFileIndexSerialization(content)) {
    return Status::DATA_INTEGRITY_ERROR;
  }
  *file_index = GetFileIndex(content.data());
  return Status::OK;
}

void FileIndexSerialization::BuildFileIndex(const std::vector<ObjectIdentifierAndSize>& children,
                                            std::unique_ptr<DataSource::DataChunk>* output,
                                            size_t* total_size) {
  auto builder = std::make_unique<flatbuffers::FlatBufferBuilder>();
  size_t local_total_size = 0u;

  std::vector<flatbuffers::Offset<ObjectChild>> object_children;
  for (const auto& identifier_and_size : children) {
    local_total_size += identifier_and_size.size;
    object_children.push_back(CreateObjectChild(
        *builder, identifier_and_size.size,
        ToObjectIdentifierStorage(builder.get(), identifier_and_size.identifier)));
  }
  FinishFileIndexBuffer(*builder, CreateFileIndex(*builder, local_total_size,
                                                  builder->CreateVector(object_children.data(),
                                                                        object_children.size())));

  *output = DataSource::DataChunk::Create(std::move(builder));
  *total_size = local_total_size;
}

}  // namespace storage
