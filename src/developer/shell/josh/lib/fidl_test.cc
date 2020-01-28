// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/test/frobinator/cpp/fidl.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <zircon/syscalls.h>

#include <memory>
#include <string>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/test/async_loop_for_test.h"
#include "lib/fidl/cpp/test/frobinator_impl.h"
#include "src/developer/shell/josh/lib/js_testing_utils.h"
#include "src/developer/shell/josh/lib/runtime.h"
#include "src/developer/shell/josh/lib/zx.h"
#include "src/lib/fidl_codec/library_loader_test_data.h"

namespace shell {

// TODO(jeremymanson): Dedup this with the version in async_loop_for_test.h

class AsyncLoopForTestImpl {
 public:
  AsyncLoopForTestImpl() : loop_(&kAsyncLoopConfigAttachToCurrentThread) {}
  ~AsyncLoopForTestImpl() = default;

  async::Loop* loop() { return &loop_; }

 private:
  async::Loop loop_;
};

class AsyncLoopForTest {
 public:
  // The AsyncLoopForTest constructor should also call
  // async_set_default_dispatcher() with the chosen dispatcher implementation.
  AsyncLoopForTest();
  ~AsyncLoopForTest();

  // This call matches the behavior of async_loop_run_until_idle().
  zx_status_t RunUntilIdle();

  // This call matches the behavior of async_loop_run().
  zx_status_t Run();

  // Returns the underlying async_dispatcher_t.
  async_dispatcher_t* dispatcher();

 private:
  std::unique_ptr<AsyncLoopForTestImpl> impl_;
};

AsyncLoopForTest::AsyncLoopForTest() : impl_(std::make_unique<AsyncLoopForTestImpl>()) {}

AsyncLoopForTest::~AsyncLoopForTest() = default;

zx_status_t AsyncLoopForTest::RunUntilIdle() { return impl_->loop()->RunUntilIdle(); }

zx_status_t AsyncLoopForTest::Run() { return impl_->loop()->Run(); }

async_dispatcher_t* AsyncLoopForTest::dispatcher() { return impl_->loop()->dispatcher(); }

class FidlTest : public JsTest {
 protected:
  void SetUp() override { JsTest::SetUp(); }
};

// Sanity check test to make sure Hello World works.
TEST_F(FidlTest, SimpleFrobinator) {
  InitBuiltins("", "");

  // Load up some FIDL to call.
  fidl_codec_test::FidlcodecExamples examples;
  const std::string fidl_to_find = "frobinator.fidl.json";
  std::string frob_fidl;
  for (const auto& element : examples.map()) {
    if (0 == element.first.compare(element.first.length() - fidl_to_find.length(),
                                   fidl_to_find.length(), fidl_to_find)) {
      frob_fidl = element.second;
    }
  }
  std::string load = "fidl.loadLibraryIr(`" + frob_fidl + "`);\n";

  // Set up a channel to call over.
  zx_handle_t out0, out1;
  zx_status_t status = zx_channel_create(0, &out0, &out1);
  ASSERT_EQ(ZX_OK, status) << "Unable to create zx_channel";
  JSContext* ctx = ctx_->Get();
  JSValue js_handle = zx::HandleCreate(ctx, out0, ZX_OBJ_TYPE_CHANNEL);
  JS_DefinePropertyValueStr(ctx, JS_GetGlobalObject(ctx), "outHandle", js_handle,
                            JS_PROP_CONFIGURABLE);

  // Set up the server side of the channel.
  AsyncLoopForTest loop;
  fidl::test::FrobinatorImpl impl;
  fidl::Binding<fidl::test::frobinator::Frobinator> binding(&impl, ::zx::channel(out1));

  binding.set_error_handler(
      [](zx_status_t status) { FAIL() << "Frob call failed with status " << status; });
  // Send a message from a JS client.
  std::string test_string = load + R"(
if (globalThis.outHandle == undefined) {
  throw "outHandle undefined";
}

fidl.loadLibrary("fidl.test.frobinator");
client = new fidl.ProtocolClient(
    new zx.Channel(globalThis.outHandle), fidling.fidl_test_frobinator.Frobinator);
client.Frob("one");
)";
  ASSERT_TRUE(Eval(test_string));
  EXPECT_EQ(0, impl.frobs.size());

  loop.RunUntilIdle();

  // This means that the message was received.
  EXPECT_EQ(1u, impl.frobs.size());
}

// TODO(jeremymanson): Write a test that relies on a service.  This requires us to componentize
// these tests.

}  // namespace shell
