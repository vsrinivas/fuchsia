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

}  // namespace

TEST_F(ContextRepositoryTest, GetSetRemove) {
  // Values that don't exist shouldn't return a value.
  EXPECT_EQ(repository_.Get("topic"), nullptr);

  // Show that when we set values, we can get them back.
  repository_.Set("topic", "value");
  EXPECT_EQ(*repository_.Get("topic"), "value");

  // Setting another value doesn't affect the original value.
  repository_.Set("topic2", "value2");
  EXPECT_EQ(*repository_.Get("topic"), "value");
  EXPECT_EQ(*repository_.Get("topic2"), "value2");

  // Removing a value means it can't be fetched any more.
  repository_.Remove("topic");
  EXPECT_EQ(repository_.Get("topic"), nullptr);
  EXPECT_EQ(*repository_.Get("topic2"), "value2");
}

/*
TEST_F(ContextRepositoryTest, PublishAfterSubscribe) {
  // Show that a subscription made before a value is published will cause the
  // subscriber's callback to be called the moment a value is published.
  TestListener listener;
  provider_->Subscribe(CreateQuery("topic"), listener.GetHandle());
  Sleep();

  publisher_->Publish("topic", "foobar");
  listener.WaitForUpdate();

  ContextUpdatePtr update;
  EXPECT_TRUE((update = listener.PopLast()));
}

TEST_F(ContextRepositoryTest, MultipleSubscribers) {
  // When multiple subscriptions are made to the same topic, all listeners
  // should be notified of new values.
  TestListener listener1;
  TestListener listener2;
  provider_->Subscribe(CreateQuery("topic"), listener1.GetHandle());
  provider_->Subscribe(CreateQuery("topic"), listener2.GetHandle());

  publisher_->Publish("topic", "flkjsd");
  WAIT_UNTIL(listener1.PopLast());
  WAIT_UNTIL(listener2.PopLast());
}

TEST_F(ContextRepositoryTest, WildcardQuery) {
  // Show that the wildcard query returns all topics that have been published.
  publisher_->Publish("topic1", "1");
  publisher_->Publish("topic2", "2");
  Sleep();  // Give the runloop a chance to dispatch the above messages.

  TestListener listener;
  // The wildcard query is just a query without any topics.
  provider_->Subscribe(CreateWildcardQuery(), listener.GetHandle());
  listener.WaitForUpdate();

  ContextUpdatePtr update;
  ASSERT_TRUE((update = listener.PopLast()));
  EXPECT_EQ("1", update->values["topic1"]);
  EXPECT_EQ("2", update->values["topic2"]);
}
*/

}  // namespace maxwell

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
