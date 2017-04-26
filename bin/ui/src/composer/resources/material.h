// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/src/composer/resources/resource.h"

namespace mozart {
namespace composer {

class Material : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  explicit Material(Session* session,
                    float red,
                    float green,
                    float blue,
                    float alpha);
  float red() const { return red_; }
  float green() const { return green_; }
  float blue() const { return blue_; }
  float alpha() const { return alpha_; }

 private:
  float red_;
  float green_;
  float blue_;
  float alpha_;
};

typedef ftl::RefPtr<Material> MaterialPtr;

}  // namespace composer
}  // namespace mozart
