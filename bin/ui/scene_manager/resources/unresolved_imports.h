// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "lib/fsl/tasks/message_loop.h"

#include "garnet/bin/ui/scene_manager/resources/import.h"

namespace scene_manager {

// Stores a list of imports that have not yet been bound to an export handle.
class UnresolvedImports : private fsl::MessageLoopHandler {
 public:
  UnresolvedImports(ResourceLinker* resource_linker);
  ~UnresolvedImports();

  // Adds an entry for an unresolved import. |import_koid| must be the koid
  // for |import_token|.
  void AddUnresolvedImport(Import* import,
                           zx::eventpair import_token,
                           zx_koid_t import_koid);

  // Listen for the death of the corresponding export token and removes any
  // matching imports if that happens.
  // If |import| is not in the collection of unresolved imports, this is a
  // no-op.
  void ListenForPeerHandleDeath(Import* import);

  // Removes and returns all imports corresponding to |import_koid|.
  std::vector<Import*> GetAndRemoveUnresolvedImportsForKoid(
      zx_koid_t import_koid);

  // A callback that informs us when an import has been destroyed.
  void OnImportDestroyed(Import* import);

  size_t NumUnresolvedImportsForKoid(zx_koid_t import_koid) const;

  size_t size() const { return imports_.size(); }

 private:
  struct ImportEntry {
    Import* import_ptr;
    zx::eventpair import_token;
    zx_koid_t import_koid;
    fsl::MessageLoop::HandlerKey death_handler_key = 0;
  };

  void OnHandleReady(zx_handle_t handle,
                     zx_signals_t pending,
                     uint64_t count) override;

  void OnHandleError(zx_handle_t handle, zx_status_t error) override;

  std::vector<Import*> RemoveUnresolvedImportsForHandle(
      zx_handle_t import_handle);

  using ImportKoid = zx_koid_t;
  using ImportPtrsToImportEntries = std::unordered_map<Import*, ImportEntry>;
  using HandlesToImportKoidsMap = std::unordered_map<zx_handle_t, ImportKoid>;
  using KoidsToImportPtrs =
      std::unordered_map<ImportKoid, std::vector<Import*>>;

  ImportPtrsToImportEntries imports_;
  HandlesToImportKoidsMap handles_to_koids_;
  KoidsToImportPtrs koids_to_import_ptrs_;
  ResourceLinker* resource_linker_;
#ifndef NDEBUG
  size_t num_handler_keys_ = 0;
#endif
};

}  // namespace scene_manager
