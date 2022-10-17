// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_CONNECT_SERVICE_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_CONNECT_SERVICE_H_

#include <lib/fidl/cpp/wire/channel.h>
#include <lib/fidl/cpp/wire/internal/transport.h>
#include <lib/fidl/cpp/wire/string_view.h>
#include <lib/fidl/cpp/wire/sync_call.h>
#include <zircon/fidl.h>

#ifdef __Fuchsia__
#include <lib/zx/channel.h>
#include <lib/zx/status.h>
#endif  // __Fuchsia__

namespace fidl {

#ifdef __Fuchsia__

namespace internal {

// The method signature required to implement the method that issues the Directory::Open
// FIDL call for a Service's member protocol.
using ConnectMemberFunc = zx::result<> (*)(zx::unowned_channel service_dir,
                                           fidl::StringView member_name,
                                           fidl::internal::AnyTransport channel);

}  // namespace internal

#endif  // __Fuchsia__

namespace internal {
// This struct template is specialized in generated bindings to include the following
// protocol-specific members:
//  - static constexpr char DiscoverableName[] - the discoverable name if any exists.
template <typename Protocol>
struct ProtocolDetails;

// Helper type for compile-time string concatenation.
template <const char*, typename>
struct default_service_path;
template <const char* n, size_t... i>
struct default_service_path<n, std::integer_sequence<size_t, i...>> {
  static constexpr const char value[]{'/', 's', 'v', 'c', '/', n[i]...};
};
}  // namespace internal

// DiscoverableProtocolName<Protocol> evaluates to a string containing the name of the protocol,
// including its library.
template <typename Protocol>
constexpr const char* DiscoverableProtocolName =
    fidl::internal::ProtocolDetails<Protocol>::DiscoverableName;

// DiscoverableProtocolDefaultPath<Protocol> evaluates to a string containing the default path for
// the protocol endpoint, something like "/svc/fuchsia.library.Protocol".
template <typename Protocol>
constexpr const char* DiscoverableProtocolDefaultPath = fidl::internal::default_service_path<
    fidl::internal::ProtocolDetails<Protocol>::DiscoverableName,
    std::make_integer_sequence<
        size_t, sizeof(fidl::internal::ProtocolDetails<Protocol>::DiscoverableName)>>::value;

}  // namespace fidl

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_CONNECT_SERVICE_H_
