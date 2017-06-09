// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <set>
#include <string>

#include "apps/maxwell/src/context_engine/coprocessors/aggregate.h"
#include "apps/maxwell/src/context_engine/coprocessors/focused_story.h"
#include "apps/maxwell/src/context_engine/scope_utils.h"
#include "gtest/gtest.h"

namespace maxwell {

class CoprocessorsTest : public ::testing::Test {
 public:
  CoprocessorsTest() = default;

  std::string Set(const std::string& topic, const std::string& value) {
    repository_->Set(topic, value);
    return topic;
  }

  void Run(std::set<std::string> updated_topics) {
    output_.clear();
    coprocessor_->ProcessTopicUpdate(repository_.get(), updated_topics,
                                     &output_);
  }

  void Reset(ContextCoprocessor* coprocessor) {
    coprocessor_.reset(coprocessor);
    repository_.reset(new ContextRepository());
  }

  std::map<std::string, std::string> output() { return output_; }

 protected:
  std::unique_ptr<ContextCoprocessor> coprocessor_;
  std::unique_ptr<ContextRepository> repository_;

  std::map<std::string, std::string> output_;
};

///////////////////////////////////////////
using AggregateTest = CoprocessorsTest;

TEST_F(AggregateTest, All) {
  const char kTopic[] = "the_topic";
  Reset(new AggregateCoprocessor(kTopic));

  auto wrong_topic =
      Set(MakeModuleScopeTopic("story1", "module1", "not_the_topic"), "foo");
  auto right_topic =
      Set(MakeModuleScopeTopic("story1", "module1", kTopic), "[1]");
  Set(MakeModuleScopeTopic("story1", "module2", kTopic), "2");
  Set(MakeModuleScopeTopic("story1", "module3", kTopic), "[{\"k\":5},6]");
  auto other_story =
      Set(MakeModuleScopeTopic("story2", "module1", kTopic), "\"other story\"");

  // If the wrong topic was updated, we shouldn't see the any changes.
  Run({wrong_topic});
  ASSERT_EQ(0lu, output().size());

  // If a kTopic was updated, we should see the aggregated version. We should
  // not see anything from story2, since that Story wasn't updated.
  Run({right_topic});
  ASSERT_EQ(1lu, output().size());
  EXPECT_EQ("[1,2,{\"k\":5},6]",
            output()[MakeStoryScopeTopic("story1", kTopic)]);

  // Similarly, if we indicate that the same topic for story2 was updated, we
  // should see an aggregation for story2.
  Run({other_story});
  ASSERT_EQ(1lu, output().size());
  EXPECT_EQ("[\"other story\"]",
            output()[MakeStoryScopeTopic("story2", kTopic)]);
}

///////////////////////////////////////////
using FocusedStoryTest = CoprocessorsTest;

TEST_F(FocusedStoryTest, All) {
  Reset(new FocusedStoryCoprocessor());

  // Set some values to start with for two different stories.
  const auto topic1_1 = Set(MakeStoryScopeTopic("1", "topic1"), "11");
  const auto topic1_2 = Set(MakeStoryScopeTopic("1", "topic2"), "12");

  const auto topic2_1 = Set(MakeStoryScopeTopic("2", "topic1"), "21");
  const auto topic2_2 = Set(MakeStoryScopeTopic("2", "topic2"), "22");

  const auto focused_1 = MakeFocusedStoryScopeTopic("topic1");
  const auto focused_2 = MakeFocusedStoryScopeTopic("topic2");
  const auto focused_3 = MakeFocusedStoryScopeTopic("topic3");

  // There should be nothing in /story/focused/topic* so far.
  Run({topic1_1});
  ASSERT_EQ(0lu, output().size());

  // Set the focused story to an ID that has no values. We should still see
  // nothing.
  const auto focused_id = Set("/story/focused_id", "\"no_exist\"");
  Run({focused_id});
  ASSERT_EQ(0lu, output().size());

  // Now set the value to "1".
  Set(focused_id, "\"1\"");
  Run({focused_id});
  ASSERT_EQ(2lu, output().size());
  EXPECT_EQ("11", output()[focused_1]);
  EXPECT_EQ("12", output()[focused_2]);

  // The focused story is now "1". Set some values in it.
  Set(topic1_1, "111");
  const auto topic1_3 = Set(MakeStoryScopeTopic("1", "topic3"), "43");
  // Tell it topics from story 3 changed also.
  Run({topic1_1, topic1_3, topic2_1});
  ASSERT_EQ(2lu, output().size());
  EXPECT_EQ("111", output()[focused_1]);
  EXPECT_EQ("43", output()[focused_3]);

  // Finally set focused_id to null, and we should see all values in the
  // focused story scope set to null.
  Set(focused_id, "null");
  Set(focused_1, "foo");
  Set(focused_2, "bar");
  Run({focused_id});
  ASSERT_EQ(2lu, output().size());
  EXPECT_EQ("null", output()[focused_1]);
  EXPECT_EQ("null", output()[focused_2]);
}

}  // namespace maxwell

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
