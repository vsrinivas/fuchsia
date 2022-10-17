// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/driver_compat/context.h>

namespace compat {

void Context::ConnectAndCreate(driver::DriverContext* driver_context,
                               async_dispatcher_t* dispatcher,
                               fit::callback<void(zx::result<std::unique_ptr<Context>>)> callback) {
  auto context = std::make_unique<Context>();

  // Connect to our parent.
  auto result = component::ConnectAt<fuchsia_driver_compat::Service::Device>(
      driver_context->incoming()->svc_dir());
  if (result.is_error()) {
    return callback(result.take_error());
  }
  context->parent_device_.Bind(std::move(result.value()), dispatcher);

  // Connect to DevfsExporter.
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (endpoints.is_error()) {
    return callback(endpoints.take_error());
  }
  auto status = driver_context->outgoing()->Serve(std::move(endpoints->server));
  if (status.is_error()) {
    return callback(status.take_error());
  }
  auto svc_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (svc_endpoints.is_error()) {
    return callback(svc_endpoints.take_error());
  }
  auto response =
      fidl::WireCall(endpoints->client)
          ->Open(fuchsia_io::wire::OpenFlags::kRightReadable |
                     fuchsia_io::wire::OpenFlags::kRightWritable,
                 0, "svc/", fidl::ServerEnd<fuchsia_io::Node>(svc_endpoints->server.TakeHandle()));
  if (response.status() != ZX_OK) {
    return callback(zx::error(response.status()));
  }

  auto exporter = driver::DevfsExporter::Create(
      *driver_context->incoming(), dispatcher,
      fidl::WireSharedClient(std::move(svc_endpoints->client), dispatcher));
  if (exporter.is_error()) {
    return callback(zx::error(exporter.error_value()));
  }
  context->devfs_exporter_ = std::move(*exporter);

  // Get the topological path.
  auto context_ptr = context.get();
  context_ptr->parent_device_->GetTopologicalPath().Then(
      [context = std::move(context), callback = std::move(callback)](auto& result) mutable {
        context->parent_topological_path_ = std::move(result->path());
        callback(zx::ok(std::move(context)));
      });
}

std::string Context::TopologicalPath(std::string_view relative_child_path) const {
  std::string path = parent_topological_path_;
  path.append("/").append(relative_child_path);
  return path;
}

}  // namespace compat
