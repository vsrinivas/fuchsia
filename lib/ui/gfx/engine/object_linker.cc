// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/gfx/engine/object_linker.h"

#include <lib/async/default.h>
#include <functional>

#include "garnet/public/lib/fsl/handles/object_info.h"
#include "lib/fxl/logging.h"

namespace scenic {
namespace gfx {

uint64_t ObjectLinkerBase::RegisterEntry(PendingEntry new_pending_entry,
                                         Entry new_entry,
                                         ErrorReporter* error_reporter,
                                         bool import) {
  zx_handle_t token = new_pending_entry.token.get();
  std::pair<zx_koid_t, zx_koid_t> peer_koids = fsl::GetKoids(token);
  if (peer_koids.first == ZX_KOID_INVALID ||
      peer_koids.second == ZX_KOID_INVALID) {
    error_reporter->ERROR() << "Token with ID " << token << " is invalid";
    return ZX_KOID_INVALID;
  }

  auto entry_iter = entries_.find(peer_koids.first);
  if (entry_iter != entries_.end()) {
    error_reporter->ERROR() << "Token with ID " << token
                            << " is already in use by this ObjectLinker";
    return ZX_KOID_INVALID;
  }

  // Actually add the entry, and immediately try to link it.
  auto emplaced_pending_entry =
      pending_entries_.emplace(peer_koids.first, std::move(new_pending_entry));
  FXL_DCHECK(emplaced_pending_entry.second);
  auto emplaced_entry =
      entries_.emplace(peer_koids.first, std::move(new_entry));
  FXL_DCHECK(emplaced_entry.second);
  if (import) {
    import_count_++;
    unresolved_import_count_++;
  } else {
    export_count_++;
    unresolved_export_count_++;
  }
  if (!AttemptLinking(peer_koids, emplaced_pending_entry.first,
                      emplaced_entry.first)) {
    // Linking failed, so listen for the peer's token dying to know if we need
    // to remove ourselves.
    WaitForPeerDeath(emplaced_pending_entry.first, import);
  }

  return peer_koids.first;
}

void ObjectLinkerBase::UnregisterEntry(uint64_t entry_handle, bool import) {
  const zx_koid_t entry_koid = static_cast<zx_koid_t>(entry_handle);
  auto entry_iter = entries_.find(entry_koid);
  if (entry_iter == entries_.end()) {
    FXL_LOG(WARNING) << "Attempted to remove unknown entry " << entry_handle
                     << "from ObjectLinker";
    return;
  }

  // If the object has a peer linked, tell it about the object being removed.
  zx_koid_t peer_entry_koid = entry_iter->second.peer_koid;
  if (peer_entry_koid != ZX_KOID_INVALID) {
    auto peer_entry_iter = entries_.find(peer_entry_koid);
    FXL_DCHECK(peer_entry_iter != entries_.end());

    peer_entry_iter->second.on_peer_destroyed();
    peer_entry_iter->second.peer_koid = ZX_KOID_INVALID;
  }

  // Erase the entries for the object.
  auto pending_entry_iter = pending_entries_.find(entry_koid);
  if (pending_entry_iter != pending_entries_.end()) {
    pending_entries_.erase(entry_koid);
    if (import) {
      unresolved_import_count_--;
    } else {
      unresolved_export_count_--;
    }
  }
  entries_.erase(entry_iter);
  if (import) {
    import_count_--;
  } else {
    export_count_--;
  }
}

void ObjectLinkerBase::WaitForPeerDeath(
    KoidToPendingEntryMap::iterator& pending_entry, bool import) {
  // Each entry must be removed from being considered for linking if its
  // partner's handle on the other side is closed before the 2 entries are
  // successfully linked.  This communication happens via the connection_closed
  // callback.
  //
  // Once linking has occured, this communication happens via UnregisterExport/
  // UnregisterImport and the peer_destroyed callback.
  zx_koid_t entry_koid = pending_entry->first;
  zx_handle_t entry_handle = pending_entry->second.token.get();
  pending_entry->second.peer_waiter = std::make_unique<async::Wait>(
      entry_handle, ZX_EVENTPAIR_PEER_CLOSED,
      std::bind([this, entry_koid, import]() {
        auto pending_entry_iter = pending_entries_.find(entry_koid);
        FXL_DCHECK(pending_entry_iter != pending_entries_.end());
        auto entry_iter = entries_.find(entry_koid);
        FXL_DCHECK(entry_iter != entries_.end());
        auto& entry = entry_iter->second;

        // Close the connection and erase the entry.
        entry.on_connection_closed();
        entries_.erase(entry_iter);
        pending_entries_.erase(pending_entry_iter);
        if (import) {
          unresolved_import_count_--;
          import_count_--;
        } else {
          unresolved_export_count_--;
          export_count_--;
        }
      }));
  zx_status_t status =
      pending_entry->second.peer_waiter->Begin(async_get_default());
  FXL_DCHECK(status == ZX_OK);
}

bool ObjectLinkerBase::AttemptLinking(
    KoidPair peer_koids, KoidToPendingEntryMap::iterator& pending_entry_iter,
    KoidToEntryMap::iterator& entry_iter) {
  auto peer_pending_entry_iter = pending_entries_.find(peer_koids.second);
  if (peer_pending_entry_iter == pending_entries_.end()) {
    return false;
  }

  // Destroy the pending entries (with the tokens and waiters) now that they
  // are no longer useful.
  pending_entries_.erase(pending_entry_iter);
  pending_entries_.erase(peer_pending_entry_iter);
  unresolved_export_count_--;
  unresolved_import_count_--;

  // These will always exist if both of the pending entries did.  Mark them as
  // linked.
  auto peer_entry_iter = entries_.find(peer_koids.second);
  FXL_DCHECK(peer_entry_iter != entries_.end());
  Entry& entry = entry_iter->second;
  Entry& peer_entry = peer_entry_iter->second;
  entry.peer_koid = peer_koids.second;
  peer_entry.peer_koid = peer_koids.first;

  // Fire callbacks last, so clients see a consistent view of the Linker.
  entry.on_link_resolved(this, &peer_entry);
  peer_entry.on_link_resolved(this, &entry);

  return true;
}

}  // namespace gfx
}  // namespace scenic
