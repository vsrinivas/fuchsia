// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/sketchy/canvas.h"

namespace sketchy_lib {

using sketchy::Op;
using sketchy::OpPtr;
using sketchy::ResourceArgs;
using sketchy::ResourceArgsPtr;

ResourceManager::ResourceManager(Canvas* canvas)
    : canvas_(canvas),
      next_resource_id_(1) {}

ResourceId ResourceManager::CreateAnonymousResource() {
  return next_resource_id_++;
}

ResourceId ResourceManager::CreateStroke() {
  sketchy::StrokePtr stroke = sketchy::Stroke::New();
  ResourceArgsPtr resource_args = ResourceArgs::New();
  resource_args->set_stroke(std::move(stroke));
  return CreateResource(std::move(resource_args));
}

ResourceId ResourceManager::CreateStrokeGroup() {
  sketchy::StrokeGroupPtr stroke_group = sketchy::StrokeGroup::New();
  ResourceArgsPtr resource_args = ResourceArgs::New();
  resource_args->set_stroke_group(std::move(stroke_group));
  return CreateResource(std::move(resource_args));
}

ResourceId ResourceManager::CreateResource(ResourceArgsPtr args) {
  ResourceId resource_id = next_resource_id_++;
  auto create_resource = sketchy::CreateResourceOp::New();
  create_resource->id = resource_id;
  create_resource->args = std::move(args);
  auto op = Op::New();
  op->set_create_resource(std::move(create_resource));
  canvas_->ops_.push_back(std::move(op));
  return resource_id;
}

void ResourceManager::ReleaseResource(ResourceId resource_id) {
  auto release_resource = sketchy::ReleaseResourceOp::New();
  release_resource->id = resource_id;
  auto op = Op::New();
  op->set_release_resource(std::move(release_resource));
  canvas_->ops_.push_back(std::move(op));
}

}  // namespace sketchy_lib
