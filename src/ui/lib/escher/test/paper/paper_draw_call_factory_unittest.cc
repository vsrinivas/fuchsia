// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/paper/paper_draw_call_factory.h"

#include <gtest/gtest.h>

#include "src/ui/lib/escher/paper/paper_material.h"
#include "src/ui/lib/escher/test/common/gtest_escher.h"
#include "src/ui/lib/escher/test/paper/paper_tester.h"

#include <glm/gtc/matrix_access.hpp>

namespace {
using namespace escher;

TEST(PaperDrawCallFactory, OpaqueSortKeyBits) {
  Hash dddd, bbbb;
  dddd.val = 0xdddddddddddddddd;
  bbbb.val = 0xbbbbbbbbbbbbbbbb;
  float depth = 11.2345f;

  auto key = PaperDrawCallFactory::SortKey::NewOpaque(dddd, bbbb, depth);

  EXPECT_EQ(0xdddd00000000bbbb, key.key() & 0xffff00000000ffff);
  EXPECT_EQ(depth, glm::uintBitsToFloat((key.key() >> 16) & 0xffffffff));
}

TEST(PaperDrawCallFactory, WireframeSortKeyBits) {
  Hash dddd, bbbb;
  dddd.val = 0xdddddddddddddddd;
  bbbb.val = 0xbbbbbbbbbbbbbbbb;
  float depth = 11.2345f;

  auto key = PaperDrawCallFactory::SortKey::NewWireframe(dddd, bbbb, depth);

  EXPECT_EQ(0xdddd00000000bbbb, key.key() & 0xffff00000000ffff);
  EXPECT_EQ(depth, glm::uintBitsToFloat((key.key() >> 16) & 0xffffffff));
}

TEST(PaperDrawCallFactory, TranslucentSortKeyBits) {
  Hash dddd, bbbb;
  dddd.val = 0xdddddddddddddddd;
  bbbb.val = 0xbbbbbbbbbbbbbbbb;
  float depth = 11.2345f;

  auto key = PaperDrawCallFactory::SortKey::NewTranslucent(dddd, bbbb, depth);

  EXPECT_EQ(0x00000000ddddbbbb, key.key() & 0x00000000ffffffff);
  EXPECT_EQ(depth, glm::uintBitsToFloat((key.key() >> 32) ^ 0xffffffff));
}

TEST(PaperDrawCallFactory, SortKeyComparisons) {
  Hash low_hash, high_hash;
  low_hash.val = 0xaaaaaaaaaaaaaaaa;
  high_hash.val = 0xbbbbbbbbbbbbbbbb;
  float near_depth = 11.2345f;
  float far_depth = 22.6789f;

  using Key = PaperDrawCallFactory::SortKey;

  // For both opaque and translucent, all else being equal, a low hash is sorted
  // earlier than a high hash.
  EXPECT_LT(Key::NewOpaque(low_hash, low_hash, near_depth).key(),
            Key::NewOpaque(low_hash, high_hash, near_depth).key());
  EXPECT_LT(Key::NewOpaque(low_hash, low_hash, near_depth).key(),
            Key::NewOpaque(high_hash, low_hash, near_depth).key());
  EXPECT_LT(Key::NewTranslucent(low_hash, low_hash, near_depth).key(),
            Key::NewTranslucent(low_hash, high_hash, near_depth).key());
  EXPECT_LT(Key::NewTranslucent(low_hash, low_hash, near_depth).key(),
            Key::NewTranslucent(high_hash, low_hash, near_depth).key());

  // For both opaque and translucent, the pipeline hash is more important than
  // the draw hash.
  EXPECT_LT(Key::NewOpaque(low_hash, high_hash, near_depth).key(),
            Key::NewOpaque(high_hash, low_hash, near_depth).key());
  EXPECT_LT(Key::NewTranslucent(low_hash, high_hash, near_depth).key(),
            Key::NewTranslucent(high_hash, low_hash, near_depth).key());

  // For opaque, depth sorting is front-to-back (to reduce overdraw), and for
  // translucent it is back-to-front (necessary for correct rendering).
  EXPECT_LT(Key::NewOpaque(low_hash, low_hash, near_depth).key(),
            Key::NewOpaque(low_hash, low_hash, far_depth).key());
  EXPECT_LT(Key::NewTranslucent(low_hash, low_hash, far_depth).key(),
            Key::NewTranslucent(low_hash, low_hash, near_depth).key());

  // For translucent, depth sorting is most important (this is necessary for
  // correct rendering).
  EXPECT_LT(Key::NewTranslucent(low_hash, low_hash, far_depth).key(),
            Key::NewTranslucent(high_hash, high_hash, near_depth).key());

  // For opaque, sorting by pipeline is most important, then depth, then draw
  // hash.
  EXPECT_LT(Key::NewOpaque(low_hash, low_hash, far_depth).key(),
            Key::NewOpaque(high_hash, low_hash, near_depth).key());
  EXPECT_LT(Key::NewOpaque(low_hash, high_hash, near_depth).key(),
            Key::NewOpaque(low_hash, low_hash, far_depth).key());
}

using PaperDrawCallFactoryTest = test::TestWithVkValidationLayer;

VK_TEST_F(PaperDrawCallFactoryTest, ShaderList) {
  auto escher = test::GetEscher()->GetWeakPtr();
  PaperRendererConfig no_shadow_config{.shadow_type = PaperRendererShadowType::kNone};
  PaperRendererConfig shadow_config{.shadow_type = PaperRendererShadowType::kShadowVolume};

  PaperDrawCallFactory factory(escher, no_shadow_config);
  MaterialPtr mat = Material::New(vec4(1, 1, 1, 1));
  PaperShaderList shader_list{};

  // Opaque material.
  {
    mat->set_type(Material::Type::kOpaque);

    factory.SetConfig(no_shadow_config);
    shader_list = PaperTester::GetShaderList(factory, *mat.get(), false);
    EXPECT_EQ(PaperTester::ambient_light_program(factory),
              shader_list.get_shader(PaperShaderListSelector::kAmbientLighting));
    EXPECT_EQ(PaperTester::point_light_program(factory),
              shader_list.get_shader(PaperShaderListSelector::kPointLighting));
    EXPECT_EQ(nullptr, shader_list.get_shader(PaperShaderListSelector::kShadowCaster));
    EXPECT_EQ(nullptr, shader_list.get_shader(PaperShaderListSelector::kShadowCasterDebug));

    factory.SetConfig(shadow_config);
    shader_list = PaperTester::GetShaderList(factory, *mat.get(), true);
    EXPECT_EQ(PaperTester::ambient_light_program(factory),
              shader_list.get_shader(PaperShaderListSelector::kAmbientLighting));
    EXPECT_EQ(PaperTester::point_light_program(factory),
              shader_list.get_shader(PaperShaderListSelector::kPointLighting));
    EXPECT_EQ(PaperTester::shadow_volume_geometry_program(factory),
              shader_list.get_shader(PaperShaderListSelector::kShadowCaster));
    EXPECT_EQ(PaperTester::shadow_volume_geometry_debug_program(factory),
              shader_list.get_shader(PaperShaderListSelector::kShadowCasterDebug));
  }

  // Wireframe material.
  {
    mat->set_type(Material::Type::kWireframe);

    factory.SetConfig(no_shadow_config);
    shader_list = PaperTester::GetShaderList(factory, *mat.get(), false);
    EXPECT_EQ(PaperTester::no_lighting_program(factory),
              shader_list.get_shader(PaperShaderListSelector::kAmbientLighting));
    EXPECT_EQ(nullptr, shader_list.get_shader(PaperShaderListSelector::kPointLighting));
    EXPECT_EQ(nullptr, shader_list.get_shader(PaperShaderListSelector::kShadowCaster));
    EXPECT_EQ(nullptr, shader_list.get_shader(PaperShaderListSelector::kShadowCasterDebug));

    factory.SetConfig(shadow_config);
    shader_list = PaperTester::GetShaderList(factory, *mat.get(), true);
    EXPECT_EQ(PaperTester::no_lighting_program(factory),
              shader_list.get_shader(PaperShaderListSelector::kAmbientLighting));
    EXPECT_EQ(nullptr, shader_list.get_shader(PaperShaderListSelector::kPointLighting));
    EXPECT_EQ(nullptr, shader_list.get_shader(PaperShaderListSelector::kShadowCaster));
    EXPECT_EQ(nullptr, shader_list.get_shader(PaperShaderListSelector::kShadowCasterDebug));
  }

  // Translucent material.
  {
    mat->set_type(Material::Type::kTranslucent);

    factory.SetConfig(no_shadow_config);
    shader_list = PaperTester::GetShaderList(factory, *mat.get(), false);
    EXPECT_EQ(PaperTester::no_lighting_program(factory),
              shader_list.get_shader(PaperShaderListSelector::kAmbientLighting));
    EXPECT_EQ(nullptr, shader_list.get_shader(PaperShaderListSelector::kPointLighting));
    EXPECT_EQ(nullptr, shader_list.get_shader(PaperShaderListSelector::kShadowCaster));
    EXPECT_EQ(nullptr, shader_list.get_shader(PaperShaderListSelector::kShadowCasterDebug));

    factory.SetConfig(shadow_config);
    shader_list = PaperTester::GetShaderList(factory, *mat.get(), true);
    EXPECT_EQ(PaperTester::no_lighting_program(factory),
              shader_list.get_shader(PaperShaderListSelector::kAmbientLighting));
    EXPECT_EQ(nullptr, shader_list.get_shader(PaperShaderListSelector::kPointLighting));
    EXPECT_EQ(nullptr, shader_list.get_shader(PaperShaderListSelector::kShadowCaster));
    EXPECT_EQ(nullptr, shader_list.get_shader(PaperShaderListSelector::kShadowCasterDebug));
  }
}

}  // namespace
