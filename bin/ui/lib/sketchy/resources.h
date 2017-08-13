// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/lib/scene/client/session.h"
#include "apps/mozart/services/fun/sketchy/resources.fidl.h"

namespace sketchy_lib {

using ResourceId = uint32_t;

class Canvas;

class ResourceManager final {
 public:
  ResourceManager(Canvas* canvas);
  ResourceId CreateAnonymousResource();
  ResourceId CreateStroke();
  ResourceId CreateStrokeGroup();
  void ReleaseResource(ResourceId resource_id);

 private:
  ResourceId CreateResource(sketchy::ResourceArgsPtr args);

  Canvas* const canvas_;
  ResourceId next_resource_id_;
};

}  // namespace sketchy_lib
