// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/component/incoming/cpp/constants.h>
#include <lib/svc/dir.h>
#include <lib/sys/component/cpp/handlers.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/zx/result.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/types.h>

#include <memory>
#include <sstream>
#include <string>

namespace {

// Path delimiter used by svc library.
constexpr const char kSvcPathDelimiter[] = "/";

constexpr const char kOutgoingDirectoryThreadSafetyDescription[] =
    "|component::OutgoingDirectory| is thread-unsafe.";

}  // namespace

namespace component {

OutgoingDirectory OutgoingDirectory::Create(async_dispatcher_t* dispatcher) {
  ZX_ASSERT_MSG(dispatcher != nullptr, "OutgoingDirectory::Create received nullptr |dispatcher|.");

  svc_dir_t* root = nullptr;
  // It's safe to ignore return value here since the function always returns
  // ZX_OK.
#if __Fuchsia_API_level__ >= 10
  (void)svc_directory_create(&root);
#else
  (void)svc_dir_create_without_serve(&root);
#endif

  return OutgoingDirectory(dispatcher, root);
}

OutgoingDirectory::OutgoingDirectory(async_dispatcher_t* dispatcher, svc_dir_t* root)
    : inner_(std::make_unique<Inner>(dispatcher, root)) {}

OutgoingDirectory::Inner::Inner(async_dispatcher_t* dispatcher, svc_dir_t* root)
    : dispatcher_(dispatcher),
      checker_(dispatcher, kOutgoingDirectoryThreadSafetyDescription),
      root_(root),
      unbind_protocol_callbacks_() {
  // TODO(fxbug.dev/113997): Post a task onto the dispatcher to verify that the
  // dispatcher has mutual exclusion guarantees.
}

OutgoingDirectory::OutgoingDirectory(OutgoingDirectory&& other) noexcept = default;

OutgoingDirectory& OutgoingDirectory::operator=(OutgoingDirectory&& other) noexcept = default;

OutgoingDirectory::~OutgoingDirectory() = default;

OutgoingDirectory::Inner::~Inner() {
  std::lock_guard guard(checker_);

  if (root_) {
#if __Fuchsia_API_level__ >= 10
    svc_directory_destroy(root_);
#else
    svc_dir_destroy(root);
#endif
  }
}

zx::result<> OutgoingDirectory::Serve(fidl::ServerEnd<fuchsia_io::Directory> directory_server_end) {
  if (!directory_server_end.is_valid()) {
    return zx::error_result(ZX_ERR_BAD_HANDLE);
  }
  std::lock_guard guard(inner().checker_);

#if __Fuchsia_API_level__ >= 10
  zx_status_t status = svc_directory_serve(inner().root_, inner().dispatcher_,
                                           directory_server_end.TakeHandle().release());
#else
  zx_status_t status = svc_dir_serve(inner().root_, inner().dispatcher_,
                                     directory_server_end.TakeHandle().release());
#endif
  if (status != ZX_OK) {
    return zx::error_result(status);
  }

  return zx::make_result(status);
}

zx::result<> OutgoingDirectory::ServeFromStartupInfo() {
  fidl::ServerEnd<fuchsia_io::Directory> directory_request(
      zx::channel(zx_take_startup_handle(PA_DIRECTORY_REQUEST)));
  return Serve(std::move(directory_request));
}

zx::result<> OutgoingDirectory::AddProtocol(AnyHandler handler, cpp17::string_view name) {
  return AddProtocolAt(std::move(handler), kServiceDirectoryWithNoSlash, name);
}

zx::result<> OutgoingDirectory::AddProtocolAt(AnyHandler handler, cpp17::string_view path,
                                              cpp17::string_view name) {
  std::lock_guard guard(inner().checker_);

  // More thorough path validation is done in |svc_add_service|.
  if (path.empty() || name.empty()) {
    return zx::error_result(ZX_ERR_INVALID_ARGS);
  }

  std::string directory_entry(path);
  std::string protocol_entry(name);
  if (inner().registered_handlers_.count(directory_entry) != 0 &&
      inner().registered_handlers_[directory_entry].count(protocol_entry) != 0) {
    return zx::make_result(ZX_ERR_ALREADY_EXISTS);
  }

  // |svc_dir_add_service_by_path| takes in a void* |context| that is passed to
  // the |handler| callback passed as the last argument to the function call.
  // The context will first be stored in the heap for this path, then a pointer
  // to it will be passed to this function. The callback, in this case |OnConnect|,
  // will then cast the void* type to OnConnectContext*.
  auto context =
      std::make_unique<OnConnectContext>(OnConnectContext{.handler = std::move(handler)});
#if __Fuchsia_API_level__ >= 10
  zx_status_t status =
      svc_directory_add_service(inner().root_, directory_entry.c_str(), directory_entry.size(),
                                name.data(), name.size(), context.get(), OnConnect);
#else
  zx_status_t status = svc_dir_add_service(inner().root_, directory_entry.c_str(), name.data(),
                                           context.get(), OnConnect);
#endif

  auto& directory_handlers = inner().registered_handlers_[directory_entry];
  directory_handlers[protocol_entry] = std::move(context);

  return zx::make_result(status);
}

zx::result<> OutgoingDirectory::AddDirectory(fidl::ClientEnd<fuchsia_io::Directory> remote_dir,
                                             cpp17::string_view directory_name) {
  return AddDirectoryAt(std::move(remote_dir), /*path=*/"", directory_name);
}

zx::result<> OutgoingDirectory::AddDirectoryAt(fidl::ClientEnd<fuchsia_io::Directory> remote_dir,
                                               cpp17::string_view path,
                                               cpp17::string_view directory_name) {
  std::lock_guard guard(inner().checker_);

  if (!remote_dir.is_valid()) {
    return zx::error_result(ZX_ERR_BAD_HANDLE);
  }
  if (directory_name.empty()) {
    return zx::error_result(ZX_ERR_INVALID_ARGS);
  }

#if __Fuchsia_API_level__ >= 10
  zx_status_t status =
      svc_directory_add_directory(inner().root_, path.data(), path.size(), directory_name.data(),
                                  directory_name.size(), remote_dir.TakeChannel().release());
#else
  zx_status_t status = svc_dir_add_directory_by_path(
      inner().root_, path.data(), directory_name.data(), remote_dir.TakeChannel().release());
#endif

  return zx::make_result(status);
}

zx::result<> OutgoingDirectory::AddService(ServiceInstanceHandler handler,
                                           cpp17::string_view service,
                                           cpp17::string_view instance) {
  if (service.empty() || instance.empty()) {
    return zx::error_result(ZX_ERR_INVALID_ARGS);
  }

  auto handlers = handler.TakeMemberHandlers();
  if (handlers.empty()) {
    return zx::make_result(ZX_ERR_INVALID_ARGS);
  }

  std::string basepath = MakePath(service, instance);
  for (auto& [member_name, member_handler] : handlers) {
    zx::result<> status = AddProtocolAt(std::move(member_handler), basepath, member_name);
    if (status.is_error()) {
      // If we encounter an error with any of the instance members, scrub entire
      // directory entry.
      inner().registered_handlers_.erase(basepath);
      return status;
    }
  }

  return zx::ok();
}

zx::result<> OutgoingDirectory::RemoveProtocol(cpp17::string_view name) {
  return RemoveProtocolAt(kServiceDirectoryWithNoSlash, name);
}

zx::result<> OutgoingDirectory::RemoveProtocolAt(cpp17::string_view directory,
                                                 cpp17::string_view name) {
  std::lock_guard guard(inner().checker_);

  std::string key(directory);

  if (inner().registered_handlers_.count(key) == 0) {
    return zx::make_result(ZX_ERR_NOT_FOUND);
  }

  auto& svc_root_handlers = inner().registered_handlers_[key];
  std::string entry_key = std::string(name);
  if (svc_root_handlers.count(entry_key) == 0) {
    return zx::make_result(ZX_ERR_NOT_FOUND);
  }

  // Remove svc_dir_t entry first so that no new connections are attempted on
  // handler after we remove the pointer to it in |svc_root_handlers|.
#if __Fuchsia_API_level__ >= 10
  zx_status_t status = svc_directory_remove_entry(
      inner().root_, kServiceDirectoryWithNoSlash,
      std::char_traits<char>::length(kServiceDirectoryWithNoSlash), name.data(), name.size());
#else
  zx_status_t status =
      svc_dir_remove_service(inner().root_, kServiceDirectoryWithNoSlash, name.data());
#endif
  if (status != ZX_OK) {
    return zx::make_result(status);
  }

  // If teardown is managed, e.g. through |AddProtocol| overload for `fidl::Server<T>*`,
  // then close all active connections.
  UnbindAllConnections(name);

  // Now that all active connections have been closed, and no new connections
  // are being accepted under |name|, it's safe to remove the handlers.
  svc_root_handlers.erase(entry_key);

  return zx::ok();
}

zx::result<> OutgoingDirectory::RemoveService(cpp17::string_view service,
                                              cpp17::string_view instance) {
  std::lock_guard guard(inner().checker_);

  std::string path = MakePath(service, instance);
  if (inner().registered_handlers_.count(path) == 0) {
    return zx::make_result(ZX_ERR_NOT_FOUND);
  }

  // Remove svc_dir_t entry first so that channels close _before_ we remove
  // pointer values out from underneath handlers.
  std::string service_path = std::string(kServiceDirectoryWithNoSlash) +
                             std::string(kSvcPathDelimiter) + std::string(service);
#if __Fuchsia_API_level__ >= 10
  zx_status_t status = svc_directory_remove_entry(
      inner().root_, service_path.c_str(), service_path.size(), instance.data(), instance.size());
#else
  zx_status_t status =
      svc_dir_remove_service_by_path(inner().root_, service_path.c_str(), instance.data());
#endif

  // Now it's safe to remove entry from map.
  inner().registered_handlers_.erase(path);

  return zx::make_result(status);
}

zx::result<> OutgoingDirectory::RemoveDirectory(cpp17::string_view directory_name) {
  return RemoveDirectoryAt(/*path=*/"", directory_name);
}

zx::result<> OutgoingDirectory::RemoveDirectoryAt(cpp17::string_view path,
                                                  cpp17::string_view directory_name) {
  std::lock_guard guard(inner().checker_);

  if (directory_name.empty()) {
    return zx::make_result(ZX_ERR_INVALID_ARGS);
  }

#if __Fuchsia_API_level__ >= 10
  zx_status_t status = svc_directory_remove_entry(inner().root_, path.data(), path.size(),
                                                  directory_name.data(), directory_name.size());
#else
  zx_status_t status =
      svc_dir_remove_entry_by_path(inner().root_, path.data(), directory_name.data());
#endif
  return zx::make_result(status);
}

void OutgoingDirectory::OnConnect(void* raw_context, const char* service_name, zx_handle_t handle) {
  OnConnectContext* context = reinterpret_cast<OnConnectContext*>(raw_context);
  (context->handler)(zx::channel(handle));
}

void OutgoingDirectory::AppendUnbindConnectionCallback(UnbindCallbackMap* unbind_protocol_callbacks,
                                                       const std::string& name,
                                                       UnbindConnectionCallback callback) {
  (*unbind_protocol_callbacks)[name].emplace_back(std::move(callback));
}

void OutgoingDirectory::UnbindAllConnections(cpp17::string_view name) {
  auto key = std::string(name);
  auto iterator = inner().unbind_protocol_callbacks_.find(key);
  if (iterator != inner().unbind_protocol_callbacks_.end()) {
    std::vector<UnbindConnectionCallback>& callbacks = iterator->second;
    for (auto& cb : callbacks) {
      cb();
    }
    inner().unbind_protocol_callbacks_.erase(iterator);
  }
}

std::string OutgoingDirectory::MakePath(cpp17::string_view service, cpp17::string_view instance) {
  std::stringstream path;
  path << kServiceDirectoryWithNoSlash << kSvcPathDelimiter << service << kSvcPathDelimiter
       << instance;
  return path.str();
}

}  // namespace component
