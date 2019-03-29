// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_OBJECT_LINKER_H_
#define GARNET_LIB_UI_GFX_ENGINE_OBJECT_LINKER_H_

#include <lib/async/cpp/wait.h>
#include <lib/fit/function.h>
#include <lib/zx/handle.h>
#include <lib/zx/object_traits.h>
#include <zircon/types.h>
#include <memory>
#include <type_traits>
#include <unordered_map>

#include "garnet/lib/ui/scenic/util/error_reporter.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace scenic_impl {
namespace gfx {

// Contains common linking functionality that operates on type-erased objects.
// Use ObjectLinker to link objects of concrete types together.
class ObjectLinkerBase {
 public:
  virtual ~ObjectLinkerBase() = default;

  // Returns the corresponding import's client object.
  void* GetImport(zx_koid_t endpoint_id) {
    auto it = imports_.find(endpoint_id);
    return it != imports_.end() ? it->second.object : nullptr;
  }
  size_t ExportCount() { return exports_.size(); }
  size_t UnresolvedExportCount() { return unresolved_exports_.size(); }
  size_t ImportCount() { return imports_.size(); }
  size_t UnresolvedImportCount() { return unresolved_imports_.size(); }

 protected:
  // Information for one end of a Link registered with the linker.
  struct Endpoint {
    zx_koid_t peer_endpoint_id = ZX_KOID_INVALID;
    void* object = nullptr;  // Opaque pointer to client object
    fit::function<void(void* linked_object)> link_resolved;
    fit::closure link_failed;  // TODO(SCN-769): How to multiple imports?
  };

  // Information used to match one end of a link with its peer(s) on the
  // other end.
  struct UnresolvedEndpoint {
    zx::handle token;  // Token for initial matching to peer endpoint.
    std::unique_ptr<async::Wait> peer_death_waiter;
  };

  // Only concrete ObjectLinker types should instantiate these.
  ObjectLinkerBase() = default;

  // Creates a new Endpoint for linking and reports any errors in creation
  // using |error_reporter|.
  //
  // Returns a zx_koid_t that uniquely identifies the registered Endpoint, or
  // ZX_KOID_INVALID if creation failed.
  zx_koid_t CreateEndpoint(zx::handle token, ErrorReporter* error_reporter,
                           bool is_import);

  // Destroys the Endpoint pointed to by |endpoint_id| and removes all traces
  // of it from the linker.  If the Endpoint is linked to a peer, the peer
  // will be notified of the Endpoint's destruction.
  void DestroyEndpoint(zx_koid_t endpoint_id, bool is_import);

  // Puts the Endpoint pointed to by |endpoint_id| into an initialized state
  // by supplying it with an object and connection callbacks.  The Endpoint
  // will not be linked until its peer is also initialized.
  void InitializeEndpoint(
      zx_koid_t endpoint_id, void* object,
      fit::function<void(void* linked_object)> link_resolved,
      fit::closure link_failed, bool is_import);

  // Attempts linking of the endpoints associated with |endpoint_id| and
  // |peer_endpoint_id|.
  //
  // The operation will only succeed if both endpoints have been initialized
  // first.
  void AttemptLinking(zx_koid_t endpoint_id, zx_koid_t peer_endpoint_id,
                      bool is_import);

  // Sets up an async::Wait on |Endpoint| that will fire a callback if the
  // Endpoint peer's token is destroyed before a link has been established.
  std::unique_ptr<async::Wait> WaitForPeerDeath(zx_handle_t endpoint_handle,
                                                zx_koid_t endpoint_id,
                                                bool is_import);

  std::unordered_map<zx_koid_t, Endpoint> exports_;
  std::unordered_map<zx_koid_t, Endpoint> imports_;
  std::unordered_map<zx_koid_t, UnresolvedEndpoint> unresolved_exports_;
  std::unordered_map<zx_koid_t, UnresolvedEndpoint> unresolved_imports_;

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
// TODO(SCN-769): Allow multiple Imports.
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
  class Link {
   public:
    using Obj = typename std::conditional<is_import, Import, Export>::type;
    using PeerObj = typename std::conditional<is_import, Export, Import>::type;

    Link() {}
    ~Link() { Invalidate(); }
    Link(Link&& other) { *this = std::move(other); }
    Link& operator=(nullptr_t) { Invalidate(); }
    Link& operator=(Link&& other);

    bool valid() const { return linker_ && endpoint_id_ != ZX_KOID_INVALID; }
    bool initialized() const { return valid() && initialized_; }
    PeerObj* peer() { return peer_object_; }

    // Initialize the Link with an |object| and callbacks for |link_resolved|
    // and |link_failed| events, making it ready for connection to its
    // peer.
    void Initialize(
        Obj* object,
        fit::function<void(PeerObj* peer_object)> link_resolved =
            [](PeerObj*) {},
        fit::closure link_failed = []() {});

   private:
    // Kept private so only an ObjectLinker can construct a valid Link.
    Link(zx_koid_t endpoint_id, fxl::WeakPtr<ObjectLinker> linker)
        : linker_(std::move(linker)), endpoint_id_(endpoint_id) {}

    void Invalidate(bool destroy_endpoint = true);

    fxl::WeakPtr<ObjectLinker> linker_;
    zx_koid_t endpoint_id_ = ZX_KOID_INVALID;
    PeerObj* peer_object_ = nullptr;
    bool initialized_ = false;

    friend class ObjectLinker;
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
  template <typename T,
            typename = std::enable_if_t<zx::object_traits<T>::has_peer_handle>>
  ExportLink CreateExport(T token, ErrorReporter* error_reporter) {
    const zx_koid_t endpoint_id =
        CreateEndpoint(std::move(token), error_reporter, false);
    return ExportLink(endpoint_id, weak_factory_.GetWeakPtr());
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
  template <typename T,
            typename = std::enable_if_t<zx::object_traits<T>::has_peer_handle>>
  ImportLink CreateImport(T token, ErrorReporter* error_reporter) {
    const zx_koid_t endpoint_id =
        CreateEndpoint(std::move(token), error_reporter, true);
    return ImportLink(endpoint_id, weak_factory_.GetWeakPtr());
  }

 private:
  // Should be last.  See weak_ptr.h.
  fxl::WeakPtrFactory<ObjectLinker> weak_factory_;
};

template <typename Export, typename Import>
template <bool is_import>
auto ObjectLinker<Export, Import>::Link<is_import>::operator=(Link&& other)
    -> Link& {
  // Invalidate the existing Link if its still valid.
  Invalidate();

  // Move data from the other Link and invalidate it, so it won't destroy
  // its endpoint when it dies.
  linker_ = other.linker_;
  peer_object_ = other.peer_object_;
  endpoint_id_ = other.endpoint_id_;
  initialized_ = other.initialized_;
  other.Invalidate(false /* destroy_endpoint */);

  return *this;
}

template <typename Export, typename Import>
template <bool is_import>
void ObjectLinker<Export, Import>::Link<is_import>::Initialize(
    Obj* object, fit::function<void(PeerObj* peer_object)> link_resolved,
    fit::closure link_failed) {
  FXL_DCHECK(valid());
  FXL_DCHECK(!initialized());
  FXL_DCHECK(!peer());
  FXL_DCHECK(object);
  FXL_DCHECK(link_resolved);
  FXL_DCHECK(link_failed);

  linker_->InitializeEndpoint(
      endpoint_id_, object,
      [this, resolved_cb = std::move(link_resolved)](void* object) {
        peer_object_ = static_cast<PeerObj*>(object);
        resolved_cb(peer_object_);
      },
      // Be careful when invoking this closure! It needs to be moved out of the
      // underlying endpoint before being invoked, because the underlying
      // endpoint will be destroyed in Invalidate() and we don't want
      // disconnected_cb to be destroyed along with it.
      // TODO(SCN-1257): Make this safe to invoke.
      [this, disconnected_cb = std::move(link_failed)]() {
        Invalidate();
        disconnected_cb();
      },
      is_import);
  initialized_ = true;
}

template <typename Export, typename Import>
template <bool is_import>
void ObjectLinker<Export, Import>::Link<is_import>::Invalidate(
    bool destroy_endpoint) {
  if (valid() && destroy_endpoint) {
    linker_->DestroyEndpoint(endpoint_id_, is_import);
  }
  linker_.reset();
  peer_object_ = nullptr;
  endpoint_id_ = ZX_KOID_INVALID;
  initialized_ = false;
}

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_ENGINE_OBJECT_LINKER_H_
