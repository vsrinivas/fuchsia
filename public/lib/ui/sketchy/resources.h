// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/ui/scenic/client/session.h"
#include "lib/ui/fun/sketchy/fidl/ops.fidl.h"
#include "lib/ui/fun/sketchy/fidl/resources.fidl.h"

namespace sketchy_lib {

using ResourceId = uint32_t;

class Canvas;

// The classes in this file provide a friendlier way to interact with the
// resources defined by the Sketchy Canvas FIDL API; each class in this file
// corresponds to a separate canvas resource.
//
// Resource is the base class for these other classes.
// provides lifecycle management; the constructor enqueues a CreateResourceOp
// and the destructor enqueues a ReleaseResourceOp.
class Resource {
 public:
  Canvas* canvas() const { return canvas_; }
  ResourceId id() const { return id_; }

 protected:
  Resource(Canvas* canvas);

  // Enqueue an op in canvas to destroy a resource. Called in destructor of
  // concrete resources. The remote resource may still live until no other
  // resource references it.
  ~Resource();

  // Enqueue an op in canvas to create a resource. Called in the constructor of
  // concrete resources to be created.
  void EnqueueCreateResourceOp(ResourceId resource_id,
                               sketchy::ResourceArgsPtr args);

  // Enqueue an op in canvas to import the resource. Called in the constructor
  // of concrete resources to be imported.
  // |resource_id| Id of the resource.
  // |token| Token that is exported by the local resource, with which the remote
  //     canvas can import.
  // |spec| Type of the resource.
  void EnqueueImportResourceOp(ResourceId resource_id,
                               mx::eventpair token,
                               scenic::ImportSpec spec);

  // Enqueue an op in canvas.
  void EnqueueOp(sketchy::OpPtr op);

 private:
  Canvas* const canvas_;
  const ResourceId id_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Resource);
};

// Represents a stroke in a canvas.
class Stroke final : public Resource {
 public:
  Stroke(Canvas* canvas);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Stroke);
};

// Represents a group of stroke(s) in a canvas.
class StrokeGroup final : public Resource {
 public:
  StrokeGroup(Canvas* canvas);
  void AddStroke(Stroke& stroke);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(StrokeGroup);
};

// Represents an imported node in a canvas.
class ImportNode final : public Resource {
 public:
  ImportNode(Canvas* canvas, scenic_lib::EntityNode& export_node);
  void AddChild(const Resource& child);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(ImportNode);
};

}  // namespace sketchy_lib
