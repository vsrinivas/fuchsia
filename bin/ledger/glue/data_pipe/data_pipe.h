// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_GLUE_DATA_PIPE_DATA_PIPE_H_
#define APPS_LEDGER_SRC_GLUE_DATA_PIPE_DATA_PIPE_H_

#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "mx/datapipe.h"

namespace glue {

// DataPipe produce and cosnumer factory
class DataPipe {
 public:
  DataPipe();
  explicit DataPipe(size_t size);
  ~DataPipe();

  mx::datapipe_producer producer_handle;
  mx::datapipe_consumer consumer_handle;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(DataPipe);
};

inline DataPipe::DataPipe() {
  mx::datapipe_producer::create(1u, 0u, 0u, &producer_handle, &consumer_handle);
}

inline DataPipe::DataPipe(size_t size) {
  mx::datapipe_producer::create(1u, size, 0u, &producer_handle,
                                &consumer_handle);
}

inline DataPipe::~DataPipe() {}

}  // namespace glue

#endif  // APPS_LEDGER_SRC_GLUE_DATA_PIPE_DATA_PIPE_H_
