// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/paper/paper_transform_stack.h"

#include <gtest/gtest.h>

#include "src/ui/lib/escher/geometry/plane_ops.h"
#include "src/ui/lib/escher/geometry/transform.h"

namespace {
using namespace escher;

TEST(PaperTransformStack, StackSize) {
  PaperTransformStack stack;
  EXPECT_EQ(0U, stack.size());
  EXPECT_TRUE(stack.empty());
  stack.PushScale(2.f);
  stack.PushScale(3.f);
  stack.PushScale(4.f);
  EXPECT_EQ(3U, stack.size());
  EXPECT_FALSE(stack.empty());
  stack.Pop();
  stack.Pop();
  EXPECT_EQ(1U, stack.size());
  EXPECT_FALSE(stack.empty());
  stack.Pop();
  EXPECT_EQ(0U, stack.size());
  EXPECT_TRUE(stack.empty());
}

TEST(PaperTransformStack, TransformVector) {
  PaperTransformStack stack;
  stack.PushTranslation(vec3(3, 4, 5));
  stack.PushScale(2);

  // The vector is scaled first, then translated.
  vec4 transformed = stack.Top().matrix * vec4(10, 10, 10, 1);
  EXPECT_EQ(transformed, vec4(23, 24, 25, 1));

  // The vector is translated first, then scaled.
  stack.Clear();
  stack.PushScale(2);
  stack.PushTranslation(vec3(3, 4, 5));
  transformed = stack.Top().matrix * vec4(10, 10, 10, 1);
  EXPECT_EQ(transformed, vec4(26, 28, 30, 1));
}

void TestPushIdentity(PaperTransformStack* stack) {
  PaperTransformStack::Item item = stack->Top();
  size_t sz = stack->size();
  stack->PushIdentity();
  EXPECT_EQ(item.matrix, stack->Top().matrix);
  EXPECT_EQ(item.clip_planes, stack->Top().clip_planes);
  EXPECT_EQ(sz + 1, stack->size());
  stack->Pop();
  EXPECT_EQ(item.matrix, stack->Top().matrix);
  EXPECT_EQ(item.clip_planes, stack->Top().clip_planes);
  EXPECT_EQ(sz, stack->size());
}

TEST(PaperTransformStack, Transform) {
  std::vector<Transform> transforms{
      Transform(vec3(5, 7, 9), vec3(5, 5, 5), glm::angleAxis(2.1f, glm::normalize(vec3(1, 2, -5))),
                vec3(2, 1, 2)),
      Transform(vec3(-2, 7, 13), vec3(.5f, .5f, .5f),
                glm::angleAxis(.9f, glm::normalize(vec3(3, -1, -2))), vec3(2, 1, 2)),
      Transform(vec3(-2, -3, -5), vec3(.75f, .75f, .75f),
                glm::angleAxis(.4f, glm::normalize(vec3(4, 1, -2))), vec3(2, 1, 2)),
  };

  PaperTransformStack stack;
  stack.PushIdentity();

  std::vector<plane3> clip_planes{plane3(glm::normalize(vec3(1, 0, 0)), 5.f),
                                  plane3(glm::normalize(vec3(1, 1, 1)), -5.f)};

  mat4 top_matrix;
  for (auto& t : transforms) {
    // Matrix which will be pushed onto the stack.
    mat4 m = static_cast<mat4>(t);

    // Generate the matrix that we expect to see on top of the stack after
    // pushing |m|.
    top_matrix = top_matrix * m;

    // Generate the clip-planes that we expect to see on top of the stack after
    // pushing |m|.
    std::vector<plane3> top_clip_planes = stack.Top().clip_planes;
    for (size_t i = 0; i < top_clip_planes.size(); ++i) {
      top_clip_planes[i] = TransformPlane(m, top_clip_planes[i]);
    }

    stack.PushTransform(m);
    EXPECT_EQ(stack.Top().clip_planes, top_clip_planes);
    EXPECT_EQ(stack.Top().matrix, top_matrix);

    // Add some additional clip-planes to the stack.  They are not transformed
    // until the next transform is pushed.
    const size_t num_clip_planes = stack.Top().clip_planes.size();
    stack.AddClipPlanes(clip_planes);
    ASSERT_EQ(num_clip_planes + 2, stack.Top().clip_planes.size());
    EXPECT_EQ(clip_planes[0], stack.Top().clip_planes[num_clip_planes]);
    EXPECT_EQ(clip_planes[1], stack.Top().clip_planes[num_clip_planes + 1]);
    TestPushIdentity(&stack);
  }
}

void TestPlanesSimilar(const plane3& p1, const plane3& p2) {
  vec4 diff = vec4(p1.dir(), p1.dist()) - vec4(p2.dir(), p2.dist());
  EXPECT_NEAR(0.f, glm::dot(diff, diff), kEpsilon);
}

TEST(PaperTransformStack, Translation) {
  PaperTransformStack stack1;
  PaperTransformStack stack2;

  std::vector<plane3> clip_planes{plane3(glm::normalize(vec3(1, 0, 0)), 5.f),
                                  plane3(glm::normalize(vec3(1, 1, 1)), -5.f)};

  stack1.PushIdentity();
  stack1.AddClipPlanes(clip_planes);
  stack2.PushIdentity();
  stack2.AddClipPlanes(clip_planes);

  std::vector<vec3> translations{vec3(10, 19, 31), vec3(-1, 3, 17), vec3(-17, -14, 13),
                                 vec3(2, 4, 6)};
  for (auto& t : translations) {
    // Generate matrix that has the same effect as the translation.
    mat4 m = glm::translate(mat4(), t);

    stack1.PushTranslation(t);
    stack2.PushTransform(m);

    EXPECT_EQ(stack1.size(), stack2.size());
    EXPECT_EQ(stack1.Top().matrix, stack2.Top().matrix);
    TestPlanesSimilar(stack1.Top().clip_planes[0], stack2.Top().clip_planes[0]);
    TestPlanesSimilar(stack1.Top().clip_planes[1], stack2.Top().clip_planes[1]);
  }
}

TEST(PaperTransformStack, Scale) {
  PaperTransformStack stack1;
  PaperTransformStack stack2;

  std::vector<plane3> clip_planes{plane3(glm::normalize(vec3(1, 0, 0)), 5.f),
                                  plane3(glm::normalize(vec3(1, 1, 1)), -5.f)};

  stack1.PushIdentity();
  stack1.AddClipPlanes(clip_planes);
  stack2.PushIdentity();
  stack2.AddClipPlanes(clip_planes);

  std::vector<float> scales{2, 5, 7, 9};

  for (auto& s : scales) {
    // Generate matrix that has the same effect as the translation.
    mat4 m = glm::scale(mat4(), vec3(s, s, s));

    stack1.PushScale(s);
    stack2.PushTransform(m);

    EXPECT_EQ(stack1.size(), stack2.size());
    EXPECT_EQ(stack1.Top().matrix, stack2.Top().matrix);
    TestPlanesSimilar(stack1.Top().clip_planes[0], stack2.Top().clip_planes[0]);
    TestPlanesSimilar(stack1.Top().clip_planes[1], stack2.Top().clip_planes[1]);
  }
}

}  // namespace
