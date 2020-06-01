// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_HWSTRESS_TESTING_UTIL_H_
#define GARNET_BIN_HWSTRESS_TESTING_UTIL_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding.h>

#include <any>

namespace hwstress {
namespace testing {

// LoopbackConnectionFactory simplifies creation of client connections
// to a local object implementing a FIDL interface.
//
// For example, use as follows:
//
//   // Create a factory.
//   LoopbackConnectionFactory factory;
//
//   // Create an object implementing "FidlProtocol".
//   FakeFidlProtocolImpl instance;
//
//   // Create a channel to it...
//   zx::channel channel = factory.CreateChannelTo<FidlProtocol>(&instance);
//
//   // ... or generate a sync binding.
//   FidlProtocolSyncPtr ptr = factory.CreateSyncPtrTo<FidlProtocol>(&instance);
//
// The channels will continue to be serviced while ever the
// LoopbackConnectionFactory remains live.
class LoopbackConnectionFactory {
 public:
  LoopbackConnectionFactory() : loop_(&kAsyncLoopConfigNeverAttachToThread) { loop_.StartThread(); }

  ~LoopbackConnectionFactory() {
    loop_.Quit();
    loop_.JoinThreads();
  }

  // Create a channel to |impl| implementing FIDL interface |T|.
  template <typename T>
  zx::channel CreateChannelTo(T* impl) {
    zx::channel client_channel, server_channel;
    zx_status_t status = zx::channel::create(0, &client_channel, &server_channel);
    ZX_ASSERT(status == ZX_OK);
    auto binding =
        std::make_shared<fidl::Binding<T>>(impl, std::move(server_channel), loop_.dispatcher());
    bindings_.push_back(std::any(std::move(binding)));
    return client_channel;
  }

  // Create a fidl::SynchronousInterfacePtr to |impl| implementing FIDL
  // interface |T|.
  template <typename T>
  fidl::SynchronousInterfacePtr<T> CreateSyncPtrTo(T* impl) {
    fidl::SynchronousInterfacePtr<T> ptr;
    ptr.Bind(CreateChannelTo<T>(impl));
    return ptr;
  }

 private:
  async::Loop loop_;
  std::vector<std::any> bindings_;
};

}  // namespace testing
}  // namespace hwstress

#endif  // GARNET_BIN_HWSTRESS_TESTING_UTIL_H_
