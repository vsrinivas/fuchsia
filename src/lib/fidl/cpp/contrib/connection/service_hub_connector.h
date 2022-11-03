// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_CONTRIB_CONNECTION_SERVICE_HUB_CONNECTOR_H_
#define SRC_LIB_FIDL_CPP_CONTRIB_CONNECTION_SERVICE_HUB_CONNECTOR_H_

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/client.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/fidl/cpp/wire/internal/transport.h>
#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>

#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <utility>

#include "src/lib/backoff/exponential_backoff.h"
#include "src/lib/fidl/cpp/contrib/connection/service_reconnector.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"

namespace fidl::contrib {

// ServiceHubConnector is a utility class to make connecting to fidl protocol factories easier.
//
// To use this class, it must be extended with implementations of the
// methods |ConnectToServiceHub()| and |ConnectToService()|.
//
// NOTE: ServiceHubConnector and its subclasses must be used from the |dispatcher| thread.
// This includes construction, destruction, and making calls.
//
// For example, if you have a fidl service like:
//
//   type Error = strict enum : int32 {
//       PERMANENT = 1;
//       TRANSIENT = 2;
//   };
//
//   @discoverable
//   protocol ProtocolFactory {
//       CreateProtocol(resource struct {
//           protocol server_end:Protocol;
//       }) -> () error Error;
//   };
//
//   protocol Protocol {
//       DoAction() -> () error Error;
//   };
//
// Then you could implement ServiceHubConnector like this:
//
//   class ProtocolConnector final : private ServiceHubConnector<ProtocolFactory, Protocol, Status>
//   {
//    public:
//     explicit ProtocolConnector(async_dispatcher_t*dispatcher,
//                                fidl::UnownedClientEnd<fuchsia_io::Directory> directory)
//         : ServiceHubConnector(dispatcher), directory_(directory) {}
//
//    private:
//     void ConnectToServiceHub(ServiceHubConnectResolver resolver) override {
//       auto connection = component::ConnectAt<ProtocolFactory>(directory_);
//       if (connection.is_error()) {
//         resolver.resolve(std::nullopt);
//       } else {
//         resolver.resolve(std::move(connection.value()));
//       }
//     }
//
//     void ConnectToService(fidl::Client<ProtocolFactory>& factory,
//                           ServiceConnectResolver resolver) override {
//       auto endpoints = fidl::CreateEndpoints<Protocol>();
//
//       factory
//           ->CreateProtocol(
//               test_protocol::ProtocolFactoryCreateProtocolRequest(std::move(endpoints->server)))
//           .Then([resolver = std::move(resolver), client_end = std::move(endpoints->client)](
//                     fidl::Result<ProtocolFactory::CreateProtocol> &response) mutable {
//             if (response.is_ok()) {
//               resolver.resolve(std::move(client_end));
//             } else {
//               resolver.resolve(std::nullopt);
//             }
//           });
//     }
//
//     fidl::UnownedClientEnd<fuchsia_io::Directory> directory_;
//   };
//
// Then you could use it like:
//
//   ProtocolConnector connector(...);
//   connector.Do([](fidl::Client<Protocol>& protocol, DoResolver resolver) {
//     protocol->DoAction().Then(
//         [resolver = std::move(resolver)](
//             fidl::Result<test_protocol::Protocol::DoAction>& status) mutable {
//           resolver.resolve(status.is_error() &&
//                            (status.error_value().is_framework_error() ||
//                            status.error_value().domain_error() == Error::kTransient));
//         });
//   });
//
template <class ServiceHub, class Service>
class ServiceHubConnector {
 public:
  using ServiceHubConnectResolver = typename ServiceReconnector<ServiceHub>::ConnectResolver;
  using ServiceConnectResolver = typename ServiceReconnector<Service>::ConnectResolver;
  using ConnectToServiceHubLambda = typename ServiceReconnector<ServiceHub>::ConnectLambda;
  using ConnectToServiceLambda =
      fit::function<void(fidl::Client<ServiceHub>&, ServiceConnectResolver)>;

  class DoResolver;
  using DoCallback = std::function<void(fidl::Client<Service>&, DoResolver)>;

 private:
  class ServiceHubConnectorInner : public std::enable_shared_from_this<ServiceHubConnectorInner> {
   private:
    // |dispatcher| is the dispatcher that will be used for the service connections. This class must
    //              be destroyed from the same thread as the dispatcher.
    // |max_queued_callbacks| The number of lambdas to queue before rejecting new ones. This is to
    //                        avoid a situation where the remote service is not accepting calls for
    //                        a long period of time causing this class to consume too much memory.
    explicit ServiceHubConnectorInner(async_dispatcher_t* dispatcher, size_t max_queued_callbacks)
        : dispatcher_(dispatcher), max_queued_callbacks_(max_queued_callbacks) {}
    friend class ServiceHubConnector;

    std::weak_ptr<ServiceHubConnectorInner> get_this() {
      std::shared_ptr<ServiceHubConnectorInner> this_ptr = this->shared_from_this();
      FX_DCHECK(!this_ptr.unique());
      return this_ptr;
    }

    void Setup(ConnectToServiceHubLambda&& connect_to_service_hub,
               ConnectToServiceLambda&& connect_to_service, size_t max_queued_callbacks)
        FXL_LOCKS_EXCLUDED(mutex_) {
      auto connect_to_service_ptr =
          std::make_shared<ConnectToServiceLambda>(std::move(connect_to_service));
      service_reconnector_ = ServiceReconnector<Service>::Create(
          dispatcher_, "Service",
          [weak_this = get_this(), connect_to_service_ptr](ServiceConnectResolver resolver) {
            if (auto shared_this = weak_this.lock()) {
              shared_this->service_hub_reconnector_->Do(
                  [resolver = std::move(resolver), connect = connect_to_service_ptr](
                      fidl::Client<ServiceHub>& service_hub) mutable {
                    (*connect)(service_hub, std::move(resolver));
                  });
            }
          },
          max_queued_callbacks);

      service_hub_reconnector_ = ServiceReconnector<ServiceHub>::Create(
          dispatcher_, "ServiceHub", std::move(connect_to_service_hub), max_queued_callbacks,
          [service_reconnector = service_reconnector_]() {
            // When service hub disconnects, trigger reconnect in service_reconnector_.
            service_reconnector->Reconnect();
          });
    }

    void Shutdown() FXL_LOCKS_EXCLUDED(mutex_) {
      std::lock_guard<std::mutex> lock(mutex_);
      shutdown_ = true;
      service_hub_reconnector_->Shutdown();
      service_reconnector_->Shutdown();
    }

   public:
    // ServiceHubConnectorInner should not be moved or copied
    ServiceHubConnectorInner(const ServiceHubConnectorInner&) = delete;
    ServiceHubConnectorInner& operator=(const ServiceHubConnectorInner&) = delete;
    ServiceHubConnectorInner(ServiceHubConnectorInner&&) = delete;
    ServiceHubConnectorInner& operator=(ServiceHubConnectorInner&&) = delete;

    void Do(DoCallback&& callback) FXL_LOCKS_EXCLUDED(mutex_) {
      auto cb = std::make_shared<DoCallback>(std::move(callback));
      InnerDo(cb);
    }

   private:
    void InnerDo(std::shared_ptr<DoCallback> callback) FXL_LOCKS_EXCLUDED(mutex_) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (callbacks_in_flight_ >= max_queued_callbacks_) {
          FX_LOGS_FIRST_N(WARNING, 10)
              << "Callback dropped because there are too many callbacks currently in flight";
          return;
        }
        callbacks_in_flight_ += 1;
      }
      service_reconnector_->Do([callback, resolver = DoResolver(get_this(), callback)](
                                   fidl::Client<Service>& service) mutable {
        (*callback)(service, std::move(resolver));
      });
    }

    void DoComplete() FXL_LOCKS_EXCLUDED(mutex_) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (callbacks_in_flight_ == 0) {
        FX_LOGS(ERROR) << "More callbacks have been completed than were queued.";
        return;
      }
      callbacks_in_flight_ -= 1;
    }

    void RetryDo(std::shared_ptr<DoCallback> callback) FXL_LOCKS_EXCLUDED(mutex_) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (shutdown_) {
        FX_LOGS(INFO) << "Ignoring retry while shutdown in progress";
        return;
      }
      async::PostDelayedTask(
          dispatcher_,
          [weak_this = get_this(), callback]() {
            if (auto shared_this = weak_this.lock()) {
              shared_this->InnerDo(callback);
            }
          },
          backoff_.GetNext());
    }

    async_dispatcher_t* const dispatcher_;
    const size_t max_queued_callbacks_;

    std::shared_ptr<ServiceReconnector<ServiceHub>> service_hub_reconnector_;
    std::shared_ptr<ServiceReconnector<Service>> service_reconnector_;

    std::mutex mutex_;
    bool shutdown_ FXL_GUARDED_BY(mutex_) = false;
    backoff::ExponentialBackoff backoff_ FXL_GUARDED_BY(mutex_);
    size_t callbacks_in_flight_ FXL_GUARDED_BY(mutex_) = 0;
  };

 protected:
  // ConnectToServiceHub is used to get a handle for the service hub.
  virtual void ConnectToServiceHub(ServiceHubConnectResolver resolver) = 0;

  // ConnectToService is used once the factory service has been connected.
  virtual void ConnectToService(fidl::Client<ServiceHub>& service_hub,
                                ServiceConnectResolver resolver) = 0;

 public:
  // DoResolver is used to notify the ServiceHubConnector when a call is done, and if it should be
  // retried.
  //
  // If the DoCallback should be retried, resolve should be called with true, otherwise it should be
  // called with false. If the DoResolver is dropped before calling resolve, it will implicitly
  // resolve with false (no retry).
  class DoResolver {
   private:
    explicit DoResolver(std::weak_ptr<ServiceHubConnectorInner> connector,
                        std::shared_ptr<DoCallback> cb)
        : cb_(cb), connector_(std::move(connector)) {}
    friend class ServiceHubConnector;

   public:
    // DoResolver should be move-only.
    DoResolver(const DoResolver&) = delete;
    DoResolver& operator=(const DoResolver&) = delete;
    DoResolver(DoResolver&& other) noexcept { MoveImpl(std::move(other)); }
    DoResolver& operator=(DoResolver&& other) noexcept {
      if (this != &other) {
        MoveImpl(std::move(other));
      }
      return *this;
    }

    ~DoResolver() noexcept { resolve(false); }

    // Resolve the current Do call.
    //
    // Note: if resolve is called multiple times on DoResolver, only the first call will be handled,
    //       and all future calls will be ignored.
    void resolve(bool should_retry) {
      if (!resolved_) {
        if (should_retry) {
          if (auto connector = connector_.lock()) {
            connector->RetryDo(cb_);
          }
        } else {
          if (auto connector = connector_.lock()) {
            connector->DoComplete();
          }
        }
        resolved_ = true;
      }
    }

   private:
    // Moves the DoResolver to a new location.
    //
    // Note: Since we want to ensure that the resolver only resolves once, the source resolver
    //       `other` is manually set to `resolved_` = true, so that when it is destroyed, it doesn't
    //       resolve.
    void MoveImpl(DoResolver&& other) noexcept {
      connector_ = std::move(other.connector_);
      cb_ = std::move(other.cb_);
      resolved_ = other.resolved_;
      other.resolved_ = true;
    }

    bool resolved_ = false;
    std::shared_ptr<DoCallback> cb_;
    std::weak_ptr<ServiceHubConnectorInner> connector_;
  };

  // The |Do()| method is the only way of performing actions using the underlying |Protocol|. This
  // method must be called from the dispatcher thread.
  //
  // It is recommended for classes that extend ServiceHubConnector create wrapper functions to
  // ease the calling of this method e.g.:
  //
  //     void DoAction() {
  //       Do([](fidl::Client<Protocol>& protocol, DoResolver resolver) {
  //         protocol->DoAction().Then(
  //             [resolver = std::move(resolver)](
  //                 fidl::Result<test_protocol::Protocol::DoAction>& status) mutable {
  //               resolver.resolve(status.is_error() &&
  //                                (status.error_value().is_framework_error() ||
  //                                status.error_value().domain_error() == Error::kTransient));
  //             });
  //       });
  //     }
  void Do(DoCallback&& cb) { inner_->Do(std::move(cb)); }

  // |dispatcher| the dispatcher thread where the fidl services should be connected from.
  // |max_queued_callbacks| (default: 20) How many callbacks should each ServiceReconnector cache
  //                        before rejecting new ones.
  explicit ServiceHubConnector(async_dispatcher_t* dispatcher, size_t max_queued_callbacks = 20)
      : inner_(new ServiceHubConnectorInner(dispatcher, max_queued_callbacks)) {
    inner_->Setup(
        [this](ServiceHubConnectResolver resolver) { ConnectToServiceHub(std::move(resolver)); },
        [this](fidl::Client<ServiceHub>& service_hub, ServiceConnectResolver resolver) {
          ConnectToService(service_hub, std::move(resolver));
        },
        max_queued_callbacks);
  }

  virtual ~ServiceHubConnector() {
    if (inner_) {
      inner_->Shutdown();
    }
  }

  // ServiceHubConnector should not be copy or movable.
  ServiceHubConnector(const ServiceHubConnector&) = delete;
  ServiceHubConnector& operator=(const ServiceHubConnector&) = delete;
  ServiceHubConnector(ServiceHubConnector&& other) = delete;
  ServiceHubConnector& operator=(ServiceHubConnector&& other) = delete;

 private:
  std::shared_ptr<ServiceHubConnectorInner> inner_;
};
}  // namespace fidl::contrib

#endif  // SRC_LIB_FIDL_CPP_CONTRIB_CONNECTION_SERVICE_HUB_CONNECTOR_H_
