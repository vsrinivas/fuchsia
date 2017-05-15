// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "apps/mozart/services/composer/session.fidl.h"
#include "apps/mozart/src/composer/resources/resource_map.h"
#include "apps/mozart/src/composer/session/session_context.h"
#include "apps/mozart/src/composer/util/error_reporter.h"
#include "lib/ftl/tasks/task_runner.h"

namespace mozart {
namespace composer {

typedef uint64_t SessionId;

class Session;
typedef ::ftl::RefPtr<Session> SessionPtr;

struct SessionUpdate {
  SessionPtr session;
  ::fidl::Array<mozart2::OpPtr> ops;
  ::fidl::Array<mx::event> wait_events;
  ::fidl::Array<mx::event> signal_events;
};

// TODO: use unsafe ref-counting for better performance (our architecture
// guarantees that this is safe).
class Session : public ftl::RefCountedThreadSafe<Session> {
 public:
  Session(SessionId id,
          SessionContext* context,
          ErrorReporter* error_reporter = ErrorReporter::Default());
  ~Session();

  // Apply the operation to the current session state.  Return true if
  // successful, and false if the op is somehow invalid.  In the latter case,
  // the Session is left unchanged.
  bool ApplyOp(const mozart2::OpPtr& op);

  SessionId id() const { return id_; }
  SessionContext* context() const { return context_; }

  // Return the total number of existing resources associated with this Session.
  size_t GetTotalResourceCount() const { return resource_count_; }

  // Return the number of resources that a client can identify via a ResourceId.
  // This number is decremented when a ReleaseResourceOp is applied.  However,
  // the resource may continue to exist if it is referenced by other resources.
  size_t GetMappedResourceCount() const { return resources_.size(); }

  void TearDown();

  // Session becomes invalid once TearDown is called.
  bool is_valid() const { return is_valid_; }

  ErrorReporter* error_reporter() const;

  ResourceMap* resources() { return &resources_; }

 private:
  // Operation application functions, called by ApplyOp().
  bool ApplyCreateResourceOp(const mozart2::CreateResourceOpPtr& op);
  bool ApplyReleaseResourceOp(const mozart2::ReleaseResourceOpPtr& op);
  bool ApplyAddChildOp(const mozart2::AddChildOpPtr& op);
  bool ApplyAddPartOp(const mozart2::AddPartOpPtr& op);
  bool ApplyDetachOp(const mozart2::DetachOpPtr& op);
  bool ApplyDetachChildrenOp(const mozart2::DetachChildrenOpPtr& op);
  bool ApplySetTransformOp(const mozart2::SetTransformOpPtr& op);
  bool ApplySetShapeOp(const mozart2::SetShapeOpPtr& op);
  bool ApplySetMaterialOp(const mozart2::SetMaterialOpPtr& op);
  bool ApplySetClipOp(const mozart2::SetClipOpPtr& op);

  // Resource creation functions, called by ApplyCreateResourceOp().
  bool ApplyCreateMemory(ResourceId id, const mozart2::MemoryPtr& args);
  bool ApplyCreateImage(ResourceId id, const mozart2::ImagePtr& args);
  bool ApplyCreateBuffer(ResourceId id, const mozart2::BufferPtr& args);
  bool ApplyCreateLink(ResourceId id, const mozart2::LinkPtr& args);
  bool ApplyCreateRectangle(ResourceId id, const mozart2::RectanglePtr& args);
  bool ApplyCreateCircle(ResourceId id, const mozart2::CirclePtr& args);
  bool ApplyCreateMesh(ResourceId id, const mozart2::MeshPtr& args);
  bool ApplyCreateMaterial(ResourceId id, const mozart2::MaterialPtr& args);
  bool ApplyCreateClipNode(ResourceId id, const mozart2::ClipNodePtr& args);
  bool ApplyCreateEntityNode(ResourceId id, const mozart2::EntityNodePtr& args);
  bool ApplyCreateLinkNode(ResourceId id, const mozart2::LinkNodePtr& args);
  bool ApplyCreateShapeNode(ResourceId id, const mozart2::ShapeNodePtr& args);
  bool ApplyCreateTagNode(ResourceId id, const mozart2::TagNodePtr& args);

  // Actually create resources.
  ResourcePtr CreateClipNode(ResourceId id, const mozart2::ClipNodePtr& args);
  ResourcePtr CreateEntityNode(ResourceId id,
                               const mozart2::EntityNodePtr& args);
  ResourcePtr CreateLinkNode(ResourceId id, const mozart2::LinkNodePtr& args);
  ResourcePtr CreateShapeNode(ResourceId id, const mozart2::ShapeNodePtr& args);
  ResourcePtr CreateTagNode(ResourceId id, const mozart2::TagNodePtr& args);
  ResourcePtr CreateCircle(ResourceId id, float initial_radius);
  ResourcePtr CreateMaterial(ResourceId id,
                             float red,
                             float green,
                             float blue,
                             float alpha);

  friend class Resource;
  void IncrementResourceCount() { ++resource_count_; }
  void DecrementResourceCount() { --resource_count_; }

  const SessionId id_;
  SessionContext* const context_;
  ErrorReporter* error_reporter_ = nullptr;

  ResourceMap resources_;

  size_t resource_count_ = 0;
  bool is_valid_ = true;
};

}  // namespace composer
}  // namespace mozart
