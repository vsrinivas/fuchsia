// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/object_linker.h"

#include <lib/async/default.h>
#include <functional>

#include "lib/fsl/handles/object_info.h"
#include "lib/fxl/logging.h"

namespace scenic {
namespace gfx {

zx_koid_t ObjectLinkerBase::CreateEndpoint(zx::eventpair token,
                                           ErrorReporter* error_reporter,
                                           bool is_import) {
  // Select imports or exports to operate on based on the flag.
  auto& endpoints = is_import ? imports_ : exports_;
  auto& unresolved_endpoints =
      is_import ? unresolved_imports_ : unresolved_exports_;

  if (!token.is_valid()) {
    error_reporter->ERROR() << "Token is invalid";
    return ZX_KOID_INVALID;
  }

  zx_handle_t token_handle = token.get();
  std::pair<zx_koid_t, zx_koid_t> peer_koids = fsl::GetKoids(token_handle);
  zx_koid_t& endpoint_id = peer_koids.first;
  zx_koid_t& peer_endpoint_id = peer_koids.second;
  if (endpoint_id == ZX_KOID_INVALID || peer_endpoint_id == ZX_KOID_INVALID) {
    error_reporter->ERROR()
        << "Token with ID " << token_handle << " refers to invalid objects";
    return ZX_KOID_INVALID;
  }

  auto endpoint_iter = endpoints.find(endpoint_id);
  if (endpoint_iter != endpoints.end()) {
    error_reporter->ERROR() << "Token with ID " << token_handle
                            << " is already in use by this ObjectLinker";
    return ZX_KOID_INVALID;
  }

  // Create a new endpoint in an unresolved state.  Full linking cannot occur
  // until Initialize() is called on the endpoint to provide a link object and
  // handler callbacks.
  Endpoint new_endpoint;
  UnresolvedEndpoint new_unresolved_endpoint;
  new_endpoint.peer_endpoint_id = peer_endpoint_id;
  new_unresolved_endpoint.token = std::move(token);
  new_unresolved_endpoint.peer_death_waiter =
      WaitForPeerDeath(token_handle, endpoint_id, is_import);
  auto emplaced_endpoint =
      endpoints.emplace(endpoint_id, std::move(new_endpoint));
  auto emplaced_unresolved_endpoint = unresolved_endpoints.emplace(
      endpoint_id, std::move(new_unresolved_endpoint));
  FXL_DCHECK(emplaced_endpoint.second);
  FXL_DCHECK(emplaced_unresolved_endpoint.second);

  return endpoint_id;
}

void ObjectLinkerBase::DestroyEndpoint(zx_koid_t endpoint_id, bool is_import) {
  auto& endpoints = is_import ? imports_ : exports_;
  auto& peer_endpoints = is_import ? exports_ : imports_;
  auto& unresolved_endpoints =
      is_import ? unresolved_imports_ : unresolved_exports_;

  auto endpoint_iter = endpoints.find(endpoint_id);
  if (endpoint_iter == endpoints.end()) {
    FXL_LOG(ERROR) << "Attempted to remove unknown endpoint " << endpoint_id
                   << "from ObjectLinker";
    return;
  }

  // If the object has a peer linked tell it about the object being removed,
  // which will invalidate the peer.
  zx_koid_t peer_endpoint_id = endpoint_iter->second.peer_endpoint_id;
  auto peer_endpoint_iter = peer_endpoints.find(peer_endpoint_id);
  if (peer_endpoint_iter != peer_endpoints.end()) {
    Endpoint& peer_endpoint = peer_endpoint_iter->second;

    // Invalidate the peer endpoint.  If Initialize() has already been called on
    // the peer endpoint, then close its connection.  Any future connection
    // attempts will fail immediately with a link_disconnected call, due to
    // peer_endpoint_id being marked as invalid.
    //
    // This handles the case where the peer exists but Initialize() has not been
    // called on it yet (so no callbacks exist).
    peer_endpoint.peer_endpoint_id = ZX_KOID_INVALID;
    if (peer_endpoint.link_disconnected) {
      peer_endpoint.link_disconnected();
    }
  }

  // At this point it is safe to completely erase the endpoint for the object.
  unresolved_endpoints.erase(endpoint_id);
  endpoints.erase(endpoint_iter);
}

void ObjectLinkerBase::InitializeEndpoint(
    zx_koid_t endpoint_id, void* object,
    fit::function<void(void* linked_object)> link_resolved,
    fit::closure link_disconnected, bool is_import) {
  FXL_DCHECK(object);
  FXL_DCHECK(link_resolved);
  FXL_DCHECK(link_disconnected);

  auto& endpoints = is_import ? imports_ : exports_;

  // Update the endpoint with the connection information.
  auto endpoint_iter = endpoints.find(endpoint_id);
  FXL_DCHECK(endpoint_iter != endpoints.end());
  Endpoint& endpoint = endpoint_iter->second;
  FXL_DCHECK(!endpoint.object);
  endpoint.object = object;
  endpoint.link_resolved = std::move(link_resolved);
  endpoint.link_disconnected = std::move(link_disconnected);

  // If the endpoint is no longer valid (i.e. its peer no longer exists), then
  // immediately signal a disconnection on the endpoint instead of linking.
  // The invalidated endpoint will be cleaned up when its owning Link is
  // destroyed.
  //
  // This exotic edge-case happens if the endpoint's peer is destroyed after
  // the endpoint is created, but before Initialize() is called on it.
  zx_koid_t peer_endpoint_id = endpoint.peer_endpoint_id;
  if (peer_endpoint_id == ZX_KOID_INVALID) {
    endpoint.link_disconnected();
    return;
  }

  // Attempt to locate and link with the endpoint's peer.
  AttemptLinking(endpoint_id, peer_endpoint_id, is_import);
}

void ObjectLinkerBase::AttemptLinking(zx_koid_t endpoint_id,
                                      zx_koid_t peer_endpoint_id,
                                      bool is_import) {
  auto& endpoints = is_import ? imports_ : exports_;
  auto& peer_endpoints = is_import ? exports_ : imports_;
  auto& unresolved_endpoints =
      is_import ? unresolved_imports_ : unresolved_exports_;
  auto& peer_unresolved_endpoints =
      is_import ? unresolved_exports_ : unresolved_imports_;

  auto endpoint_iter = endpoints.find(endpoint_id);
  FXL_DCHECK(endpoint_iter != endpoints.end());

  auto peer_endpoint_iter = peer_endpoints.find(peer_endpoint_id);
  if (peer_endpoint_iter == peer_endpoints.end()) {
    return;  // Peer endpoint hasn't even been created yet, bail.
  }

  Endpoint& endpoint = endpoint_iter->second;
  Endpoint& peer_endpoint = peer_endpoint_iter->second;
  if (!peer_endpoint.object) {
    return;  // Peer endpoint isn't connected yet, bail.
  }

  // Destroy the pending entries (with the tokens and waiters) now that they
  // are no longer useful.
  size_t erase_count = unresolved_endpoints.erase(endpoint_id);
  FXL_DCHECK(erase_count == 1);
  size_t peer_erase_count = peer_unresolved_endpoints.erase(peer_endpoint_id);
  FXL_DCHECK(peer_erase_count == 1);

  // Do linking last, so clients see a consistent view of the Linker.
  endpoint.link_resolved(peer_endpoint.object);
  peer_endpoint.link_resolved(endpoint.object);
}

std::unique_ptr<async::Wait> ObjectLinkerBase::WaitForPeerDeath(
    zx_handle_t endpoint_handle, zx_koid_t endpoint_id, bool is_import) {
  // Each endpoint must be removed from being considered for linking if its
  // peer's handle on the other side is closed before the two entries are
  // successfully linked.  This communication happens via the link_disconnected
  // callback.
  //
  // Once linking has occurred, this communication happens via UnregisterExport
  // or UnregisterImport and the peer_destroyed callback.
  auto waiter = std::make_unique<async::Wait>(
      endpoint_handle, ZX_EVENTPAIR_PEER_CLOSED,
      std::bind([this, endpoint_id, is_import]() {
        auto& endpoints = is_import ? imports_ : exports_;
        auto& unresolved_endpoints =
            is_import ? unresolved_imports_ : unresolved_exports_;

        auto endpoint_iter = endpoints.find(endpoint_id);
        FXL_DCHECK(endpoint_iter != endpoints.end());
        Endpoint& endpoint = endpoint_iter->second;

        // Invalidate the endpoint.  If Initialize() has already been called on
        // the endpoint, then close its connection.  Any future connection
        // attempts will fail immediately with a link_disconnected call, due to
        // peer_endpoint_id being marked as invalid.
        endpoint.peer_endpoint_id = ZX_KOID_INVALID;
        if (endpoint.object) {
          endpoint.link_disconnected();
        }
        unresolved_endpoints.erase(endpoint_id);
      }));

  zx_status_t status = waiter->Begin(async_get_default_dispatcher());
  FXL_DCHECK(status == ZX_OK);

  return waiter;
}

}  // namespace gfx
}  // namespace scenic
