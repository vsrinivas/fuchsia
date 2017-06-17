// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/context_engine/context_repository.h"
#include "apps/maxwell/lib/context/formatting.h"
#include "apps/maxwell/services/context/context_engine.fidl.h"
#include "gtest/gtest.h"

namespace maxwell {
namespace {

class ContextRepositoryTest : public ::testing::Test {
 public:
  ContextRepositoryTest() {}

 protected:
  ContextRepository repository_;
};

class TestListener : public ContextListener {
 public:
  ContextUpdatePtr last_update;

  void OnUpdate(ContextUpdatePtr update) override {
    last_update = std::move(update);
  }

  void reset() { last_update.reset(); }
};

ContextQueryPtr CreateQuery(const std::string& topic) {
  auto query = ContextQuery::New();
  query->topics.push_back(topic);
  return query;
}

ContextQueryPtr CreateWildcardQuery() {
  auto query = ContextQuery::New();
  query->topics.resize(0);
  return query;
}

}  // namespace

TEST_F(ContextRepositoryTest, GetSetRemove) {
  // Values that don't exist shouldn't return a value.
  EXPECT_EQ(nullptr, repository_.Get("topic"));

  // Show that when we set values, we can get them back.
  repository_.Set("topic", "value");
  EXPECT_EQ("value", *repository_.Get("topic"));

  // Setting another value doesn't affect the original value.
  repository_.Set("topic2", "value2");
  EXPECT_EQ("value", *repository_.Get("topic"));
  EXPECT_EQ("value2", *repository_.Get("topic2"));

  // Removing a value means it can't be fetched any more.
  repository_.Remove("topic");
  EXPECT_EQ(nullptr, repository_.Get("topic"));
  EXPECT_EQ("value2", *repository_.Get("topic2"));
}

TEST_F(ContextRepositoryTest, Listener_Basic) {
  // Show that a subscription made before a value is published will cause the
  // subscriber's callback to be called the moment a value is published.
  TestListener listener;
  auto id = repository_.AddSubscription(CreateQuery("topic"), &listener);
  // No update because "topic" doesn't exist.
  EXPECT_FALSE(listener.last_update);

  // Add some random values to show that subscriptions only get back the topics
  // they subscribed to.
  repository_.Set("random", "123");
  repository_.Set("idea", "no");

  repository_.Set("topic", "foobar");
  ASSERT_TRUE(listener.last_update);
  EXPECT_EQ(1ul, listener.last_update->values.size());
  EXPECT_EQ("foobar", listener.last_update->values["topic"]);

  // If we set a different topic, listener should not be notified.
  listener.reset();
  repository_.Set("another_topic", "hi");
  ASSERT_FALSE(listener.last_update);

  // If we set a new value, listener should be notified.
  listener.reset();
  repository_.Set("topic", "baz");
  ASSERT_TRUE(listener.last_update);
  EXPECT_EQ("baz", listener.last_update->values["topic"]);

  // And finally if we remove the subscription, we should no longer be
  // notified.
  repository_.RemoveSubscription(id);
  listener.reset();
  repository_.Set("topic", "zag");
  ASSERT_FALSE(listener.last_update);
}

TEST_F(ContextRepositoryTest, Listener_SubscribeToExistingTopic) {
  // Show that a subscription made after the subscribed topic has been set,
  // the listener gets a notification immediately.
  repository_.Set("toppik", "hoi");
  TestListener listener;
  repository_.AddSubscription(CreateQuery("toppik"), &listener);
  ASSERT_TRUE(listener.last_update);
  EXPECT_EQ("hoi", listener.last_update->values["toppik"]);
}

TEST_F(ContextRepositoryTest, MultipleSubscribers) {
  // When multiple subscriptions are made to the same topic, all listeners
  // should be notified of new values.
  TestListener listener1;
  TestListener listener2;
  repository_.AddSubscription(CreateQuery("topic"), &listener1);
  repository_.AddSubscription(CreateQuery("topic"), &listener2);

  repository_.Set("topic", "1234");
  EXPECT_TRUE(listener1.last_update);
  EXPECT_TRUE(listener2.last_update);
}

TEST_F(ContextRepositoryTest, WildcardQuery) {
  // Show that the wildcard query returns all topics that have been set.
  repository_.Set("topic1", "1");
  repository_.Set("topic2", "2");

  TestListener listener;
  // The wildcard query is just a query without any topics.
  repository_.AddSubscription(CreateWildcardQuery(), &listener);

  ASSERT_TRUE(listener.last_update);
  EXPECT_EQ("1", listener.last_update->values["topic1"]);
  EXPECT_EQ("2", listener.last_update->values["topic2"]);
}

class TestCoprocessor : public ContextCoprocessor {
  using ProcessCall = std::function<void(const ContextRepository*,
                                         const std::set<std::string>&,
                                         std::map<std::string, std::string>*)>;

 public:
  void ProcessTopicUpdate(const ContextRepository* repository,
                          const std::set<std::string>& topics_updated,
                          std::map<std::string, std::string>* out) override {
    ASSERT_FALSE(process_calls_.empty());
    auto f = process_calls_.front();
    process_calls_.pop_front();
    f(repository, topics_updated, out);
  }

  // Adds an expected ProcessTopicUpdate() call and sets behavior. For each
  // call to ProcessTopicUpdate(), the front of |process_calls_| will be popped
  // and executed with the same parameters.
  void AddExpectedProcessCall(ProcessCall call) {
    process_calls_.push_back(call);
  }

  void CheckExpectations() {
    ASSERT_TRUE(process_calls_.empty())
        << "TestCoprocessor has expectations of "
           "ProcessTopicUpdate() that were not met.";
  }

 private:
  std::list<ProcessCall> process_calls_;
};

TEST_F(ContextRepositoryTest, Coprocessor_Basic) {
  auto coprocessor1 = new TestCoprocessor();
  auto coprocessor2 = new TestCoprocessor();
  repository_.AddCoprocessor(coprocessor1);
  repository_.AddCoprocessor(coprocessor2);

  // The first coprocessor expects that it is called and sees that "topic1" has
  // been updated. We expect that |repository_| already reflects this change.
  // It instructs |repository_| to also update "topic2".
  coprocessor1->AddExpectedProcessCall(
      [this](const ContextRepository* repository,
             const std::set<std::string>& topics,
             std::map<std::string, std::string>* out) {
        ASSERT_FALSE(out == nullptr);
        ASSERT_EQ(&repository_, repository);
        EXPECT_EQ(1ul, topics.size());
        EXPECT_EQ("topic1", *topics.begin());
        EXPECT_EQ("hello", *repository->Get("topic1"));
        (*out)["topic2"] = "foobar";
      });
  // The second coprocessor should see both "topic1" and "topic2" changes.
  coprocessor2->AddExpectedProcessCall(
      [](const ContextRepository* repository,
         const std::set<std::string>& topics,
         std::map<std::string, std::string>* out) {
        ASSERT_FALSE(out == nullptr);
        EXPECT_EQ(2ul, topics.size());
        EXPECT_EQ("topic1", *topics.begin());
        EXPECT_EQ("hello", *repository->Get("topic1"));

        EXPECT_EQ("topic2", *++topics.begin());
        EXPECT_EQ("foobar", *repository->Get("topic2"));
      });

  // Before setting "topic1", we want to show that a listener for "topic2" (set
  // by a coprocessor above) correctly gets notified of changes.
  TestListener listener;
  repository_.AddSubscription(CreateQuery("topic2"), &listener);
  repository_.Set("topic1", "hello");

  // Make sure all expectations were processed.
  coprocessor1->CheckExpectations();
  coprocessor2->CheckExpectations();

  // And lastly ensure that our listener was called.
  ASSERT_TRUE(listener.last_update);
  EXPECT_EQ("foobar", listener.last_update->values["topic2"]);
}

}  // namespace maxwell

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
