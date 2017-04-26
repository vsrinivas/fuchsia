// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/src/composer/resources/resource.h"

namespace mozart {
namespace composer {

class Link : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  explicit Link(Session* session);
};

typedef ftl::RefPtr<Link> LinkPtr;

}  // namespace composer
}  // namespace mozart
