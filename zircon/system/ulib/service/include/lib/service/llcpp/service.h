// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SERVICE_LLCPP_SERVICE_H_
#define LIB_SERVICE_LLCPP_SERVICE_H_

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fidl/llcpp/connect_service.h>
#include <lib/fidl/llcpp/string_view.h>
#include <lib/fit/result.h>
#include <lib/service/llcpp/constants.h>
#include <lib/stdcompat/string_view.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>

#include <utility>

namespace service {

// Opens the directory containing incoming services in the application's default
// incoming namespace. By default the path is "/svc". Users may specify a custom path.
::zx::status<::fidl::ClientEnd<llcpp::fuchsia::io::Directory>> OpenServiceRoot(
    const char* path = llcpp::sys::kServiceDirectory);

namespace internal {

// Implementation of |service::Connect| that is independent from the actual |Protocol|.
::zx::status<zx::channel> ConnectRaw(const char* path);

// Implementation of |service::ConnectAt| that is independent from the actual |Protocol|.
::zx::status<zx::channel> ConnectAtRaw(
    ::fidl::UnownedClientEnd<llcpp::fuchsia::io::Directory> svc_dir, const char* protocol_name);

template <size_t... I>
using seq = std::integer_sequence<size_t, I...>;
template <size_t N>
using make_seq = std::make_integer_sequence<size_t, N>;

template <const char*, typename, const char*, typename>
struct concat;
template <const char* s1, size_t... i1, const char* s2, size_t... i2>
struct concat<s1, seq<i1...>, s2, seq<i2...>> {
  // |s1| followed by |s2| followed by trailing NUL.
  static constexpr const char value[]{s1[i1]..., s2[i2]..., 0};
};

template <size_t N>
constexpr size_t string_length(const char (&literal)[N], size_t size = N - 1) {
  return size;
}

// Returns the default path for a protocol in the `/svc/{name}` format,
// where `{name}` is the fully qualified name of the FIDL protocol.
// The string concatentation happens at compile time.
template <typename Protocol>
constexpr const char* DefaultPath() {
  constexpr auto svc_length = string_length(llcpp::sys::kServiceDirectoryTrailingSlash);
  constexpr auto protocol_length = string_length(Protocol::Name);
  return concat<llcpp::sys::kServiceDirectoryTrailingSlash, make_seq<svc_length>, Protocol::Name,
                make_seq<protocol_length>>::value;
}

}  // namespace internal

// Typed channel wrapper around |fdio_service_connect|.
//
// Connects to the |Protocol| protocol in the default namespace for the current
// process. |path| defaults to `/svc/{name}`, where `{name}` is the fully
// qualified name of the FIDL protocol. The path may be overridden to
// a custom value.
//
// See documentation on |fdio_service_connect| for details.
template <typename Protocol>
::zx::status<::fidl::ClientEnd<Protocol>> Connect(
    const char* path = internal::DefaultPath<Protocol>()) {
  auto channel = internal::ConnectRaw(path);
  if (channel.is_error()) {
    return channel.take_error();
  }
  return ::zx::ok(::fidl::ClientEnd<Protocol>(std::move(channel.value())));
}

// Typed channel wrapper around |fdio_service_connect_at|.
//
// Connects to the |Protocol| protocol relative to the |svc_dir| directory.
// |protocol_name| defaults to the fully qualified name of the FIDL protocol,
// but may be overridden to a custom value.
//
// See documentation on |fdio_service_connect_at| for details.
template <typename Protocol>
::zx::status<::fidl::ClientEnd<Protocol>> ConnectAt(
    ::fidl::UnownedClientEnd<llcpp::fuchsia::io::Directory> svc_dir,
    const char* protocol_name = Protocol::Name) {
  auto channel = internal::ConnectAtRaw(svc_dir, protocol_name);
  if (channel.is_error()) {
    return channel.take_error();
  }
  return ::zx::ok(::fidl::ClientEnd<Protocol>(std::move(channel.value())));
}

}  // namespace service

namespace llcpp::sys {

// Opens a connection to the default instance of a FIDL service of type `FidlService`, rooted at
// `dir`. The default instance is called 'default'. See
// `OpenServiceAt(zx::unowned_channel,fidl::StringView)` for details.
template <typename FidlService>
::zx::status<typename FidlService::ServiceClient> OpenServiceAt(
    ::fidl::UnownedClientEnd<llcpp::fuchsia::io::Directory> dir);

// Opens a connection to the given instance of a FIDL service of type `FidlService`, rooted at
// `dir`. The result, if successful, is a `FidlService::ServiceClient` that exposes methods that
// connect to the various members of the FIDL service.
//
// If the service or instance does not exist, the resulting `FidlService::ServiceClient` will fail
// to connect to a member.
//
// Returns a zx::status of status Ok on success. In the event of failure, an error status variant
// is returned, set to an error value.
//
// Returns a zx::status of state Error set to ZX_ERR_INVALID_ARGS if `instance` is more than 255
// characters long.
//
// ## Example
//
// ```C++
// using Echo = ::llcpp::fuchsia::echo::Echo;
// using EchoService = ::llcpp::fuchsia::echo::EchoService;
//
// zx::status<EchoService::ServiceClient> open_result =
//     sys::OpenServiceAt<EchoService>(std::move(svc_));
// ASSERT_TRUE(open_result.is_ok());
//
// EchoService::ServiceClient service = open_result.take_value();
//
// zx::status<fidl::ClientEnd<Echo>> connect_result = service.ConnectFoo();
// ASSERT_TRUE(connect_result.is_ok());
//
// Echo::SyncClient client = fidl::BindSyncClient(connect_result.take_value());
// ```
template <typename FidlService>
::zx::status<typename FidlService::ServiceClient> OpenServiceAt(
    ::fidl::UnownedClientEnd<::llcpp::fuchsia::io::Directory> dir, cpp17::string_view instance);

// Opens a connection to the given instance of a FIDL service with the name `service_name`, rooted
// at `dir`. The `remote` channel is passed to the remote service, and its local twin can be used to
// issue FIDL protocol messages. Most callers will want to use `OpenServiceAt(...)`.
//
// If the service or instance does not exist, the `remote` channel will be closed.
//
// Returns ZX_OK on success. In the event of failure, an error value is returned.
//
// Returns ZX_ERR_INVALID_ARGS if `service_path` or `instance` are more than 255 characters long.
::zx::status<> OpenNamedServiceAt(::fidl::UnownedClientEnd<::llcpp::fuchsia::io::Directory> dir,
                                  cpp17::string_view service_path, cpp17::string_view instance,
                                  ::zx::channel remote);

namespace internal {

// The internal |DirectoryOpenFunc| needs to take raw Zircon channels,
// because the FIDL runtime that interfaces with it cannot depend on the
// |fuchsia.io| FIDL library. See <lib/fidl/llcpp/connect_service.h>.
::zx::status<> DirectoryOpenFunc(::zx::unowned_channel dir, ::fidl::StringView path,
                                 ::zx::channel remote);

}  // namespace internal

template <typename FidlService>
::zx::status<typename FidlService::ServiceClient> OpenServiceAt(
    ::fidl::UnownedClientEnd<::llcpp::fuchsia::io::Directory> dir, cpp17::string_view instance) {
  ::zx::channel local, remote;
  if (zx_status_t status = ::zx::channel::create(0, &local, &remote); status != ZX_OK) {
    return ::zx::error(status);
  }

  ::zx::status<> result = OpenNamedServiceAt(dir, FidlService::Name, instance, std::move(remote));
  if (result.is_error()) {
    return result.take_error();
  }
  return ::zx::ok(
      typename FidlService::ServiceClient(std::move(local), internal::DirectoryOpenFunc));
}

template <typename FidlService>
::zx::status<typename FidlService::ServiceClient> OpenServiceAt(
    ::fidl::UnownedClientEnd<::llcpp::fuchsia::io::Directory> dir) {
  return OpenServiceAt<FidlService>(dir, kDefaultInstance);
}

}  // namespace llcpp::sys

#endif  // LIB_SERVICE_LLCPP_SERVICE_H_
