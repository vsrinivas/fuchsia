// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/composer/resources/link.h"

namespace mozart {
namespace composer {

const ResourceTypeInfo Link::kTypeInfo = {ResourceType::kLink, "Link"};

Link::Link(Session* session) : Resource(session, Link::kTypeInfo) {}

}  // namespace composer
}  // namespace mozart
