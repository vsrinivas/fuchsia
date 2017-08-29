// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "apps/mozart/services/scenic/ops.fidl-common.h"
#include "apps/mozart/src/scene_manager/resources/resource.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/handles/object_info.h"
#include "lib/mtl/tasks/message_loop.h"
#include "third_party/gtest/include/gtest/gtest_prod.h"

namespace scene_manager {

/// Allows linking of resources in different sessions.

/// Accepts a resource and one endpoint of an event pair for export. That
/// exported resource can be imported in another session by providing the peer
/// for the token used in the export call. The same exported resource can be
/// imported multiple times by duplicating the peer token and making the import
/// call multiple times with each duplicated token.
///
/// The linker owns the tokens provided in the export calls and handles cases
/// where the import call arrives before the resource that matches that query
/// has been exported.
///
/// A resource can be exported multiple times; we refer to one of those
/// times as an "export."
class ResourceLinker : private mtl::MessageLoopHandler {
 public:
  ResourceLinker();

  ~ResourceLinker();

  // Returns false if there was an error with the inputs, true otherwise.
  bool ExportResource(ResourcePtr resource, mx::eventpair export_token);

  enum class ResolutionResult {
    kSuccess,
  };
  using OnImportResolvedCallback =
      std::function<void(ResourcePtr, ResolutionResult)>;
  void ImportResource(scenic::ImportSpec spec,
                      const mx::eventpair& import_token,
                      OnImportResolvedCallback import_resolved_callback);

  size_t NumExports() const;

  size_t NumUnresolvedImports() const;

  enum class ExpirationCause {
    kInternalError,
    kImportHandleClosed,
  };
  using OnExpiredCallback = std::function<void(ResourcePtr, ExpirationCause)>;
  void SetOnExpiredCallback(OnExpiredCallback callback);

  size_t GetExportedResourceCountForSession(Session* session);

 private:
  struct ExportedResourceEntry {
    mx::eventpair export_token;
    mtl::MessageLoop::HandlerKey death_handler_key = 0;
    ResourcePtr resource;
  };
  struct UnresolvedImportEntry {
    OnImportResolvedCallback resolution_callback;
  };
  using ImportKoid = mx_koid_t;
  using HandleToKoidMap = std::unordered_map<mx_handle_t, ImportKoid>;
  using KoidToExportMap = std::unordered_map<ImportKoid, ExportedResourceEntry>;
  using KoidToUnresolvedImportsMap =
      std::unordered_map<ImportKoid, std::vector<UnresolvedImportEntry>>;

  OnExpiredCallback expiration_callback_;
  HandleToKoidMap export_handles_to_import_koids_;
  KoidToExportMap exports_;
  KoidToUnresolvedImportsMap unresolved_imports_;

  void OnHandleReady(mx_handle_t handle,
                     mx_signals_t pending,
                     uint64_t count) override;

  void OnHandleError(mx_handle_t handle, mx_status_t error) override;

  ResourcePtr RemoveExportForExpiredHandle(mx_handle_t handle);

  void PerformLinkingNow(mx_koid_t import_koid);

  FTL_DISALLOW_COPY_AND_ASSIGN(ResourceLinker);
};

}  // namespace scene_manager
