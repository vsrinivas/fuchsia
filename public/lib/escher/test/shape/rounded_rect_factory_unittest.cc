// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/shape/rounded_rect_factory.h"

#include "garnet/public/lib/escher/test/gtest_escher.h"

#include "lib/escher/shape/mesh.h"

namespace {
using namespace escher;

VK_TEST(RoundedRectFactory, NegativeBounds) {
  auto escher = test::GetEscher();
  RoundedRectSpec rect_spec(-1.f, -1.f, -2.f, -2.f, -2.f, -2.f);
  MeshSpec mesh_spec{MeshAttribute::kPosition2D | MeshAttribute::kUV};
  auto factory = std::make_unique<RoundedRectFactory>(escher->GetWeakPtr());
  auto mesh = factory->NewRoundedRect(rect_spec, mesh_spec);
  EXPECT_NE(MeshPtr(), mesh);

  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
}

}  // namespace
