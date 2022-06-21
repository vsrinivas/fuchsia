// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_COMMON_BASE_FIDL_SERVER_H_
#define SRC_MEDIA_AUDIO_SERVICES_COMMON_BASE_FIDL_SERVER_H_

#include <lib/fidl/llcpp/server.h>
#include <lib/sync/cpp/completion.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/types.h>

namespace media_audio {

// Base class for FIDL servers. Example of use:
//
// ```cpp
// class FidlServer : public BaseFidlServer<FidlServer, Protocol> {
//  public:
//    static std::shared_ptr<FidlServer> Create(async_dispatcher_t* dispatcher,
//                                              fidl::ServerEnd<Protocol> server_end,
//                                              int arg) {
//      return BaseFidlServer::Create(dispatcher, std::move(server_end), arg);
//    }
//
//    // Override all methods from fidl::WireServer<Protocol>
//    // ...
//
//  private:
//    static inline const std::string_view Name = "FidlServer";
//    template<class ServerT, class ProtocolT>
//    friend class BaseFidlServer;
//
//    FidlServer(int arg);
// };
// ```
//
// As shown above, subclasses should be created via a `Create` static method that calls
// into `BaseFidlServer::Create`.
template <typename ServerT, typename ProtocolT>
class BaseFidlServer : public fidl::WireServer<ProtocolT> {
 public:
  ~BaseFidlServer() = default;

  // Returns the dispatcher used by this server. Never null.
  async_dispatcher_t* dispatcher() const { return dispatcher_; }

  // Triggers a shutdown of this server using the given epitaph. The actual shutdown process happens
  // asynchronously. This may be called from any thread. After the first call, subsequent calls are
  // no-ops.
  void Shutdown(zx_status_t epitaph = ZX_ERR_CANCELED) { binding_->Close(epitaph); }

  // Waits until the server has shut down. This is a blocking call that can be invoked from any
  // thread. This is primarily intended for unit tests. Returns false if the server does not
  // shutdown before the given timeout has expired.
  bool WaitForShutdown(zx::duration timeout = zx::duration::infinite()) {
    return shutdown_complete_.Wait(timeout) == ZX_OK;
  }

 protected:
  BaseFidlServer() = default;

  // Helper to create a server. The ServerT object is constructed via `ServerT(args...)`.
  // Methods received on `server_end` will be dispatched on `dispatcher`.
  template <typename... Args>
  static std::shared_ptr<ServerT> Create(async_dispatcher_t* dispatcher,
                                         fidl::ServerEnd<ProtocolT> server_end, Args... args) {
    // std::make_shared requires a public ctor, but we hide our ctor to force callers to use Create.
    struct WithPublicCtor : public ServerT {
     public:
      explicit WithPublicCtor(Args... args) : ServerT(args...) {}
    };

    auto server = std::make_shared<WithPublicCtor>(args...);
    server->dispatcher_ = dispatcher;

    // Callback invoked when the server shuts down.
    auto on_unbound = [](ServerT* server, fidl::UnbindInfo info,
                         fidl::ServerEnd<ProtocolT> server_end) {
      server->OnShutdown(info);
      server->shutdown_complete_.Signal();
    };

    // Passing `server` (a shared_ptr) to BindServer ensures that the `server` object
    // lives until on_unbound is called.
    server->binding_ =
        fidl::BindServer(dispatcher, std::move(server_end), server, std::move(on_unbound));

    return server;
  }

  // Invoked from `dispatcher()` as the last step before the server shuts down.
  // Can be overridden by subclasses.
  virtual void OnShutdown(fidl::UnbindInfo info) {
    if (!info.is_user_initiated() && !info.is_peer_closed()) {
      FX_LOGS(ERROR) << ServerT::Name << " shutdown with unexpected status: " << info;
    } else {
      FX_LOGS(DEBUG) << ServerT::Name << " shutdown with status: " << info;
    }
  }

 private:
  async_dispatcher_t* dispatcher_;
  std::optional<fidl::ServerBindingRef<ProtocolT>> binding_;
  ::libsync::Completion shutdown_complete_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_COMMON_BASE_FIDL_SERVER_H_
