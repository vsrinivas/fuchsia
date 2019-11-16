// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ID_H_
#define SRC_UI_SCENIC_LIB_GFX_ID_H_

#include <cstdint>
#include <ostream>

#include "src/ui/scenic/lib/scheduling/id.h"

namespace scenic_impl {

using SessionId = scheduling::SessionId;
using ResourceId = uint32_t;

struct GlobalId {
  GlobalId() : session_id(0), resource_id(0) {}
  GlobalId(SessionId s, ResourceId r) : session_id(s), resource_id(r) {}

  explicit operator bool();

  struct Hash {
    size_t operator()(const GlobalId& id) const {
      static_assert(sizeof(SessionId) == sizeof(uint64_t));
      static_assert(sizeof(ResourceId) == sizeof(uint32_t));

      return id.session_id + ((uint64_t)id.resource_id << 32);
    }
  };

  SessionId session_id;
  ResourceId resource_id;
};

bool operator==(const GlobalId& lhs, const GlobalId& rhs);
bool operator!=(const GlobalId& lhs, const GlobalId& rhs);

std::ostream& operator<<(std::ostream& os, const GlobalId& value);

}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_ID_H_
