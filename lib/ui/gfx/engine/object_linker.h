// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_OBJECT_LINKER_H_
#define GARNET_LIB_UI_GFX_ENGINE_OBJECT_LINKER_H_

#include <lib/async/cpp/wait.h>
#include <lib/fit/function.h>
#include <lib/zx/eventpair.h>
#include <zircon/types.h>
#include <memory>
#include <type_traits>
#include <unordered_map>

#include "garnet/lib/ui/scenic/util/error_reporter.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace scenic {
namespace gfx {

// Allows linking of peer objects in different sessions via eventpairs.  Two
// objects are considered peers if they each hold either end of an eventpair.
//
// This class contains common link functionality and operates on type-erased
// objects.  Use ObjectLinker to link objects of concrete types together.
class ObjectLinkerBase {
 public:
  ~ObjectLinkerBase() = default;

  size_t ExportCount() { return exports_.size(); }
  size_t UnresolvedExportCount() { return unresolved_exports_.size(); }
  size_t ImportCount() { return imports_.size(); }
  size_t UnresolvedImportCount() { return unresolved_imports_.size(); }

 protected:
  // Information for one end of a link registered with the linker.
  struct Endpoint {
    zx_koid_t peer_endpoint_id = ZX_KOID_INVALID;
    void* object = nullptr;  // Opaque pointer to client object
    fit::function<void(void* linked_object)> link_resolved;
    fit::closure link_disconnected;  // TODO: How to handle multiple imports?
  };

  // Information used to match one end of a link with its peer(s) on the
  // other end.
  struct UnresolvedEndpoint {
    zx::eventpair token;  // Token for initial matching to peer endpoint
    std::unique_ptr<async::Wait> peer_death_waiter;
  };

  // Only concrete ObjectLinker types should instantiate these.
  ObjectLinkerBase() = default;

  // Creates a new Endpoint for linking and reports any errors in creation using
  // |error_reporter|.
  //
  // Returns a koid that can be used to identify the registered Endpoint, or
  // ZX_KOID_INVALID if creation failed.
  zx_koid_t CreateEndpoint(zx::eventpair token, ErrorReporter* error_reporter,
                           bool is_import);

  // Destroys the Endpoint pointed to by |endpoint_id| and removes all traces
  // of it from the linker.  If the Endpoint is linked to a peer, the peer
  // will be notified of the Endpoint's destruction.
  void DestroyEndpoint(zx_koid_t endpoint_id, bool is_import);

  // Puts the Endpoint pointed to by |endpoint_id| into an initialized state by
  // supplying it with an object and connection callbacks.  The Endpoint will
  // not be linked until its peer is also initialized.
  void InitializeEndpoint(
      zx_koid_t endpoint_id, void* object,
      fit::function<void(void* linked_object)> link_resolved,
      fit::closure link_disconnected, bool is_import);

  // Attempts linking of the endpoints associated with |endpoint_id| and
  // |peer_endpoint_id|.
  //
  // The link will only succeed if both endpoints have been initialized first.
  void AttemptLinking(zx_koid_t endpoint_id, zx_koid_t peer_endpoint_id,
                      bool is_import);

  // Sets up an async::Wait on |Endpoint| that will fire a callback if the
  // Endpoint peer's token is destroyed before a link has been established.
  std::unique_ptr<async::Wait> WaitForPeerDeath(zx_handle_t Endpoint_handle,
                                                zx_koid_t Endpoint_koid,
                                                bool is_import);

  std::unordered_map<zx_koid_t, Endpoint> exports_;
  std::unordered_map<zx_koid_t, Endpoint> imports_;
  std::unordered_map<zx_koid_t, UnresolvedEndpoint> unresolved_exports_;
  std::unordered_map<zx_koid_t, UnresolvedEndpoint> unresolved_imports_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ObjectLinkerBase);
};

// Class which adds type information to ObjectLinkerBase.
//
// Allows linking of peer objects in different sessions via eventpairs.  Two
// objects are considered peers if they each hold either end of an eventpair.
//
// Accepts one endpoint of an eventpair and returns a Link object in response.
// The Link can be connected with its peer by providing a concrete object to
// link along with callbacks for both successful and unsuccessful resolution of
// the link.
//
// When the other endpoint of the eventpair is registered with the ObjectLinker,
// and Initialize() is called on the cooresponding Link, the provided resolution
// callbacks in both Links will be fired.
//
// If either endpoint is destroyed, this will cause the provided disconnection
// callback in its peer to be fired.  If the peer has not provided any callbacks
// yet via Initialize(), the disconnection callback will be fired when
// Initialize() is first called on the peer.  The callback will be fired
// regardless which peer is registered first.
//
// If the link was successful, the two peer objects have direct access to each
// other via pointers.
//
// Attempts to register either eventpair peer multiple times will result in an
// error.
// TODO(SCN-769): Allow multiple Imports.
//
// This class is thread-hostile.  It requires the owning thread to have a
// default async loop.
template <typename Export, typename Import>
class ObjectLinker : public ObjectLinkerBase {
 public:
  // Represents one endpoint of a Link.  This is returned by the CreateExport
  // and CreateImport calls.
  template <bool is_import>
  class Link {
   public:
    using Obj = typename std::conditional<is_import, Import, Export>::type;
    using LinkedObj =
        typename std::conditional<is_import, Export, Import>::type;

    // Move assignment/constructor.  These explicitly invalidate |other| after
    // they are called.
    Link(Link&& other) { *this = std::move(other); }
    Link& operator=(Link&& other);

    // The destructor will disconnect and invalidate the Link if it is still
    // connected.
    ~Link();

    bool valid() const { return linker_ && endpoint_id_ != ZX_KOID_INVALID; }
    bool initialized() const { return valid() && initialized_; }

    void Initialize(
        Obj* object,
        fit::function<void(LinkedObj* linked_object)> link_resolved =
            [](LinkedObj*) {},
        fit::closure link_disconnected = []() {});

   private:
    // Kept private so only an ObjectLinker can construct a Link.
    Link(zx_koid_t endpoint_id, fxl::WeakPtr<ObjectLinker> linker)
        : linker_(std::move(linker)), endpoint_id_(endpoint_id) {}
    void Destroy();

    fxl::WeakPtr<ObjectLinker> linker_;
    zx_koid_t endpoint_id_ = ZX_KOID_INVALID;
    bool initialized_ = false;

    friend class ObjectLinker;
  };
  using ExportLink = Link<false>;
  using ImportLink = Link<true>;

  ObjectLinker() : weak_factory_(this) {}
  ~ObjectLinker() = default;

  // Creates a cross-Session Link so an object can be associated with a paired
  // object in another Session.  The ObjectLinker takes ownership over the
  // provided |token|, which is used to locate the paired object.
  //
  // If a link cannot be created for some reason, |error_reporter| will be
  // used to flag an error.
  //
  // Once the objects are linked, they can communicate directly via pointers
  // within Scenic. This is true as soon as the Initialize() method is called on
  // the Links for both objects.
  //
  // The returned |ExportLink| is used to initiate and close the connection.
  ExportLink CreateExport(zx::eventpair token, ErrorReporter* error_reporter);

  // Creates a cross-Session Link so an object can be associated with a paired
  // object in another Session.  The ObjectLinker takes ownership over the
  // provided |token|, which is used to locate the paired object.
  //
  // If a link cannot be created for some reason, |error_reporter| will be
  // used to flag an error.
  //
  // Once the objects are linked, they can communicate directly via pointers
  // within Scenic. This is true as soon as the Initialize() method is called on
  // the Links for both objects.
  //
  // The returned |ImportLink| is used to initiate and close the connection.
  ImportLink CreateImport(zx::eventpair token, ErrorReporter* error_reporter);

 private:
  // Should be last.  See weak_ptr.h.
  fxl::WeakPtrFactory<ObjectLinker> weak_factory_;
};

// Template functions must be defined in the header.
template <typename Export, typename Import>
template <bool is_import>
auto ObjectLinker<Export, Import>::Link<is_import>::operator=(
    Link<is_import>&& other) -> Link<is_import>& {
  linker_ = std::move(other.linker_);
  endpoint_id_ = other.endpoint_id_;
  initialized_ = other.initialized_;

  // Invalidate it, so it won't cause a disconnection when it dies.
  other.endpoint_id_ = ZX_KOID_INVALID;
  other.initialized_ = false;
  return *this;
}

// Template functions must be defined in the header.
template <typename Export, typename Import>
template <bool is_import>
ObjectLinker<Export, Import>::Link<is_import>::~Link() {
  if (valid()) {
    Destroy();
  }
}

// Template functions must be defined in the header.
template <typename Export, typename Import>
template <bool is_import>
void ObjectLinker<Export, Import>::Link<is_import>::Initialize(
    Obj* object, fit::function<void(LinkedObj* linked_object)> link_resolved,
    fit::closure link_disconnected) {
  FXL_DCHECK(valid());
  FXL_DCHECK(!initialized());
  FXL_DCHECK(object);
  FXL_DCHECK(link_resolved);
  FXL_DCHECK(link_disconnected);

  linker_->InitializeEndpoint(
      endpoint_id_, object,
      [resolved_cb = std::move(link_resolved)](void* object) {
        resolved_cb(static_cast<LinkedObj*>(object));
      },
      std::move(link_disconnected), is_import);
  initialized_ = true;
}

// Template functions must be defined in the header.
template <typename Export, typename Import>
template <bool is_import>
void ObjectLinker<Export, Import>::Link<is_import>::Destroy() {
  FXL_DCHECK(valid());

  linker_->DestroyEndpoint(endpoint_id_, is_import);
}

// Template functions must be defined in the header.
template <typename Export, typename Import>
typename ObjectLinker<Export, Import>::ExportLink
ObjectLinker<Export, Import>::CreateExport(zx::eventpair token,
                                           ErrorReporter* error_reporter) {
  zx_koid_t endpoint_id =
      CreateEndpoint(std::move(token), error_reporter, false);
  return ExportLink(endpoint_id, weak_factory_.GetWeakPtr());
}

// Template functions must be defined in the header.
template <typename Export, typename Import>
typename ObjectLinker<Export, Import>::ImportLink
ObjectLinker<Export, Import>::CreateImport(zx::eventpair token,
                                           ErrorReporter* error_reporter) {
  zx_koid_t endpoint_id =
      CreateEndpoint(std::move(token), error_reporter, true);
  return ImportLink(endpoint_id, weak_factory_.GetWeakPtr());
}

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_ENGINE_OBJECT_LINKER_H_
