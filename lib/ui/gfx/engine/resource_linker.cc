// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/resource_linker.h"

#include <lib/async/default.h>

#include "garnet/lib/ui/gfx/resources/import.h"

namespace scenic {
namespace gfx {

static zx_signals_t kEventPairDeathSignals = ZX_EVENTPAIR_PEER_CLOSED;

#define ASSERT_INTERNAL_EXPORTS_CONSISTENCY                \
  FXL_DCHECK(exported_resources_to_import_koids_.size() == \
             export_entries_.size());

ResourceLinker::ResourceLinker() : unresolved_imports_(this){};

ResourceLinker::~ResourceLinker() {
  for (const auto& item : export_entries_) {
    // Resource could be null if ExportResource() were called, but the
    // import tokens weren't all released yet.
    if (item.second.resource != nullptr) {
      item.second.resource->SetExported(false);
    }
  }
}

bool ResourceLinker::ExportResource(Resource* resource,
                                    zx::eventpair export_token) {
  // Basic sanity checks for resource validity.
  FXL_DCHECK(resource);

  zx_koid_t import_koid = fsl::GetRelatedKoid(export_token.get());
  if (import_koid == ZX_KOID_INVALID) {
    // We were passed a bad export handle.
    return false;
  }

  // Ensure that the peer koid has not already been registered with the
  // linker.
  auto found = export_entries_.find(import_koid);
  if (found != export_entries_.end()) {
    // Cannot export more than once with the same |export_token|.
    return false;
  }

  // The resource must be removed from being considered for import if its
  // peer is closed.
  auto wait = std::make_unique<async::Wait>(export_token.get(),     // handle
                                            kEventPairDeathSignals  // trigger
  );
  wait->set_handler(std::bind(&ResourceLinker::OnTokenPeerDeath, this,
                              import_koid, std::placeholders::_3,
                              std::placeholders::_4));
  zx_status_t status = wait->Begin(async_get_default());
  FXL_CHECK(status == ZX_OK);

  // Add the resource and export token to our data structures.
  export_entries_[import_koid] = ExportEntry{
      .export_token = std::move(export_token),     // own the export token
      .token_peer_death_waiter = std::move(wait),  //
      .resource = resource,                        //
  };
  exported_resources_to_import_koids_.insert({resource, import_koid});
  exported_resources_.insert(resource);
  resource->SetExported(true);

  ASSERT_INTERNAL_EXPORTS_CONSISTENCY;

  // Always perform linking last because it involves firing resolution
  // callbacks which may access the linker. We need that view to be
  // consistent.
  PerformLinkingNow(import_koid);

  return true;
}

void ResourceLinker::OnImportDestroyed(Import* import) {
  // The exported resource is called the "imported resource" from Import's
  // perspective.
  Resource* exported_resource = import->imported_resource();
  if (exported_resource) {
    RemoveExportedResourceIfUnbound(exported_resource);
  } else {
    unresolved_imports_.OnImportDestroyed(import);
  }
}

void ResourceLinker::OnImportResolvedForResource(
    Import* import, Resource* exported_resource,
    ImportResolutionResult resolution_result) {
  switch (resolution_result) {
    case ImportResolutionResult::kSuccess:
      exported_resource->AddImport(import);
      break;
    case ImportResolutionResult::kExportHandleDiedBeforeBind:
      import->UnbindImportedResource();
      break;
    case ImportResolutionResult::kImportDestroyedBeforeBind:
      break;
  }
  if (import_resolved_callback_) {
    import_resolved_callback_(import, exported_resource, resolution_result);
  }
}

void ResourceLinker::RemoveExportedResourceIfUnbound(
    Resource* exported_resource) {
  FXL_DCHECK(exported_resource);

  if (!exported_resource->imports().empty()) {
    // |exported_resource| still has imports bound to it.
    return;
  }

  auto range =
      exported_resources_to_import_koids_.equal_range(exported_resource);
  if (range.first != range.second) {
    // There are outstanding import tokens that could be used to import the
    // device.
    return;
  }

  // |exported_resource| is unbound. Remove it.
  exported_resources_.erase(exported_resource);

  // Mark the resource as not exported, so it doesn't have to
  // call back to us when it dies.
  exported_resource->SetExported(false);

  InvokeExpirationCallback(exported_resource, ExpirationCause::kNoImportsBound);
}

bool ResourceLinker::ImportResource(Import* import,
                                    ::fuchsia::ui::gfx::ImportSpec import_spec,
                                    zx::eventpair import_token) {
  // Make sure the import handle is valid.
  zx_koid_t import_koid = fsl::GetKoid(import_token.get());
  if (import_koid == ZX_KOID_INVALID) {
    // We were passed a bad import handle.
    return false;
  }

  // Register the import entry.
  unresolved_imports_.AddUnresolvedImport(import, std::move(import_token),
                                          import_koid);

  // Always perform linking last because it involves firing resolution callbacks
  // which may access the linker. We need that view to be consistent.

  if (!PerformLinkingNow(import_koid)) {
    // If |import| was not bound, it should listen for its peer token dying
    // to know if it should be removed.
    unresolved_imports_.ListenForTokenPeerDeath(import);
  }
  return true;
}

void ResourceLinker::OnTokenPeerDeath(zx_koid_t import_koid, zx_status_t status,
                                      const zx_packet_signal* signal) {
  // This is invoked when all the peers for the registered export
  // handle are closed, or if there is a loop death or other error.
  RemoveExportEntryForExpiredKoid(import_koid);
}

void ResourceLinker::InvokeExpirationCallback(Resource* resource,
                                              ExpirationCause cause) {
  if (expiration_callback_) {
    expiration_callback_(resource, cause);
  }
}

Resource* ResourceLinker::RemoveExportEntryForExpiredKoid(
    zx_koid_t import_koid) {
  auto export_entry_iter = export_entries_.find(import_koid);
  FXL_DCHECK(export_entry_iter != export_entries_.end());
  Resource* resource = export_entry_iter->second.resource;

  // Remove from |export_entries_|.
  export_entries_.erase(export_entry_iter);

  // Remove from |exported_resources_to_import_koids_|.
  RemoveFromExportedResourceToImportKoidsMap(resource, import_koid);

  // Remove from |resources_| if appropriate.
  RemoveExportedResourceIfUnbound(resource);

  ASSERT_INTERNAL_EXPORTS_CONSISTENCY;

  return resource;
}

void ResourceLinker::OnExportedResourceDestroyed(Resource* resource) {
  FXL_DCHECK(resource);

  // Find all the exports for the Resource (it could have been
  // exported more than once).
  auto range = exported_resources_to_import_koids_.equal_range(resource);
  for (auto import_koid_iter = range.first; import_koid_iter != range.second;) {
    zx_koid_t import_koid = import_koid_iter->second;

    auto export_entry_iter = export_entries_.find(import_koid);
    FXL_DCHECK(export_entry_iter != export_entries_.end());

    // Remove from |export_entries_|.
    export_entries_.erase(export_entry_iter);

    // Remove from |resources_to_import_koids_|, and advance iterator.
    import_koid_iter =
        exported_resources_to_import_koids_.erase(import_koid_iter);
  }

  // Mark the resource as not exported, so it doesn't have to
  // call back to us when it dies.
  resource->SetExported(false);

  // Remove from |resources_|.
  size_t num_removed = exported_resources_.erase(resource);

  FXL_DCHECK(num_removed == 1);

  InvokeExpirationCallback(resource, ExpirationCause::kResourceDestroyed);

  // When the resource dies, it will unbind all its imported resources.
}

size_t ResourceLinker::NumExports() const {
  ASSERT_INTERNAL_EXPORTS_CONSISTENCY;
  return exported_resources_.size();
}

size_t ResourceLinker::NumUnresolvedImports() const {
  return unresolved_imports_.size();
}

void ResourceLinker::SetOnExpiredCallback(OnExpiredCallback callback) {
  expiration_callback_ = std::move(callback);
}

void ResourceLinker::SetOnImportResolvedCallback(
    OnImportResolvedCallback callback) {
  import_resolved_callback_ = std::move(callback);
}

size_t ResourceLinker::NumExportsForSession(Session* session) {
  size_t count = 0;
  for (auto& pair : export_entries_) {
    if (session == pair.second.resource->session()) {
      ++count;
    }
  }
  return count;
}

bool ResourceLinker::PerformLinkingNow(zx_koid_t import_koid) {
  // Find the unresolved import entries if present.
  size_t num_imports =
      unresolved_imports_.NumUnresolvedImportsForKoid(import_koid);

  if (num_imports == 0) {
    // Nothing to resolve yet.
    return false;
  }

  // Find the corresponding entry in the exported resource registrations.
  auto export_entry_iter = export_entries_.find(import_koid);
  if (export_entry_iter == export_entries_.end()) {
    // Export is not present yet.
    return false;
  }

  // We have import and export entries that match for the same import_koid.
  // Collect all the resolution callbacks that need to be invoked.
  auto imports =
      unresolved_imports_.GetAndRemoveUnresolvedImportsForKoid(import_koid);

  // Finally, invoke the resolution callbacks last. This is
  // important because we want to ensure that any code that runs
  // within the callbacks sees a consistent view of the linker.
  auto matched_resource = export_entry_iter->second.resource;
  for (const auto& import : imports) {
    OnImportResolvedForResource(import, matched_resource,
                                ImportResolutionResult::kSuccess);
  }

  return true;
}

void ResourceLinker::RemoveFromExportedResourceToImportKoidsMap(
    Resource* resource, zx_koid_t import_koid) {
  // Remove this specific export from |export_entries_by_resource_|. (The
  // same resource can be exported multiple times).
  auto range = exported_resources_to_import_koids_.equal_range(resource);
  size_t range_size = 0;

  for (auto it = range.first; it != range.second;) {
    range_size++;
    if (it->second == import_koid) {
      it = exported_resources_to_import_koids_.erase(it);
    } else {
      it++;
    }
  }
}

}  // namespace gfx
}  // namespace scenic
