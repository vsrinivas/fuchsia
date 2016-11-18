// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "lib/mtl/data_pipe/data_pipe_drainer.h"
#include "lib/mtl/data_pipe/strings.h"
#include "lib/mtl/tasks/message_loop.h"
#include "mx/datapipe.h"

namespace mtl {
namespace {

class Client : public DataPipeDrainer::Client {
 public:
  Client(const std::function<void()>& available_callback,
         const std::function<void()>& completion_callback)
      : available_callback_(available_callback),
        completion_callback_(completion_callback) {}
  ~Client() override {}

  std::string GetValue() { return value_; }

 private:
  void OnDataAvailable(const void* data, size_t num_bytes) override {
    value_.append(static_cast<const char*>(data), num_bytes);
    available_callback_();
  }
  void OnDataComplete() override { completion_callback_(); }

  std::string value_;
  std::function<void()> available_callback_;
  std::function<void()> completion_callback_;
};

TEST(DataPipeDrainer, ReadData) {
  MessageLoop message_loop;
  Client client([] {}, [&message_loop] { message_loop.QuitNow(); });
  DataPipeDrainer drainer(&client);
  drainer.Start(mtl::WriteStringToConsumerHandle("Hello"));
  message_loop.Run();
  EXPECT_EQ("Hello", client.GetValue());
}

TEST(DataPipeDrainer, DeleteOnCallback) {
  MessageLoop message_loop;
  std::unique_ptr<DataPipeDrainer> drainer;
  Client client(
      [&message_loop, &drainer] {
        message_loop.PostQuitTask();
        drainer.reset();
      },
      [] {});
  drainer = std::make_unique<DataPipeDrainer>(&client);
  drainer->Start(mtl::WriteStringToConsumerHandle("H"));
  message_loop.Run();
  EXPECT_EQ("H", client.GetValue());
  EXPECT_EQ(nullptr, drainer.get());
}

}  // namespace
}  // namespace mtl
