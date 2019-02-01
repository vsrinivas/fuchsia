// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/escher/test/gtest_escher.h"

#include "lib/escher/fs/hack_filesystem.h"
#include "lib/escher/vk/shader_module_template.h"
#include "lib/escher/vk/shader_variant_args.h"

#include "third_party/shaderc/libshaderc/include/shaderc/shaderc.hpp"

namespace {
using namespace escher;

// Test fixture used by all ShaderModuleTemplate tests.  All tests begin with
// the same filesystem and module-template.
class ShaderModuleTemplateTest : public ::testing::Test {
 public:
  // |::testing::Test|.
  void SetUp() override;

  // |::testing::Test|.
  void TearDown() override;

  const HackFilesystemPtr& filesystem() const { return filesystem_; }
  const ShaderModuleTemplatePtr& module_template() const {
    return module_template_;
  }

 private:
  HackFilesystemPtr filesystem_;
  ShaderModuleTemplatePtr module_template_;
};

// Shader file that exists in ShaderModuleTemplateTest's filesystem.
static const char* kMainPath = "main";
static constexpr char kMain[] = R"GLSL(
#version 450
#extension GL_ARB_separate_shader_objects : enable

#include <descriptor_sets>
#include <per_vertex_out>
#include <vertex_attributes>

#ifdef SHIFTED_MODEL_POSITION
#include <compute_shifted_position>
#else
#include <compute_identity_position>
#endif

layout(location = 0) out vec2 fragUV;

void main() {
  vec4 pos = ComputeVertexPosition();
  gl_Position = vp_matrix * model_transform  * pos;
  fragUV = inUV;
}

)GLSL";

// Shader file that exists in ShaderModuleTemplateTest's filesystem.
static const char* kPerVertexOutPath = "per_vertex_out";
static constexpr char kPerVertexOut[] = R"GLSL(
out gl_PerVertex {
  vec4 gl_Position;
};
)GLSL";

// Shader file that exists in ShaderModuleTemplateTest's filesystem.
static const char* kDescriptorSetsPath = "descriptor_sets";
static constexpr char kDescriptorSets[] = R"GLSL(
layout(set = 0, binding = 0) uniform PerModel {
  vec2 frag_coord_to_uv_multiplier;
  float time;
  vec3 ambient_light_intensity;
  vec3 direct_light_intensity;
};

// Use binding 2 to avoid potential collision with PerModelSampler
layout(set = 0, binding = 2) uniform ViewProjection {
  mat4 vp_matrix;
};

layout(set = 1, binding = 0) uniform PerObject {
  mat4 model_transform;
  mat4 light_transform;
  vec4 color;
};
)GLSL";

// Shader file that exists in ShaderModuleTemplateTest's filesystem.
static const char* kVertexAttributesPath = "vertex_attributes";
static constexpr char kVertexAttributes[] = R"GLSL(
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
#ifdef ATTR_POSITION_OFFSET
layout(location = 2) in vec3 inPositionOffset;
#endif
#ifdef ATTR_PERIMETER
layout(location = 3) in float inPerimeter;
#endif
)GLSL";

// Shader file that exists in ShaderModuleTemplateTest's filesystem.
static const char* kComputeIdentityPositionPath = "compute_identity_position";
static constexpr char kComputeIdentityPosition[] = R"GLSL(
vec4 ComputeVertexPosition() {
  return vec4(inPosition, 1);
}
)GLSL";

// Shader file that exists in ShaderModuleTemplateTest's filesystem.
static const char* kComputeShiftedPositionPath = "compute_shifted_position";
static constexpr char kComputeShiftedPosition[] = R"GLSL(
vec4 ComputeVertexPosition() {
  return vec4(inPosition + vec3(10, 10, 0), 1);
}
)GLSL";

void ShaderModuleTemplateTest::SetUp() {
  // Initialize filesystem.
  filesystem_ = HackFilesystem::New();
  filesystem_->WriteFile(HackFilePath(kMainPath), kMain);
  filesystem_->WriteFile(HackFilePath(kPerVertexOutPath), kPerVertexOut);
  filesystem_->WriteFile(HackFilePath(kDescriptorSetsPath), kDescriptorSets);
  filesystem_->WriteFile(HackFilePath(kVertexAttributesPath),
                         kVertexAttributes);
  filesystem_->WriteFile(HackFilePath(kComputeIdentityPositionPath),
                         kComputeIdentityPosition);
  filesystem_->WriteFile(HackFilePath(kComputeShiftedPositionPath),
                         kComputeShiftedPosition);

  // Initialize module template.
  auto escher = test::GetEscher();
  module_template_ = fxl::MakeRefCounted<ShaderModuleTemplate>(
      escher->vk_device(), escher->shaderc_compiler(), ShaderStage::kVertex,
      kMainPath, filesystem());
}

void ShaderModuleTemplateTest::TearDown() {
  module_template_ = nullptr;
  filesystem_ = nullptr;
}

VK_TEST_F(ShaderModuleTemplateTest, SameAndDifferentVariants) {
  ShaderVariantArgs args1({{"ATTR_POSITION_OFFSET", "1"}});
  ShaderVariantArgs args2({{"ATTR_POSITION_OFFSET", "1"}});
  ShaderVariantArgs args3({{"ATTR_PERIMETER", "1"}});

  auto module1 = module_template()->GetShaderModuleVariant(args1);
  auto module2 = module_template()->GetShaderModuleVariant(args2);
  auto module3 = module_template()->GetShaderModuleVariant(args3);

  // Because two of the calls to GetShaderModuleVariant() use the same args,
  // module1 and module2 both refer to the same variant.
  EXPECT_EQ(module1.get(), module2.get());
  EXPECT_NE(module1.get(), module3.get());
  EXPECT_NE(module2.get(), module3.get());
}

class TestShaderModuleListener : public ShaderModuleListener {
 public:
  explicit TestShaderModuleListener(ShaderModulePtr module)
      : module_(std::move(module)) {
    module_->AddShaderModuleListener(this);
  }

  ~TestShaderModuleListener() { module_->RemoveShaderModuleListener(this); }

  int32_t update_count() const { return update_count_; }

 private:
  void OnShaderModuleUpdated(ShaderModule* shader_module) override {
    EXPECT_EQ(shader_module, module_.get());
    ++update_count_;
  }

  ShaderModulePtr module_;
  int32_t update_count_ = 0;
};

VK_TEST_F(ShaderModuleTemplateTest, Listeners) {
  ShaderVariantArgs args({{"ATTR_POSITION_OFFSET", "1"}});
  auto module = module_template()->GetShaderModuleVariant(args);

  // New listeners are immediately updated.
  TestShaderModuleListener listener(std::move(module));
  EXPECT_EQ(listener.update_count(), 1);

  // This doesn't cause any problems because no variants use this file, because
  // SHIFTED_MODEL_POSITION isn't defined.
  filesystem()->WriteFile(HackFilePath(kComputeShiftedPositionPath),
                          "garbage glsl code");
  EXPECT_EQ(listener.update_count(), 1);

  // Changing a file that was transitively included causes the module's SPIR-V
  // to be regenerated. (NOTE: HackFilesystem could be smarter and only notify
  // when something has actually changed, but it doesn't).
  filesystem()->WriteFile(HackFilePath(kComputeIdentityPositionPath),
                          kComputeIdentityPosition);
  EXPECT_EQ(listener.update_count(), 2);
}

}  // anonymous namespace
