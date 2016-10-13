// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_GLUE_DATA_PIPE_DATA_PIPE_DRAINER_CLIENT_H_
#define APPS_LEDGER_GLUE_DATA_PIPE_DATA_PIPE_DRAINER_CLIENT_H_

#include <functional>
#include <memory>
#include <string>

#include "lib/ftl/macros.h"
#include "lib/mtl/data_pipe/data_pipe_drainer.h"

namespace glue {

class DataPipeDrainerClient : public mtl::DataPipeDrainer::Client {
 public:
  DataPipeDrainerClient();

  ~DataPipeDrainerClient() override;

  void Start(mojo::ScopedDataPipeConsumerHandle source,
             const std::function<void(std::string)>& callback);

 private:
  void OnDataAvailable(const void* data, size_t num_bytes) override;

  void OnDataComplete() override;

  std::function<void(std::string)> callback_;
  std::string data_;
  mtl::DataPipeDrainer drainer_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DataPipeDrainerClient);
};

}  // namespace glue

#endif  // APPS_LEDGER_GLUE_DATA_PIPE_DATA_PIPE_DRAINER_CLIENT_H_
