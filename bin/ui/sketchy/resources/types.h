// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include "escher/base/type_info.h"

namespace sketchy_service {

using ResourceId = uint32_t;

enum class ResourceType {
  kResource = 1 << 0,
  kImportNode = 1 << 1,
  kStrokeGroup = 1 << 2,
};

typedef escher::TypeInfo<ResourceType> ResourceTypeInfo;

}  // namespace sketchy_service
