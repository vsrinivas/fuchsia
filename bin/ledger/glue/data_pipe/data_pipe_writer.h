// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_GLUE_DATA_PIPE_DATA_PIPE_WRITER_H_
#define APPS_LEDGER_SRC_GLUE_DATA_PIPE_DATA_PIPE_WRITER_H_

#include <memory>
#include <string>

#include <mojo/environment/async_waiter.h>

#include "lib/ftl/macros.h"
#include "mojo/public/cpp/environment/environment.h"
#include "mojo/public/cpp/system/data_pipe.h"

namespace glue {

// Deletes itself when the data pipe is closed or the write is completed.
class DataPipeWriter {
 public:
  DataPipeWriter(const MojoAsyncWaiter* waiter =
                     mojo::Environment::GetDefaultAsyncWaiter());
  ~DataPipeWriter();

  void Start(const std::string& data,
             mojo::ScopedDataPipeProducerHandle destination);

 private:
  void WriteData();
  void WaitForPipe();
  static void WaitComplete(void* context, MojoResult result);
  void Done();

  void* buffer_ = nullptr;
  uint32_t buffer_size_ = 0u;
  std::string data_;
  // Position of the next byte in |data_| to be written.
  size_t offset_ = 0u;
  mojo::ScopedDataPipeProducerHandle destination_;
  const MojoAsyncWaiter* waiter_;
  MojoAsyncWaitID wait_id_ = 0;

  FTL_DISALLOW_COPY_AND_ASSIGN(DataPipeWriter);
};

}  // namespace glue

#endif  // APPS_LEDGER_SRC_GLUE_DATA_PIPE_DATA_PIPE_WRITER_H_
