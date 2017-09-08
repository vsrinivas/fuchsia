// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "lib/mtl/tasks/message_loop.h"

#include "garnet/bin/ui/scene_manager/resources/import.h"

namespace scene_manager {

// Stores a list of imports that have not yet been bound to an export handle.
class UnresolvedImports : private mtl::MessageLoopHandler {
 public:
  UnresolvedImports(ResourceLinker* resource_linker);
  ~UnresolvedImports();

  // Adds an entry for an unresolved import. |import_koid| must be the koid
  // for |import_token|.
  void AddUnresolvedImport(Import* import,
                           mx::eventpair import_token,
                           mx_koid_t import_koid);

  // Listen for the death of the corresponding export token and removes any
  // matching imports if that happens.
  // If |import| is not in the collection of unresolved imports, this is a
  // no-op.
  void ListenForPeerHandleDeath(Import* import);

  // Removes and returns all imports corresponding to |import_koid|.
  std::vector<Import*> GetAndRemoveUnresolvedImportsForKoid(
      mx_koid_t import_koid);

  // A callback that informs us when an import has been destroyed.
  void OnImportDestroyed(Import* import);

  size_t NumUnresolvedImportsForKoid(mx_koid_t import_koid) const;

  size_t size() const { return imports_.size(); }

 private:
  struct ImportEntry {
    Import* import_ptr;
    mx::eventpair import_token;
    mx_koid_t import_koid;
    mtl::MessageLoop::HandlerKey death_handler_key = 0;
  };

  void OnHandleReady(mx_handle_t handle,
                     mx_signals_t pending,
                     uint64_t count) override;

  void OnHandleError(mx_handle_t handle, mx_status_t error) override;

  std::vector<Import*> RemoveUnresolvedImportsForHandle(
      mx_handle_t import_handle);

  using ImportKoid = mx_koid_t;
  using ImportPtrsToImportEntries = std::unordered_map<Import*, ImportEntry>;
  using HandlesToImportKoidsMap = std::unordered_map<mx_handle_t, ImportKoid>;
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
