// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/svc/dir.h>
#include <lib/sys/component/llcpp/constants.h>
#include <lib/sys/component/llcpp/outgoing_directory.h>
#include <lib/sys/component/llcpp/service_handler.h>
#include <lib/zx/status.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/types.h>

#include <memory>
#include <sstream>

namespace component_llcpp {

OutgoingDirectory::OutgoingDirectory(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

OutgoingDirectory::~OutgoingDirectory() {
  if (root_ != nullptr) {
    svc_dir_destroy(root_);
  }
}

zx::status<> OutgoingDirectory::Serve(fidl::ServerEnd<fuchsia_io::Directory> directory_request) {
  if (root_ != nullptr) {
    return zx::make_status(ZX_ERR_ALREADY_EXISTS);
  }

  if (!directory_request.is_valid()) {
    return zx::make_status(ZX_ERR_BAD_HANDLE);
  }

  zx_status_t status =
      svc_dir_create(dispatcher_, directory_request.TakeHandle().release(), &root_);
  return zx::make_status(status);
}

zx::status<> OutgoingDirectory::ServeFromStartupInfo() {
  fidl::ServerEnd<fuchsia_io::Directory> directory_request(
      zx::channel(zx_take_startup_handle(PA_DIRECTORY_REQUEST)));
  return Serve(std::move(directory_request));
}

zx::status<> OutgoingDirectory::AddNamedService(ServiceHandler handler, cpp17::string_view service,
                                                cpp17::string_view instance) {
  if (root_ == nullptr) {
    return zx::make_status(ZX_ERR_ACCESS_DENIED);
  }

  std::string basepath = MakePath(service, instance);
  if (registered_service_connectors_.count(basepath) != 0) {
    return zx::make_status(ZX_ERR_ALREADY_EXISTS);
  }

  auto connectors = handler.GetMemberConnectors();
  if (connectors.empty()) {
    return zx::make_status(ZX_ERR_INVALID_ARGS);
  }

  std::stack<OnConnectContext> member_contexts;
  for (auto& [member_name, member_handler] : connectors) {
    // |svc_dir_add_service_by_path| takes in a void* |context| that is passed to
    // the |handler| callback passed as the last argument to the function call.
    // The context will first be stored in the heap for this path, then a pointer
    // to it will be passed to this function. The callback, in this case |OnConnect|,
    // will then cast the void* type to OnConnectContext*.
    member_contexts.emplace(OnConnectContext{.connector = std::move(member_handler), .self = this});

    // Retrieve reference to entry added in previous line so that a pointer to
    // it can be passed to |svc_dir_add_service_by_path|.
    auto& context = member_contexts.top();
    zx_status_t status = svc_dir_add_service_by_path(root_, basepath.c_str(), member_name.c_str(),
                                                     reinterpret_cast<void*>(&context), OnConnect);
    if (status != ZX_OK) {
      return zx::make_status(status);
    }
  }

  registered_service_connectors_[basepath] = std::move(member_contexts);

  return zx::ok();
}

zx::status<> OutgoingDirectory::RemoveNamedService(cpp17::string_view service,
                                                   cpp17::string_view instance) {
  if (root_ == nullptr) {
    return zx::make_status(ZX_ERR_ACCESS_DENIED);
  }

  std::string path = MakePath(service, instance);
  if (registered_service_connectors_.count(path) == 0) {
    return zx::make_status(ZX_ERR_NOT_FOUND);
  }

  // Remove svc_dir_t entry first so that channels close _before_ we remove
  // pointer values out from underneath handlers.
  std::string service_path =
      std::string(kServiceDirectory) + std::string(kSvcPathDelimiter) + std::string(service);
  zx_status_t status = svc_dir_remove_service_by_path(root_, service_path.c_str(), instance.data());

  // Now it's safe to remove entry from map.
  registered_service_connectors_.erase(path);

  return zx::make_status(status);
}

void OutgoingDirectory::OnConnect(void* raw_context, const char* service_name, zx_handle_t handle) {
  OnConnectContext* context = reinterpret_cast<OnConnectContext*>(raw_context);
  (context->connector)(zx::channel(handle));
}

std::string OutgoingDirectory::MakePath(cpp17::string_view service, cpp17::string_view instance) {
  std::stringstream path;
  path << kServiceDirectory << kSvcPathDelimiter << service << kSvcPathDelimiter << instance;
  return path.str();
}

}  // namespace component_llcpp
