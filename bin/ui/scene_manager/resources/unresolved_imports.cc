// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include "garnet/bin/ui/scene_manager/resources/resource_linker.h"
#include "lib/fidl/cpp/waiter/default.h"
#include "lib/fsl/tasks/message_loop.h"

namespace scene_manager {

static mx_signals_t kEventPairDeathSignals = MX_EPAIR_PEER_CLOSED;

#define ASSERT_INTERNAL_EXPORTS_CONSISTENCY                \
  FXL_DCHECK(imports_.size() == handles_to_koids_.size()); \
  {                                                        \
    size_t i = 0;                                          \
    for (auto& it : koids_to_import_ptrs_)                 \
      i += it.second.size();                               \
    FXL_DCHECK(imports_.size() == i);                      \
  }

UnresolvedImports::UnresolvedImports(ResourceLinker* resource_linker)
    : resource_linker_(resource_linker){};

UnresolvedImports::~UnresolvedImports() {
  auto message_loop = fsl::MessageLoop::GetCurrent();
  for (auto& entry_iter : imports_) {
    FXL_DCHECK(entry_iter.second.import_token);
    message_loop->RemoveHandler(entry_iter.second.death_handler_key);
#ifndef NDEBUG
    num_handler_keys_--;
#endif
  }
#ifndef NDEBUG
  FXL_DCHECK(num_handler_keys_ == 0);
#endif
}

void UnresolvedImports::AddUnresolvedImport(Import* import,
                                            mx::eventpair import_token,
                                            mx_koid_t import_koid) {
  // Make sure the import koid we've been passed is valid.
  FXL_DCHECK(import_koid != MX_KOID_INVALID);
  FXL_DCHECK(import_koid == fsl::GetKoid(import_token.get()));

  // Make sure we're not using the same import twice.
  FXL_DCHECK(imports_.find(import) == imports_.end());

  // Add to our data structures.
  FXL_DCHECK(handles_to_koids_.find(import_token.get()) ==
             handles_to_koids_.end());
  handles_to_koids_[import_token.get()] = import_koid;
  imports_[import] = ImportEntry{.import_ptr = import,
                                 .import_token = std::move(import_token),
                                 .import_koid = import_koid,
                                 .death_handler_key = 0};
  koids_to_import_ptrs_[import_koid].push_back(import);

  ASSERT_INTERNAL_EXPORTS_CONSISTENCY;
}

void UnresolvedImports::ListenForPeerHandleDeath(Import* import) {
  auto import_entry_iter = imports_.find(import);
  if (import_entry_iter != imports_.end()) {
    // The resource must be removed from being considered for import
    // if its peer is closed.
    fsl::MessageLoop::HandlerKey death_key =
        fsl::MessageLoop::GetCurrent()->AddHandler(
            this,                                          // handler
            import_entry_iter->second.import_token.get(),  // handle
            kEventPairDeathSignals,                        // trigger
            fxl::TimeDelta::Max()                          // timeout
        );
    import_entry_iter->second.death_handler_key = death_key;
#ifndef NDEBUG
    num_handler_keys_++;
#endif
  }
  ASSERT_INTERNAL_EXPORTS_CONSISTENCY;
#ifndef NDEBUG
  FXL_DCHECK(imports_.size() == num_handler_keys_);
#endif
}

std::vector<Import*> UnresolvedImports::RemoveUnresolvedImportsForHandle(
    mx_handle_t import_handle) {
  auto import_koid_iter = handles_to_koids_.find(import_handle);
  FXL_DCHECK(import_koid_iter != handles_to_koids_.end());

  auto imports = GetAndRemoveUnresolvedImportsForKoid(import_koid_iter->second);

  for (auto& entry : imports) {
    resource_linker_->OnImportResolvedForResource(
        entry, nullptr, ImportResolutionResult::kExportHandleDiedBeforeBind);
  }

  return imports;
}

std::vector<Import*> UnresolvedImports::GetAndRemoveUnresolvedImportsForKoid(
    mx_koid_t import_koid) {
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

    // Remove from |handles_to_koids_|.
    size_t num_removed =
        handles_to_koids_.erase(entry_iter->second.import_token.get());
    FXL_DCHECK(num_removed == 1);

    // Unregister the handler
    if (entry_iter->second.death_handler_key != 0) {
      FXL_DCHECK(entry_iter->second.import_token);
      fsl::MessageLoop::GetCurrent()->RemoveHandler(
          entry_iter->second.death_handler_key);
#ifndef NDEBUG
      num_handler_keys_--;
#endif
    }

    // Remove from |imports_|.
    imports_.erase(entry_iter);
  }

  // Remove from |koids_to_import_ptrs_|.
  koids_to_import_ptrs_.erase(import_ptr_collection_iter);

  ASSERT_INTERNAL_EXPORTS_CONSISTENCY;
#ifndef NDEBUG
  FXL_DCHECK(imports_.size() == num_handler_keys_);
#endif

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
  mx_koid_t import_koid = entry_iter->second.import_koid;
  Remove(import, &koids_to_import_ptrs_[import_koid]);

  // Remove from |handles_to_koids_|.
  size_t num_removed =
      handles_to_koids_.erase(entry_iter->second.import_token.get());
  FXL_DCHECK(num_removed == 1);

  // Unregister the handler
  if (entry_iter->second.death_handler_key != 0) {
    FXL_DCHECK(entry_iter->second.import_token);
    fsl::MessageLoop::GetCurrent()->RemoveHandler(
        entry_iter->second.death_handler_key);
#ifndef NDEBUG
    num_handler_keys_--;
#endif
  }

  // Remove from |imports_|.
  imports_.erase(entry_iter);

  resource_linker_->InvokeExpirationCallback(
      import, ResourceLinker::ExpirationCause::kResourceDestroyed);

  ASSERT_INTERNAL_EXPORTS_CONSISTENCY;
#ifndef NDEBUG
  FXL_DCHECK(imports_.size() == num_handler_keys_);
#endif
}

size_t UnresolvedImports::NumUnresolvedImportsForKoid(
    mx_koid_t import_koid) const {
  // Look up the import entries for this koid.
  auto import_ptr_collection_iter = koids_to_import_ptrs_.find(import_koid);
  if (import_ptr_collection_iter == koids_to_import_ptrs_.end()) {
    return 0;
  } else {
    return import_ptr_collection_iter->second.size();
  }
}

void UnresolvedImports::OnHandleReady(mx_handle_t import_handle,
                                      mx_signals_t pending,
                                      uint64_t count) {
  // This is invoked when all the peers for the registered import
  // handle are closed.
  if (pending & kEventPairDeathSignals) {
    auto imports = RemoveUnresolvedImportsForHandle(import_handle);

    for (auto& import : imports) {
      resource_linker_->InvokeExpirationCallback(
          import, ResourceLinker::ExpirationCause::kExportHandleClosed);
    }
  }
}

void UnresolvedImports::OnHandleError(mx_handle_t import_handle,
                                      mx_status_t error) {
  // Should only happen in case of timeout or loop death.
  if (error == MX_ERR_TIMED_OUT || error == MX_ERR_CANCELED) {
    auto imports = RemoveUnresolvedImportsForHandle(import_handle);

    for (auto& import : imports) {
      resource_linker_->InvokeExpirationCallback(
          import, ResourceLinker::ExpirationCause::kInternalError);
    }
  }
}

}  // namespace scene_manager
