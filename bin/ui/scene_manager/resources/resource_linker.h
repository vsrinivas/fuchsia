// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>
#include <unordered_set>

#include "lib/fsl/handles/object_info.h"
#include "lib/fsl/tasks/message_loop.h"

#include "lib/ui/scenic/fidl/ops.fidl-common.h"
#include "garnet/bin/ui/scene_manager/resources/resource.h"
#include "garnet/bin/ui/scene_manager/resources/unresolved_imports.h"

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
///
/// There are two ways an export can be removed: Either the resource is
/// destroyed using |OnExportedResourceDestroyed|, or all the import tokens have
/// been closed which results in a call to |RemoveExportEntryForExpiredHandle|.

class ResourceLinker : private fsl::MessageLoopHandler {
 public:
  ResourceLinker();

  ~ResourceLinker();

  // Register a |resource| so that it can be imported into a different session
  // via ImportResourceOp with the peer of |export_token|.
  //
  // Returns true if there are no errors.
  bool ExportResource(Resource* resource, zx::eventpair export_token);

  // Registers an |import| with a given |spec| and |import_token|. |import| is
  // new resource in the importing session that acts as a import for a resource
  // that was exported by another session.
  //
  // Since the other end (the resource being imported) might not be registered
  // yet, the binding is not guaranteed to happen immediately.
  //
  // Returns true if there are no errors.
  bool ImportResource(Import* import,
                      scenic::ImportSpec spec,
                      zx::eventpair import_token);

  size_t NumExports() const;

  size_t NumUnresolvedImports() const;

  enum class ExpirationCause {
    kInternalError,
    kNoImportsBound,
    kExportHandleClosed,
    kResourceDestroyed,
  };
  using OnExpiredCallback = std::function<void(Resource*, ExpirationCause)>;
  void SetOnExpiredCallback(OnExpiredCallback callback);
  using OnImportResolvedCallback =
      std::function<void(Import*, Resource*, ImportResolutionResult)>;
  void SetOnImportResolvedCallback(OnImportResolvedCallback callback);

  size_t NumExportsForSession(Session* session);

 private:
  friend class UnresolvedImports;
  friend class Resource;
  friend class Import;

  struct ExportEntry {
    zx::eventpair export_token;
    fsl::MessageLoop::HandlerKey death_handler_key = 0;
    Resource* resource;
  };

  using ImportKoid = zx_koid_t;
  using HandleToKoidMap = std::unordered_map<zx_handle_t, ImportKoid>;
  using KoidsToExportsMap = std::unordered_map<ImportKoid, ExportEntry>;
  using ResourcesToKoidsMap = std::multimap<Resource*, ImportKoid>;

  OnExpiredCallback expiration_callback_;
  OnImportResolvedCallback import_resolved_callback_;

  HandleToKoidMap export_handles_to_import_koids_;
  KoidsToExportsMap export_entries_;
  ResourcesToKoidsMap exported_resources_to_import_koids_;

  // |exported_resources_| can contain resources that are not included in the
  // maps above. Even if all the import tokens for a given export are destroyed,
  // the associated resource could still be exported because it's already been
  // bound to an import.
  std::unordered_set<Resource*> exported_resources_;
  UnresolvedImports unresolved_imports_;

  // Remove exports for this resource. Called when the resource is being
  // destroyed.
  void OnExportedResourceDestroyed(Resource* resource);

  // A callback that informs us when an import has been destroyed.
  void OnImportDestroyed(Import* import);

  void OnImportResolvedForResource(Import* import,
                                   Resource* actual,
                                   ImportResolutionResult resolution_result);

  void OnHandleReady(zx_handle_t handle,
                     zx_signals_t pending,
                     uint64_t count) override;

  void OnHandleError(zx_handle_t handle, zx_status_t error) override;

  void InvokeExpirationCallback(Resource* resource, ExpirationCause cause);

  Resource* RemoveExportEntryForExpiredHandle(zx_handle_t handle);

  void RemoveExportedResourceIfUnbound(Resource* exported_resource);

  // Returns true if a link was made.
  bool PerformLinkingNow(zx_koid_t import_koid);

  // Helper method to remove |import_koid| from |resources_to_import_koids_|.
  void RemoveFromExportedResourceToImportKoidsMap(Resource* resource,
                                                  zx_koid_t import_koid);

  FXL_DISALLOW_COPY_AND_ASSIGN(ResourceLinker);
};

}  // namespace scene_manager
