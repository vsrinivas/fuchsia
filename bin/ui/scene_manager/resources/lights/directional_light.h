// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/ui/scene_manager/resources/resource.h"
#include "lib/escher/geometry/types.h"

namespace scene_manager {

class DirectionalLight final : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  DirectionalLight(Session* session,
                   scenic::ResourceId id,
                   const escher::vec3& direction,
                   float intensity);

  const escher::vec3& direction() const { return direction_; }
  float intensity() const { return intensity_; }

  void set_intensity(float intensity) { intensity_ = intensity; }

  // |Resource|.
  void Accept(class ResourceVisitor* visitor) override;

 private:
  escher::vec3 direction_;
  float intensity_;
};

}  // namespace scene_manager
