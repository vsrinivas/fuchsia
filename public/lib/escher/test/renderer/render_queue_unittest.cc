// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/renderer/render_queue.h"

#include "gtest/gtest.h"

namespace {
using namespace escher;

struct TestStatistics;

// Per-object data stored in a RenderQueue.  If this were for rendering a mesh,
// it might contain references to the vertex and index buffers.
struct TestRenderObject {
  uint32_t id = 0;
  TestStatistics* stats = nullptr;
};

// Per-draw-call data stored in a RenderQueue.  If this were for rendering a
// mesh, it might contain the transform matrix, as well as number of indices to
// render.
struct TestRenderInstance {
  uint32_t foo;  // unused
};

// Instead of emitting Vulkan commands, the render-functions add instances of
// TestFuncInvocation to a TestStatistics container, for later analysis.
struct TestFuncInvocation {
  // Which render-func was called, RenderFuncOne() or RenderFuncTwo() ?
  // 0 - not called
  // 1 - RenderFuncOne()
  // 2 - RenderFuncTwo()
  uint32_t render_func_id = 0;

  // ID of TestRenderObject.
  uint32_t obj_id;

  // Sort-keys of all instances drawn by a render-func invocation.
  std::vector<uint64_t> sort_keys;

  // Pointers to all instance data passed to a render-func invocation.
  std::vector<const TestRenderInstance*> instance_data;
};

struct TestStatistics {
  std::vector<TestFuncInvocation> invocations;
};

void BaseRenderFunc(CommandBuffer* cb, const RenderQueueItem* items,
                    uint32_t instance_count, uint32_t render_func_id) {
  EXPECT_NE(0U, instance_count);
  ASSERT_TRUE(render_func_id == 1 || render_func_id == 2);

  TestFuncInvocation invocation;
  invocation.render_func_id = render_func_id;

  auto* obj = static_cast<const TestRenderObject*>(items[0].object_data);
  invocation.obj_id = obj->id;

  for (uint32_t i = 0; i < instance_count; ++i) {
    auto* other = static_cast<const TestRenderObject*>(items[i].object_data);
    auto* inst = static_cast<const TestRenderInstance*>(items[i].instance_data);
    EXPECT_EQ(obj->id, other->id);
    invocation.sort_keys.push_back(items[i].sort_key);
    invocation.instance_data.push_back(inst);
  }

  obj->stats->invocations.push_back(invocation);
}

void RenderFuncOne(CommandBuffer* cb, const RenderQueueItem* items,
                   uint32_t instance_count) {
  BaseRenderFunc(cb, items, instance_count, 1);
}

void RenderFuncTwo(CommandBuffer* cb, const RenderQueueItem* items,
                   uint32_t instance_count) {
  BaseRenderFunc(cb, items, instance_count, 2);
}

TEST(RenderQueue, PushSortGenerate) {
  RenderQueue queue;

  TestStatistics stats;

  TestRenderObject one = {.id = 1, .stats = &stats};
  TestRenderInstance one1;
  TestRenderInstance one2;
  TestRenderInstance one3;

  TestRenderObject two = {.id = 2, .stats = &stats};
  TestRenderInstance two1;
  TestRenderInstance two2;
  TestRenderInstance two3;

  queue.Push(1, &one, &one1, RenderFuncOne);
  queue.Push(2, &one, &one2, RenderFuncOne);
  queue.Push(6, &one, &one3, RenderFuncOne);

  queue.Push(3, &two, &two1, RenderFuncTwo);
  queue.Push(5, &two, &two2, RenderFuncTwo);
  queue.Push(4, &two, &two3, RenderFuncTwo);

  // A real application would sort the queue first, but this allows us to verify
  // that things are rendered in the order that they were inserted.  There
  // should be one invocation of RenderFuncOne() and RenderFuncTwo(), each with
  // an instance-count of 3.
  queue.GenerateCommands(nullptr, nullptr);
  ASSERT_EQ(2U, stats.invocations.size());
  EXPECT_EQ(1U, stats.invocations[0].render_func_id);
  EXPECT_EQ(2U, stats.invocations[1].render_func_id);
  ASSERT_EQ(3U, stats.invocations[0].sort_keys.size());
  ASSERT_EQ(3U, stats.invocations[0].instance_data.size());
  ASSERT_EQ(3U, stats.invocations[1].sort_keys.size());
  ASSERT_EQ(3U, stats.invocations[1].instance_data.size());

  // Furthermore, both the sort-keys and instance data should appear in the same
  // order they were pushed.
  EXPECT_EQ(1U, stats.invocations[0].sort_keys[0]);
  EXPECT_EQ(2U, stats.invocations[0].sort_keys[1]);
  EXPECT_EQ(6U, stats.invocations[0].sort_keys[2]);
  EXPECT_EQ(3U, stats.invocations[1].sort_keys[0]);
  EXPECT_EQ(5U, stats.invocations[1].sort_keys[1]);
  EXPECT_EQ(4U, stats.invocations[1].sort_keys[2]);
  EXPECT_EQ(&one1, stats.invocations[0].instance_data[0]);
  EXPECT_EQ(&one2, stats.invocations[0].instance_data[1]);
  EXPECT_EQ(&one3, stats.invocations[0].instance_data[2]);
  EXPECT_EQ(&two1, stats.invocations[1].instance_data[0]);
  EXPECT_EQ(&two2, stats.invocations[1].instance_data[1]);
  EXPECT_EQ(&two3, stats.invocations[1].instance_data[2]);

  // Clear stats and sort before generating commands.  This time we expect three
  // render-func invocations, two of RenderFuncOne() and one of RenderFuncTwo(),
  // because "one1" and "one2" have the lowest sort keys, and "one3" has the
  // highest sort key.
  stats.invocations.clear();
  queue.Sort();
  queue.GenerateCommands(nullptr, nullptr);
  ASSERT_EQ(3U, stats.invocations.size());
  EXPECT_EQ(1U, stats.invocations[0].render_func_id);
  EXPECT_EQ(2U, stats.invocations[1].render_func_id);
  EXPECT_EQ(1U, stats.invocations[2].render_func_id);
  EXPECT_EQ(1U, stats.invocations[0].obj_id);
  EXPECT_EQ(2U, stats.invocations[1].obj_id);
  EXPECT_EQ(1U, stats.invocations[2].obj_id);
  ASSERT_EQ(2U, stats.invocations[0].sort_keys.size());
  ASSERT_EQ(2U, stats.invocations[0].instance_data.size());
  ASSERT_EQ(3U, stats.invocations[1].sort_keys.size());
  ASSERT_EQ(3U, stats.invocations[1].instance_data.size());
  ASSERT_EQ(1U, stats.invocations[2].sort_keys.size());
  ASSERT_EQ(1U, stats.invocations[2].instance_data.size());

  // Verify that all instances appear in order of their sort-key.
  EXPECT_EQ(1U, stats.invocations[0].sort_keys[0]);
  EXPECT_EQ(2U, stats.invocations[0].sort_keys[1]);
  EXPECT_EQ(3U, stats.invocations[1].sort_keys[0]);
  EXPECT_EQ(4U, stats.invocations[1].sort_keys[1]);
  EXPECT_EQ(5U, stats.invocations[1].sort_keys[2]);
  EXPECT_EQ(6U, stats.invocations[2].sort_keys[0]);
  EXPECT_EQ(&one1, stats.invocations[0].instance_data[0]);
  EXPECT_EQ(&one2, stats.invocations[0].instance_data[1]);
  EXPECT_EQ(&two1, stats.invocations[1].instance_data[0]);
  EXPECT_EQ(&two3, stats.invocations[1].instance_data[1]);
  EXPECT_EQ(&two2, stats.invocations[1].instance_data[2]);
  EXPECT_EQ(&one3, stats.invocations[2].instance_data[0]);

  // Finally, verify that clearing the queue works.
  stats.invocations.clear();
  queue.clear();
  queue.Sort();
  queue.GenerateCommands(nullptr, nullptr);
  EXPECT_TRUE(stats.invocations.empty());
}

TEST(RenderQueue, SameObjectDifferentFuncs) {
  RenderQueue queue;

  TestStatistics stats;

  TestRenderObject obj = {.id = 1, .stats = &stats};
  TestRenderInstance inst1;
  TestRenderInstance inst2;
  TestRenderInstance inst3;
  TestRenderInstance inst4;
  TestRenderInstance inst5;
  TestRenderInstance inst6;

  queue.Push(1, &obj, &inst1, RenderFuncOne);
  queue.Push(2, &obj, &inst2, RenderFuncOne);
  queue.Push(3, &obj, &inst3, RenderFuncTwo);
  queue.Push(4, &obj, &inst4, RenderFuncTwo);
  queue.Push(5, &obj, &inst5, RenderFuncTwo);
  queue.Push(6, &obj, &inst6, RenderFuncOne);

  // Don't bother sorting, we already tested that in a different test case.
  queue.GenerateCommands(nullptr, nullptr);
  ASSERT_EQ(3U, stats.invocations.size());
  // Expect two instances rendered with RenderFuncOne().
  EXPECT_EQ(1U, stats.invocations[0].render_func_id);
  EXPECT_EQ(2U, stats.invocations[0].instance_data.size());
  // Expect three instances rendered with RenderFuncTwo().
  EXPECT_EQ(2U, stats.invocations[1].render_func_id);
  EXPECT_EQ(3U, stats.invocations[1].instance_data.size());
  // Expect one final instance rendered with RenderFuncOne().
  EXPECT_EQ(1U, stats.invocations[2].render_func_id);
  EXPECT_EQ(1U, stats.invocations[2].instance_data.size());
}

}  // namespace
