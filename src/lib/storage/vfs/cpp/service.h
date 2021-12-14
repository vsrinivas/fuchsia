// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_SERVICE_H_
#define SRC_LIB_STORAGE_VFS_CPP_SERVICE_H_

#include <lib/fit/function.h>
#include <lib/fit/traits.h>
#include <lib/zx/channel.h>

#include <fbl/macros.h>

#include "vnode.h"

namespace fs {

// A node which binds a channel to a service implementation when opened.
//
// This class is thread-safe.
class Service : public Vnode {
 public:
  // Construct with fbl::MakeRefCounted.

  // Handler called to bind the provided channel to an implementation of the service.
  using Connector = fit::function<zx_status_t(zx::channel channel)>;

 private:
  // Determines if |T| has a nested type |T::ProtocolType|.
  template <typename, typename = void>
  struct has_protocol_type : public std::false_type {};
  template <typename T>
  struct has_protocol_type<T, std::void_t<typename T::ProtocolType>> : public std::true_type {};
  template <typename T>
  static constexpr inline auto has_protocol_type_v = has_protocol_type<T>::value;

  template <typename T>
  using remove_cvref_t = typename std::remove_reference<typename std::remove_cv<T>::type>::type;

  // Returns if |T| could potentially be a protocol connector:
  // - It is not |Service|.
  // - It cannot be converted to the untyped connector.
  template <typename T>
  static constexpr inline auto maybe_protocol_connector =
      std::conjunction_v<std::negation<std::is_same<remove_cvref_t<T>, Service>>,
                         std::negation<std::is_convertible<T, Connector>>>;

 public:
  // Handler called to bind the provided channel to an implementation of the service. This version
  // is typed to the exact FIDL protocol the handler will support.
  template <typename Protocol>
  using ProtocolConnector = fit::function<zx_status_t(fidl::ServerEnd<Protocol>)>;

  // |Vnode| implementation:
  VnodeProtocolSet GetProtocols() const final;
  zx_status_t GetAttributes(fs::VnodeAttributes* a) final;
  zx_status_t ConnectService(zx::channel channel) final;
  zx_status_t GetNodeInfoForProtocol(VnodeProtocol protocol, Rights rights,
                                     VnodeRepresentation* info) final;
  bool IsService() const override { return true; }

 protected:
  friend fbl::internal::MakeRefCountedHelper<Service>;
  friend fbl::RefPtr<Service>;

  // Creates a service with the specified connector.
  //
  // If the |connector| is null, then incoming connection requests will be dropped.
  explicit Service(Connector connector);

  // Creates a service with the specified connector. This version is typed to the exact FIDL
  // protocol the handler will support:
  //
  //     auto service = fbl::MakeRefCounted<fs::Service>(
  //         [](fidl::ServerEnd<fidl_library::SomeProtocol> server_end) {
  //             // |server_end| speaks the |fidl_library::SomeProtocol| protocol.
  //             // Handle FIDL messages on |server_end|.
  //         });
  //
  // If the |connector| is null, then incoming connection requests will be dropped.
  //
  // The connector should be a callable taking a single |fidl::ServerEnd<ProtocolType>| as argument,
  // and return a |zx_status_t|.
  template <typename Callable, std::enable_if_t<maybe_protocol_connector<Callable>, bool> = true>
  explicit Service(Callable&& connector)
      : Service([connector = std::forward<Callable>(connector)](zx::channel channel) mutable {
          static_assert(
              std::is_same_v<typename fit::callable_traits<remove_cvref_t<Callable>>::return_type,
                             zx_status_t>,
              "The protocol connector should return |zx_status_t|.");
          static_assert(fit::callable_traits<remove_cvref_t<Callable>>::args::size == 1,
                        "The protocol connector should take exactly one argument.");
          using FirstArg =
              typename fit::callable_traits<remove_cvref_t<Callable>>::args::template at<0>;
          static_assert(
              has_protocol_type_v<FirstArg>,
              "The first argument of the protocol connector should be |fidl::ServerEnd<T>|.");

          using Protocol = typename FirstArg::ProtocolType;
          return connector(fidl::ServerEnd<Protocol>(std::move(channel)));
        }) {}

  // Destroys the services and releases its connector.
  ~Service() override;

 private:
  Connector connector_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(Service);
};

}  // namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_SERVICE_H_
