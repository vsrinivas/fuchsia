// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_OBJECT_LINKER_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_OBJECT_LINKER_H_

#include <lib/async/cpp/wait.h>
#include <lib/fit/function.h>
#include <lib/zx/handle.h>
#include <lib/zx/object_traits.h>
#include <zircon/types.h>

#include <cstdint>
#include <memory>
#include <type_traits>
#include <unordered_map>

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/scenic/lib/scenic/util/error_reporter.h"

namespace scenic_impl {
namespace gfx {

// Contains common linking functionality that operates on type-erased objects.
// Use ObjectLinker to link objects of concrete types together.
class ObjectLinkerBase {
 public:
  virtual ~ObjectLinkerBase() = default;

  size_t ExportCount() { return exports_.size(); }
  size_t UnresolvedExportCount();
  size_t ImportCount() { return imports_.size(); }
  size_t UnresolvedImportCount();

 protected:
  class Link {
   public:
    virtual ~Link() = default;

   protected:
    // When invalidating a link, the caller may choose to not invalidate the peer, which instead
    // returns the peer to an initialized-but-unresolved state. This is primarily used when the
    // caller releases the token of an existing link.
    virtual void Invalidate(bool on_destruction, bool invalidate_peer) = 0;
    // Must be virtual so ObjectLinker::Link can pull the typed PeerObject from `peer_link`.
    virtual void LinkResolved(ObjectLinkerBase::Link* peer_link) = 0;

    // Invalidating a link deletes the token it was created with, making the link permanently
    // invalid and therefore allowing for the deletion of the |link_invalidated_| callback.
    // Unresolving a link means its peer's token was released and may be used again, so the callback
    // is called but not deleted.
    void LinkInvalidated(bool on_destruction);
    void LinkUnresolved();

    fit::function<void(bool on_destruction)> link_invalidated_;

    friend class ObjectLinkerBase;
  };

  // Information for one end of a Link registered with the linker.
  struct Endpoint {
    zx_koid_t peer_endpoint_id = ZX_KOID_INVALID;
    Link* link = nullptr;
    zx::handle token;                                // The token may be released by the link owner.
    std::unique_ptr<async::Wait> peer_death_waiter;  // Only non-null if the link is unresolved.

    bool IsUnresolved() const { return peer_death_waiter != nullptr; }
  };

  // Only concrete ObjectLinker types should instantiate these.
  ObjectLinkerBase() = default;

  // Creates a new Endpoint for linking and reports any errors in creation
  // using |error_reporter|.
  //
  // Returns a zx_koid_t that uniquely identifies the registered Endpoint, or
  // ZX_KOID_INVALID if creation failed.
  zx_koid_t CreateEndpoint(zx::handle token, ErrorReporter* error_reporter, bool is_import);

  // Destroys the Endpoint pointed to by |endpoint_id| and removes all traces
  // of it from the linker.  If the Endpoint is linked to a peer, the peer
  // will be notified of the Endpoint's destruction.
  void DestroyEndpoint(zx_koid_t endpoint_id, bool is_import, bool destroy_peer);

  // Puts the Endpoint pointed to by |endpoint_id| into an initialized state
  // by supplying it with an object and connection callbacks.  The Endpoint
  // will not be linked until its peer is also initialized.
  void InitializeEndpoint(ObjectLinkerBase::Link* link, zx_koid_t endpoint_id, bool is_import);

  // Attempts linking of the endpoints associated with |endpoint_id| and
  // |peer_endpoint_id|.
  //
  // The operation will only succeed if both endpoints have been initialized
  // first.
  void AttemptLinking(zx_koid_t endpoint_id, zx_koid_t peer_endpoint_id, bool is_import);

  // Sets up an async::Wait on |Endpoint| that will fire a callback if the
  // Endpoint peer's token is destroyed before a link has been established.
  std::unique_ptr<async::Wait> WaitForPeerDeath(zx_handle_t endpoint_handle, zx_koid_t endpoint_id,
                                                bool is_import);

  // Releases the zx::handle for the Endpoint associated with |endpoint_id|, allowing the caller
  // to establish a new link with it.
  //
  // This operation works regardless of whether or not the link has resolved. If the link was
  // resolved, the peer Endpoint receives a |link_invalidated| callback and is put back in the
  // initialized-but-unresolved state.
  zx::handle ReleaseToken(zx_koid_t endpoint_id, bool is_import);

  std::unordered_map<zx_koid_t, Endpoint> exports_;
  std::unordered_map<zx_koid_t, Endpoint> imports_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ObjectLinkerBase);
};

// Allows direct linking of peer objects, regardless of which session(s) they
// exist in.  Once the objects are linked, they have direct references to each
// other.
//
// This linking is accomplished via lookup between pairable kernel objects.
// zx::eventpair objects are a natural fit for this purpose and are commonly
// used.
//
// To create a Link, provide a handle to one half of a pairable kernel object
// (zx::object_traits:supports_duplication is true) to the |CreateExport| or
// |CreateImport| methods.  It can be connected with its peer by providing a
// concrete object to link along with callbacks for both successful and
// unsuccessful resolution.
//
// When the other half of the kernel object is registered with the
// ObjectLinker, and Initialize() is called on the corresponding Link, the
// provided resolution callbacks in both Links will be fired.  The callback
// associated with the Export will always fire first.
//
// If either Link endpoint is destroyed, this will cause the provided
// disconnection callback on its peer endpoint to be fired.  If the peer
// endpoint has not been provided any callbacks yet via Initialize(), the
// disconnection callback will be fired later when Initialize() is first called
// on it.
//
// Attempts to register either half of the kernel object multiple times, even
// through cloned handles, will result in an error.
// TODO(fxbug.dev/23989): Allow multiple Imports.
//
// This class is thread-hostile.  It requires the owning thread to have a
// default async loop.
template <typename Export, typename Import>
class ObjectLinker : public ObjectLinkerBase {
 public:
  // Represents one endpoint of a Link between two objects in different
  // |Session|s.
  //
  // Links can be moved, but not copied.  Valid Links can only be constructed by
  // the CreateExport and CreateImport methods.
  template <bool is_import>
  class Link : public ObjectLinkerBase::Link {
   public:
    using Obj = typename std::conditional<is_import, Import, Export>::type;
    using PeerObj = typename std::conditional<is_import, Export, Import>::type;

    Link() = default;
    virtual ~Link() { Invalidate(/*on_destruction=*/true, /*invalidate_peer=*/true); }
    Link(Link&& other) { *this = std::move(other); }
    Link& operator=(nullptr_t) { Invalidate(/*on_destruction=*/false, /*invalidate_peer=*/true); }
    Link& operator=(Link&& other);

    bool valid() const { return linker_ && endpoint_id_ != ZX_KOID_INVALID; }
    bool initialized() const { return valid() && link_resolved_; }
    zx_koid_t endpoint_id() const { return endpoint_id_; }

    // Initialize the Link with an |object| and callbacks for |link_resolved|
    // and |link_invalidated| events, making it ready for connection to its peer.
    // peer. The |link_invalidated| event is guaranteed to be called regardless of
    // whether or not the |link_resolved| callback is, including if this Link
    // is destroyed, in which case |on_destruction| will be true.
    void Initialize(fit::function<void(PeerObj peer_object)> link_resolved = nullptr,
                    fit::function<void(bool on_destruction)> link_invalidated = nullptr);

    // Releases the zx::handle for this link, allowing the caller to establish a new link with it.
    //
    // This operation works regardless of whether or not the link has resolved. If the link was
    // resolved, the peer receives a |link_invalidated| callback and is put back in the
    // initialized-but-unresolved state.
    std::optional<zx::handle> ReleaseToken();

   private:
    // Kept private so only an ObjectLinker can construct a valid Link.
    Link(Obj object, zx_koid_t endpoint_id, fxl::WeakPtr<ObjectLinker> linker)
        : object_(std::move(object)), endpoint_id_(endpoint_id), linker_(std::move(linker)) {}

    void LinkResolved(ObjectLinkerBase::Link* peer_link) override;
    void Invalidate(bool on_destruction, bool invalidate_peer) override;

    std::optional<Obj> object_;
    zx_koid_t endpoint_id_ = ZX_KOID_INVALID;
    fxl::WeakPtr<ObjectLinker> linker_;
    fit::function<void(PeerObj peer_link)> link_resolved_;

    friend class ObjectLinker;
    friend class ObjectLinker::Link<!is_import>;
  };
  using ExportLink = Link<false>;
  using ImportLink = Link<true>;

  ObjectLinker() : weak_factory_(this) {}
  ~ObjectLinker() = default;

  // Creates an outgoing cross-session ExportLink between two objects, which
  // can be used to initiate and close the connection between them.
  //
  // The ObjectLinker uses the provided |token| to locate the paired ImportLink.
  // |token| must reference a pairable kernel object type such as |zx::channel|
  // or |zx::eventpair|.  |token| may not reference a kernel object that is in
  // use by this ObjectLinker.
  //
  // If a link cannot be created, |error_reporter| will be used to flag an
  // error.
  //
  // The objects are linked as soon as the |Initialize()| method is called on
  // the links for both objects.
  template <typename T, typename = std::enable_if_t<zx::object_traits<T>::has_peer_handle>>
  ExportLink CreateExport(Export export_obj, T token, ErrorReporter* error_reporter) {
    const zx_koid_t endpoint_id = CreateEndpoint(std::move(token), error_reporter, false);
    return ExportLink(std::move(export_obj), endpoint_id, weak_factory_.GetWeakPtr());
  }

  // Creates an incoming cross-session ImportLink between two objects, which
  // can be used to initiate and close the connection between them.
  //
  // The ObjectLinker uses the provided |token| to locate the paired ExportLink.
  // |token| must reference a pairable kernel object type such as |zx::channel|
  // or |zx::eventpair|.  |token| may not reference a kernel object that is in
  // use by this ObjectLinker.
  //
  // If a link cannot be created, |error_reporter| will be used to flag an

  // the links for both objects.
  template <typename T, typename = std::enable_if_t<zx::object_traits<T>::has_peer_handle>>
  ImportLink CreateImport(Import import_obj, T token, ErrorReporter* error_reporter) {
    const zx_koid_t endpoint_id = CreateEndpoint(std::move(token), error_reporter, true);
    return ImportLink(std::move(import_obj), endpoint_id, weak_factory_.GetWeakPtr());
  }

 private:
  // Should be last.  See weak_ptr.h.
  fxl::WeakPtrFactory<ObjectLinker> weak_factory_;
};

template <typename Export, typename Import>
template <bool is_import>
auto ObjectLinker<Export, Import>::Link<is_import>::operator=(Link&& other) -> Link& {
  // Invalidate the existing Link if its still valid.
  Invalidate(/*on_destruction=*/false, /*invalidate_peer=*/true);

  // Move data from the other Link and manually invalidate it, so it won't destroy
  // its endpoint when it dies.
  link_resolved_ = std::move(other.link_resolved_);
  link_invalidated_ = std::move(other.link_invalidated_);
  object_ = std::move(other.object_);
  linker_ = std::move(other.linker_);
  endpoint_id_ = std::move(other.endpoint_id_);
  other.endpoint_id_ = ZX_KOID_INVALID;

  if (initialized()) {
    linker_->InitializeEndpoint(this, endpoint_id_, is_import);
  }

  return *this;
}

template <typename Export, typename Import>
template <bool is_import>
void ObjectLinker<Export, Import>::Link<is_import>::Initialize(
    fit::function<void(PeerObj peer_object)> link_resolved,
    fit::function<void(bool on_destruction)> link_invalidated) {
  FX_DCHECK(valid());
  FX_DCHECK(!initialized());
  FX_DCHECK(link_resolved);

  link_resolved_ = std::move(link_resolved);
  link_invalidated_ = std::move(link_invalidated);

  linker_->InitializeEndpoint(this, endpoint_id_, is_import);
}

template <typename Export, typename Import>
template <bool is_import>
std::optional<zx::handle> ObjectLinker<Export, Import>::Link<is_import>::ReleaseToken() {
  if (!valid()) {
    return std::optional<zx::handle>();
  }
  zx::handle token = linker_->ReleaseToken(endpoint_id_, is_import);
  Invalidate(/*on_destruction=*/false, /*invalidate_peer=*/false);
  return std::move(token);
}

template <typename Export, typename Import>
template <bool is_import>
void ObjectLinker<Export, Import>::Link<is_import>::Invalidate(bool on_destruction,
                                                               bool invalidate_peer) {
  if (valid()) {
    linker_->DestroyEndpoint(endpoint_id_, is_import, invalidate_peer);
  }
  linker_.reset();
  object_.reset();
  link_resolved_ = nullptr;
  endpoint_id_ = ZX_KOID_INVALID;
  LinkInvalidated(on_destruction);
}

template <typename Export, typename Import>
template <bool is_import>
void ObjectLinker<Export, Import>::Link<is_import>::LinkResolved(
    ObjectLinkerBase::Link* peer_link) {
  if (link_resolved_) {
    auto* typed_peer_link = static_cast<Link<!is_import>*>(peer_link);
    FX_DCHECK(typed_peer_link->object_.has_value());
    link_resolved_(std::move(typed_peer_link->object_.value()));
  }
}

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_OBJECT_LINKER_H_
