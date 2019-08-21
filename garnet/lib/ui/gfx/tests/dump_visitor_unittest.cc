// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/dump_visitor.h"

#include <gtest/gtest.h>

#include "garnet/lib/ui/gfx/resources/host_image.h"
#include "garnet/lib/ui/gfx/resources/image_pipe.h"
#include "garnet/lib/ui/gfx/resources/material.h"
#include "garnet/lib/ui/gfx/tests/session_test.h"

namespace scenic_impl {
namespace gfx {
namespace test {

class DumpVisitorTest : public SessionTest {
 public:
  ImagePtr CreateImage(ResourceId id) {
    fuchsia::images::ImageInfo image_info;
    return fxl::AdoptRef(new HostImage(session(), id, /* memory */ nullptr,
                                       /* image */ nullptr,
                                       /* memory_offset */ 0, image_info));
  }
};

TEST_F(DumpVisitorTest, DynamicVisitOfBaseImageTypes) {
  std::ostringstream ostream;
  std::unordered_set<GlobalId, GlobalId::Hash> visited;
  DumpVisitor::VisitorContext context(ostream, &visited);
  DumpVisitor visitor(std::move(context));

  ResourceId next_id = 1;
  MaterialPtr image_material = fxl::MakeRefCounted<Material>(session(), next_id++);
  MaterialPtr pipe_material = fxl::MakeRefCounted<Material>(session(), next_id++);
  ImagePtr image = CreateImage(next_id++);
  ImagePipePtr pipe = fxl::MakeRefCounted<ImagePipe>(
      session(), next_id++, session()->image_pipe_updater(), session()->shared_error_reporter());

  image_material->SetTexture(image);
  pipe_material->SetTexture(pipe);

  visitor.Visit(image_material.get());
  visitor.Visit(pipe_material.get());

  ASSERT_TRUE(ostream.str().find("Image:"));
  ASSERT_TRUE(ostream.str().find("ImagePipe:"));
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
