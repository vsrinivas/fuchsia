// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/object_linker.h"

#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>

#include <functional>

#include "src/lib/fsl/handles/object_info.h"

namespace scenic_impl {
namespace gfx {

void ObjectLinkerBase::Link::LinkInvalidated(bool on_destruction) {
  if (link_invalidated_) {
    // link_invalidated_ will be reset to nullptr; this way we can avoid
    // assignment operators to |link_validated_|.
    auto link_invalidated_fn = std::move(link_invalidated_);
    link_invalidated_fn(on_destruction);
  }
}

void ObjectLinkerBase::Link::LinkUnresolved() {
  if (link_invalidated_) {
    link_invalidated_(false);
  }
}

size_t ObjectLinkerBase::UnresolvedExportCount() {
  return std::count_if(exports_.begin(), exports_.end(),
                       [](const auto& iter) { return iter.second.IsUnresolved(); });
}

size_t ObjectLinkerBase::UnresolvedImportCount() {
  return std::count_if(imports_.begin(), imports_.end(),
                       [](const auto& iter) { return iter.second.IsUnresolved(); });
}

zx_koid_t ObjectLinkerBase::CreateEndpoint(zx::handle token, ErrorReporter* error_reporter,
                                           bool is_import) {
  // Select imports or exports to operate on based on the flag.
  auto& endpoints = is_import ? imports_ : exports_;

  if (!token) {
    error_reporter->ERROR() << "Token is invalid";
    return ZX_KOID_INVALID;
  }

  zx_koid_t endpoint_id;
  zx_koid_t peer_endpoint_id;
  std::tie(endpoint_id, peer_endpoint_id) = fsl::GetKoids(token.get());
  if (endpoint_id == ZX_KOID_INVALID || peer_endpoint_id == ZX_KOID_INVALID) {
    error_reporter->ERROR() << "Token with ID " << token.get() << " refers to invalid objects";
    return ZX_KOID_INVALID;
  }

  auto endpoint_iter = endpoints.find(endpoint_id);
  if (endpoint_iter != endpoints.end()) {
    error_reporter->ERROR() << "Endpoint with id " << endpoint_id
                            << " is already in use by this ObjectLinker";
    return ZX_KOID_INVALID;
  }

  // Create a new endpoint in an unresolved state.  Full linking cannot occur
  // until Initialize() is called on the endpoint to provide a link object and
  // handler callbacks.
  Endpoint new_endpoint;
  new_endpoint.peer_endpoint_id = peer_endpoint_id;
  new_endpoint.peer_death_waiter = WaitForPeerDeath(token.get(), endpoint_id, is_import);
  new_endpoint.token = std::move(token);
  auto emplaced_endpoint = endpoints.emplace(endpoint_id, std::move(new_endpoint));
  FX_DCHECK(emplaced_endpoint.second);

  return endpoint_id;
}

void ObjectLinkerBase::DestroyEndpoint(zx_koid_t endpoint_id, bool is_import, bool destroy_peer) {
  auto& endpoints = is_import ? imports_ : exports_;
  auto& peer_endpoints = is_import ? exports_ : imports_;

  auto endpoint_iter = endpoints.find(endpoint_id);
  if (endpoint_iter == endpoints.end()) {
    FX_LOGS(ERROR) << "Attempted to remove an unknown endpoint " << endpoint_id
                   << " from ObjectLinker";
    return;
  }

  // If the object has a peer linked tell it about the object being removed,
  // which will immediately invalidate the peer.
  if (destroy_peer) {
    zx_koid_t peer_endpoint_id = endpoint_iter->second.peer_endpoint_id;
    auto peer_endpoint_iter = peer_endpoints.find(peer_endpoint_id);
    if (peer_endpoint_iter != peer_endpoints.end()) {
      Endpoint& peer_endpoint = peer_endpoint_iter->second;

      // Invalidate the peer endpoint.  If Initialize() has already been called on
      // the peer endpoint, then close its connection which will destroy it.
      // Otherwise, any future connection attempts will fail immediately with a
      // link_failed callback, due to peer_endpoint_id being marked as
      // invalid.
      //
      // This handles the case where the peer exists but Initialize() has not been
      // called on it yet (so no callbacks exist).
      peer_endpoint.peer_endpoint_id = ZX_KOID_INVALID;
      FX_DCHECK(peer_endpoint.link);
      peer_endpoint.link->Invalidate(/*on_destruction=*/false, /*invalidate_peer=*/true);
    }
  }

  // At this point it is safe to completely erase the endpoint for the object.
  endpoints.erase(endpoint_iter);
}

void ObjectLinkerBase::InitializeEndpoint(ObjectLinkerBase::Link* link, zx_koid_t endpoint_id,
                                          bool is_import) {
  FX_DCHECK(link);

  auto& endpoints = is_import ? imports_ : exports_;

  // Update the endpoint with the connection information.
  auto endpoint_iter = endpoints.find(endpoint_id);
  FX_DCHECK(endpoint_iter != endpoints.end());
  Endpoint& endpoint = endpoint_iter->second;

  // If the endpoint is no longer valid (i.e. its peer no longer exists), then
  // immediately signal a disconnection (which will destroy the endpoint)
  // instead of linking.
  //
  // This edge-case happens if the endpoint's peer is destroyed after the
  // endpoint is created, but before Initialize() is called on it.
  zx_koid_t peer_endpoint_id = endpoint.peer_endpoint_id;
  if (peer_endpoint_id == ZX_KOID_INVALID) {
    link->Invalidate(/*on_destruction=*/false, /*invalidate_peer=*/true);
    return;
  }

  if (!endpoint.link) {
    endpoint.link = link;

    // Attempt to locate and link with the endpoint's peer.
    AttemptLinking(endpoint_id, peer_endpoint_id, is_import);
  } else {
    endpoint.link = link;
  }
}

void ObjectLinkerBase::AttemptLinking(zx_koid_t endpoint_id, zx_koid_t peer_endpoint_id,
                                      bool is_import) {
  auto& endpoints = is_import ? imports_ : exports_;
  auto& peer_endpoints = is_import ? exports_ : imports_;

  auto endpoint_iter = endpoints.find(endpoint_id);
  FX_DCHECK(endpoint_iter != endpoints.end());

  auto peer_endpoint_iter = peer_endpoints.find(peer_endpoint_id);
  if (peer_endpoint_iter == peer_endpoints.end()) {
    return;  // Peer endpoint hasn't even been created yet, bail.
  }

  Endpoint& endpoint = endpoint_iter->second;
  Endpoint& peer_endpoint = peer_endpoint_iter->second;
  if (!peer_endpoint.link) {
    return;  // Peer endpoint isn't connected yet, bail.
  }

  // Delete the peer waiters now that the endpoints are resolved.
  endpoint.peer_death_waiter = nullptr;
  peer_endpoint.peer_death_waiter = nullptr;

  // Do linking last, so clients see a consistent view of the Linker.
  // Always fire the callback for the Export first, so clients can rely on
  // callbacks firing in a certain order.
  if (is_import) {
    peer_endpoint.link->LinkResolved(endpoint.link);
    endpoint.link->LinkResolved(peer_endpoint.link);
  } else {
    endpoint.link->LinkResolved(peer_endpoint.link);
    peer_endpoint.link->LinkResolved(endpoint.link);
  }
}

std::unique_ptr<async::Wait> ObjectLinkerBase::WaitForPeerDeath(zx_handle_t endpoint_handle,
                                                                zx_koid_t endpoint_id,
                                                                bool is_import) {
  // Each endpoint must be removed from being considered for linking if its
  // peer's handle is closed before the two entries are successfully linked.
  // This communication happens via the link_failed callback.
  //
  // Once linking has occurred, this communication happens via UnregisterExport
  // or UnregisterImport and the peer_destroyed callback.
  // TODO(fxbug.dev/24197): Follow up on __ZX_OBJECT_PEER_CLOSED with Zircon.
  static_assert(ZX_CHANNEL_PEER_CLOSED == __ZX_OBJECT_PEER_CLOSED, "enum mismatch");
  static_assert(ZX_EVENTPAIR_PEER_CLOSED == __ZX_OBJECT_PEER_CLOSED, "enum mismatch");
  static_assert(ZX_FIFO_PEER_CLOSED == __ZX_OBJECT_PEER_CLOSED, "enum mismatch");
  static_assert(ZX_SOCKET_PEER_CLOSED == __ZX_OBJECT_PEER_CLOSED, "enum mismatch");
  auto waiter = std::make_unique<async::Wait>(
      endpoint_handle, __ZX_OBJECT_PEER_CLOSED, 0, std::bind([this, endpoint_id, is_import]() {
        auto& endpoints = is_import ? imports_ : exports_;
        auto endpoint_iter = endpoints.find(endpoint_id);
        FX_DCHECK(endpoint_iter != endpoints.end());
        Endpoint& endpoint = endpoint_iter->second;

        // Invalidate the endpoint.  If Initialize() has
        // already been called on the endpoint, then close
        // its connection (which will cause it to be
        // destroyed).  Any future connection attempts will
        // fail immediately with a link_failed call, due to
        // peer_endpoint_id being marked as invalid.
        endpoint.peer_endpoint_id = ZX_KOID_INVALID;
        if (endpoint.link) {
          endpoint.link->Invalidate(/*on_destruction=*/false, /*invalidate_peer=*/true);
        }
      }));

  zx_status_t status = waiter->Begin(async_get_default_dispatcher());
  FX_DCHECK(status == ZX_OK);

  return waiter;
}

zx::handle ObjectLinkerBase::ReleaseToken(zx_koid_t endpoint_id, bool is_import) {
  auto& endpoints = is_import ? imports_ : exports_;
  auto& peer_endpoints = is_import ? exports_ : imports_;

  // If the endpoint was resolved, it will still be invalidated, but the peer endpoint must be
  // unresolved first if it exists.
  auto endpoint_iter = endpoints.find(endpoint_id);
  FX_DCHECK(endpoint_iter != endpoints.end());

  zx_koid_t peer_endpoint_id = endpoint_iter->second.peer_endpoint_id;

  auto peer_endpoint_iter = peer_endpoints.find(peer_endpoint_id);
  if (peer_endpoint_iter == peer_endpoints.end()) {
    return std::move(endpoint_iter->second.token);
  }

  // Signal that the link is now unresolved, then re-create the peer death waiter to flag the
  // endpoint as unresolved.
  if (peer_endpoint_iter->second.link) {
    peer_endpoint_iter->second.link->LinkUnresolved();
  }

  peer_endpoint_iter->second.peer_death_waiter =
      WaitForPeerDeath(peer_endpoint_iter->second.token.get(), peer_endpoint_id, !is_import);

  return std::move(endpoint_iter->second.token);
}

}  // namespace gfx
}  // namespace scenic_impl
