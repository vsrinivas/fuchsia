// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/wire/array.h>
#include <lib/fidl/cpp/wire/connect_service.h>
// #include <lib/fpromise/result.h>
#include <lib/component/cpp/incoming/constants.h>
#include <lib/component/cpp/incoming/service_client.h>

namespace component {

zx::result<fidl::ClientEnd<fuchsia_io::Directory>> OpenServiceRoot(cpp17::string_view path) {
  return component::Connect<fuchsia_io::Directory>(path);
}

namespace internal {

zx::result<zx::channel> ConnectRaw(cpp17::string_view path) {
  zx::channel client_end, server_end;
  if (zx_status_t status = zx::channel::create(0, &client_end, &server_end); status != ZX_OK) {
    return zx::error(status);
  }
  if (zx::result<> status = ConnectRaw(std::move(server_end), path); status.is_error()) {
    return status.take_error();
  }
  return zx::ok(std::move(client_end));
}

zx::result<> ConnectRaw(zx::channel server_end, cpp17::string_view path) {
  if (zx_status_t status = fdio_service_connect(std::string(path).c_str(), server_end.release());
      status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok();
}

zx::result<zx::channel> ConnectAtRaw(fidl::UnownedClientEnd<fuchsia_io::Directory> svc_dir,
                                     cpp17::string_view protocol_name) {
  zx::channel client_end, server_end;
  if (zx_status_t status = zx::channel::create(0, &client_end, &server_end); status != ZX_OK) {
    return zx::error(status);
  }
  if (zx::result<> status = ConnectAtRaw(svc_dir, std::move(server_end), protocol_name);
      status.is_error()) {
    return status.take_error();
  }
  return zx::ok(std::move(client_end));
}

zx::result<> ConnectAtRaw(fidl::UnownedClientEnd<fuchsia_io::Directory> svc_dir,
                          zx::channel server_end, cpp17::string_view protocol_name) {
  if (zx_status_t status = fdio_service_connect_at(
          svc_dir.handle()->get(), std::string(protocol_name).c_str(), server_end.release());
      status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok();
}

zx::result<zx::channel> CloneRaw(zx::unowned_channel&& node) {
  zx::channel client_end, server_end;
  zx_status_t status = zx::channel::create(0, &client_end, &server_end);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  if (zx::result<> status = CloneRaw(std::move(node), std::move(server_end)); status.is_error()) {
    return status.take_error();
  }
  return zx::ok(std::move(client_end));
}

zx::result<> CloneRaw(zx::unowned_channel&& node, zx::channel server_end) {
  if (zx_status_t status = fdio_service_clone_to(node->get(), server_end.release());
      status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok();
}

}  // namespace internal

}  // namespace component

namespace component {

namespace {

constexpr uint64_t kMaxFilename = fuchsia_io::wire::kMaxFilename;

// Max path length will be two path components, separated by a file separator.
constexpr uint64_t kMaxPath = (2 * kMaxFilename) + 1;

zx::result<fidl::StringView> ValidateAndJoinPath(fidl::Array<char, kMaxPath>* buffer,
                                                 fidl::StringView service,
                                                 fidl::StringView instance) {
  if (service.empty() || service.size() > kMaxFilename) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  if (instance.size() > kMaxFilename) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  if (service[0] == '/') {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  const uint64_t path_size = service.size() + instance.size() + 1;
  ZX_ASSERT(path_size <= kMaxPath);

  char* path_cursor = buffer->data();
  memcpy(path_cursor, service.data(), service.size());
  path_cursor += service.size();
  *path_cursor++ = '/';
  memcpy(path_cursor, instance.data(), instance.size());
  return zx::ok(fidl::StringView::FromExternal(buffer->data(), path_size));
}

}  // namespace

namespace internal {

zx::result<> DirectoryOpenFunc(zx::unowned_channel dir, fidl::StringView path,
                               fidl::internal::AnyTransport remote) {
  constexpr fuchsia_io::wire::OpenFlags flags =
      fuchsia_io::wire::OpenFlags::kRightReadable | fuchsia_io::wire::OpenFlags::kRightWritable;
  fidl::UnownedClientEnd<fuchsia_io::Directory> dir_end(dir);
  fidl::ServerEnd<fuchsia_io::Node> node_end(remote.release<fidl::internal::ChannelTransport>());
  fidl::WireResult<fuchsia_io::Directory::Open> result =
      fidl::WireCall<fuchsia_io::Directory>(dir_end)->Open(flags, 0755u, path, std::move(node_end));
  return zx::make_result(result.status());
}

}  // namespace internal

zx::result<> OpenNamedServiceAt(fidl::UnownedClientEnd<fuchsia_io::Directory> dir,
                                std::string_view service, std::string_view instance,
                                zx::channel remote) {
  fidl::Array<char, kMaxPath> path_buffer;
  zx::result<fidl::StringView> path_result =
      ValidateAndJoinPath(&path_buffer, fidl::StringView::FromExternal(service),
                          fidl::StringView::FromExternal(instance));
  if (!path_result.is_ok()) {
    return path_result.take_error();
  }
  return internal::DirectoryOpenFunc(dir.channel(), std::move(path_result.value()),
                                     fidl::internal::MakeAnyTransport(std::move(remote)));
}

}  // namespace component
