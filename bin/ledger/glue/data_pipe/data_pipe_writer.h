// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_GLUE_DATA_PIPE_DATA_PIPE_WRITER_H_
#define APPS_LEDGER_SRC_GLUE_DATA_PIPE_DATA_PIPE_WRITER_H_

#include <memory>
#include <string>

#include "lib/fidl/c/waiter/async_waiter.h"
#include "lib/fidl/cpp/waiter/default.h"
#include "lib/ftl/macros.h"
#include "mx/datapipe.h"

namespace glue {

// Deletes itself when the data pipe is closed or the write is completed.
class DataPipeWriter {
 public:
  DataPipeWriter(const FidlAsyncWaiter* waiter = fidl::GetDefaultAsyncWaiter());
  ~DataPipeWriter();

  void Start(const std::string& data, mx::datapipe_producer destination);

 private:
  void WriteData();
  void WaitForPipe();
  static void WaitComplete(mx_status_t result,
                           mx_signals_t pending,
                           void* context);
  void Done();

  void* buffer_ = nullptr;
  mx_size_t buffer_size_ = 0u;
  std::string data_;
  // Position of the next byte in |data_| to be written.
  size_t offset_ = 0u;
  mx::datapipe_producer destination_;
  const FidlAsyncWaiter* waiter_;
  FidlAsyncWaitID wait_id_ = 0;

  FTL_DISALLOW_COPY_AND_ASSIGN(DataPipeWriter);
};

}  // namespace glue

#endif  // APPS_LEDGER_SRC_GLUE_DATA_PIPE_DATA_PIPE_WRITER_H_
