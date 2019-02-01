// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/resources/import_node.h"

#include "garnet/bin/ui/sketchy/resources/stroke_group.h"

namespace sketchy_service {

const ResourceTypeInfo ImportNode::kTypeInfo("ImportNode",
                                             ResourceType::kImportNode,
                                             ResourceType::kResource);

ImportNode::ImportNode(scenic::Session* session, zx::eventpair token)
    : node_(session) {
  node_.Bind(std::move(token));
}

void ImportNode::AddChild(const StrokeGroupPtr& stroke_group) {
  node_.AddChild(stroke_group->shape_node());
}

}  // namespace sketchy_service
