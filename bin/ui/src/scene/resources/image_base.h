// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/src/scene/resources/resource.h"
#include "apps/mozart/src/scene/session/session.h"

namespace mozart {
namespace scene {

// Abstract superclass for Image and ImagePipe.
class ImageBase : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  // Returns the image that should currently be presented. Can be null.
  virtual const escher::ImagePtr& GetEscherImage() = 0;

 protected:
  ImageBase(Session* session, ResourceId id, const ResourceTypeInfo& type_info);
};

using ImageBasePtr = ftl::RefPtr<ImageBase>;

}  // namespace scene
}  // namespace mozart
