// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_RENDERER_SHADOW_MAP_TYPE_INFO_H_
#define LIB_ESCHER_RENDERER_SHADOW_MAP_TYPE_INFO_H_

#include "lib/escher/base/type_info.h"

namespace escher {

// Typically the type-info encodes the inheritance tree of the type.
// For example, see buffer.cc: a Buffer is also a Resource and a
// WaitableResource. However, in this case we don't want a MomentShadowMap to be
// usable as a default ShadowMap, because the shader won't know what to do with
// the contents. Therefore, MomentShadowMap uses only the kMoment flag, not also
// the kDefault flag.
enum class ShadowMapType {
  kDefault = 1,
  kMoment = 1 << 1,
};

typedef TypeInfo<ShadowMapType> ShadowMapTypeInfo;

}  // namespace escher

#endif  // LIB_ESCHER_RENDERER_SHADOW_MAP_TYPE_INFO_H_
