// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include <lib/async/default.h>

#include "garnet/lib/ui/gfx/engine/resource_linker.h"

namespace scenic {
namespace gfx {

static zx_signals_t kEventPairDeathSignals = ZX_EVENTPAIR_PEER_CLOSED;

#define ASSERT_INTERNAL_EXPORTS_CONSISTENCY \
  {                                         \
    size_t i = 0;                           \
    for (auto& it : koids_to_import_ptrs_)  \
      i += it.second.size();                \
    FXL_DCHECK(imports_.size() == i);       \
  }

UnresolvedImports::UnresolvedImports(ResourceLinker* resource_linker)
    : resource_linker_(resource_linker){};

void UnresolvedImports::AddUnresolvedImport(Import* import,
                                            zx::eventpair import_token,
                                            zx_koid_t import_koid) {
  // Make sure the import koid we've been passed is valid.
  FXL_DCHECK(import_koid != ZX_KOID_INVALID);
  FXL_DCHECK(import_koid == fsl::GetKoid(import_token.get()));

  // Make sure we're not using the same import twice.
  FXL_DCHECK(imports_.find(import) == imports_.end());

  // Add to our data structures.
  imports_[import] = ImportEntry{.import_ptr = import,
                                 .import_token = std::move(import_token),
                                 .import_koid = import_koid};
  koids_to_import_ptrs_[import_koid].push_back(import);

  ASSERT_INTERNAL_EXPORTS_CONSISTENCY;
}

void UnresolvedImports::ListenForTokenPeerDeath(Import* import) {
  auto import_entry_iter = imports_.find(import);
  if (import_entry_iter != imports_.end()) {
    zx_handle_t import_handle = import_entry_iter->second.import_token.get();
    zx_koid_t import_koid = import_entry_iter->second.import_koid;

    // The resource must be removed from being considered for import
    // if its peer is closed.
    auto wait =
        std::make_unique<async::Wait>(import_handle, kEventPairDeathSignals);
    wait->set_handler(std::bind(&UnresolvedImports::OnTokenPeerDeath, this,
                                import_koid, std::placeholders::_3,
                                std::placeholders::_4));
    zx_status_t status = wait->Begin(async_get_default_dispatcher());
    FXL_CHECK(status == ZX_OK);

    import_entry_iter->second.token_peer_death_waiter = std::move(wait);
  }
  ASSERT_INTERNAL_EXPORTS_CONSISTENCY;
}

std::vector<Import*> UnresolvedImports::RemoveUnresolvedImportsForKoid(
    zx_koid_t import_koid) {
  auto imports = GetAndRemoveUnresolvedImportsForKoid(import_koid);

  for (auto& entry : imports) {
    resource_linker_->OnImportResolvedForResource(
        entry, nullptr, ImportResolutionResult::kExportHandleDiedBeforeBind);
  }

  return imports;
}

std::vector<Import*> UnresolvedImports::GetAndRemoveUnresolvedImportsForKoid(
    zx_koid_t import_koid) {
  // Look up the import entries for this koid.
  auto import_ptr_collection_iter = koids_to_import_ptrs_.find(import_koid);
  FXL_DCHECK(import_ptr_collection_iter != koids_to_import_ptrs_.end());

  // Construct a list of the callbacks, and erase the imports from our data
  // structures.
  std::vector<Import*> imports = std::move(import_ptr_collection_iter->second);

  for (auto& import_ptr : imports) {
    // Look up entry and add its resolution_callback to |callbacks|.
    auto entry_iter = imports_.find(import_ptr);
    FXL_DCHECK(entry_iter != imports_.end());

    // Remove from |imports_|.
    imports_.erase(entry_iter);
  }

  // Remove from |koids_to_import_ptrs_|.
  koids_to_import_ptrs_.erase(import_ptr_collection_iter);

  ASSERT_INTERNAL_EXPORTS_CONSISTENCY;

  return imports;
}

void Remove(Import* import, std::vector<Import*>* vec) {
  auto it = std::find(vec->begin(), vec->end(), import);
  FXL_DCHECK(it != vec->end());
  vec->erase(it);
}

void UnresolvedImports::OnImportDestroyed(Import* import) {
  auto entry_iter = imports_.find(import);
  if (entry_iter == imports_.end())
    return;

  // Call the resolution callback.
  resource_linker_->OnImportResolvedForResource(
      import, nullptr, ImportResolutionResult::kImportDestroyedBeforeBind);

  // Remove from |koids_to_import_ptrs_|.
  zx_koid_t import_koid = entry_iter->second.import_koid;
  Remove(import, &koids_to_import_ptrs_[import_koid]);

  // Remove from |imports_|.
  imports_.erase(entry_iter);

  resource_linker_->InvokeExpirationCallback(
      import, ResourceLinker::ExpirationCause::kResourceDestroyed);

  ASSERT_INTERNAL_EXPORTS_CONSISTENCY;
}

size_t UnresolvedImports::NumUnresolvedImportsForKoid(
    zx_koid_t import_koid) const {
  // Look up the import entries for this koid.
  auto import_ptr_collection_iter = koids_to_import_ptrs_.find(import_koid);
  if (import_ptr_collection_iter == koids_to_import_ptrs_.end()) {
    return 0;
  } else {
    return import_ptr_collection_iter->second.size();
  }
}

void UnresolvedImports::OnTokenPeerDeath(zx_koid_t import_koid,
                                         zx_status_t status,
                                         const zx_packet_signal* signal) {
  // Remove |import_koid|, even if there was an error (i.e. status != ZX_OK).
  auto imports = RemoveUnresolvedImportsForKoid(import_koid);

  for (auto& import : imports) {
    resource_linker_->InvokeExpirationCallback(
        import, status == ZX_OK
                    ? ResourceLinker::ExpirationCause::kExportTokenClosed
                    : ResourceLinker::ExpirationCause::kInternalError);
  }
}

}  // namespace gfx
}  // namespace scenic
