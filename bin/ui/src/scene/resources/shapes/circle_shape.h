// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/src/scene/resources/shapes/shape.h"

namespace mozart {
namespace scene {

class CircleShape final : public Shape {
 public:
  static const ResourceTypeInfo kTypeInfo;

  CircleShape(Session* session, float initial_radius);

  float radius() const { return radius_; }

  bool ContainsPoint(const escher::vec2& point) const override;

  void Accept(class ResourceVisitor* visitor) override;

 private:
  float radius_;
};

}  // namespace scene
}  // namespace mozart
