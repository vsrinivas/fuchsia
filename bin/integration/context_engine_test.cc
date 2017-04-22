// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/lib/context/formatting.h"
#include "apps/maxwell/services/context/context_engine.fidl.h"
#include "apps/maxwell/src/integration/context_engine_test_base.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace maxwell {
namespace {

class TestListener : public ContextListener {
 public:
  TestListener() : binding_(this) {}

  void OnUpdate(ContextUpdatePtr update) override {
    FTL_LOG(INFO) << "OnUpdate(" << update << ")";
    last_update_ = std::move(update);
  }

  // void WaitForUpdate() { binding_.WaitForIncomingMethodCall(kSignalDeadline);
  // }
  void WaitForUpdate() {
    WaitUntil([this] { return last_update_ ? true : false; });
  }

  ContextUpdatePtr PopLast() { return std::move(last_update_); }

  // Binds a new handle to |binding_| and returns it.
  fidl::InterfaceHandle<ContextListener> GetHandle() {
    return binding_.NewBinding();
  }

 private:
  ContextUpdatePtr last_update_;
  fidl::Binding<ContextListener> binding_;
};

class ContextEngineTest : public ContextEngineTestBase {
 public:
  ContextEngineTest() : ContextEngineTestBase() {
    ComponentScopePtr scope = ComponentScope::New();
    scope->set_global_scope(GlobalScope::New());
    context_engine()->GetProvider(scope->Clone(), provider_.NewRequest());
    context_engine()->GetPublisher(std::move(scope), publisher_.NewRequest());
  }

 protected:
  ContextProviderPtr provider_;
  ContextPublisherPtr publisher_;
};

ContextQueryPtr CreateQuery(const std::string& topic) {
  auto query = ContextQuery::New();
  query->topics.push_back(topic);
  return query;
}

}  // namespace

TEST_F(ContextEngineTest, CorrectValues) {
  // Show that when we're notified of an update, we get the value that was
  // published back.
  //
  // Also show that when a subscription is created for an existing topic, the
  // value is immediately sent to the subscription listener.
  publisher_->Publish("topic", "foobar");
  publisher_->Publish("a_different_topic", "baz");

  TestListener listener;
  provider_->Subscribe(CreateQuery("topic"), listener.GetHandle());
  listener.WaitForUpdate();

  ContextUpdatePtr update;
  ASSERT_TRUE((update = listener.PopLast()));
  // Make sure we only got the only topic we subscribed to.
  EXPECT_EQ(1ul, update->values.size());
  // And that it has the expected value.
  EXPECT_EQ(update->values["topic"], "foobar");
}

TEST_F(ContextEngineTest, PublishAfterSubscribe) {
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

TEST_F(ContextEngineTest, MultipleSubscribers) {
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

}  // namespace maxwell
