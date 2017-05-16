// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/src/composer/resources/resource.h"

namespace mozart {
namespace composer {

class Shape : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

 protected:
  Shape(Session* session, const ResourceTypeInfo& type_info);
};

typedef ftl::RefPtr<Shape> ShapePtr;

}  // namespace composer
}  // namespace mozart
