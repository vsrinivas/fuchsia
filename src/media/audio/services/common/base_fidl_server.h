// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_COMMON_BASE_FIDL_SERVER_H_
#define SRC_MEDIA_AUDIO_SERVICES_COMMON_BASE_FIDL_SERVER_H_

#include <lib/fidl/cpp/wire/server.h>
#include <lib/sync/cpp/completion.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <zircon/types.h>

#include <map>
#include <thread>

#include "src/media/audio/services/common/fidl_thread.h"

namespace media_audio {

namespace internal {
// Base class for BaseFidlServer. This is an implementation detail. Don't use directly.
class BaseFidlServerUntyped {
 public:
  ~BaseFidlServerUntyped() = default;

  virtual bool WaitForShutdown(zx::duration timeout) = 0;

 protected:
  bool WaitForShutdownOfThisServer(zx::duration timeout) {
    return shutdown_complete_.Wait(timeout) == ZX_OK;
  }
  void SetShutdownComplete() { shutdown_complete_.Signal(); }

 private:
  ::libsync::Completion shutdown_complete_;
};
}  // namespace internal

// Base class for FIDL servers. Example of use:
//
// ```cpp
// class ProtocolServer : public BaseFidlServer<ProtocolServer, fidl::WireServer<Protocol>> {
//  public:
//    static std::shared_ptr<ProtocolServer> Create(std::shared_ptr<const FidlThread> thread,
//                                              fidl::ServerEnd<Protocol> server_end,
//                                              int arg) {
//      return BaseFidlServer::Create(std::move(thread), std::move(server_end), arg);
//    }
//
//    // Override all methods from fidl::WireServer<Protocol>
//    // ...
//
//  private:
//    static inline const std::string_view kClassName = "ProtocolServer";
//    template <typename ServerT, template <typename T> typename FidlServerT, typename ProtocolT>
//    friend class BaseFidlServer;
//
//    ProtocolServer(int arg);
// };
// ```
//
// As shown above, subclasses should be created via a `Create` static method that calls
// into `BaseFidlServer::Create`. The `FidlServerT` template argument can be `fidl::Server<>` or
// `fidl::WireServer`.
template <typename ServerT, template <typename ProtocolT> typename FidlServerT, typename ProtocolT>
class BaseFidlServer : public FidlServerT<ProtocolT>, public internal::BaseFidlServerUntyped {
 public:
  using Protocol = ProtocolT;

  // Returns the thread used by this server.
  const FidlThread& thread() const { return *thread_; }

  // Like thread, but returns a `std::shared_ptr` which can be copied.
  std::shared_ptr<const FidlThread> thread_ptr() const { return thread_; }

  // Triggers a shutdown of this server using the given epitaph. The actual shutdown process happens
  // asynchronously. This may be called from any thread. After the first call, subsequent calls are
  // no-ops.
  void Shutdown(zx_status_t epitaph = ZX_ERR_CANCELED) { binding_->Close(epitaph); }

  // Waits until the server and all its children have shut down. This does not actually shut down
  // any servers -- shutdown must be triggered separately. A server can be shutdown either via an
  // explicit call to `Shutdown` or by closing the client channel, both of which trigger shutdown
  // asynchronously. This is a blocking call that can be invoked from any thread. This is primarily
  // intended for tests.
  //
  // Returns false if the server(s) do not shutdown before the given timeout has expired.
  bool WaitForShutdown(zx::duration timeout = zx::duration::infinite()) {
    // Wait for this server to shutdown first.
    if (!WaitForShutdownOfThisServer(timeout)) {
      return false;
    }

    // Wait for all children. Since this server has shutdown, it should not call `AddChildServer`
    // concurrently.
    std::lock_guard<std::mutex> lock(mutex_);

    auto deadline = zx::clock::get_monotonic() + timeout;
    for (auto [p, weak_child] : children_) {
      if (auto child = weak_child.lock(); child) {
        timeout = deadline - zx::clock::get_monotonic();
        if (!child->WaitForShutdown(timeout)) {
          return false;
        }
      }
    }

    return true;
  }

 protected:
  BaseFidlServer() = default;

  // Helper to create a server. The ServerT object is constructed via `ServerT(args...)`.
  // Methods received on `server_end` will be dispatched on `thread->dispatcher()`.
  template <typename... Args>
  static std::shared_ptr<ServerT> Create(std::shared_ptr<const FidlThread> thread,
                                         fidl::ServerEnd<ProtocolT> server_end, Args... args) {
    // std::make_shared requires a public ctor, but we hide our ctor to force callers to use Create.
    struct WithPublicCtor : public ServerT {
     public:
      explicit WithPublicCtor(Args... args) : ServerT(std::forward<Args>(args)...) {}
    };

    auto server = std::make_shared<WithPublicCtor>(std::forward<Args>(args)...);
    server->thread_ = thread;

    // Callback invoked when the server shuts down.
    auto on_unbound = [](ServerT* server, fidl::UnbindInfo info,
                         fidl::ServerEnd<ProtocolT> server_end) {
      server->OnShutdown(info);
      server->SetShutdownComplete();
    };

    // Passing `server` (a shared_ptr) to BindServer ensures that the `server` object
    // lives until on_unbound is called.
    server->binding_ = fidl::BindServer(thread->dispatcher(), std::move(server_end), server,
                                        std::move(on_unbound));

    return server;
  }

  // Invoked on `thread()` as the last step before the server shuts down.
  // Can be overridden by subclasses.
  virtual void OnShutdown(fidl::UnbindInfo info) {
    if (!info.is_user_initiated() && !info.is_peer_closed()) {
      FX_LOGS(ERROR) << ServerT::kClassName << " shutdown with unexpected status: " << info;
    } else {
      FX_LOGS(DEBUG) << ServerT::kClassName << " shutdown with status: " << info;
    }
  }

  // Adds a child server. The child is held as a weak_ptr so it will be automatically
  // garbage collected after it is destroyed.
  void AddChildServer(const std::shared_ptr<internal::BaseFidlServerUntyped>& server) {
    std::lock_guard<std::mutex> lock(mutex_);
    GarbageCollectChildren();  // avoid unbounded growth
    children_[server.get()] = server;
  }

 private:
  using BaseFidlServerUntyped::SetShutdownComplete;
  using BaseFidlServerUntyped::WaitForShutdownOfThisServer;

  void GarbageCollectChildren() TA_REQ(mutex_) {
    for (auto it = children_.begin(); it != children_.end();) {
      if (it->second.expired()) {
        it = children_.erase(it);
      } else {
        ++it;
      }
    }
  }

  std::shared_ptr<const FidlThread> thread_;
  std::optional<fidl::ServerBindingRef<ProtocolT>> binding_;

  // This lock is required so that WaitForShutdown can be called from any thread, while
  // AddChildServer is always called from `thread_`. In practice those should never be called
  // concurrently, but to be safe we require a lock anyway.
  std::mutex mutex_;
  std::map<internal::BaseFidlServerUntyped*, std::weak_ptr<internal::BaseFidlServerUntyped>>
      children_ TA_GUARDED(mutex_);
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_COMMON_BASE_FIDL_SERVER_H_
