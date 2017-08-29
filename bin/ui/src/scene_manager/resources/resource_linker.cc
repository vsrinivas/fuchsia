// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene_manager/resources/resource_linker.h"
#include "lib/fidl/cpp/waiter/default.h"
#include "lib/mtl/tasks/message_loop.h"

namespace scene_manager {

static mx_signals_t kEventPairDeathSignals = MX_EPAIR_PEER_CLOSED;

#define ASSERT_INTERNAL_EXPORTS_CONSISTENCY \
  FTL_DCHECK(export_handles_to_import_koids_.size() == exports_.size());

ResourceLinker::ResourceLinker() = default;

ResourceLinker::~ResourceLinker() {
  auto message_loop = mtl::MessageLoop::GetCurrent();
  for (const auto& item : exports_) {
    message_loop->RemoveHandler(item.second.death_handler_key);
  }
}

bool ResourceLinker::ExportResource(ResourcePtr resource,
                                    mx::eventpair export_token) {
  // Basic sanity checks for resource validity.
  FTL_DCHECK(resource);

  // If the peer koid of the handle has already expired, there is no point in
  // registering the resource because an import can never be resolved. Bail.
  mx_koid_t import_koid = mtl::GetRelatedKoid(export_token.get());
  if (import_koid == MX_KOID_INVALID) {
    // This happens if we are passed an invalid export_token.
    return false;
  }

  // Ensure that the peer koid has not already been registered with the linker.
  auto found = exports_.find(import_koid);
  if (found != exports_.end()) {
    return false;
  }

  // The resource must be removed from being considered for import if its peer
  // is closed.
  mtl::MessageLoop::HandlerKey death_key =
      mtl::MessageLoop::GetCurrent()->AddHandler(
          this,                    // handler
          export_token.get(),      // handle
          kEventPairDeathSignals,  // trigger
          ftl::TimeDelta::Max()    // timeout
      );

  // Add the export to our internal maps.
  export_handles_to_import_koids_[export_token.get()] = import_koid;
  exports_[import_koid] = ExportedResourceEntry{
      .export_token = std::move(export_token),  // own the export token
      .death_handler_key = death_key,           //
      .resource = resource,                     //
  };

  ASSERT_INTERNAL_EXPORTS_CONSISTENCY;

  // Always perform linking last because it involves firing resolution callbacks
  // which may access the linker. We need that view to be consistent.
  PerformLinkingNow(import_koid);

  return true;
}

void ResourceLinker::ImportResource(
    mozart2::ImportSpec import_spec,
    const mx::eventpair& import_token,
    OnImportResolvedCallback import_resolved_callback) {
  // Make sure a callback is present.
  FTL_DCHECK(import_resolved_callback);

  // Make sure the import handle is valid.
  mx_koid_t import_koid = mtl::GetKoid(import_token.get());
  FTL_DCHECK(import_koid != MX_KOID_INVALID);

  // Register the import entry.
  unresolved_imports_[import_koid].emplace_back(UnresolvedImportEntry{
      .resolution_callback = import_resolved_callback,
  });

  // Always perform linking last because it involves firing resolution callbacks
  // which may access the linker. We need that view to be consistent.
  PerformLinkingNow(import_koid);
}

void ResourceLinker::OnHandleReady(mx_handle_t export_handle,
                                   mx_signals_t pending,
                                   uint64_t count) {
  // This is invoked when all the peers for the registered export handle are
  // closed.
  if (pending & kEventPairDeathSignals) {
    auto resource = RemoveExportForExpiredHandle(export_handle);
    if (expiration_callback_) {
      expiration_callback_(std::move(resource),
                           ExpirationCause::kImportHandleClosed);
    }
  }
}

void ResourceLinker::OnHandleError(mx_handle_t export_handle,
                                   mx_status_t error) {
  // Should only happen in case of timeout or loop death.
  if (error == MX_ERR_TIMED_OUT || error == MX_ERR_CANCELED) {
    auto resource = RemoveExportForExpiredHandle(export_handle);
    if (expiration_callback_) {
      expiration_callback_(std::move(resource),
                           ExpirationCause::kInternalError);
    }
  }
}

ResourcePtr ResourceLinker::RemoveExportForExpiredHandle(
    mx_handle_t export_handle) {
  // Find the import_koid that maps to |export_handle|.
  auto import_koid_iter = export_handles_to_import_koids_.find(export_handle);
  FTL_DCHECK(import_koid_iter != export_handles_to_import_koids_.end());

  // Find the export.
  auto export_iter = exports_.find(import_koid_iter->second);
  FTL_DCHECK(export_iter != exports_.end());

  // Unregister our message loop handler.
  mtl::MessageLoop::GetCurrent()->RemoveHandler(
      export_iter->second.death_handler_key);

  auto resource = export_iter->second.resource;

  // Remove the export from our internal maps.
  exports_.erase(export_iter);
  export_handles_to_import_koids_.erase(import_koid_iter);

  ASSERT_INTERNAL_EXPORTS_CONSISTENCY;

  return resource;
}

size_t ResourceLinker::NumExports() const {
  ASSERT_INTERNAL_EXPORTS_CONSISTENCY;
  return exports_.size();
}

size_t ResourceLinker::NumUnresolvedImports() const {
  size_t unresolved_imports = 0;
  for (const auto& collection : unresolved_imports_) {
    unresolved_imports += collection.second.size();
  }
  return unresolved_imports;
}

void ResourceLinker::SetOnExpiredCallback(OnExpiredCallback callback) {
  expiration_callback_ = callback;
}

size_t ResourceLinker::GetExportedResourceCountForSession(Session* session) {
  size_t count = 0;
  for (auto& pair : exports_) {
    if (session == pair.second.resource->session()) {
      ++count;
    }
  }
  return count;
}

void ResourceLinker::PerformLinkingNow(mx_koid_t import_koid) {
  // Find the unresolved import entry if present.
  auto found = unresolved_imports_.find(import_koid);
  if (found == unresolved_imports_.end()) {
    return;
  }

  // Find the corresponding entry in the exported resource registrations.
  auto export_iter = exports_.find(import_koid);
  if (export_iter == exports_.end()) {
    return;
  }

  // Collect all the resolution callbacks that need to be invoked.
  std::vector<OnImportResolvedCallback> callbacks;
  callbacks.reserve(found->second.size());
  for (const UnresolvedImportEntry& import_entry : found->second) {
    callbacks.emplace_back(import_entry.resolution_callback);
  }

  // Resolution done. Cleanup the unresolved imports collection.
  unresolved_imports_.erase(found);

  // Finally, invoke the resolution callbacks last. This is important because we
  // want to ensure that any code that runs within the callbacks sees a
  // consistent view of the linker.
  auto matched_resource = export_iter->second.resource;
  for (const auto& callback : callbacks) {
    callback(matched_resource, ResolutionResult::kSuccess);
  }
}

}  // namespace scene_manager
