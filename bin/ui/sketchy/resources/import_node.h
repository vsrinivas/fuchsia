// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/lib/scenic/client/resources.h"
#include "apps/mozart/lib/scenic/client/session.h"
#include "apps/mozart/src/sketchy/resources/resource.h"
#include "lib/ftl/memory/ref_counted.h"

namespace sketchy_service {

class StrokeGroup;
using StrokeGroupPtr = ftl::RefPtr<StrokeGroup>;

// Wrapper of scenic_lib::ImportNode. To import a node, client should
// export it as token, and this class takes that token, so that it functions
// as if it were the exported node.
class ImportNode final : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  ImportNode(scenic_lib::Session* session, mx::eventpair token);
  void AddChild(const StrokeGroupPtr& stroke_group);

  // Resource ID shared with scene manager.
  uint32_t id() { return node_.id(); }

 private:
  scenic_lib::ImportNode node_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ImportNode);
};

}  // namespace sketchy_service
