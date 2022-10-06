// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl/cpp/contrib/fpromise/client.h"

#include <fidl/test.basic.protocol/cpp/fidl.h>
#include <fidl/test.error.methods/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/executor.h>
#include <lib/fpromise/promise.h>

#include <zxtest/zxtest.h>

namespace {

const std::string kExpectedReply = "7";

struct EchoServer : fidl::Server<test_basic_protocol::ValueEcho> {
  void Echo(EchoRequest& request, EchoCompleter::Sync& completer) override {
    completer.Reply(request.s());
  }
};

TEST(Client, Promisify) {
  auto server = std::make_unique<EchoServer>();
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  async::Executor executor(loop.dispatcher());
  zx::status endpoints = fidl::CreateEndpoints<test_basic_protocol::ValueEcho>();
  ASSERT_OK(endpoints.status_value());
  fidl::BindServer(loop.dispatcher(), std::move(endpoints->server), server.get());
  fidl::Client client(std::move(endpoints->client), loop.dispatcher());

  fpromise::promise<test_basic_protocol::ValueEchoEchoResponse, fidl::Error> p =
      fidl_fpromise::as_promise(client->Echo({kExpectedReply}));

  auto task = p.then(
      [&](fpromise::result<test_basic_protocol::ValueEchoEchoResponse, fidl::Error>& result) {
        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(kExpectedReply, result.value());
        loop.Quit();
      });

  executor.schedule_task(std::move(task));
  ASSERT_STATUS(ZX_ERR_CANCELED, loop.Run());
}

TEST(Client, PromisifyChaining) {
  auto server = std::make_unique<EchoServer>();
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  async::Executor executor(loop.dispatcher());
  zx::status endpoints = fidl::CreateEndpoints<test_basic_protocol::ValueEcho>();
  ASSERT_OK(endpoints.status_value());
  fidl::BindServer(loop.dispatcher(), std::move(endpoints->server), server.get());
  fidl::Client client(std::move(endpoints->client), loop.dispatcher());

  // Chain another continuation which operates on the FIDL result.
  auto p = fidl_fpromise::as_promise(client->Echo({kExpectedReply}))
               .and_then([&](const test_basic_protocol::ValueEchoEchoResponse& payload) {
                 return fpromise::ok(std::stoi(payload.s()));
               });

  // |p| is now transformed to a promise that resolves to a string.
  auto task = p.then([&](fpromise::result<int, fidl::Error>& result) {
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(7, result.value());
    loop.Quit();
  });

  executor.schedule_task(std::move(task));
  ASSERT_STATUS(ZX_ERR_CANCELED, loop.Run());
}

TEST(Client, PromisifyTransportError) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  async::Executor executor(loop.dispatcher());
  zx::status endpoints = fidl::CreateEndpoints<test_basic_protocol::ValueEcho>();
  ASSERT_OK(endpoints.status_value());
  fidl::Client client(std::move(endpoints->client), loop.dispatcher());

  fpromise::promise<test_basic_protocol::ValueEchoEchoResponse, fidl::Error> p =
      fidl_fpromise::as_promise(client->Echo({kExpectedReply}));
  endpoints->server.reset();

  auto task = p.then(
      [&](fpromise::result<test_basic_protocol::ValueEchoEchoResponse, fidl::Error>& result) {
        ASSERT_TRUE(result.is_error());
        ASSERT_TRUE(result.error().is_peer_closed());
        loop.Quit();
      });

  executor.schedule_task(std::move(task));
  ASSERT_STATUS(ZX_ERR_CANCELED, loop.Run());
}

class ErrorServer : public fidl::Server<test_error_methods::ErrorMethods> {
  void NoArgsPrimitiveError(NoArgsPrimitiveErrorRequest& request,
                            NoArgsPrimitiveErrorCompleter::Sync& completer) final {
    completer.Reply(fit::error(42));
  }
  void ManyArgsCustomError(ManyArgsCustomErrorRequest& request,
                           ManyArgsCustomErrorCompleter::Sync& completer) final {
    completer.Reply(fit::error(test_error_methods::MyError::kBadError));
  }
};

TEST(Client, PromisifyApplicationErrorMethodCasePrimitiveError) {
  auto server = std::make_unique<ErrorServer>();
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  async::Executor executor(loop.dispatcher());
  zx::status endpoints = fidl::CreateEndpoints<test_error_methods::ErrorMethods>();
  ASSERT_OK(endpoints.status_value());
  fidl::BindServer(loop.dispatcher(), std::move(endpoints->server), server.get());
  fidl::Client client(std::move(endpoints->client), loop.dispatcher());

  fpromise::promise<void, fidl::ErrorsIn<test_error_methods::ErrorMethods::NoArgsPrimitiveError>>
      p = fidl_fpromise::as_promise(client->NoArgsPrimitiveError({{.should_error = true}}));
  endpoints->server.reset();

  auto task = p.then(
      [&](fpromise::result<
          void, fidl::ErrorsIn<test_error_methods::ErrorMethods::NoArgsPrimitiveError>>& result) {
        ASSERT_TRUE(result.is_error());
        ASSERT_TRUE(result.error().is_domain_error());
        ASSERT_EQ(42, result.error().domain_error());
        loop.Quit();
      });

  executor.schedule_task(std::move(task));
  ASSERT_STATUS(ZX_ERR_CANCELED, loop.Run());
}

TEST(Client, PromisifyApplicationErrorMethodCaseCustomError) {
  auto server = std::make_unique<ErrorServer>();
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  async::Executor executor(loop.dispatcher());
  zx::status endpoints = fidl::CreateEndpoints<test_error_methods::ErrorMethods>();
  ASSERT_OK(endpoints.status_value());
  fidl::BindServer(loop.dispatcher(), std::move(endpoints->server), server.get());
  fidl::Client client(std::move(endpoints->client), loop.dispatcher());

  fpromise::promise<test_error_methods::ErrorMethodsManyArgsCustomErrorResponse,
                    fidl::ErrorsIn<test_error_methods::ErrorMethods::ManyArgsCustomError>>
      p = fidl_fpromise::as_promise(client->ManyArgsCustomError({{.should_error = true}}));

  auto task = p.then(
      [&](fpromise::result<test_error_methods::ErrorMethodsManyArgsCustomErrorResponse,
                           fidl::ErrorsIn<test_error_methods::ErrorMethods::ManyArgsCustomError>>&
              result) {
        ASSERT_TRUE(result.is_error());
        ASSERT_TRUE(result.error().is_domain_error());
        ASSERT_EQ(test_error_methods::MyError::kBadError, result.error().domain_error());
        loop.Quit();
      });

  executor.schedule_task(std::move(task));
  ASSERT_STATUS(ZX_ERR_CANCELED, loop.Run());
}

class SuccessServer : public fidl::Server<test_error_methods::ErrorMethods> {
  void NoArgsPrimitiveError(NoArgsPrimitiveErrorRequest& request,
                            NoArgsPrimitiveErrorCompleter::Sync& completer) final {
    completer.Reply(fit::ok());
  }
  void ManyArgsCustomError(ManyArgsCustomErrorRequest& request,
                           ManyArgsCustomErrorCompleter::Sync& completer) final {
    completer.Reply(fit::ok(
        test_error_methods::ErrorMethodsManyArgsCustomErrorResponse{{.a = 1, .b = 2, .c = 3}}));
  }
};

TEST(Client, PromisifyApplicationErrorMethodCaseNoArgsSuccess) {
  auto server = std::make_unique<SuccessServer>();
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  async::Executor executor(loop.dispatcher());
  zx::status endpoints = fidl::CreateEndpoints<test_error_methods::ErrorMethods>();
  ASSERT_OK(endpoints.status_value());
  fidl::BindServer(loop.dispatcher(), std::move(endpoints->server), server.get());
  fidl::Client client(std::move(endpoints->client), loop.dispatcher());

  fpromise::promise<void, fidl::ErrorsIn<test_error_methods::ErrorMethods::NoArgsPrimitiveError>>
      p = fidl_fpromise::as_promise(client->NoArgsPrimitiveError({{.should_error = false}}));

  auto task = p.then(
      [&](fpromise::result<
          void, fidl::ErrorsIn<test_error_methods::ErrorMethods::NoArgsPrimitiveError>>& result) {
        ASSERT_TRUE(result.is_ok());
        loop.Quit();
      });

  executor.schedule_task(std::move(task));
  ASSERT_STATUS(ZX_ERR_CANCELED, loop.Run());
}

TEST(Client, PromisifyApplicationErrorMethodCaseManyArgsSuccess) {
  auto server = std::make_unique<SuccessServer>();
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  async::Executor executor(loop.dispatcher());
  zx::status endpoints = fidl::CreateEndpoints<test_error_methods::ErrorMethods>();
  ASSERT_OK(endpoints.status_value());
  fidl::BindServer(loop.dispatcher(), std::move(endpoints->server), server.get());
  fidl::Client client(std::move(endpoints->client), loop.dispatcher());

  fpromise::promise<test_error_methods::ErrorMethodsManyArgsCustomErrorResponse,
                    fidl::ErrorsIn<test_error_methods::ErrorMethods::ManyArgsCustomError>>
      p = fidl_fpromise::as_promise(client->ManyArgsCustomError({{.should_error = false}}));

  auto task = p.then(
      [&](fpromise::result<test_error_methods::ErrorMethodsManyArgsCustomErrorResponse,
                           fidl::ErrorsIn<test_error_methods::ErrorMethods::ManyArgsCustomError>>&
              result) {
        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(test_error_methods::ErrorMethodsManyArgsCustomErrorResponse(1, 2, 3),
                  result.value());
        loop.Quit();
      });

  executor.schedule_task(std::move(task));
  ASSERT_STATUS(ZX_ERR_CANCELED, loop.Run());
}

}  // namespace
