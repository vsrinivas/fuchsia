// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "adb-file-sync.h"

#include <fuchsia/io/cpp/fidl.h>
#include <lib/component/incoming/cpp/service_client.h>
#include <lib/fdio/fdio.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/syslog/cpp/macros.h>

#include <vector>

#include "src/developer/adb/third_party/adb-file-sync/file_sync_service.h"
#include "src/developer/adb/third_party/adb-file-sync/util.h"

namespace adb_file_sync {

zx_status_t AdbFileSync::StartService(std::optional<std::string> default_component) {
  FX_LOGS(DEBUG) << "Starting ADB File Sync Service";
  std::unique_ptr file_sync = std::make_unique<AdbFileSync>(std::move(default_component));

  component::OutgoingDirectory outgoing =
      component::OutgoingDirectory::Create(file_sync->loop_.dispatcher());
  zx::result result = outgoing.ServeFromStartupInfo();
  if (result.is_error()) {
    FX_LOGS(ERROR) << "Failed to serve outgoing directory: " << result.status_string();
    return result.error_value();
  }

  result = outgoing.AddProtocol<fuchsia_hardware_adb::Provider>(
      [file_sync_ptr =
           file_sync.get()](fidl::ServerEnd<fuchsia_hardware_adb::Provider> server_end) {
        file_sync_ptr->binding_ref_.emplace(fidl::BindServer(file_sync_ptr->loop_.dispatcher(),
                                                             std::move(server_end), file_sync_ptr,
                                                             std::mem_fn(&AdbFileSync::OnUnbound)));
      });
  if (result.is_error()) {
    FX_LOGS(ERROR) << "Could not publish service " << result.error_value();
    return result.error_value();
  }

  auto endpoints = fidl::CreateEndpoints<fuchsia_sys2::RealmQuery>();
  if (endpoints.is_error()) {
    FX_LOGS(ERROR) << "Could not create endpoints " << endpoints.error_value();
    return endpoints.error_value();
  }
  auto status = file_sync->context_->svc()->Connect("fuchsia.sys2.RealmQuery.root",
                                                    endpoints->server.TakeChannel());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not connect to cache RealmQuery " << status;
    return status;
  }
  file_sync->realm_query_.Bind(std::move(endpoints->client));
  file_sync->loop_.JoinThreads();
  return ZX_OK;
}

void AdbFileSync::OnUnbound(fidl::UnbindInfo info,
                            fidl::ServerEnd<fuchsia_hardware_adb::Provider> server_end) {
  if (info.is_user_initiated()) {
    return;
  }
  if (info.is_peer_closed()) {
    // If the peer (the client) closed their endpoint, log that as DEBUG.
    FX_LOGS(DEBUG) << "Client disconnected";
  } else {
    // Treat other unbind causes as errors.
    FX_LOGS(ERROR) << "Server error: " << info;
  }
}

void AdbFileSync::ConnectToService(
    fuchsia_hardware_adb::wire::ProviderConnectToServiceRequest* request,
    ConnectToServiceCompleter::Sync& completer) {
  completer.Reply(fit::ok());
  file_sync_service(this, std::move(request->socket));
}

zx::result<zx::channel> AdbFileSync::ConnectToComponent(std::string name,
                                                        std::vector<std::string>* out_path) {
  const std::string kDeliminator = "::";

  // Parse component moniker
  const auto component_path = split_string(name, kDeliminator);
  std::string component_moniker;
  std::string path;
  if (component_path.size() == 1) {
    component_moniker = default_component().value_or("");
    path = component_path[0];
  } else if (component_path.size() == 2) {
    component_moniker = component_path[0];
    path = component_path[1];
  } else {
    FX_LOGS(ERROR) << "Invalid address! " << component_path.size() << " " << name;
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  if (component_moniker.empty()) {
    FX_LOGS(ERROR) << "Must have a component!";
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  if (component_moniker[0] != '.') {
    component_moniker.insert(0, ".");
  }
  // Parse path
  *out_path = split_string(path, "/");
  if (out_path->empty()) {
    FX_LOGS(ERROR) << "Must have at least directory!";
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  // Connect to component
  auto result = realm_query_->GetInstanceDirectories(component_moniker);
  if (result.is_error()) {
    FX_LOGS(ERROR) << "RealmQuery failed " << result.error_value().FormatDescription();
    return zx::error(result.error_value().is_domain_error()
                         ? static_cast<uint32_t>(result.error_value().domain_error())
                         : result.error_value().framework_error().status());
  }

  for (auto& entry : result->resolved_dirs()->ns_entries()) {
    if ("/" + (*out_path)[0] == entry.path()) {
      out_path->erase(out_path->begin());
      return zx::success(entry.directory()->TakeChannel());
    }
  }

  FX_LOGS(ERROR) << "Could not find directory " << (*out_path)[0];
  return zx::error(ZX_ERR_NOT_FOUND);
}

}  // namespace adb_file_sync
