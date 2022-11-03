// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_CONTRIB_CONNECTION_SERVICE_RECONNECTOR_H_
#define SRC_LIB_FIDL_CPP_CONTRIB_CONNECTION_SERVICE_RECONNECTOR_H_

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/client.h>
#include <lib/fidl/cpp/wire/internal/transport.h>
#include <lib/syslog/cpp/macros.h>

#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <utility>

#include "src/lib/backoff/exponential_backoff.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"

namespace fidl::contrib {

// ServiceReconnector is a utility class to make staying connected to a fidl protocol easier.
//
// Using this class requires defining a |ConnectLambda| that takes as an argument a
// |ConnectResolver|.
//
// NOTE: ServiceReconnector must be used from the |dispatcher| thread.
// This includes construction, destruction, and making calls.
//
// For example, if you had a fidl service like:
//
//   type error = strict enum : int32 {
//     ERROR = 1;
//   }
//
//   @discoverable
//   protocol SimpleProtocol {
//     DoAction() -> () error Error;
//   }
//
// Then using service connector would be as simple as:
//
//   auto reconnector = ServiceReconnector<SimpleProtocol>::Create(dispatcher_, "SimpleProtocol",
//   [](ServiceReconnector<SimpleProtocol>::ConnectResolver resolver) {
//     auto connection = component::ConnectAt<SimpleProtocol>(svc());
//     if (connection.is_error()) {
//       resolver.resolve(std::nullopt);
//     } else {
//       resolver.resolve(std::move(connection.value()));
//     }
//   });
//
//   reconnector->Do([](fidl::Client<SimpleProtocol> &protocol) {
//     // Do something with |protocol| here.
//   })
//
template <class Service>
class ServiceReconnector : public std::enable_shared_from_this<ServiceReconnector<Service>> {
 public:
  // ConnectResolver is used to give the ServiceReconnector back an instance of
  // |fidl::ClientEnd<Service>|.
  //
  // When the connection has been made successfully, resolve is called with the client end of the
  // channel. If the connection fails, resolve can be called manually with std::nullopt, or the
  // ConnectResolver can be dropped, which will implicitly resolve with std::nullopt.
  class ConnectResolver {
   private:
    explicit ConnectResolver(std::weak_ptr<ServiceReconnector> reconnector)
        : reconnector_(std::move(reconnector)) {}
    friend class ServiceReconnector;

   public:
    // Connect resolver should be move-only.
    ConnectResolver(const ConnectResolver&) = delete;
    ConnectResolver& operator=(const ConnectResolver&) = delete;
    ConnectResolver(ConnectResolver&& other) noexcept { MoveImpl(std::move(other)); }
    ConnectResolver& operator=(ConnectResolver&& other) noexcept {
      if (this != other) {
        MoveImpl(std::move(other));
      }
      return *this;
    }

    ~ConnectResolver() noexcept { resolve(std::nullopt); }

    // Resolve the current connection request.
    //
    // Note: if resolve is called multiple times on ConnectResolver, only the first call will be
    //       handled, and all future calls will be ignored.
    void resolve(std::optional<fidl::ClientEnd<Service>> result) {
      if (!resolved_) {
        if (auto reconnector = reconnector_.lock()) {
          reconnector->HandleConnectResult(std::move(result));
        }
        resolved_ = true;
      }
    }

   private:
    // Moves the ConnectResolver to a new location.
    //
    // Note: Since we want to ensure that the resolver only resolves once, the source resolver
    //       `other` is manually set to `resolved_` = true, so that when it is destroyed, it doesn't
    //       resolve.
    void MoveImpl(ConnectResolver&& other) noexcept {
      reconnector_ = std::move(other.reconnector_);
      resolved_ = other.resolved_;
      other.resolved_ = true;
    }

    bool resolved_ = false;
    std::weak_ptr<ServiceReconnector> reconnector_;
  };

  using ConnectLambda = fit::function<void(ConnectResolver)>;
  using DisconnectLambda = fit::function<void()>;
  // Create makes an instance of ServiceReconnector.
  // |dispatcher| the dispatcher thread where the fidl service should be connected from.
  // |tag| Used in error messages, so that multiple ConnectResolvers will have distinguishable
  //       logging.
  // |connect| A lambda that is called each time ServiceReconnector tries to connect or re-connect
  //           to the service.
  // |max_queued_callbacks| (default: 20) How many |DoCallback|s should be stored while waiting for
  //                        a connection before further |DoCallback|s will be ignored.
  // |disconnect| Called whenever the ServiceReconnector detects that the underlying service has
  //              been disconnected. Useful in the case of a nested ServiceReconnector, so that the
  //              sub-service reconnect can be triggered if the parent service disconnects.
  static std::shared_ptr<ServiceReconnector> Create(
      async_dispatcher_t* dispatcher, std::string tag, ConnectLambda&& connect,
      size_t max_queued_callbacks = 20, DisconnectLambda&& disconnect = []() {}) {
    auto reconnector = std::shared_ptr<ServiceReconnector>(
        new ServiceReconnector(dispatcher, std::move(tag), std::move(connect), max_queued_callbacks,
                               std::move(disconnect)));
    async::PostTask(reconnector->dispatcher_, [weak_this = reconnector->get_this()] {
      if (auto shared_this = weak_this.lock()) {
        shared_this->Connect();
      }
    });
    return reconnector;
  }

  // Queues a lambda that will be called whenever the underlying service is successfully connected.
  //
  //   reconnector->Do([](fidl::Client<Service>& service) {
  //     // |service| is guaranteed to be connected, use it as such.
  //   })
  //
  // Note: if more than |max_queued_callbacks_| callbacks have been queued, future calls to Do will
  // be a noop.
  using DoCallback = fit::function<void(fidl::Client<Service>&)>;
  void Do(DoCallback&& callback) FXL_LOCKS_EXCLUDED(mutex_) {
    {
      std::lock_guard<std::mutex> lock(mutex_);

      if (callbacks_to_run_.size() >= max_queued_callbacks_) {
        FX_LOGS_FIRST_N(WARNING, 20) << tag_ << ": Buffer full; dropping callback.";
        return;
      }
      if (is_shutdown_) {
        FX_LOGS_FIRST_N(WARNING, 20) << tag_ << ": Ignoring do callback during shutdown.";
        return;
      }
      callbacks_to_run_.emplace(std::move(callback));
    }
    async::PostTask(dispatcher_, [weak_this = get_this()]() {
      if (auto shared_this = weak_this.lock()) {
        shared_this->RunCallbacks();
      }
    });
  }

  // Shutdown makes sure that no new |DoCallback|s will be queued, so the class can cleanly shut
  // down.
  void Shutdown() FXL_LOCKS_EXCLUDED(mutex_) {
    std::lock_guard<std::mutex> lock(mutex_);
    is_shutdown_ = true;
  }

  // Force a reconnection to the underlying service.
  void Reconnect() FXL_LOCKS_EXCLUDED(mutex_) {
    std::lock_guard<std::mutex> lock(mutex_);
    InnerReconnect();
  }

 private:
  ServiceReconnector() = delete;

  explicit ServiceReconnector(async_dispatcher_t* dispatcher, std::string tag,
                              ConnectLambda&& connect, size_t max_queued_callbacks,
                              DisconnectLambda&& disconnect)
      : dispatcher_(dispatcher),
        tag_(std::move(tag)),
        connect_(std::move(connect)),
        disconnect_(std::move(disconnect)),
        max_queued_callbacks_(max_queued_callbacks) {}

  class ServiceEventHandler : public fidl::AsyncEventHandler<Service> {
   public:
    explicit ServiceEventHandler(std::weak_ptr<ServiceReconnector> reconnector)
        : reconnector_(reconnector) {}

    void on_fidl_error(fidl::UnbindInfo error) override {
      if (auto reconnector = reconnector_.lock()) {
        FX_LOGS(WARNING) << reconnector->tag_ << ": service encountered an error: " << error
                         << ". Triggering reconnect.";
        std::lock_guard<std::mutex> lock(reconnector->mutex_);
        reconnector->InnerReconnect();
      }
    }

   private:
    std::weak_ptr<ServiceReconnector> reconnector_;
  };

  std::weak_ptr<ServiceReconnector> get_this() {
    std::shared_ptr<ServiceReconnector> this_ptr = this->shared_from_this();
    FX_DCHECK(!this_ptr.unique());
    return this_ptr;
  }

  ServiceEventHandler* event_handler() FXL_REQUIRE(mutex_) {
    if (event_handler_ == nullptr) {
      event_handler_ = std::make_unique<ServiceEventHandler>(get_this());
    }
    return event_handler_.get();
  }

  void InnerReconnect() FXL_REQUIRE(mutex_) {
    disconnect_();
    is_connected_ = false;
    async::PostDelayedTask(
        dispatcher_,
        [weak_this = get_this()] {
          if (auto shared_this = weak_this.lock()) {
            shared_this->Connect();
          }
        },
        backoff_.GetNext());
  }

  void Connect() FXL_LOCKS_EXCLUDED(mutex_) {
    FX_DCHECK(dispatcher_ == async_get_default_dispatcher())
        << tag_ << ": Connect may only be called from the dispatcher thread";

    // Ensure that we don't try to connect multiple times.
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (is_connecting_ || is_shutdown_) {
        return;
      }
      is_connecting_ = true;
    }

    connect_(ConnectResolver(get_this()));
  }

  void HandleConnectResult(std::optional<fidl::ClientEnd<Service>> client_end)
      FXL_LOCKS_EXCLUDED(mutex_) {
    FX_DCHECK(dispatcher_ == async_get_default_dispatcher())
        << tag_ << ": HandleConnectResult may only be called from the dispatcher thread";

    {
      std::lock_guard<std::mutex> lock(mutex_);
      is_connecting_ = false;
      if (client_end) {
        service_client_ =
            fidl::Client<Service>(std::move(client_end.value()), dispatcher_, event_handler());
        is_connected_ = true;
      } else {
        InnerReconnect();
      }
    }
    // Attempt to run callbacks.
    RunCallbacks();
  }

  void RunCallbacks() FXL_LOCKS_EXCLUDED(mutex_) {
    FX_DCHECK(dispatcher_ == async_get_default_dispatcher());
    while (true) {
      DoCallback callback;

      {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!is_connected_) {
          async::PostTask(dispatcher_, [weak_this = get_this()]() {
            if (auto shared_this = weak_this.lock()) {
              shared_this->Reconnect();
            }
          });
          return;
        }

        if (callbacks_to_run_.empty()) {
          return;
        }

        callback = std::move(callbacks_to_run_.front());
        callbacks_to_run_.pop();
      }

      callback(service_client_);
    }
  }

  async_dispatcher_t* dispatcher_;
  std::string tag_;
  ConnectLambda connect_;
  DisconnectLambda disconnect_;
  size_t max_queued_callbacks_;

  fidl::Client<Service> service_client_;  // Should only be modified by the dispatcher_ thread.

  std::mutex mutex_;
  bool is_connecting_ FXL_GUARDED_BY(mutex_) = false;
  bool is_connected_ FXL_GUARDED_BY(mutex_) = false;
  bool is_shutdown_ FXL_GUARDED_BY(mutex_) =
      false;  // When shutdown is set, connect_ should not be accessed.
  backoff::ExponentialBackoff backoff_ FXL_GUARDED_BY(mutex_);
  std::unique_ptr<ServiceEventHandler> event_handler_ FXL_GUARDED_BY(mutex_);
  std::queue<DoCallback> callbacks_to_run_ FXL_GUARDED_BY(mutex_);
};

}  // namespace fidl::contrib

#endif  // SRC_LIB_FIDL_CPP_CONTRIB_CONNECTION_SERVICE_RECONNECTOR_H_
