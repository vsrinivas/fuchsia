// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_SKETCHY_CLIENT_RESOURCES_H_
#define LIB_UI_SKETCHY_CLIENT_RESOURCES_H_

#include <fuchsia/ui/sketchy/cpp/fidl.h>
#include <garnet/public/lib/fxl/memory/ref_counted.h>

#include "lib/ui/scenic/client/session.h"
#include "lib/ui/sketchy/client/types.h"

namespace sketchy_lib {

using ResourceId = uint32_t;

class Canvas;

// The classes in this file provide a friendlier way to interact with the
// resources defined by the Sketchy Canvas FIDL API; each class in this file
// corresponds to a separate canvas resource.
//
// Resource is the base class for these other classes.
// provides lifecycle management; the constructor enqueues a
// CreateResourceCmd and the destructor enqueues a ReleaseResourceCmd.
class Resource {
 public:
  Canvas* canvas() const { return canvas_; }
  ResourceId id() const { return id_; }

 protected:
  explicit Resource(Canvas* canvas);

  // Enqueue an op in canvas to destroy a resource. Called in destructor of
  // concrete resources. The remote resource may still live until no other
  // resource references it.
  ~Resource();

  // Enqueue an op in canvas to create a resource. Called in the constructor of
  // concrete resources to be created.
  void EnqueueCreateResourceCmd(
      ResourceId resource_id, ::fuchsia::ui::sketchy::ResourceArgs args) const;

  // Enqueue an op in canvas to import the resource. Called in the constructor
  // of concrete resources to be imported.
  // |resource_id| Id of the resource.
  // |token| Token that is exported by the local resource, with which the remote
  //     canvas can import.
  // |spec| Type of the resource.
  void EnqueueImportResourceCmd(ResourceId resource_id, zx::eventpair token,
                                    fuchsia::ui::gfx::ImportSpec spec) const;

  // Enqueue an op in canvas.
  void EnqueueCmd(::fuchsia::ui::sketchy::Command command) const;

 private:
  Canvas* const canvas_;
  const ResourceId id_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Resource);
};

// Represents a stroke in a canvas.
class Stroke final : public Resource, public fxl::RefCountedThreadSafe<Stroke> {
 public:
  explicit Stroke(Canvas* canvas);
  void SetPath(const StrokePath& path) const;
  void Begin(glm::vec2 pt) const;
  // TODO(MZ-269): Also pass in predicted points.
  void Extend(std::vector<glm::vec2> pts) const;
  void Finish() const;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Stroke);
};

using StrokePtr = fxl::RefPtr<Stroke>;

// Represents a group of stroke(s) in a canvas.
class StrokeGroup final : public Resource {
 public:
  explicit StrokeGroup(Canvas* canvas);
  void AddStroke(const Stroke& stroke) const;
  void RemoveStroke(const Stroke& stroke) const;
  void Clear() const;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(StrokeGroup);
};

// Represents an imported node in a canvas.
class ImportNode final : public Resource {
 public:
  ImportNode(Canvas* canvas, scenic::EntityNode& export_node);
  void AddChild(const Resource& child) const;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(ImportNode);
};

}  // namespace sketchy_lib

#endif  // LIB_UI_SKETCHY_CLIENT_RESOURCES_H_
