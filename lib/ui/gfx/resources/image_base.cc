// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/image_base.h"

namespace scenic {
namespace gfx {

const ResourceTypeInfo ImageBase::kTypeInfo = {ResourceType::kImageBase,
                                               "ImageBase"};

ImageBase::ImageBase(Session* session, scenic::ResourceId id,
                     const ResourceTypeInfo& type_info)
    : Resource(session, id, type_info) {
  FXL_DCHECK(type_info.IsKindOf(ImageBase::kTypeInfo));
}

}  // namespace gfx
}  // namespace scenic
