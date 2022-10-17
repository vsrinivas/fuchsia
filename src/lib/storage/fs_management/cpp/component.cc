// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/fs_management/cpp/component.h"

#include <fidl/fuchsia.component/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/sys/component/cpp/service_client.h>

namespace fs_management {

zx::result<fidl::ClientEnd<fuchsia_io::Directory>> ConnectFsComponent(
    std::string_view component_url, std::string_view component_child_name,
    std::optional<std::string_view> component_collection_name) {
  auto realm_client_end = component::Connect<fuchsia_component::Realm>();
  if (realm_client_end.is_error())
    return realm_client_end.take_error();
  fidl::WireSyncClient realm{std::move(*realm_client_end)};

  fidl::ClientEnd<fuchsia_io::Directory> client_end;
  fuchsia_component_decl::wire::ChildRef child_ref{
      .name = fidl::StringView::FromExternal(component_child_name)};
  if (component_collection_name)
    child_ref.collection = fidl::StringView::FromExternal(*component_collection_name);

  auto exposed_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (exposed_endpoints.is_error())
    return exposed_endpoints.take_error();
  auto open_exposed_res = realm->OpenExposedDir(child_ref, std::move(exposed_endpoints->server));
  if (!open_exposed_res.ok())
    return zx::error(open_exposed_res.status());

  if (open_exposed_res->is_ok()) {
    client_end = std::move(exposed_endpoints->client);
  } else if (open_exposed_res->error_value() == fuchsia_component::wire::Error::kInstanceNotFound) {
    if (!component_collection_name)
      return zx::error(ZX_ERR_NOT_FOUND);

    // If the error was INSTANCE_NOT_FOUND, and it's expected to be in a collection, try launching
    // the component ourselves.
    fidl::Arena allocator;
    fuchsia_component_decl::wire::CollectionRef collection_ref{
        .name = fidl::StringView::FromExternal(*component_collection_name)};
    auto child_decl = fuchsia_component_decl::wire::Child::Builder(allocator)
                          .name(component_child_name)
                          .url(component_url)
                          .startup(fuchsia_component_decl::wire::StartupMode::kLazy)
                          .Build();
    fuchsia_component::wire::CreateChildArgs child_args;
    auto create_res = realm->CreateChild(collection_ref, child_decl, child_args);
    if (!create_res.ok())
      return zx::error(create_res.status());
    if (create_res->is_error())
      return zx::error(ZX_ERR_INVALID_ARGS);

    auto exposed_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    if (exposed_endpoints.is_error())
      return exposed_endpoints.take_error();
    auto open_exposed_res = realm->OpenExposedDir(child_ref, std::move(exposed_endpoints->server));
    if (!open_exposed_res.ok()) {
      return zx::error(open_exposed_res.status());
    }
    if (open_exposed_res->is_error())
      return zx::error(ZX_ERR_INVALID_ARGS);

    client_end = std::move(exposed_endpoints->client);
  } else {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  return zx::ok(std::move(client_end));
}

zx::result<> DestroyFsComponent(std::string_view component_child_name,
                                std::string_view component_collection_name) {
  auto realm_client_end = component::Connect<fuchsia_component::Realm>();
  if (realm_client_end.is_error())
    return realm_client_end.take_error();
  fidl::WireSyncClient realm{std::move(*realm_client_end)};

  fuchsia_component_decl::wire::ChildRef child_ref{
      .name = fidl::StringView::FromExternal(component_child_name),
      .collection = fidl::StringView::FromExternal(component_collection_name),
  };

  auto res = realm->DestroyChild(child_ref);
  if (!res.ok())
    return zx::error(res.status());
  // If the instance was not found, that's fine. We "destroyed" it.
  if (res->is_error() && res->error_value() != fuchsia_component::wire::Error::kInstanceNotFound)
    return zx::error(ZX_ERR_INVALID_ARGS);

  return zx::ok();
}

}  // namespace fs_management
