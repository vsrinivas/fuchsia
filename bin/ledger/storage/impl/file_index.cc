// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/file_index.h"

namespace storage {

bool FileIndexSerialization::CheckValidFileIndexSerialization(
    fxl::StringView data) {
  flatbuffers::Verifier verifier(
      reinterpret_cast<const unsigned char*>(data.data()), data.size());
  return VerifyFileIndexBuffer(verifier);
}

Status FileIndexSerialization::ParseFileIndex(fxl::StringView content,
                                              const FileIndex** file_index) {
  if (!CheckValidFileIndexSerialization(content)) {
    return Status::FORMAT_ERROR;
  }
  *file_index = GetFileIndex(content.data());
  return Status::OK;
}

void FileIndexSerialization::BuildFileIndex(
    const std::vector<ObjectIdAndSize>& children,
    std::unique_ptr<DataSource::DataChunk>* output,
    size_t* total_size) {
  auto builder = std::make_unique<flatbuffers::FlatBufferBuilder>();
  size_t local_total_size = 0u;

  std::vector<flatbuffers::Offset<ObjectChild>> object_children;
  for (const auto& id : children) {
    local_total_size += id.size;
    object_children.push_back(CreateObjectChild(
        *builder, id.size, convert::ToFlatBufferVector(builder.get(), id.id)));
  }
  FinishFileIndexBuffer(
      *builder, CreateFileIndex(*builder, local_total_size,
                                builder->CreateVector(object_children.data(),
                                                      object_children.size())));

  *output = DataSource::DataChunk::Create(std::move(builder));
  *total_size = local_total_size;
}

}  // namespace storage
