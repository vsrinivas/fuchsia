// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/glue/data_pipe/data_pipe_drainer_client.h"

#include <utility>

namespace glue {

DataPipeDrainerClient::DataPipeDrainerClient() : drainer_(this) {}

DataPipeDrainerClient::~DataPipeDrainerClient() {}

void DataPipeDrainerClient::Start(
    mojo::ScopedDataPipeConsumerHandle source,
    const std::function<void(std::string)>& callback) {
  callback_ = callback;
  drainer_.Start(std::move(source));
}

void DataPipeDrainerClient::OnDataAvailable(const void* data,
                                            size_t num_bytes) {
  data_.append(static_cast<const char*>(data), num_bytes);
}

void DataPipeDrainerClient::OnDataComplete() {
  callback_(data_);
}

}  // namespace glue
