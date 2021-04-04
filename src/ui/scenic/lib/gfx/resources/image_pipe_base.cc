// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/image_pipe_base.h"

namespace scenic_impl {
namespace gfx {

ImagePipeBase::ImagePipeBase(Session* session, ResourceId id, const ResourceTypeInfo& type_info)
    : ImageBase(session, id, type_info), scheduling_id_(scheduling::GetNextSessionId()) {}

}  // namespace gfx
}  // namespace scenic_impl
