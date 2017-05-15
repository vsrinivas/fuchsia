/// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/src/composer/resources/nodes/node.h"

namespace mozart {
namespace composer {

class EntityNode final : public Node {
 public:
  static const ResourceTypeInfo kTypeInfo;

  EntityNode(Session* session);

  void Accept(class ResourceVisitor* visitor) override;
};

}  // namespace composer
}  // namespace mozart
