// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "apps/mozart/services/scene/ops.fidl-common.h"
#include "apps/mozart/src/scene/resources/resource.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/handles/object_info.h"
#include "lib/mtl/tasks/message_loop.h"
#include "third_party/gtest/include/gtest/gtest_prod.h"

namespace mozart {
namespace scene {

/// Allows linking of resources in different sessions. Accepts a resource and
/// one endpoint of an event pair for export. That exported resource can be
/// imported in another session by providing the peer for the token used in the
/// export call. The same exported resource can be imported multiple times by
/// duplicating the peer token and making the import call multiple times with
/// each duplicated token. The linker owns the tokens provided in the import and
/// export calls and handles cases where the import call arrives before the
/// resource that matches that query has been exported.
class ResourceLinker : private mtl::MessageLoopHandler {
 public:
  ResourceLinker();

  ~ResourceLinker();

  bool ExportResource(ResourcePtr resource, mx::eventpair export_handle);

  enum class ResolutionResult {
    kSuccess,
    kInvalidHandle,
  };
  using OnImportResolvedCallback =
      std::function<void(ResourcePtr, ResolutionResult)>;
  void ImportResource(mozart2::ImportSpec spec,
                      mx::eventpair import_handle,
                      OnImportResolvedCallback import_resolved_callback);

  size_t UnresolvedExports() const;

  size_t UnresolvedImports() const;

  enum class ExpirationCause {
    kInternalError,
    kImportHandleClosed,
  };
  using OnExpiredCallback = std::function<void(ResourcePtr, ExpirationCause)>;
  void SetOnExpiredCallback(OnExpiredCallback callback);

 private:
  struct ExportedResourceEntry {
    mx::eventpair export_handle;
    mx_koid_t import_koid = MX_KOID_INVALID;
    mtl::MessageLoop::HandlerKey death_handler = 0;
    ResourcePtr resource;
  };
  struct UnresolvedImportEntry {
    mx::eventpair import_handle;
    OnImportResolvedCallback resolution_callback;
  };
  using HandleKoidMap = std::unordered_map<mx_handle_t, mx_koid_t>;
  using ExportedResourcesMap =
      std::unordered_map<mx_koid_t /* import koid */, ExportedResourceEntry>;
  using UnresolvedImportEntryMap =
      std::unordered_map<mx_koid_t /* import koid */,
                         std::vector<UnresolvedImportEntry>>;

  OnExpiredCallback expiration_callback_;
  HandleKoidMap import_koid_by_export_handle_;
  ExportedResourcesMap exported_resources_by_import_koid_;
  UnresolvedImportEntryMap unresolved_imports_by_import_koid_;

  void OnHandleReady(mx_handle_t handle, mx_signals_t pending) override;

  void OnHandleError(mx_handle_t handle, mx_status_t error) override;

  ResourcePtr RemoveResourceForExpiredExportHandle(mx_handle_t handle);

  void PerformLinkingNow(mx_koid_t import_koid);

  FTL_DISALLOW_COPY_AND_ASSIGN(ResourceLinker);
};

}  // namespace scene
}  // namespace mozart
