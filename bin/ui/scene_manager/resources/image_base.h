// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/ui/scene_manager/engine/session.h"
#include "garnet/bin/ui/scene_manager/resources/resource.h"

namespace scene_manager {

// Abstract superclass for Image and ImagePipe.
class ImageBase : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  // Returns the image that should currently be presented. Can be null.
  virtual const escher::ImagePtr& GetEscherImage() = 0;

 protected:
  ImageBase(Session* session,
            scenic::ResourceId id,
            const ResourceTypeInfo& type_info);
};

using ImageBasePtr = ftl::RefPtr<ImageBase>;

}  // namespace scene_manager
