// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_GLUE_DATA_PIPE_DATA_PIPE_DRAINER_CLIENT_H_
#define APPS_LEDGER_SRC_GLUE_DATA_PIPE_DATA_PIPE_DRAINER_CLIENT_H_

#include <functional>
#include <memory>
#include <string>

#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/data_pipe/data_pipe_drainer.h"

namespace glue {

class DataPipeDrainerClient : public mtl::DataPipeDrainer::Client {
 public:
  DataPipeDrainerClient();

  ~DataPipeDrainerClient() override;

  void Start(mx::datapipe_consumer source,
             const std::function<void(std::string)>& callback);

  void set_on_empty(ftl::Closure&& on_empty_callback) {
    on_empty_callback_ = std::move(on_empty_callback);
  }

 private:
  void OnDataAvailable(const void* data, size_t num_bytes) override;

  void OnDataComplete() override;

  std::function<void(std::string)> callback_;
  std::string data_;
  mtl::DataPipeDrainer drainer_;
  ftl::Closure on_empty_callback_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DataPipeDrainerClient);
};

}  // namespace glue

#endif  // APPS_LEDGER_SRC_GLUE_DATA_PIPE_DATA_PIPE_DRAINER_CLIENT_H_
