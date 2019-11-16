// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_RESOURCE_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_RESOURCE_H_

#include <string>
#include <type_traits>
#include <vector>

#include "src/lib/fxl/memory/ref_counted.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/scenic/lib/gfx/id.h"
#include "src/ui/scenic/lib/gfx/resources/resource_type_info.h"

namespace scenic_impl {
class ErrorReporter;
class EventReporter;
}  // namespace scenic_impl

namespace scenic_impl {
namespace gfx {

class Import;
class ResourceLinker;
class Session;
struct ResourceContext;

// Resource is the base class for all client-created objects (i.e. those that
// are created in response to a CreateResourceCmd).
// TODO: use unsafe ref-counting for better performance (our architecture
// guarantees that this is safe).
class Resource : public fxl::RefCountedThreadSafe<Resource> {
 public:
  static const ResourceTypeInfo kTypeInfo;

  virtual ~Resource();

  const ResourceTypeInfo& type_info() const { return type_info_; }
  ResourceTypeFlags type_flags() const { return type_info_.flags; }
  const char* type_name() const { return type_info_.name; }

  // The session this Resource lives in and the id it was created with there.
  Session* session_DEPRECATED() const { return session_DEPRECATED_; }
  ResourceId id() const { return global_id_.resource_id; }
  scheduling::SessionId session_id() const { return global_id_.session_id; }
  GlobalId global_id() const { return global_id_; }

  // TODO(SCN-1504): this blocks the removal of Session* from resource.
  EventReporter* event_reporter() const;

  // TODO(SCN-1504): this blocks the removal of Session* from resource.  Should we stash one of
  // these in the resource?  Only for some resources?
  const ResourceContext& resource_context() const;

  // The diagnostic label.
  const std::string label() const { return label_; }
  bool SetLabel(const std::string& label);

  // The event mask.
  uint32_t event_mask() const { return event_mask_; }
  virtual bool SetEventMask(uint32_t event_mask);

  // Used by ResourceVisitor to visit a tree of Resources.
  virtual void Accept(class ResourceVisitor* visitor) = 0;

  // Return true if the specified type is identical or a base type of this
  // TypedReffable; return false otherwise.
  // TODO: Move this to a separate class we inherit from.
  template <typename TypedReffableT>
  bool IsKindOf() const {
    return type_info().IsKindOf(TypedReffableT::kTypeInfo);
  }

  // Downcasts the reference to the specified subclass. Throws an exception
  // in debug mode if the type of the object does not match.
  //
  // Example usage: fxl::RefPtr<Subclass> = object.As<Subclass>();
  // TODO: Move this to a separate class we inherit from.
  template <typename T>
  fxl::RefPtr<T> As() {
    FXL_DCHECK(this->IsKindOf<T>());
    return fxl::RefPtr<T>(static_cast<T*>(this));
  }

  /// The list of import resource that currently have a binding to this
  /// resource.
  const std::vector<Import*>& imports() const { return imports_; }

  // Returns whether this resource is currently exported or available for
  // export.
  bool is_exported() const { return exported_; }

  /// Adds the import resource to the list of importers of this resource.
  virtual void AddImport(Import* import, ErrorReporter* error_reporter);

  /// Removes the import resource from the list of importers of this resource.
  virtual void RemoveImport(Import* import);

  // Detach the resource from its parent.  Return false if this fails for some
  // reason (including if this is an object for which the command makes no
  // sense).
  virtual bool Detach(ErrorReporter* error_reporter);

 protected:
  Resource(Session* session, SessionId session_id, ResourceId id,
           const ResourceTypeInfo& type_info);

  friend class ResourceLinker;
  friend class ResourceMap;
  friend class Import;
  /// For the given resource type info, returns the resource that will act as
  /// the target for commands directed at this resource. Subclasses (notably the
  /// |Import| since their binding are not mutable) may return alternate
  /// resources to act as the recipients of commands.
  virtual Resource* GetDelegate(const ResourceTypeInfo& type_info);

  // Sets a flag that indicates if this resource is exported in ResourceLinker.
  // If so, this resource is responsible for notifying ResourceLinker when it
  // dies.
  void SetExported(bool exported, const fxl::WeakPtr<ResourceLinker>& resource_linker_weak);

 private:
  Session* const session_DEPRECATED_;
  const GlobalId global_id_;
  const ResourceTypeInfo& type_info_;
  std::string label_;
  uint32_t event_mask_ = 0u;
  std::vector<Import*> imports_;
  // If true, ResourceLinker  must be called back before this resource is
  // destroyed.
  bool exported_ = false;
  fxl::WeakPtr<ResourceLinker> resource_linker_weak_;
};

using ResourcePtr = fxl::RefPtr<Resource>;

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_RESOURCE_H_
