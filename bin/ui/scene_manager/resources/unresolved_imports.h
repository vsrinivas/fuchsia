// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include <async/auto_wait.h>

#include "garnet/bin/ui/scene_manager/resources/import.h"

namespace scene_manager {

// Stores a list of imports that have not yet been bound to an export handle.
class UnresolvedImports {
 public:
  UnresolvedImports(ResourceLinker* resource_linker);

  // Adds an entry for an unresolved import. |import_koid| must be the koid
  // for |import_token|.
  void AddUnresolvedImport(Import* import,
                           zx::eventpair import_token,
                           zx_koid_t import_koid);

  // Listen for the death of the corresponding export token and removes any
  // matching imports if that happens.
  // If |import| is not in the collection of unresolved imports, this is a
  // no-op.
  void ListenForTokenPeerDeath(Import* import);

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
    std::unique_ptr<async::AutoWait> token_peer_death_waiter;
  };

  async_wait_result_t OnTokenPeerDeath(zx_koid_t import_koid,
                                       async_t*,
                                       zx_status_t status,
                                       const zx_packet_signal* signal);

  std::vector<Import*> RemoveUnresolvedImportsForKoid(zx_koid_t import_koid);

  using ImportKoid = zx_koid_t;
  using ImportPtrsToImportEntries = std::unordered_map<Import*, ImportEntry>;
  using KoidsToImportPtrs =
      std::unordered_map<ImportKoid, std::vector<Import*>>;

  ImportPtrsToImportEntries imports_;
  KoidsToImportPtrs koids_to_import_ptrs_;
  ResourceLinker* resource_linker_;
};

}  // namespace scene_manager
