// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CODEC_FIDL_CODEC_TEST_H_
#define SRC_LIB_FIDL_CODEC_FIDL_CODEC_TEST_H_

#include <fuchsia/sys/cpp/fidl.h>

#include "sdk/lib/fidl/cpp/binding.h"
#include "src/lib/fidl_codec/library_loader.h"

namespace fidl_codec {

// Stolen from //sdk/lib/fidl/cpp/test/async_loop_for_test.{h,cc}; cc
// is not public

class AsyncLoopForTestImpl;

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

  // Returns the underlying async_t.
  async_dispatcher_t* dispatcher();

 private:
  std::unique_ptr<AsyncLoopForTestImpl> impl_;
};

class AsyncLoopForTestImpl {
 public:
  AsyncLoopForTestImpl() : loop_(&kAsyncLoopConfigAttachToCurrentThread) {}
  ~AsyncLoopForTestImpl() = default;

  async::Loop* loop() { return &loop_; }

 private:
  async::Loop loop_;
};

// The fidl_codec tests work the following way:
// 1) Create a channel.
// 2) Bind an interface pointer to the client side of that channel.
// 3) Listen at the other end of the channel for the message.
// 4) Convert the message to JSON using the JSON message converter, and check
//    that the results look as expected.

// This binds |invoke| to one end of a channel, invokes it, and drops the wire
// format bits it picks up off the other end into |message|.
template <class T>
void InterceptRequest(fidl::Message& message, std::function<void(fidl::InterfacePtr<T>&)> invoke) {
  AsyncLoopForTest loop;

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  fidl::InterfacePtr<T> ptr;
  int error_count = 0;
  ptr.set_error_handler([&error_count](zx_status_t status) {
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);
    ++error_count;
  });

  EXPECT_EQ(ZX_OK, ptr.Bind(std::move(h1)));

  invoke(ptr);

  loop.RunUntilIdle();

  EXPECT_EQ(ZX_OK, message.Read(h2.get(), 0));
}

// This creates two channels and sends an `epitaph` on one, and reads the resulting
// bytes from the other into `message`.
template <class T>
void InterceptEpitaphResponse(fidl::Message& message, zx_status_t epitaph) {
  AsyncLoopForTest loop;

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  // Create a Binding so that we can call Close. The implementation pointer is
  // not needed, since no actual method calls are being invoked.
  fidl::Binding<T> binding(nullptr, fidl::InterfaceRequest<T>(std::move(h1)));
  binding.Close(epitaph);

  loop.RunUntilIdle();

  EXPECT_EQ(ZX_OK, message.Read(h2.get(), 0));
}

LibraryLoader* GetLoader();

}  // namespace fidl_codec

#endif  // SRC_LIB_FIDL_CODEC_FIDL_CODEC_TEST_H_
