// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START example]
#include <fidl/fuchsia.examples/cpp/fidl.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/syslog/cpp/macros.h>

#include <string>

#include <zxtest/zxtest.h>

class FuchsiaExampleTest : public zxtest::Test {
 public:
  ~FuchsiaExampleTest() override = default;
};

TEST(FuchsiaExampleTest, Echo) {
  zx::result client_end = component::Connect<fuchsia_examples::Echo>();

  EXPECT_TRUE(client_end.is_ok(), "Synchronous error when connecting to the |Echo| protocol");

  fidl::SyncClient client{std::move(*client_end)};
  fidl::Result result = client->EchoString({"hello"});
  EXPECT_TRUE(result.is_ok(), "EchoString failed: ");

  const std::string& reply_string = result->response();
  FX_LOGS(INFO) << "Got response: " << reply_string;
}

// [END example]
