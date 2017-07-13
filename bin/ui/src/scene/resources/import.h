// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/src/scene/resources/resource.h"
#include "lib/ftl/macros.h"
#include "magenta/system/ulib/mx/include/mx/eventpair.h"

namespace mozart {
namespace scene {

class Import;
using ImportPtr = ftl::RefPtr<Import>;

/// Acts as a placeholder for resources imported from other sessions. Once a
/// binding between the import and the resource has been established, the
/// |imports()| collection of that resource will contain a reference to this
/// import. The import also holds a reference to the import token used for the
/// resolution of the binding between the exported resource and the imported
/// import.
class Import final : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  Import(Session* session,
         ResourceId id,
         mozart2::ImportSpec spec,
         mx::eventpair import_token);

  ~Import() override;

  // |Resource|.
  void Accept(class ResourceVisitor* visitor) override;

  /// Returns the resource that is a suitable standin for the resource being
  /// bound to by the import. Imported resources are never modified by the
  /// importing session. Ops directed at the import resource are instead applied
  /// to this delegate. This delegate also holds the side-effects of these
  /// operations such as the list of children which were attached.
  Resource* delegate() { return delegate_.get(); }

  /// The specification used to represent the underlying type of the resource
  /// being bound to the import.
  mozart2::ImportSpec import_spec() const { return import_spec_; }

  /// The import token that is currently used by the resource linker to bind to
  /// exported resources. The import token must be a peer of the token used to
  /// export the resource in the resource linker.
  const mx::eventpair& import_token() const { return import_token_; }

  /// If an active binding exists between this import and an imported resource,
  // returns that resource. If no binding exists, returns nullptr.
  Resource* imported_resource() const { return imported_resource_; }

  /// Returns true if the imported resource has been bound.
  bool is_bound() const { return imported_resource_ != nullptr; }

 private:
  // TODO(MZ-132): Don't hold onto the token for the the duration of the
  // lifetime of the import resource. This bloats kernel handle tables.
  mx::eventpair import_token_;
  const mozart2::ImportSpec import_spec_;
  const ResourcePtr delegate_;
  Resource* imported_resource_ = nullptr;

  // |Resource|.
  Resource* GetDelegate(const ResourceTypeInfo& type_info) override;

  // Needed by |Resource::AddImport()| and |Resource::RemoveImport()|.
  friend class Resource;

  /// Establish a binding between the resource and this import. The type of the
  /// resource being bound to is compatible with the import spec specified when
  /// creating the import resource.
  void BindImportedResource(Resource* resource);

  /// Clear a previous binding to an imported resource. This usually happens
  /// when the imported resource has been collected in the session that exported
  /// that resource.
  void UnbindImportedResource();

  FRIEND_MAKE_REF_COUNTED(Import);
  FRIEND_REF_COUNTED_THREAD_SAFE(Import);
  FTL_DISALLOW_COPY_AND_ASSIGN(Import);
};

}  // namespace scene
}  // namespace mozart
