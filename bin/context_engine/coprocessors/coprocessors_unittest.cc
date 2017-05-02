// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <set>
#include <string>

#include "apps/maxwell/src/context_engine/coprocessors/aggregate.h"
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
  EXPECT_EQ("[{\"k\":5},6,2,1]",
            output()[MakeStoryScopeTopic("story1", kTopic)]);

  // Similarly, if we indicate that the same topic for story2 was updated, we
  // should see an aggregation for story2.
  Run({other_story});
  ASSERT_EQ(1lu, output().size());
  EXPECT_EQ("[\"other story\"]",
            output()[MakeStoryScopeTopic("story2", kTopic)]);
}

}  // namespace maxwell

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
