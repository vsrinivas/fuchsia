// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/reachability.h"

#include <set>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/services/mixer/fidl/testing/fake_graph.h"

namespace media_audio {

using ::testing::Pair;
using ::testing::UnorderedElementsAre;

TEST(ReachabilityTest, RecomputeMaxDownstreamOutputPipelineDelay) {
  // Node graph is structured as follows:
  //
  // ```
  //    1   2    producers
  //    |   |
  //    |   3
  //     \ /
  //      |
  //    +-|-----+
  //    | 4   5 |
  //    |       | meta 50
  //    | 6   7 |
  //    +-|---|-+
  //       \ /
  //        |
  //        8
  //        |
  //    +---|---+
  //    |   9   |
  //    |       | meta 51
  //    | 10 11 |
  //    +-|---|-+
  //      12 13   consumers
  // ```
  FakeGraph graph({
      .meta_nodes =
          {
              {50, {.source_children = {4, 5}, .dest_children = {6, 7}}},
              {51, {.source_children = {9}, .dest_children = {10, 11}}},
          },
      .edges =
          {
              {1, 4},
              {2, 3},
              {3, 4},
              {6, 8},
              {7, 8},
              {8, 9},
              {10, 12},
              {11, 13},
          },
      .types =
          {
              {Node::Type::kProducer, {1, 2}},
              {Node::Type::kConsumer, {12, 13}},
          },
      .default_pipeline_direction = PipelineDirection::kOutput,
      .threads =
          {
              {ThreadId(1), {1, 2, 3, 4, 5}},
              {ThreadId(2), {6, 7, 8, 9, 10, 11, 12, 13}},
          },
  });

  // Set external values.
  graph.node(12)->set_max_downstream_output_pipeline_delay(zx::nsec(2));
  graph.node(13)->set_max_downstream_output_pipeline_delay(zx::nsec(3));
  graph.node(12)->set_max_downstream_input_pipeline_delay(zx::nsec(999));  // unused
  graph.node(13)->set_max_downstream_input_pipeline_delay(zx::nsec(999));  // unused

  // Setup callbacks.
  std::unordered_set<int> updated;
  for (int k = 1; k <= 13; k++) {
    auto node = graph.node(k);
    auto tid = node->thread()->id();
    node->SetOnSetMaxDownstreamOutputPipelineDelay([&updated, k, tid]() {
      return std::make_pair(tid, [&updated, k]() { updated.insert(k); });
    });
  }

  // Initially, delays are defined at nodes 12 and 13 only.
  // Recomputing at node 10 should flood that delay upwards to all nodes.
  {
    SCOPED_TRACE("recompute 10");

    std::map<ThreadId, std::vector<fit::closure>> closures;
    RecomputeMaxDownstreamOutputPipelineDelay(*graph.node(10), closures);

    for (int k = 1; k <= 10; k++) {
      auto node = graph.node(k);
      SCOPED_TRACE(node->name());
      EXPECT_EQ(node->max_downstream_output_pipeline_delay(), zx::nsec(2));
    }

    EXPECT_TRUE(closures.count(1) > 0);
    EXPECT_TRUE(closures.count(2) > 0);

    for (auto& [tid, fns] : closures) {
      for (auto& fn : fns) {
        fn();
      }
      if (tid == 1) {
        EXPECT_THAT(updated, UnorderedElementsAre(1, 2, 3, 4, 5));
      } else if (tid == 2) {
        EXPECT_THAT(updated, UnorderedElementsAre(6, 7, 8, 9, 10));
      } else {
        ADD_FAILURE() << "unexpected ThreadID " << tid;
      }
      updated.clear();
    }
  }

  // Recomputing at node 11 should flood that delay upwards to all nodes, overriding the delays set
  // in the prior step.
  {
    SCOPED_TRACE("recompute 11");

    std::map<ThreadId, std::vector<fit::closure>> closures;
    RecomputeMaxDownstreamOutputPipelineDelay(*graph.node(11), closures);

    for (int k = 1; k <= 11; k++) {
      auto node = graph.node(k);
      SCOPED_TRACE(node->name());
      if (k == 10) {
        EXPECT_EQ(node->max_downstream_output_pipeline_delay(), zx::nsec(2));
      } else {
        EXPECT_EQ(node->max_downstream_output_pipeline_delay(), zx::nsec(3));
      }
    }

    EXPECT_TRUE(closures.count(1) > 0);
    EXPECT_TRUE(closures.count(2) > 0);

    for (auto& [tid, fns] : closures) {
      for (auto& fn : fns) {
        fn();
      }
      if (tid == 1) {
        EXPECT_THAT(updated, UnorderedElementsAre(1, 2, 3, 4, 5));
      } else if (tid == 2) {
        EXPECT_THAT(updated, UnorderedElementsAre(6, 7, 8, 9, 11));
      } else {
        ADD_FAILURE() << "unexpected ThreadID " << tid;
      }
      updated.clear();
    }
  }

  // There have been no changes, so this is a no-op.
  {
    SCOPED_TRACE("recompute 9");

    std::map<ThreadId, std::vector<fit::closure>> closures;
    RecomputeMaxDownstreamOutputPipelineDelay(*graph.node(9), closures);
    EXPECT_TRUE(closures.empty());

    for (int k = 1; k <= 11; k++) {
      auto node = graph.node(k);
      SCOPED_TRACE(node->name());
      if (k == 10) {
        EXPECT_EQ(node->max_downstream_output_pipeline_delay(), zx::nsec(2));
      } else {
        EXPECT_EQ(node->max_downstream_output_pipeline_delay(), zx::nsec(3));
      }
    }
  }

  // Update edges 2->3, {1,3}->4, meta-{6,7}, and {6,7}->8.
  graph.node(3)->SetOnPresentationDelayForSourceEdge([&graph](const Node* source) {
    if (source == graph.node(2).get()) {
      return zx::nsec(320);
    } else {
      ADD_FAILURE() << "unexpected source " << source->name();
      return zx::nsec(0);
    }
  });
  graph.node(4)->SetOnPresentationDelayForSourceEdge([&graph](const Node* source) {
    if (source == graph.node(1).get()) {
      return zx::nsec(410);
    } else if (source == graph.node(3).get()) {
      return zx::nsec(430);
    } else {
      ADD_FAILURE() << "unexpected source " << source->name();
      return zx::nsec(0);
    }
  });
  graph.node(6)->SetOnPresentationDelayForSourceEdge([](const Node* source) {
    if (source == nullptr) {
      return zx::nsec(600);
    } else {
      ADD_FAILURE() << "unexpected source " << source->name();
      return zx::nsec(0);
    }
  });
  graph.node(7)->SetOnPresentationDelayForSourceEdge([](const Node* source) {
    if (source == nullptr) {
      return zx::nsec(700);
    } else {
      ADD_FAILURE() << "unexpected source " << source->name();
      return zx::nsec(0);
    }
  });
  graph.node(8)->SetOnPresentationDelayForSourceEdge([&graph](const Node* source) {
    if (source == graph.node(6).get()) {
      return zx::nsec(860);
    } else if (source == graph.node(7).get()) {
      return zx::nsec(870);
    } else {
      ADD_FAILURE() << "unexpected source " << source->name();
      return zx::nsec(0);
    }
  });

  // Recomputing any node 8 or below should result in no change.
  {
    SCOPED_TRACE("recompute 8");

    std::map<ThreadId, std::vector<fit::closure>> closures;
    RecomputeMaxDownstreamOutputPipelineDelay(*graph.node(8), closures);
    EXPECT_TRUE(closures.empty());

    for (int k = 1; k <= 8; k++) {
      auto node = graph.node(k);
      SCOPED_TRACE(node->name());
      EXPECT_EQ(node->max_downstream_output_pipeline_delay(), zx::nsec(3));
    }
  }

  // Recomputing 6 and 7 should flood changes upstream.
  {
    SCOPED_TRACE("recompute 6+7");

    std::map<ThreadId, std::vector<fit::closure>> closures;
    RecomputeMaxDownstreamOutputPipelineDelay(*graph.node(6), closures);
    RecomputeMaxDownstreamOutputPipelineDelay(*graph.node(7), closures);

    EXPECT_EQ(graph.node(1)->max_downstream_output_pipeline_delay(), zx::nsec(410 + 700 + 873));
    EXPECT_EQ(graph.node(2)->max_downstream_output_pipeline_delay(),
              zx::nsec(320 + 430 + 700 + 873));
    EXPECT_EQ(graph.node(3)->max_downstream_output_pipeline_delay(), zx::nsec(430 + 700 + 873));
    EXPECT_EQ(graph.node(4)->max_downstream_output_pipeline_delay(), zx::nsec(700 + 873));
    EXPECT_EQ(graph.node(5)->max_downstream_output_pipeline_delay(), zx::nsec(700 + 873));
    EXPECT_EQ(graph.node(6)->max_downstream_output_pipeline_delay(), zx::nsec(863));
    EXPECT_EQ(graph.node(7)->max_downstream_output_pipeline_delay(), zx::nsec(873));

    EXPECT_TRUE(closures.count(1) > 0);
    EXPECT_TRUE(closures.count(2) > 0);

    for (auto& [tid, fns] : closures) {
      for (auto& fn : fns) {
        fn();
      }
      if (tid == 1) {
        EXPECT_THAT(updated, UnorderedElementsAre(1, 2, 3, 4, 5));
      } else if (tid == 2) {
        EXPECT_THAT(updated, UnorderedElementsAre(6, 7));
      } else {
        ADD_FAILURE() << "unexpected ThreadID " << tid;
      }
      updated.clear();
    }
  }
}

TEST(ReachabilityTest, RecomputeMaxDownstreamLoopbackPipelineDelay) {
  // Node graph is structured as follows:
  //
  // ```
  //      1   2    producers (renderers)
  //      |   |
  //      |   3
  //       \ /
  //        |
  //    +---|---+
  //    |   4   |
  //    |       | meta 50 (splitter; 5=output, 6=loopback)
  //    | 5   6 |
  //    +-|---|-+
  //      |    \
  //      7     \     9   producer (input device)
  //      |    +-\----|-+
  //      8    | 10  11 |
  //  output   |        | meta 51 (e.g. AEC)
  // consumer  |   12   |
  // (device)  +----|---+
  //               13
  //                |
  //               14     input consumer (capturer)
  // ```
  FakeGraph graph({
      .meta_nodes =
          {
              {50, {.source_children = {4}, .dest_children = {5, 6}}},
              {51, {.source_children = {10, 11}, .dest_children = {12}}},
          },
      .edges =
          {
              {1, 4},
              {2, 3},
              {3, 4},
              {5, 7},
              {7, 8},
              {6, 10},
              {9, 11},
              {12, 13},
              {13, 14},
          },
      .types =
          {
              {Node::Type::kProducer, {1, 2}},
              {Node::Type::kConsumer, {4, 8, 14}},
          },
      .pipeline_directions =
          {
              {PipelineDirection::kOutput, {1, 2, 3, 4, 5, 6, 7, 8, 50}},
              {PipelineDirection::kInput, {9, 10, 11, 12, 13, 14, 51}},
          },
      .threads =
          {
              {ThreadId(1), {1, 2, 3, 4, 5, 7, 8}},
              {ThreadId(2), {6, 9, 10, 11, 12, 13, 14}},
          },
  });

  // Set external values.
  graph.node(8)->set_max_downstream_output_pipeline_delay(zx::nsec(8));
  graph.node(14)->set_max_downstream_input_pipeline_delay(zx::nsec(14));

  // Setup callbacks.
  std::unordered_set<int> updated_output;
  std::unordered_set<int> updated_input;
  for (int k = 1; k <= 14; k++) {
    auto node = graph.node(k);
    auto tid = node->thread()->id();
    node->SetOnSetMaxDownstreamOutputPipelineDelay([&updated_output, k, tid]() {
      return std::make_pair(tid, [&updated_output, k]() { updated_output.insert(k); });
    });
    node->SetOnSetMaxDownstreamInputPipelineDelay([&updated_input, k, tid]() {
      return std::make_pair(tid, [&updated_input, k]() { updated_input.insert(k); });
    });
  }

  // Initially, input pipeline delay is defined at node 8 only. Recomputing at node 13 should flood
  // that delay upwards.
  {
    SCOPED_TRACE("recompute 13 downstream_input_delay");

    std::map<ThreadId, std::vector<fit::closure>> closures;
    RecomputeMaxDownstreamInputPipelineDelay(*graph.node(13), closures);

    for (int k = 1; k <= 14; k++) {
      auto node = graph.node(k);
      SCOPED_TRACE(node->name());
      if (k == 5 || k == 7 || k == 8) {
        EXPECT_EQ(node->max_downstream_input_pipeline_delay(), zx::nsec(0));
      } else {
        EXPECT_EQ(node->max_downstream_input_pipeline_delay(), zx::nsec(14));
      }
    }

    EXPECT_TRUE(closures.count(1) > 0);
    EXPECT_TRUE(closures.count(2) > 0);

    for (auto& [tid, fns] : closures) {
      for (auto& fn : fns) {
        fn();
      }
      if (tid == 1) {
        EXPECT_THAT(updated_input, UnorderedElementsAre(1, 2, 3, 4));
      } else if (tid == 2) {
        EXPECT_THAT(updated_input, UnorderedElementsAre(6, 9, 10, 11, 12, 13));
      } else {
        ADD_FAILURE() << "unexpected ThreadID " << tid;
      }
      updated_input.clear();
    }
  }

  // Initially, output pipeline delay is defined at node 8 only. Recomputing at node 7 should flood
  // that delay upwards.
  {
    SCOPED_TRACE("recompute 7 downstream_output_delay");

    std::map<ThreadId, std::vector<fit::closure>> closures;
    RecomputeMaxDownstreamOutputPipelineDelay(*graph.node(7), closures);

    for (int k = 1; k <= 8; k++) {
      auto node = graph.node(k);
      SCOPED_TRACE(node->name());
      if (k == 6) {
        EXPECT_EQ(node->max_downstream_output_pipeline_delay(), zx::nsec(0));
      } else {
        EXPECT_EQ(node->max_downstream_output_pipeline_delay(), zx::nsec(8));
      }
    }

    EXPECT_TRUE(closures.count(1) > 0);
    EXPECT_TRUE(closures.count(2) == 0);

    for (auto& [tid, fns] : closures) {
      for (auto& fn : fns) {
        fn();
      }
      if (tid == 1) {
        EXPECT_THAT(updated_output, UnorderedElementsAre(1, 2, 3, 4, 5, 7));
      } else {
        ADD_FAILURE() << "unexpected ThreadID " << tid;
      }
      updated_output.clear();
    }
  }

  // There is no output pipeline delay at node 6.
  {
    SCOPED_TRACE("recompute 6 downstream_output_delay");

    std::map<ThreadId, std::vector<fit::closure>> closures;
    RecomputeMaxDownstreamOutputPipelineDelay(*graph.node(6), closures);
    EXPECT_EQ(graph.node(6)->max_downstream_output_pipeline_delay(), zx::nsec(0));
    EXPECT_TRUE(closures.empty());
  }

  // Update edges meta->6, 9->11, and 12->13.
  graph.node(6)->SetOnPresentationDelayForSourceEdge([](const Node* source) {
    if (source == nullptr) {
      return zx::nsec(600);
    } else {
      ADD_FAILURE() << "unexpected source " << source->name();
      return zx::nsec(0);
    }
  });
  graph.node(11)->SetOnPresentationDelayForSourceEdge([&graph](const Node* source) {
    if (source == graph.node(9).get()) {
      return zx::nsec(1190);
    } else {
      ADD_FAILURE() << "unexpected source " << source->name();
      return zx::nsec(0);
    }
  });
  graph.node(13)->SetOnPresentationDelayForSourceEdge([&graph](const Node* source) {
    if (source == graph.node(12).get()) {
      return zx::nsec(1312);
    } else {
      ADD_FAILURE() << "unexpected source " << source->name();
      return zx::nsec(0);
    }
  });

  // Recomputing output pipeline delay at 6 should be a no-op.
  {
    SCOPED_TRACE("recompute 6 downstream_output_delay (again)");

    std::map<ThreadId, std::vector<fit::closure>> closures;
    RecomputeMaxDownstreamOutputPipelineDelay(*graph.node(7), closures);
    EXPECT_EQ(graph.node(6)->max_downstream_output_pipeline_delay(), zx::nsec(0));
    EXPECT_TRUE(closures.empty());
  }

  // Recomputing input pipeline delay at node 13 is a no-op, since that node hasn't changed.
  {
    SCOPED_TRACE("recompute 13 downstream_input_delay (again)");

    std::map<ThreadId, std::vector<fit::closure>> closures;
    EXPECT_EQ(graph.node(13)->max_downstream_input_pipeline_delay(), zx::nsec(14));
    EXPECT_TRUE(closures.empty());
  }

  // Recomputing input pipeline delay at node 12 should flood upwards.
  {
    SCOPED_TRACE("recompute 12 downstream_input_delay");

    std::map<ThreadId, std::vector<fit::closure>> closures;
    RecomputeMaxDownstreamInputPipelineDelay(*graph.node(12), closures);

    for (int k = 1; k <= 14; k++) {
      auto node = graph.node(k);
      SCOPED_TRACE(node->name());
      if (k == 5 || k == 7 || k == 8) {
        EXPECT_EQ(node->max_downstream_input_pipeline_delay(), zx::nsec(0));
      } else if (k == 9) {
        EXPECT_EQ(node->max_downstream_input_pipeline_delay(), zx::nsec(1190 + 1312 + 14));
      } else if (k == 13 || k == 14) {
        EXPECT_EQ(node->max_downstream_input_pipeline_delay(), zx::nsec(14));
      } else {
        EXPECT_EQ(node->max_downstream_input_pipeline_delay(), zx::nsec(1312 + 14));
      }
    }

    EXPECT_TRUE(closures.count(1) > 0);
    EXPECT_TRUE(closures.count(2) > 0);

    for (auto& [tid, fns] : closures) {
      for (auto& fn : fns) {
        fn();
      }
      if (tid == 1) {
        EXPECT_THAT(updated_input, UnorderedElementsAre(1, 2, 3, 4));
      } else if (tid == 2) {
        EXPECT_THAT(updated_input, UnorderedElementsAre(6, 9, 10, 11, 12));
      } else {
        ADD_FAILURE() << "unexpected ThreadID " << tid;
      }
      updated_input.clear();
    }
  }
}

TEST(ReachabilityTest, RecomputeMaxUpstreamInputPipelineDelay) {
  // Node graph is structured as follows:
  //
  // ```
  //     1
  // +---|---+
  // |   2   |
  // |       | meta 50 (splitter; 3=output, 4=loopback)
  // | 3   4 |
  // +-|---|-+
  //   5   |
  //       |  6   7    producers (input devices)
  //       |  |   |
  //       |  |   8
  //       |   \ /
  //       |    |
  //     +-|----|-+
  //     | 9   10 |
  //     |        | meta 51
  //     | 11  12 |
  //     +--|---|-+
  //         \ /
  //          |
  //          13
  //          |
  //      +---|---+
  //      |   14  |
  //      |       | meta 52 (splitter)
  //      | 15 16 |
  //      +-|---|-+
  //        17 18   consumers (capturers)
  // ```
  FakeGraph graph({
      .meta_nodes =
          {
              {50, {.source_children = {2}, .dest_children = {3, 4}}},
              {51, {.source_children = {9, 10}, .dest_children = {11, 12}}},
              {52, {.source_children = {14}, .dest_children = {15, 16}}},
          },
      .edges =
          {
              {1, 2},
              {3, 5},
              {4, 9},
              {6, 10},
              {7, 8},
              {8, 10},
              {11, 13},
              {12, 13},
              {13, 14},
              {15, 17},
              {16, 18},
          },
      .types =
          {
              {Node::Type::kProducer, {1, 6, 7}},
              {Node::Type::kConsumer, {2, 5, 14, 17, 18}},
          },
      .pipeline_directions =
          {
              {PipelineDirection::kOutput, {1, 2, 3, 4, 5, 50}},
              {PipelineDirection::kInput, {6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 51, 52}},
          },
      .threads =
          {
              {ThreadId(1), {1, 2, 3, 5}},
              {ThreadId(2), {4, 6, 7, 8, 9, 10, 11, 12, 13, 14}},
              {ThreadId(3), {15, 16, 17, 18}},
          },
  });

  // Set external values.
  graph.node(6)->set_max_upstream_input_pipeline_delay(zx::nsec(6));
  graph.node(7)->set_max_upstream_input_pipeline_delay(zx::nsec(7));

  // Setup callbacks.
  std::unordered_set<int> updated;
  for (int k = 6; k <= 18; k++) {
    auto node = graph.node(k);
    auto tid = node->thread()->id();
    node->SetOnSetMaxUpstreamInputPipelineDelay([&updated, k, tid]() {
      return std::make_pair(tid, [&updated, k]() { updated.insert(k); });
    });
  }

  // Recomputing at node 10 should flood the delay from 6 downwards.
  {
    SCOPED_TRACE("recompute 10");

    std::map<ThreadId, std::vector<fit::closure>> closures;
    RecomputeMaxUpstreamInputPipelineDelay(*graph.node(10), closures);

    for (int k = 6; k <= 18; k++) {
      auto node = graph.node(k);
      SCOPED_TRACE(node->name());
      if (k == 8 | k == 9) {
        EXPECT_EQ(node->max_upstream_input_pipeline_delay(), zx::nsec(0));
      } else if (k == 7) {
        EXPECT_EQ(node->max_upstream_input_pipeline_delay(), zx::nsec(7));
      } else {
        EXPECT_EQ(node->max_upstream_input_pipeline_delay(), zx::nsec(6));
      }
    }

    EXPECT_TRUE(closures.count(2) > 0);
    EXPECT_TRUE(closures.count(3) > 0);

    for (auto& [tid, fns] : closures) {
      for (auto& fn : fns) {
        fn();
      }
      if (tid == 2) {
        EXPECT_THAT(updated, UnorderedElementsAre(10, 11, 12, 13, 14));
      } else if (tid == 3) {
        EXPECT_THAT(updated, UnorderedElementsAre(15, 16, 17, 18));
      } else {
        ADD_FAILURE() << "unexpected ThreadID " << tid;
      }
      updated.clear();
    }
  }

  // Recomputing at node 8 should flood the delay from 7 downwards, overwriting everything updated
  // by the prior call.
  {
    SCOPED_TRACE("recompute 8");

    std::map<ThreadId, std::vector<fit::closure>> closures;
    RecomputeMaxUpstreamInputPipelineDelay(*graph.node(8), closures);

    for (int k = 6; k <= 18; k++) {
      auto node = graph.node(k);
      SCOPED_TRACE(node->name());
      if (k == 9) {
        EXPECT_EQ(node->max_upstream_input_pipeline_delay(), zx::nsec(0));
      } else if (k == 6) {
        EXPECT_EQ(node->max_upstream_input_pipeline_delay(), zx::nsec(6));
      } else {
        EXPECT_EQ(node->max_upstream_input_pipeline_delay(), zx::nsec(7));
      }
    }

    EXPECT_TRUE(closures.count(2) > 0);
    EXPECT_TRUE(closures.count(3) > 0);

    for (auto& [tid, fns] : closures) {
      for (auto& fn : fns) {
        fn();
      }
      if (tid == 2) {
        EXPECT_THAT(updated, UnorderedElementsAre(8, 10, 11, 12, 13, 14));
      } else if (tid == 3) {
        EXPECT_THAT(updated, UnorderedElementsAre(15, 16, 17, 18));
      } else {
        ADD_FAILURE() << "unexpected ThreadID " << tid;
      }
      updated.clear();
    }
  }

  // Update edges 8->10, meta->{11,12}, 12->13
  graph.node(10)->SetOnPresentationDelayForSourceEdge([&graph](const Node* source) {
    if (source == graph.node(6).get()) {
      return zx::nsec(0);
    } else if (source == graph.node(8).get()) {
      return zx::nsec(1080);
    } else {
      ADD_FAILURE() << "unexpected source " << source->name();
      return zx::nsec(0);
    }
  });
  graph.node(11)->SetOnPresentationDelayForSourceEdge([](const Node* source) {
    if (source == nullptr) {
      return zx::nsec(1100);
    } else {
      ADD_FAILURE() << "unexpected source " << source->name();
      return zx::nsec(0);
    }
  });
  graph.node(12)->SetOnPresentationDelayForSourceEdge([](const Node* source) {
    if (source == nullptr) {
      return zx::nsec(1200);
    } else {
      ADD_FAILURE() << "unexpected source " << source->name();
      return zx::nsec(0);
    }
  });
  graph.node(13)->SetOnPresentationDelayForSourceEdge([&graph](const Node* source) {
    if (source == graph.node(11).get()) {
      return zx::nsec(0);
    } else if (source == graph.node(12).get()) {
      return zx::nsec(1312);
    } else {
      ADD_FAILURE() << "unexpected source " << source->name();
      return zx::nsec(0);
    }
  });

  // Recomputing at node 13 should detect the change on edge 12->13.
  {
    SCOPED_TRACE("recompute 13");

    std::map<ThreadId, std::vector<fit::closure>> closures;
    RecomputeMaxUpstreamInputPipelineDelay(*graph.node(13), closures);

    for (int k = 6; k <= 18; k++) {
      auto node = graph.node(k);
      SCOPED_TRACE(node->name());
      if (k == 9) {
        EXPECT_EQ(node->max_upstream_input_pipeline_delay(), zx::nsec(0));
      } else if (k == 6) {
        EXPECT_EQ(node->max_upstream_input_pipeline_delay(), zx::nsec(6));
      } else if (k == 7 || k == 8 || k == 10 | k == 11 || k == 12) {
        EXPECT_EQ(node->max_upstream_input_pipeline_delay(), zx::nsec(7));
      } else if (k >= 13) {
        EXPECT_EQ(node->max_upstream_input_pipeline_delay(), zx::nsec(1312 + 7));
      }
    }

    EXPECT_TRUE(closures.count(2) > 0);
    EXPECT_TRUE(closures.count(3) > 0);

    for (auto& [tid, fns] : closures) {
      for (auto& fn : fns) {
        fn();
      }
      if (tid == 2) {
        EXPECT_THAT(updated, UnorderedElementsAre(13, 14));
      } else if (tid == 3) {
        EXPECT_THAT(updated, UnorderedElementsAre(15, 16, 17, 18));
      } else {
        ADD_FAILURE() << "unexpected ThreadID " << tid;
      }
      updated.clear();
    }
  }

  // Recomputing at node 10 should detect all changes.
  {
    SCOPED_TRACE("recompute 10 (again)");

    std::map<ThreadId, std::vector<fit::closure>> closures;
    RecomputeMaxUpstreamInputPipelineDelay(*graph.node(10), closures);

    for (int k = 6; k <= 18; k++) {
      auto node = graph.node(k);
      SCOPED_TRACE(node->name());
      if (k == 9) {
        EXPECT_EQ(node->max_upstream_input_pipeline_delay(), zx::nsec(0));
      } else if (k == 6) {
        EXPECT_EQ(node->max_upstream_input_pipeline_delay(), zx::nsec(6));
      } else if (k == 7 || k == 8) {
        EXPECT_EQ(node->max_upstream_input_pipeline_delay(), zx::nsec(7));
      } else if (k == 10) {
        EXPECT_EQ(node->max_upstream_input_pipeline_delay(), zx::nsec(1080 + 7));
      } else if (k == 11) {
        EXPECT_EQ(node->max_upstream_input_pipeline_delay(), zx::nsec(1100 + 1080 + 7));
      } else if (k == 12) {
        EXPECT_EQ(node->max_upstream_input_pipeline_delay(), zx::nsec(1200 + 1080 + 7));
      } else if (k >= 13) {
        EXPECT_EQ(node->max_upstream_input_pipeline_delay(), zx::nsec(1200 + 1312 + 1080 + 7));
      }
    }

    EXPECT_TRUE(closures.count(2) > 0);
    EXPECT_TRUE(closures.count(3) > 0);

    for (auto& [tid, fns] : closures) {
      for (auto& fn : fns) {
        fn();
      }
      if (tid == 2) {
        EXPECT_THAT(updated, UnorderedElementsAre(10, 11, 12, 13, 14));
      } else if (tid == 3) {
        EXPECT_THAT(updated, UnorderedElementsAre(15, 16, 17, 18));
      } else {
        ADD_FAILURE() << "unexpected ThreadID " << tid;
      }
      updated.clear();
    }
  }

  // Recomputing at node 9 should be a no-op.
  {
    SCOPED_TRACE("recompute 9");

    std::map<ThreadId, std::vector<fit::closure>> closures;
    RecomputeMaxUpstreamInputPipelineDelay(*graph.node(9), closures);

    for (int k = 6; k <= 18; k++) {
      auto node = graph.node(k);
      SCOPED_TRACE(node->name());
      if (k == 9) {
        EXPECT_EQ(node->max_upstream_input_pipeline_delay(), zx::nsec(0));
      } else if (k == 6) {
        EXPECT_EQ(node->max_upstream_input_pipeline_delay(), zx::nsec(6));
      } else if (k == 7 || k == 8) {
        EXPECT_EQ(node->max_upstream_input_pipeline_delay(), zx::nsec(7));
      } else if (k == 10) {
        EXPECT_EQ(node->max_upstream_input_pipeline_delay(), zx::nsec(1080 + 7));
      } else if (k == 11) {
        EXPECT_EQ(node->max_upstream_input_pipeline_delay(), zx::nsec(1100 + 1080 + 7));
      } else if (k == 12) {
        EXPECT_EQ(node->max_upstream_input_pipeline_delay(), zx::nsec(1200 + 1080 + 7));
      } else if (k >= 13) {
        EXPECT_EQ(node->max_upstream_input_pipeline_delay(), zx::nsec(1200 + 1312 + 1080 + 7));
      }
    }

    EXPECT_TRUE(closures.empty());
  }
}

TEST(ReachabilityTest, ExistsPathOrdinaryNodeSelfEdge) {
  FakeGraph graph({
      .edges = {{1, 1}},
  });

  // Self edge 1<->1.
  EXPECT_TRUE(ExistsPath(*graph.node(1), *graph.node(1)));
}

TEST(ReachabilityTest, ExistsPathOrdinaryNodes) {
  // With ordinary nodes only, the graph must be an inverted tree.
  FakeGraph graph({
      .edges =
          {
              {1, 3},  //
              {2, 3},
              {3, 4},
              {5, 4},
              {4, 6},
              {6, 5},  // cycle
          },
  });

  // Paths that exist. Note cycle from 5->4.
  std::set<std::pair<NodeId, NodeId>> paths{
      {1, 3}, {1, 4}, {1, 5}, {1, 6},  //
      {2, 3}, {2, 4}, {2, 5}, {2, 6},  //
      {3, 4}, {3, 5}, {3, 6},          //
      {4, 4}, {4, 5}, {4, 6},          //
      {5, 4}, {5, 5}, {5, 6},          //
      {6, 4}, {6, 5}, {6, 6},          //
  };

  for (NodeId source = 1; source <= 6; source++) {
    for (NodeId dest = 1; dest <= 6; dest++) {
      bool expect_path = (paths.count({source, dest}) > 0);
      EXPECT_EQ(ExistsPath(*graph.node(source), *graph.node(dest)), expect_path)
          << "source=" << source << ", dest=" << dest;
    }
  }
}

TEST(ReachabilityTest, ExistsPathMetaNodeSelfEdge) {
  FakeGraph graph({
      .meta_nodes =
          {
              {
                  1,
                  {.source_children = {2}, .dest_children = {3}},
              },
          },
      .edges = {{3, 2}},
  });

  // Self edge 1<->1.
  EXPECT_TRUE(ExistsPath(*graph.node(1), *graph.node(1)));
  EXPECT_TRUE(ExistsPath(*graph.node(2), *graph.node(2)));
  EXPECT_TRUE(ExistsPath(*graph.node(3), *graph.node(3)));
}

TEST(ReachabilityTest, ExistsPathMetaNodes) {
  FakeGraph graph({
      .meta_nodes =
          {
              {
                  3,
                  {.source_children = {1, 2}, .dest_children = {4, 5}},
              },
              {
                  8,
                  {.source_children = {6, 7}, .dest_children = {9}},
              },
          },
      .edges = {{5, 7}},
  });

  // Paths that exist.
  std::set<std::pair<NodeId, NodeId>> paths{
      {1, 3}, {1, 4}, {1, 5}, {1, 7}, {1, 8}, {1, 9},  //
      {2, 3}, {2, 4}, {2, 5}, {2, 7}, {2, 8}, {2, 9},  //
      {3, 4}, {3, 5}, {3, 7}, {3, 8}, {3, 9},          //
      {5, 7}, {5, 8}, {5, 9},                          //
      {6, 8}, {6, 9},                                  //
      {7, 8}, {7, 9},                                  //
      {8, 9},                                          //
  };

  for (NodeId source = 1; source <= 9; source++) {
    for (NodeId dest = 1; dest <= 9; dest++) {
      bool expect_path = (paths.count({source, dest}) > 0);
      EXPECT_EQ(ExistsPath(*graph.node(source), *graph.node(dest)), expect_path)
          << "source=" << source << ", dest=" << dest;
    }
  }
}

TEST(ReachabilityTest, ExistsPathMetaAndOrdinaryNodes) {
  FakeGraph graph({
      .meta_nodes =
          {
              {
                  23,
                  {.source_children = {21, 22}, .dest_children = {24, 25}},
              },
              {
                  63,
                  {.source_children = {61, 62}, .dest_children = {64, 65}},
              },
          },
      .edges =
          {
              {10, 21},
              {24, 30},
              {25, 40},
              {40, 50},
              {50, 62},
              {65, 70},
          },
  });

  // Paths that exist.
  std::set<std::pair<NodeId, NodeId>> paths{
      {10, 21}, {10, 23}, {10, 24}, {10, 30}, {10, 25}, {10, 40}, {10, 50},
      {10, 62}, {10, 63}, {10, 64}, {10, 65}, {10, 70},

      {21, 23}, {21, 24}, {21, 30}, {21, 25}, {21, 40}, {21, 50}, {21, 62},
      {21, 63}, {21, 64}, {21, 65}, {21, 70},

      {22, 23}, {22, 24}, {22, 30}, {22, 25}, {22, 40}, {22, 50}, {22, 62},
      {22, 63}, {22, 64}, {22, 65}, {22, 70},

      {23, 24}, {23, 30}, {23, 25}, {23, 40}, {23, 50}, {23, 62}, {23, 63},
      {23, 64}, {23, 65}, {23, 70},

      {24, 30},

      {25, 40}, {25, 50}, {25, 62}, {25, 63}, {25, 64}, {25, 65}, {25, 70},

      {40, 50}, {40, 62}, {40, 63}, {40, 64}, {40, 65}, {40, 70},

      {50, 62}, {50, 63}, {50, 64}, {50, 65}, {50, 70},

      {61, 63}, {61, 64}, {61, 65}, {61, 70},

      {62, 63}, {62, 64}, {62, 65}, {62, 70},

      {63, 64}, {63, 65}, {63, 70},

      {65, 70},
  };

  std::vector<NodeId> nodes{10, 21, 22, 23, 24, 25, 30, 40, 50, 61, 62, 63, 64, 64, 70};
  for (auto source : nodes) {
    for (auto dest : nodes) {
      bool expect_path = (paths.count({source, dest}) > 0);
      EXPECT_EQ(ExistsPath(*graph.node(source), *graph.node(dest)), expect_path)
          << "source=" << source << ", dest=" << dest;
    }
  }
}

TEST(ReachabilityTest, MoveNodeToThread) {
  FakeGraph graph({
      // This is the example from the comments at MoveNodeToThread in reachability.h
      .meta_nodes =
          {
              {
                  3,
                  {.source_children = {2}, .dest_children = {4, 5, 6}},
              },
          },
      .edges =
          {
              {1, 2},    // A -> C
              {4, 7},    // P1 -> D
              {5, 8},    // P2 -> E
              {6, 9},    // P3 -> F
              {9, 12},   // F -> N
              {10, 11},  // H -> G
              {11, 12},  // G -> N
          },
      .types = {{Node::Type::kConsumer, {2}}},
  });

  auto old_thread = graph.ctx().detached_thread;
  auto new_thread = graph.CreateThread(1);

  EXPECT_THAT(MoveNodeToThread(*graph.node(12), new_thread, old_thread),
              UnorderedElementsAre(graph.node(6)->pipeline_stage(),     // P3
                                   graph.node(9)->pipeline_stage(),     // F
                                   graph.node(10)->pipeline_stage(),    // H
                                   graph.node(11)->pipeline_stage(),    // G
                                   graph.node(12)->pipeline_stage()));  // N

  EXPECT_EQ(graph.node(1)->thread(), old_thread);
  EXPECT_EQ(graph.node(2)->thread(), old_thread);
  EXPECT_EQ(graph.node(4)->thread(), old_thread);
  EXPECT_EQ(graph.node(5)->thread(), old_thread);
  EXPECT_EQ(graph.node(7)->thread(), old_thread);
  EXPECT_EQ(graph.node(8)->thread(), old_thread);

  EXPECT_EQ(graph.node(6)->thread(), new_thread);
  EXPECT_EQ(graph.node(9)->thread(), new_thread);
  EXPECT_EQ(graph.node(10)->thread(), new_thread);
  EXPECT_EQ(graph.node(11)->thread(), new_thread);
  EXPECT_EQ(graph.node(12)->thread(), new_thread);
}

}  // namespace media_audio
