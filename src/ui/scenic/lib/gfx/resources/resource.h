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

  // TODO(fxbug.dev/24687): this blocks the removal of Session* from resource.
  EventReporter* event_reporter() const;

  // TODO(fxbug.dev/24687): this blocks the removal of Session* from resource.  Should we stash one of
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
    FX_DCHECK(this->IsKindOf<T>());
    return fxl::RefPtr<T>(static_cast<T*>(this));
  }

  // Detach the resource from its parent.  Return false if this fails for some
  // reason (including if this is an object for which the command makes no
  // sense).
  virtual bool Detach(ErrorReporter* error_reporter);

 protected:
  Resource(Session* session, SessionId session_id, ResourceId id,
           const ResourceTypeInfo& type_info);

 private:
  Session* const session_DEPRECATED_;
  const GlobalId global_id_;
  const ResourceTypeInfo& type_info_;
  std::string label_;
  uint32_t event_mask_ = 0u;
};

using ResourcePtr = fxl::RefPtr<Resource>;

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_RESOURCE_H_
