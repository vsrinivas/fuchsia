// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene_manager/resources/resource_linker.h"
#include "lib/fidl/cpp/waiter/default.h"
#include "lib/mtl/tasks/message_loop.h"

namespace scene_manager {

static mx_signals_t kEventPairDeathSignals = MX_EPAIR_PEER_CLOSED;

#define ASSERT_INTERNAL_EXPORTS_CONSISTENCY          \
  FTL_DCHECK(import_koid_by_export_handle_.size() == \
             exported_resources_by_import_koid_.size());

ResourceLinker::ResourceLinker() = default;

ResourceLinker::~ResourceLinker() {
  auto message_loop = mtl::MessageLoop::GetCurrent();
  for (const auto& item : exported_resources_by_import_koid_) {
    message_loop->RemoveHandler(item.second.death_handler);
  }
}

bool ResourceLinker::ExportResource(ResourcePtr resource,
                                    mx::eventpair export_handle) {
  // Basic sanity checks for resource validity.
  if (!resource) {
    return false;
  }

  // If the peer koid of the handle has already expired, there is no point in
  // registering the resource because an import can never be resolved. Bail.
  mx_koid_t import_koid = mtl::GetRelatedKoid(export_handle.get());
  if (import_koid == MX_KOID_INVALID) {
    return false;
  }

  // Ensure that the peer koid has not already been registered with the linker.
  auto found = exported_resources_by_import_koid_.find(import_koid);
  if (found != exported_resources_by_import_koid_.end()) {
    return false;
  }

  // The resource must be removed from being considered for import if its peer
  // is closed.
  mtl::MessageLoop::HandlerKey death_key =
      mtl::MessageLoop::GetCurrent()->AddHandler(
          this,                    // handler
          export_handle.get(),     // handle
          kEventPairDeathSignals,  // trigger
          ftl::TimeDelta::Max()    // timeout
      );

  mx_handle_t raw_export_handle = export_handle.get();  // About to be moved.

  ExportedResourceEntry resource_entry = {
      .export_handle = std::move(export_handle),  // own export handle
      .import_koid = import_koid,                 //
      .death_handler = death_key,                 //
      .resource = resource,                       //
  };

  import_koid_by_export_handle_[raw_export_handle] = import_koid;
  exported_resources_by_import_koid_[import_koid] = std::move(resource_entry);

  ASSERT_INTERNAL_EXPORTS_CONSISTENCY;

  // Always perform linking last because it involves firing resolution callbacks
  // which may access the linker. We need that view to be consistent.
  PerformLinkingNow(import_koid);

  return true;
}

void ResourceLinker::ImportResource(
    mozart2::ImportSpec import_spec,
    const mx::eventpair& import_handle,
    OnImportResolvedCallback import_resolved_callback) {
  // Make sure a callback is present.
  if (!import_resolved_callback) {
    return;
  }

  // Make sure the import handle is valid.
  mx_koid_t import_koid = mtl::GetKoid(import_handle.get());
  if (import_koid == MX_KOID_INVALID) {
    import_resolved_callback(nullptr, ResolutionResult::kInvalidHandle);
    return;
  }

  // Register the import entry.
  UnresolvedImportEntry unresolved_import_entry = {
      .resolution_callback = import_resolved_callback,  // resolution callback
  };
  unresolved_imports_by_import_koid_[import_koid].emplace_back(
      std::move(unresolved_import_entry));

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
    auto resource = RemoveResourceForExpiredExportHandle(export_handle);
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
    auto resource = RemoveResourceForExpiredExportHandle(export_handle);
    if (expiration_callback_) {
      expiration_callback_(std::move(resource),
                           ExpirationCause::kInternalError);
    }
  }
}

ResourcePtr ResourceLinker::RemoveResourceForExpiredExportHandle(
    mx_handle_t export_handle) {
  auto import_koid_iterator = import_koid_by_export_handle_.find(export_handle);
  FTL_DCHECK(import_koid_iterator != import_koid_by_export_handle_.end());

  auto resource_iterator =
      exported_resources_by_import_koid_.find(import_koid_iterator->second);
  FTL_DCHECK(resource_iterator != exported_resources_by_import_koid_.end());

  mtl::MessageLoop::GetCurrent()->RemoveHandler(
      resource_iterator->second.death_handler);

  auto resource = resource_iterator->second.resource;

  exported_resources_by_import_koid_.erase(resource_iterator);
  import_koid_by_export_handle_.erase(import_koid_iterator);

  ASSERT_INTERNAL_EXPORTS_CONSISTENCY;

  return resource;
}

size_t ResourceLinker::UnresolvedExports() const {
  ASSERT_INTERNAL_EXPORTS_CONSISTENCY;
  return exported_resources_by_import_koid_.size();
}

size_t ResourceLinker::UnresolvedImports() const {
  size_t unresolved_imports = 0;
  for (const auto& collection : unresolved_imports_by_import_koid_) {
    unresolved_imports += collection.second.size();
  }
  return unresolved_imports;
}

void ResourceLinker::SetOnExpiredCallback(OnExpiredCallback callback) {
  expiration_callback_ = callback;
}

void ResourceLinker::PerformLinkingNow(mx_koid_t import_koid) {
  // Find the unresolved import entry if present.
  auto found = unresolved_imports_by_import_koid_.find(import_koid);
  if (found == unresolved_imports_by_import_koid_.end()) {
    return;
  }

  // Find the corresponding entry in the exported resource registrations.
  auto resource_iterator = exported_resources_by_import_koid_.find(import_koid);
  if (resource_iterator == exported_resources_by_import_koid_.end()) {
    return;
  }

  // Collect all the resolution callbacks that need to be invoked.
  std::vector<OnImportResolvedCallback> callbacks;
  callbacks.reserve(found->second.size());
  for (const UnresolvedImportEntry& import_entry : found->second) {
    callbacks.emplace_back(import_entry.resolution_callback);
  }

  // Resolution done. Cleanup the unresolved imports collection.
  unresolved_imports_by_import_koid_.erase(found);

  // Finally, invoke the resolution callbacks last. This is important because we
  // want to ensure that any code that runs within the callbacks sees a
  // consistent view of the linker.
  auto matched_resource = resource_iterator->second.resource;
  for (const auto& callback : callbacks) {
    callback(matched_resource, ResolutionResult::kSuccess);
  }
}

}  // namespace scene_manager
