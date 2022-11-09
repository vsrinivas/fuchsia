// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_COMPONENT_CPP_INCOMING_SERVICE_CLIENT_H_
#define LIB_COMPONENT_CPP_INCOMING_SERVICE_CLIENT_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/component/cpp/incoming/constants.h>
#include <lib/fidl/cpp/wire/string_view.h>
#include <lib/zx/channel.h>
#include <lib/zx/result.h>

#include <utility>

namespace component {

// Opens the directory containing incoming services in the application's default
// incoming namespace. By default the path is "/svc". Users may specify a custom path.
zx::result<fidl::ClientEnd<fuchsia_io::Directory>> OpenServiceRoot(
    std::string_view path = component::kServiceDirectory);

namespace internal {

// Implementations of |component::Connect| that is independent from the actual |Protocol|.
zx::result<zx::channel> ConnectRaw(std::string_view path);
zx::result<> ConnectRaw(zx::channel server_end, std::string_view path);

// Implementations of |component::ConnectAt| that is independent from the actual |Protocol|.
zx::result<zx::channel> ConnectAtRaw(fidl::UnownedClientEnd<fuchsia_io::Directory> svc_dir,
                                     std::string_view protocol_name);
zx::result<> ConnectAtRaw(fidl::UnownedClientEnd<fuchsia_io::Directory> svc_dir,
                          zx::channel server_end, std::string_view protocol_name);

// Implementations of |component::Clone| that is independent from the actual |Protocol|.
zx::result<zx::channel> CloneRaw(zx::unowned_channel&& node);
zx::result<> CloneRaw(zx::unowned_channel&& node, zx::channel server_end);

// Determines if |Protocol| contains a method named |Clone|.
// TODO(fxbug.dev/65964): This template is coupled to LLCPP codegen details,
// and as such would need to be adapted when e.g. we change the LLCPP generated
// namespace and hierarchies.
template <typename Protocol, typename = void>
struct has_fidl_method_named_clone : public ::std::false_type {};
template <typename Protocol>
struct has_fidl_method_named_clone<
    Protocol, std::void_t<decltype(fidl::WireRequest<typename Protocol::Clone>{
                  std::declval<fuchsia_io::wire::OpenFlags>() /* flags */,
                  std::declval<fidl::ServerEnd<fuchsia_io::Node>&&>() /* object */})>>
    : public std::true_type {};
template <typename Protocol>
constexpr inline auto has_fidl_method_named_clone_v = has_fidl_method_named_clone<Protocol>::value;

// Determines if |T| is fully defined i.e. |sizeof(T)| can be evaluated.
template <typename T, typename = void>
struct is_complete : public ::std::false_type {};
template <typename T>
struct is_complete<T, std::void_t<std::integral_constant<std::size_t, sizeof(T)>>>
    : public std::true_type {};
template <typename T>
constexpr inline auto is_complete_v = is_complete<T>::value;

enum class AssumeProtocolComposesNodeTag { kAssumeProtocolComposesNode };

template <typename Protocol>
void CheckProtocolForClone(fidl::UnownedClientEnd<Protocol> node, std::nullptr_t tag) {
  static_assert(internal::is_complete_v<Protocol>,
                "|Protocol| must be fully defined in order to use |component::Clone|");
  static_assert(internal::has_fidl_method_named_clone_v<Protocol>,
                "|Protocol| should be or compose the |fuchsia.io/Node| protocol");
}

template <typename Protocol>
void CheckProtocolForClone(fidl::UnownedClientEnd<Protocol> node,
                           AssumeProtocolComposesNodeTag tag) {
  static_assert(!internal::has_fidl_method_named_clone_v<Protocol>,
                "|Protocol| already appears to compose the |fuchsia.io/Node| protocol. "
                "There is no need to specify |AssumeProtocolComposesNode|.");
}
}  // namespace internal

// Gets the relative path to a service member.
//
// This is in the format of:
// `ServiceName/instance/Name`
//
// This relative path can be appended to `/svc/` to construct a full path.
template <typename ServiceMember>
std::string MakeServiceMemberPath(std::string_view instance) {
  static_assert(
      fidl::IsServiceMemberV<ServiceMember>,
      "ServiceMember type must be the Protocol inside of a Service, eg: fuchsia_hardware_pci::Service::Device.");
  return std::string(ServiceMember::ServiceName)
      .append("/")
      .append(instance)
      .append("/")
      .append(ServiceMember::Name);
}

// Typed channel wrapper around |fdio_service_connect|.
//
// Connects to the |Protocol| protocol in the default namespace for the current
// process. |path| defaults to `/svc/{name}`, where `{name}` is the fully
// qualified name of the FIDL protocol. The path may be overridden to
// a custom value.
//
// See documentation on |fdio_service_connect| for details.
template <typename Protocol>
zx::result<fidl::ClientEnd<Protocol>> Connect(
    std::string_view path = fidl::DiscoverableProtocolDefaultPath<Protocol>) {
  auto channel = internal::ConnectRaw(path);
  if (channel.is_error()) {
    return channel.take_error();
  }
  return zx::ok(fidl::ClientEnd<Protocol>(std::move(channel.value())));
}

// Typed channel wrapper around |fdio_service_connect|.
//
// Connects to the |Protocol| protocol in the default namespace for the current
// process. |path| defaults to `/svc/{name}`, where `{name}` is the fully
// qualified name of the FIDL protocol. The path may be overridden to
// a custom value.
//
// See `Connect(std::string_view)` for details.
template <typename Protocol>
zx::result<> Connect(fidl::ServerEnd<Protocol> server_end,
                     std::string_view path = fidl::DiscoverableProtocolDefaultPath<Protocol>) {
  return internal::ConnectRaw(server_end.TakeChannel(), path);
}

// Typed channel wrapper around |fdio_service_connect_at|.
//
// Connects to the |Protocol| protocol relative to the |svc_dir| directory.
// |protocol_name| defaults to the fully qualified name of the FIDL protocol,
// but may be overridden to a custom value.
//
// See `ConnectAt(UnownedClientEnd<fuchsia_io::Directory>, std::string_view)` for
// details.
template <typename Protocol, typename = std::enable_if_t<fidl::IsProtocolV<Protocol>>>
zx::result<fidl::ClientEnd<Protocol>> ConnectAt(
    fidl::UnownedClientEnd<fuchsia_io::Directory> svc_dir,
    std::string_view protocol_name = fidl::DiscoverableProtocolName<Protocol>) {
  auto channel = internal::ConnectAtRaw(svc_dir, protocol_name);
  if (channel.is_error()) {
    return channel.take_error();
  }
  return zx::ok(fidl::ClientEnd<Protocol>(std::move(channel.value())));
}

// Typed channel wrapper around |fdio_service_connect_at|.
//
// Connects |server_end| to the |Protocol| protocol relative to the |svc_dir|
// directory. |protocol_name| defaults to the fully qualified name of the FIDL
// protocol, but may be overridden to a custom value.
//
// See documentation on |fdio_service_connect_at| for details.
template <typename Protocol, typename = std::enable_if_t<fidl::IsProtocolV<Protocol>>>
zx::result<> ConnectAt(fidl::UnownedClientEnd<fuchsia_io::Directory> svc_dir,
                       fidl::ServerEnd<Protocol> server_end,
                       std::string_view protocol_name = fidl::DiscoverableProtocolName<Protocol>) {
  if (zx::result<> status =
          internal::ConnectAtRaw(svc_dir, server_end.TakeChannel(), protocol_name);
      status.is_error()) {
    return status.take_error();
  }
  return zx::ok();
}

// Typed channel wrapper around |fdio_service_connect_at|.
//
// Connects to the |Protocol| protocol relative to the |svc_dir| directory.
// |protocol_name| defaults to the fully qualified name of the FIDL protocol,
// but may be overridden to a custom value.
//
// See `ConnectAt(UnownedClientEnd<fuchsia_io::Directory>, std::string_view)` for
// details.
template <typename ServiceMember,
          typename = std::enable_if_t<fidl::IsServiceMemberV<ServiceMember>>>
zx::result<fidl::ClientEnd<typename ServiceMember::ProtocolType>> ConnectAt(
    fidl::UnownedClientEnd<fuchsia_io::Directory> svc_dir,
    std::string_view instance = kDefaultInstance) {
  auto path = MakeServiceMemberPath<ServiceMember>(instance);
  auto channel = internal::ConnectAtRaw(svc_dir, path.c_str());
  if (channel.is_error()) {
    return channel.take_error();
  }
  return zx::ok(fidl::ClientEnd<typename ServiceMember::ProtocolType>(std::move(channel.value())));
}

// Typed channel wrapper around |fdio_service_connect_at|.
//
// Connects |server_end| to the |Protocol| protocol relative to the |svc_dir|
// directory. |protocol_name| defaults to the fully qualified name of the FIDL
// protocol, but may be overridden to a custom value.
//
// See documentation on |fdio_service_connect_at| for details.
template <typename ServiceMember,
          typename = std::enable_if_t<fidl::IsServiceMemberV<ServiceMember>>>
zx::result<> ConnectAt(fidl::UnownedClientEnd<fuchsia_io::Directory> svc_dir,
                       fidl::ServerEnd<typename ServiceMember::ProtocolType> server_end,
                       std::string_view instance = kDefaultInstance) {
  auto path = MakeServiceMemberPath<ServiceMember>(instance);
  if (zx::result<> status = internal::ConnectAtRaw(svc_dir, server_end.TakeChannel(), path.c_str());
      status.is_error()) {
    return status.take_error();
  }
  return zx::ok();
}

// Passing this value to |component::Clone| implies opting out of any
// compile-time checks the the FIDL protocol supports |fuchsia.io/Node.Clone|.
// This option should be used with care. See documentation on |component::Clone|.
constexpr inline auto AssumeProtocolComposesNode =
    internal::AssumeProtocolComposesNodeTag::kAssumeProtocolComposesNode;

// Typed channel wrapper around |fuchsia.io/Node.Clone|.
//
// Given an unowned client end |node|, returns an owned clone as a new
// connection using protocol request pipelining.
//
// |node| must be a channel that implements the |fuchsia.io/Node| protocol,
// or one that composes such a protocol.
//
// This function looks a little involved due to the template programming; here
// is an example how it could be used:
//
//     // |node| could be |fidl::ClientEnd| or |fidl::UnownedClientEnd|.
//     auto clone = component::Clone(node);
//
// By default, this function will verify that the protocol type supports cloning
// (i.e. it has a FIDL method named "Clone"), which is generally satisfied by
// composing |fuchsia.io/Node|. Under special circumstances, it is possible to
// explicitly state that the protocol actually composes |fuchsia.io/Node| at
// run-time, even though it may not be defined this way in the FIDL schema. This
// could happen as a result of implicit or unsupported multiplexing of FIDL
// protocols. There will not be any compile-time validation that the cloning is
// supported, if the extra |component::AssumeProtocolComposeseNode| argument is
// provided. Note that if the channel does not implement |fuchsia.io/Node.Clone|,
// the remote endpoint of the cloned node will be asynchronously closed.
//
// As such, this override should be used sparingly, and with caution:
//
//     // Assume that |node| supports the |fuchsia.io/Node.Clone| method call.
//     // If that is not the case, there will be runtime failures at a later
//     // stage when |clone| is actually used.
//     auto clone = component::Clone(node, component::AssumeProtocolComposesNode);
//
// See documentation on |fuchsia.io/Node.Clone| for details.
template <typename Protocol, typename Tag = std::nullptr_t,
          typename = std::enable_if_t<
              std::disjunction_v<std::is_same<Tag, std::nullptr_t>,
                                 std::is_same<Tag, internal::AssumeProtocolComposesNodeTag>>>>
zx::result<fidl::ClientEnd<Protocol>> Clone(fidl::UnownedClientEnd<Protocol> node,
                                            Tag tag = nullptr) {
  internal::CheckProtocolForClone(node, tag);
  auto result = internal::CloneRaw(node.channel());
  if (!result.is_ok()) {
    return result.take_error();
  }
  return zx::ok(fidl::ClientEnd<Protocol>(std::move(*result)));
}

// Overload of |component::Clone| to emulate implicit conversion from a
// |const fidl::ClientEnd&| into |fidl::UnownedClientEnd|. C++ cannot consider
// actual implicit conversions when performing template argument deduction.
template <typename Protocol, typename Tag = std::nullptr_t>
zx::result<fidl::ClientEnd<Protocol>> Clone(const fidl::ClientEnd<Protocol>& node,
                                            Tag tag = nullptr) {
  return Clone(node.borrow(), tag);
}

// Typed channel wrapper around |fuchsia.io/Node.Clone|.
//
// Different from |Clone|, this version swallows any synchronous error and will
// return an invalid client-end in those cases. As such, |component::Clone| should
// be preferred over this function.
template <typename Protocol, typename Tag = std::nullptr_t,
          typename = std::enable_if_t<
              std::disjunction_v<std::is_same<Tag, std::nullptr_t>,
                                 std::is_same<Tag, internal::AssumeProtocolComposesNodeTag>>>>
fidl::ClientEnd<Protocol> MaybeClone(fidl::UnownedClientEnd<Protocol> node, Tag tag = nullptr) {
  auto result = Clone(node, tag);
  if (!result.is_ok()) {
    return {};
  }
  return std::move(*result);
}

// Overload of |component::MaybeClone| to emulate implicit conversion from a
// |const fidl::ClientEnd&| into |fidl::UnownedClientEnd|. C++ cannot consider
// actual implicit conversions when performing template argument deduction.
template <typename Protocol, typename Tag = std::nullptr_t>
fidl::ClientEnd<Protocol> MaybeClone(const fidl::ClientEnd<Protocol>& node, Tag tag = nullptr) {
  return MaybeClone(node.borrow(), tag);
}

}  // namespace component

namespace component {

// Opens a connection to the default instance of a FIDL service of type `FidlService`, rooted at
// `dir`. The default instance is called 'default'. See
// `OpenServiceAt(zx::unowned_channel,fidl::StringView)` for details.
template <typename FidlService>
zx::result<typename FidlService::ServiceClient> OpenServiceAt(
    fidl::UnownedClientEnd<fuchsia_io::Directory> dir);

// Opens a connection to the given instance of a FIDL service of type `FidlService`, rooted at
// `dir`. The result, if successful, is a `FidlService::ServiceClient` that exposes methods that
// connect to the various members of the FIDL service.
//
// If the service or instance does not exist, the resulting `FidlService::ServiceClient` will
// fail to connect to a member.
//
// Returns a zx::result of status Ok on success. In the event of failure, an error status
// variant is returned, set to an error value.
//
// Returns a zx::result of state Error set to ZX_ERR_INVALID_ARGS if `instance` is more than 255
// characters long.
//
// ## Example
//
// ```C++
// using Echo = fuchsia_echo::Echo;
// using EchoService = fuchsia_echo::EchoService;
//
// zx::result<EchoService::ServiceClient> open_result =
//     sys::OpenServiceAt<EchoService>(std::move(svc_));
// ASSERT_TRUE(open_result.is_ok());
//
// EchoService::ServiceClient service = open_result.take_value();
//
// zx::result<fidl::ClientEnd<Echo>> connect_result = service.ConnectFoo();
// ASSERT_TRUE(connect_result.is_ok());
//
// fidl::WireSyncClient<Echo> client{connect_result.take_value()};
// ```
template <typename FidlService>
zx::result<typename FidlService::ServiceClient> OpenServiceAt(
    fidl::UnownedClientEnd<fuchsia_io::Directory> dir, std::string_view instance);

// Opens a connection to the given instance of a FIDL service with the name `service_name`,
// rooted
//    at `dir`. The `remote` channel is passed to the remote service, and its local twin can be
// used
// to
// issue FIDL protocol messages. Most callers will want to use `OpenServiceAt(...)`.
//
// If the service or instance does not exist, the `remote` channel will be closed.
//
// Returns ZX_OK on success. In the event of failure, an error value is returned.
//
// Returns ZX_ERR_INVALID_ARGS if `service_path` or `instance` are more than 255
// characters long.
zx::result<> OpenNamedServiceAt(fidl::UnownedClientEnd<fuchsia_io::Directory> dir,
                                std::string_view service_path, std::string_view instance,
                                zx::channel remote);

namespace internal {

// The internal |DirectoryOpenFunc| needs to take raw Zircon channels,
// because the FIDL runtime that interfaces with it cannot depend on the
// |fuchsia.io| FIDL library. See <lib/fidl/llcpp/connect_service.h>.
zx::result<> DirectoryOpenFunc(zx::unowned_channel dir, fidl::StringView path,
                               fidl::internal::AnyTransport remote);

}  // namespace internal

template <typename FidlService>
zx::result<typename FidlService::ServiceClient> OpenServiceAt(
    fidl::UnownedClientEnd<fuchsia_io::Directory> dir, std::string_view instance) {
  zx::channel local, remote;
  if (zx_status_t status = zx::channel::create(0, &local, &remote); status != ZX_OK) {
    return zx::error(status);
  }

  zx::result<> result = OpenNamedServiceAt(dir, FidlService::Name, instance, std::move(remote));
  if (result.is_error()) {
    return result.take_error();
  }
  return zx::ok(typename FidlService::ServiceClient(std::move(local), internal::DirectoryOpenFunc));
}

template <typename FidlService>
zx::result<typename FidlService::ServiceClient> OpenServiceAt(
    fidl::UnownedClientEnd<fuchsia_io::Directory> dir) {
  return OpenServiceAt<FidlService>(dir, kDefaultInstance);
}

}  // namespace component

#endif  // LIB_COMPONENT_CPP_INCOMING_SERVICE_CLIENT_H_
