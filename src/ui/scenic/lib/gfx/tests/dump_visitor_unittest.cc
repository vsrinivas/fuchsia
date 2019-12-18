// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/dump_visitor.h"

#include <lib/ui/scenic/cpp/view_ref_pair.h>

#include <gtest/gtest.h>

#include "src/ui/scenic/lib/gfx/resources/host_image.h"
#include "src/ui/scenic/lib/gfx/resources/image_pipe.h"
#include "src/ui/scenic/lib/gfx/resources/material.h"
#include "src/ui/scenic/lib/gfx/resources/view.h"
#include "src/ui/scenic/lib/gfx/resources/view_holder.h"
#include "src/ui/scenic/lib/gfx/tests/session_test.h"

namespace scenic_impl {
namespace gfx {
namespace test {

class DumpVisitorTest : public SessionTest {
 public:
  // TODO(24711): Once Images can be created without interacting with the underlying renderer,
  // replace this with HostImage::New.
  ImagePtr CreateImage(ResourceId id) {
    fuchsia::images::ImageInfo image_info;
    return fxl::AdoptRef(new HostImage(session(), id, /* memory */ nullptr,
                                       /* image */ nullptr,
                                       /* memory_offset */ 0, image_info));
  }
};

TEST_F(DumpVisitorTest, NullImage) {
  std::ostringstream ostream;
  std::unordered_set<GlobalId, GlobalId::Hash> visited;
  DumpVisitor::VisitorContext context(ostream, &visited);
  DumpVisitor visitor(std::move(context));

  MaterialPtr null_image_material = fxl::MakeRefCounted<Material>(session(), 1u);

  visitor.Visit(null_image_material.get());

  EXPECT_TRUE(ostream.str().find("value=(null)") != std::string::npos);
}

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

  EXPECT_TRUE(ostream.str().find("> Image") != std::string::npos);
  EXPECT_TRUE(ostream.str().find("> ImagePipe") != std::string::npos);
  // fxb/39484. Re-enable this by injecting an Image with an associated escher::Image,
  // or by refactoring gfx::Image itself and updating RenderVisitor.
  // EXPECT_TRUE(ostream.str().find("use_protected_memory:") != std::string::npos);
}

TEST_F(DumpVisitorTest, ViewAndViewHolderDebugNames) {
  ResourceId next_id = 1;

  scenic::ViewRefPair view_ref_pair = scenic::ViewRefPair::New();

  ViewPtr view = fxl::MakeRefCounted<View>(
      session(), next_id++, std::move(view_ref_pair.control_ref), std::move(view_ref_pair.view_ref),
      "test_debug_name1", session()->shared_error_reporter(), session()->view_tree_updater(),
      session()->event_reporter()->GetWeakPtr());

  ViewHolderPtr view_holder = fxl::MakeRefCounted<ViewHolder>(
      session(), session()->id(), next_id++, "test_debug_name2", session()->view_tree_updater());

  std::ostringstream ostream;
  std::unordered_set<GlobalId, GlobalId::Hash> visited;
  DumpVisitor::VisitorContext context(ostream, &visited);
  DumpVisitor visitor(std::move(context));
  visitor.Visit(view.get());

  EXPECT_TRUE(ostream.str().find("debug_name=test_debug_name1") != std::string::npos);
  visitor.Visit(view_holder.get());
  EXPECT_TRUE(ostream.str().find("debug_name=test_debug_name2") != std::string::npos);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
