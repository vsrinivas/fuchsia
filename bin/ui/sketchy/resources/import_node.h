// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SKETCHY_RESOURCES_IMPORT_NODE_H_
#define GARNET_BIN_UI_SKETCHY_RESOURCES_IMPORT_NODE_H_

#include "garnet/bin/ui/sketchy/resources/resource.h"
#include "lib/fxl/memory/ref_counted.h"
#include "lib/ui/scenic/client/resources.h"
#include "lib/ui/scenic/client/session.h"

namespace sketchy_service {

class StrokeGroup;
using StrokeGroupPtr = fxl::RefPtr<StrokeGroup>;

// Wrapper of scenic::ImportNode. To import a node, client should
// export it as token, and this class takes that token, so that it functions
// as if it were the exported node.
class ImportNode final : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  ImportNode(scenic::Session* session, zx::eventpair token);
  void AddChild(const StrokeGroupPtr& stroke_group);

  // Resource ID shared with scene manager.
  uint32_t id() { return node_.id(); }

 private:
  scenic::ImportNode node_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ImportNode);
};

}  // namespace sketchy_service

#endif  // GARNET_BIN_UI_SKETCHY_RESOURCES_IMPORT_NODE_H_
