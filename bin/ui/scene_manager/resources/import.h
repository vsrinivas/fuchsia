// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/ui/scene_manager/resources/resource.h"
#include "lib/fxl/macros.h"
#include "zircon/system/ulib/zx/include/zx/eventpair.h"

namespace scene_manager {

class Import;
using ImportPtr = fxl::RefPtr<Import>;

// Callback used by ResourceLinker and UnresolvedImports to indicate the result
// of resolving a resource.
enum class ImportResolutionResult {
  kSuccess,                     // Import was bound successfully to a Resource.
  kExportHandleDiedBeforeBind,  // The peer token of the import was destroyed
                                // before binding could occur.
  kImportDestroyedBeforeBind    // Import was destroyed before binding could
                                // occur.
};
using OnImportResolvedCallback =
    std::function<void(Resource*, ImportResolutionResult)>;

/// Acts as a placeholder for resources imported from other sessions. Once a
/// binding between the import and the resource has been established, the
/// |imports()| collection of that resource will contain a reference to this
/// import. The import also holds a reference to the import token used for the
/// resolution of the binding between the exported resource and the imported
/// import.
class Import final : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  Import(Session* session, scenic::ResourceId id, scenic::ImportSpec spec);

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
  scenic::ImportSpec import_spec() const { return import_spec_; }

  /// If an active binding exists between this import and an imported resource,
  // returns that resource. If no binding exists, returns nullptr.
  Resource* imported_resource() const { return imported_resource_; }

  /// Returns true if the imported resource has been bound.
  bool is_bound() const { return imported_resource_ != nullptr; }

 private:
  const scenic::ImportSpec import_spec_;
  const ResourcePtr delegate_;
  Resource* imported_resource_ = nullptr;

  // |Resource|.
  Resource* GetDelegate(const ResourceTypeInfo& type_info) override;

  // Needed by |Resource::AddImport()| and |Resource::RemoveImport()|.
  friend class Resource;
  // Needed by |ResourceLinker::OnImportResolvedForResource()|.
  friend class ResourceLinker;

  /// Establish a binding between the resource and this import. The type of the
  /// resource being bound to is compatible with the import spec specified when
  /// creating the import resource.
  void BindImportedResource(Resource* resource);

  /// Clear a previous binding to an imported resource, or signal that a pending
  /// binding has failed. The first case can happen when the resource being
  /// imported is is released in its session. The second case can happen if
  /// an export handle is destroyed before ExportResource() is called.
  void UnbindImportedResource();

  FRIEND_MAKE_REF_COUNTED(Import);
  FRIEND_REF_COUNTED_THREAD_SAFE(Import);
  FXL_DISALLOW_COPY_AND_ASSIGN(Import);
};

}  // namespace scene_manager
