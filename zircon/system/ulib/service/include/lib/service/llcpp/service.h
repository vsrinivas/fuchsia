// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SERVICE_LLCPP_SERVICE_H_
#define LIB_SERVICE_LLCPP_SERVICE_H_

#include <lib/fidl/llcpp/connect_service.h>
#include <lib/fidl/llcpp/string_view.h>
#include <lib/fit/result.h>
#include <lib/fit/string_view.h>
#include <lib/service/llcpp/constants.h>
#include <lib/zx/channel.h>

namespace llcpp::sys {

// Opens a connection to the default instance of a FIDL service of type `FidlService`, rooted at
// `dir`. The default instance is called 'default'. See
// `OpenServiceAt(zx::unowned_channel,fidl::StringView)` for details.
template <typename FidlService>
::fidl::result<typename FidlService::ServiceClient> OpenServiceAt(::zx::unowned_channel dir);

// Opens a connection to the given instance of a FIDL service of type `FidlService`, rooted at
// `dir`. The result, if successful, is a `FidlService::ServiceClient` that exposes methods that
// connect to the various members of the FIDL service.
//
// If the service or instance does not exist, the resulting `FidlService::ServiceClient` will fail
// to connect to a member.
//
// Returns a fidl::result of status Ok on success. In the event of failure, a fidl::result of Error
// is returned, set to an error value.
//
// Returns a fidl::result of state Error set to ZX_ERR_INVALID_ARGS if `instance` is more than 255
// characters long.
//
// ## Example
//
// ```C++
// using Echo = ::llcpp::fuchsia::echo::Echo;
// using EchoService = ::llcpp::fuchsia::echo::EchoService;
//
// fidl::result<EchoService::ServiceClient> open_result =
//     sys::OpenServiceAt<EchoService>(std::move(svc_));
// ASSERT_TRUE(open_result.is_ok());
//
// EchoService::ServiceClient service = open_result.take_value();
//
// fidl::result<fidl::ClientChannel<Echo>> connect_result = service.ConnectFoo();
// ASSERT_TRUE(connect_result.is_ok());
//
// Echo::SyncClient client = fidl::BindSyncClient(connect_result.take_value());
// ```
template <typename FidlService>
::fidl::result<typename FidlService::ServiceClient> OpenServiceAt(::zx::unowned_channel dir,
                                                                  fit::string_view instance);

// Opens a connection to the given instance of a FIDL service with the name `service_name`, rooted
// at `dir`. The `remote` channel is passed to the remote service, and its local twin can be used to
// issue FIDL protocol messages. Most callers will want to use `OpenServiceAt(...)`.
//
// If the service or instance does not exist, the `remote` channel will be closed.
//
// Returns ZX_OK on success. In the event of failure, an error value is returned.
//
// Returns ZX_ERR_INVALID_ARGS if `service_path` or `instance` are more than 255 characters long.
zx_status_t OpenNamedServiceAt(::zx::unowned_channel dir, fit::string_view service_path,
                               fit::string_view instance, ::zx::channel remote);

namespace internal {

zx_status_t DirectoryOpenFunc(::zx::unowned_channel dir, ::fidl::StringView path,
                              ::zx::channel remote);

}  // namespace internal

template <typename FidlService>
::fidl::result<typename FidlService::ServiceClient> OpenServiceAt(::zx::unowned_channel dir,
                                                                  fit::string_view instance) {
  ::zx::channel local, remote;
  zx_status_t result = ::zx::channel::create(0, &local, &remote);
  if (result != ZX_OK) {
    return fit::error(result);
  }

  result = OpenNamedServiceAt(std::move(dir), FidlService::Name, instance, std::move(remote));
  if (result != ZX_OK) {
    return fit::error(result);
  }
  return fit::ok(
      typename FidlService::ServiceClient(std::move(local), internal::DirectoryOpenFunc));
}

template <typename FidlService>
::fidl::result<typename FidlService::ServiceClient> OpenServiceAt(::zx::unowned_channel dir) {
  return OpenServiceAt<FidlService>(std::move(dir), kDefaultInstance);
}

}  // namespace llcpp::sys

#endif  // LIB_SERVICE_LLCPP_SERVICE_H_
