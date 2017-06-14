// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/src/scene/resources/resource.h"
#include "lib/ftl/macros.h"
#include "magenta/system/ulib/mx/include/mx/eventpair.h"

namespace mozart {
namespace scene {

class ProxyResource;
using ProxyResourcePtr = ftl::RefPtr<ProxyResource>;

/// Acts as a placeholder for resources imported from other sessions. Once a
/// binding between the proxy and the resource has been established, the
/// |imports()| collection of that resource will contain a reference to this
/// proxy. The proxy also holds a reference to the import token used for the
/// resolution of the binding between the exported resource and the imported
/// proxy.
class ProxyResource final : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  ProxyResource(Session* session,
                mozart2::ImportSpec spec,
                mx::eventpair import_token);

  ~ProxyResource() override;

  // |Resource|.
  void Accept(class ResourceVisitor* visitor) override;

  /// Returns the resource that is a suitable standin for the resource being
  /// bound to by the proxy. Imported resources are never modified by the
  /// importing session. Ops directed at the proxy resource are instead applied
  /// to this delegate.
  Resource* ops_delegate() { return ops_delegate_.get(); }

  /// The specification used to represent the underlying type of the resource
  /// being bound to the proxy.
  mozart2::ImportSpec import_spec() const { return import_spec_; }

  /// The import token that is currently used by the resource linker to bind to
  /// exported resources. The import token must be a peer of the token used to
  /// export the resource in the resource linker.
  const mx::eventpair& import_token() const { return import_token_; }

  /// If an active binding exists between this proxy and a resource, returns
  /// that resource. If no binding exists, returns nullptr.
  const Resource* bound_resource() { return imported_resource_; }

 private:
  // TODO(MZ-132): Don't hold onto the token for the the duration of the
  // lifetime of the proxy resource. This bloats kernel handle tables.
  mx::eventpair import_token_;
  const mozart2::ImportSpec import_spec_;
  const ResourcePtr ops_delegate_;
  const Resource* imported_resource_;

  // |Resource|.
  Resource* GetOpsDelegate(const ResourceTypeInfo& type_info) override;

  friend class Resource;

  /// Establish a binding between the resource and this proxy. The type of the
  /// resource being bound to is compatible with the import spec specified when
  /// creating the proxy resource.
  void SetBoundResource(const Resource* resource);

  /// Clear a previous binding to an imported resource. This usually happens
  /// when the imported resource has been collected in the session that exported
  /// that resource.
  void ClearBoundResource();

  FRIEND_MAKE_REF_COUNTED(ProxyResource);
  FRIEND_REF_COUNTED_THREAD_SAFE(ProxyResource);
  FTL_DISALLOW_COPY_AND_ASSIGN(ProxyResource);
};

}  // namespace scene
}  // namespace mozart
