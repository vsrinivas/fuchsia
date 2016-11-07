// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/glue/data_pipe/data_pipe_writer.h"

#include <utility>

#include "apps/ledger/src/glue/data_pipe//data_pipe_drainer_client.h"
#include "gtest/gtest.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"
#include "mojo/public/cpp/system/data_pipe.h"

namespace glue {
namespace {

TEST(DataPipeWriter, WriteAndRead) {
  mtl::MessageLoop message_loop;
  mojo::DataPipe data_pipe;
  DataPipeWriter* writer = new DataPipeWriter();
  writer->Start("bazinga\n", std::move(data_pipe.producer_handle));

  std::string value;
  std::unique_ptr<DataPipeDrainerClient> drainer(new DataPipeDrainerClient());
  drainer->Start(std::move(data_pipe.consumer_handle),
                 [&value, &message_loop](const std::string& v) {
                   value = v;
                   message_loop.QuitNow();
                 });
  message_loop.Run();

  EXPECT_EQ("bazinga\n", value);
}

TEST(DataPipeWriter, ClientClosedTheirEnd) {
  mtl::MessageLoop message_loop;
  mojo::DataPipe data_pipe;
  DataPipeWriter* writer = new DataPipeWriter();
  data_pipe.consumer_handle.reset();
  writer->Start("bazinga\n", std::move(data_pipe.producer_handle));
}

}  // namespace
}  // namespace glue
