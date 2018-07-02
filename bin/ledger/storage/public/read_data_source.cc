// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/public/read_data_source.h"

#include <lib/fit/function.h>


namespace storage {

void ReadDataSource(
    callback::ManagedContainer* managed_container,
    std::unique_ptr<DataSource> data_source,
    fit::function<void(Status, std::unique_ptr<DataSource::DataChunk>)>
        callback) {
  auto managed_data_source = managed_container->Manage(std::move(data_source));
  auto chunks = std::vector<std::unique_ptr<DataSource::DataChunk>>();
  (*managed_data_source)
      ->Get([managed_data_source = std::move(managed_data_source),
             chunks = std::move(chunks), callback = std::move(callback)](
                std::unique_ptr<DataSource::DataChunk> chunk,
                DataSource::Status status) mutable {
        if (status == DataSource::Status::ERROR) {
          callback(Status::INTERNAL_IO_ERROR, nullptr);
          return;
        }

        if (chunk) {
          chunks.push_back(std::move(chunk));
        }

        if (status == DataSource::Status::TO_BE_CONTINUED) {
          return;
        }

        FXL_DCHECK(status == DataSource::Status::DONE);

        if (chunks.empty()) {
          callback(Status::OK, DataSource::DataChunk::Create(""));
          return;
        }

        if (chunks.size() == 1) {
          callback(Status::OK, std::move(chunks.front()));
          return;
        }
        size_t final_size = 0;
        for (const auto& chunk : chunks) {
          final_size += chunk->Get().size();
        }
        std::string final_content;
        final_content.reserve(final_size);
        for (const auto& chunk : chunks) {
          final_content.append(chunk->Get().data(), chunk->Get().size());
        }
        callback(Status::OK,
                 DataSource::DataChunk::Create(std::move(final_content)));
      });
}

}  // namespace storage
