// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include "apps/cobalt_client/src/hello.grpc.pb.h"
#include "apps/cobalt_client/src/hello.pb.h"
#include "grpc++/grpc++.h"
#include "gtest/gtest.h"

namespace cobalt {
namespace fuchsia {

class HelloImpl final : public Hello::Service {
  grpc::Status SayHello(grpc::ServerContext* context, const Person* request,
                        HelloResponse* response) override {
    response->set_greeting(std::string("Hello ") + request->name());
    return grpc::Status::OK;
  }
};

TEST(HelloProto, Hello) {
  Person person;
  person.set_name("Fred");
  HelloImpl impl;
  Hello::Service* service = &impl;
  grpc::ServerContext context;
  HelloResponse response;
  auto status = service->SayHello(&context, &person, &response);
  ASSERT_TRUE(status.ok());
  EXPECT_EQ("Hello Fred", response.greeting());
}

}  // namespace fuchsia
}  // namespace cobalt
