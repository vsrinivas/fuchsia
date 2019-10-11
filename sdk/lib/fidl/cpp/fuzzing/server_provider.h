// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_FUZZING_SERVER_PROVIDER_H_
#define LIB_FIDL_CPP_FUZZING_SERVER_PROVIDER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <iterator>
#include <memory>
#include <utility>
#include <vector>

namespace fidl {
namespace fuzzing {

// Enumerate the different ways`ServerProvider` objects can dispatch work to server requests.
enum ServerProviderDispatcherMode {
  // Use the `async_dispatcher_t*` passed to `ServerProvider.Connect()` to dispatch work to server
  // requests. This usually means that such work will be queued on the same `async::Loop` as the
  // client-side work to make requests to the server.
  kFromCaller = 0,
  // Construct an `async::Loop` (with its associated `async_dispatcher_t*`) on a separate thread.
  // The `async::Loop` will be is owned by the `ServerProvider`. This means that nothing but the
  // server's work will be dispatched on said thread/loop/dispatcher. This mode is generally
  // discouraged because the use of a separate thread makes it more difficult to reproduce bugs
  // discovered by fuzzers. Only use this mode if dispatching server work on the same
  // thread/loop/dispatcher as the client can cause a deadlock.
  kOwned,
};

// Server provider that implements the server lifecycle for a generated FIDL server fuzzer
template <typename Interface, typename Impl>
class ServerProvider {
 public:
  explicit ServerProvider(ServerProviderDispatcherMode dispatcher_mode) {
    if (dispatcher_mode == kOwned) {
      loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    }
  }

  // Implementation of fuzzer-to-server library's `fuzzer_init`.
  // Forward arguments to server constructor.
  template <typename... Args>
  zx_status_t Init(Args&&... args) {
    if (impl_.get() == nullptr) {
      printf("ServerProvider.Init: Creating new server instance\n");
      impl_ = std::make_unique<Impl>(std::forward<Args>(args)...);
    }

    printf("ServerProvider.Init: return ZX_OK\n");
    return ZX_OK;
  }

  // Implementation of fuzzer-to-server library's `fuzzer_connect`.
  zx_status_t Connect(zx_handle_t channel_handle, async_dispatcher_t* dispatcher) {
    if (loop_.get()) {
      dispatcher = loop_->dispatcher();
    }

    auto binding = std::make_unique<::fidl::Binding<Interface, Impl*>>(
        impl_.get(), zx::channel(channel_handle), dispatcher);
    bindings_.push_back(std::move(binding));

    printf("ServerProvider.Connect: return ZX_OK\n");
    return ZX_OK;
  }

  // Implementation of fuzzer-to-server library's `fuzzer_disconnect`.
  zx_status_t Disconnect(zx_handle_t channel_handle, async_dispatcher_t* dispatcher) {
    for (auto it = bindings_.begin(); it != bindings_.end(); it++) {
      if ((*it)->channel().get() == channel_handle) {
        printf("ServerProvider.Disconnect: Closing and removing binding\n");
        zx_status_t status = CloseAndDeleteBinding(it);
        if (status != ZX_OK) {
          printf("ServerProvider.Disconnect: CloseAndDeleteBinding returned bad status: %d\n",
                 status);
          return status;
        }

        break;
      }
    }

    printf("ServerProvider.Disconnect: return ZX_OK\n");
    return ZX_OK;
  }

  // Implementation of fuzzer-to-server library's `fuzzer_clean_up`.
  zx_status_t CleanUp() {
    if (bindings_.size() != 0) {
      printf("ServerProvider.CleanUp warning: %lu unexpected dangling bindings\n",
             bindings_.size());
    }

    while (!bindings_.empty()) {
      CloseAndDeleteBinding(bindings_.begin());
    }

    impl_.reset(nullptr);

    printf("ServerProvider.CleanUp: return ZX_OK\n");
    return ZX_OK;
  }

 private:
  // Close and delete a binding in a way that is consistent with server provider's thread model.
  zx_status_t CloseAndDeleteBinding(
      typename std::vector<std::unique_ptr<::fidl::Binding<Interface, Impl*>>>::iterator it) {
    // Single-threaded case: Close channel and delete binding immediately.
    if (!loop_.get()) {
      (*it)->Close(ZX_OK);
      bindings_.erase(it);

      printf("ServerProvider.CloseAndDeleteBinding: Return ZX_OK\n");
      return ZX_OK;
    }

    // Multi-threaded case: Post task to close channel and delete binding, then synchronize.
    zx::event event;
    zx_status_t status = zx::event::create(0, &event);
    if (status != ZX_OK) {
      printf("ServerProvider.CloseAndDeleteBinding: zx::event::create returned bad status: %d\n",
             status);
      return status;
    }

    async::PostTask((*it)->dispatcher(), [&]() {
      (*it)->Close(ZX_OK);
      status = event.signal(0, ZX_USER_SIGNAL_1);
      bindings_.erase(it);
      if (status != ZX_OK) {
        printf(
            "ServerProvider.CloseAndDeleteBinding: Channel closed signal returned bad status: "
            "%d\n",
            status);
        printf("ServerProvider.CloseAndDeleteBinding: Fuzzer may hang indefinitely!\n");
      }
    });

    status = event.wait_one(ZX_USER_SIGNAL_1, zx::time::infinite(), nullptr);
    if (status != ZX_OK) {
      printf(
          "ServerProvider.CloseAndDeleteBinding: Wait for channel close signal returned bad "
          "status: %d\n",
          status);
      return status;
    }

    printf("ServerProvider.CloseAndDeleteBinding: Return ZX_OK\n");
    return ZX_OK;
  }

  std::unique_ptr<async::Loop> loop_;
  std::unique_ptr<Impl> impl_;
  std::vector<std::unique_ptr<::fidl::Binding<Interface, Impl*>>> bindings_;
};

}  // namespace fuzzing
}  // namespace fidl

// Macro for defining fuzzer-to-server library C symbols. The code will instantiate and leak a
// `ServiceProvider` that is reused between fuzz target runs. This is because
// threads/loops/dispatchers should be reused between fuzz target runs, and the fuzz target API
// contains no cleanup mechanism that triggers after all fuzz target runs are complete.
//
// Parameters:
//   `ServerProvider`: The server provider class; usually fidl::fuzzing::ServerProvider.
//   `Interface`: The FIDL interface class; an abstract class from fidlgen-generated code.
//   `Impl`: The class that implements `Interface` to be fuzzed.
//   `dispatcher_mode`: The first (and only) parameter to be passed to the `ServerProvider`
//     constructor.
//   `...`: Parameters to be forwarded to `ServerProvider.Init(...)`.
#define FIDL_FUZZER_DEFINITION(ServerProvider, Interface, Impl, dispatcher_mode, ...)         \
                                                                                              \
  namespace {                                                                                 \
                                                                                              \
  static ServerProvider<Interface, Impl>* fuzzer_server_provider;                             \
  }                                                                                           \
                                                                                              \
  extern "C" {                                                                                \
                                                                                              \
  zx_status_t fuzzer_init() {                                                                 \
    if (fuzzer_server_provider == nullptr) {                                                  \
      fuzzer_server_provider = new ServerProvider<Interface, Impl>(dispatcher_mode);          \
    }                                                                                         \
    return fuzzer_server_provider->Init(__VA_ARGS__);                                         \
  }                                                                                           \
                                                                                              \
  zx_status_t fuzzer_connect(zx_handle_t channel_handle, async_dispatcher_t* dispatcher) {    \
    return fuzzer_server_provider->Connect(channel_handle, dispatcher);                       \
  }                                                                                           \
                                                                                              \
  zx_status_t fuzzer_disconnect(zx_handle_t channel_handle, async_dispatcher_t* dispatcher) { \
    return fuzzer_server_provider->Disconnect(channel_handle, dispatcher);                    \
  }                                                                                           \
                                                                                              \
  zx_status_t fuzzer_clean_up() { return fuzzer_server_provider->CleanUp(); }                 \
  }

#endif  // LIB_FIDL_CPP_FUZZING_SERVER_PROVIDER_H_
