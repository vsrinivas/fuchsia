// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_UNRESOLVED_IMPORTS_H_
#define GARNET_LIB_UI_GFX_ENGINE_UNRESOLVED_IMPORTS_H_

#include <unordered_map>

#include <lib/async/cpp/wait.h>

#include "garnet/lib/ui/gfx/resources/import.h"

namespace scenic {
namespace gfx {

// Stores a list of imports that have not yet been bound to an export handle.
class UnresolvedImports {
 public:
  UnresolvedImports(ResourceLinker* resource_linker);

  // Adds an entry for an unresolved import. |import_koid| must be the koid
  // for |import_token|.
  void AddUnresolvedImport(Import* import, zx::eventpair import_token,
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
    std::unique_ptr<async::Wait> token_peer_death_waiter;
  };

  void OnTokenPeerDeath(zx_koid_t import_koid, zx_status_t status,
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

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_ENGINE_UNRESOLVED_IMPORTS_H_
