// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/lib/context/formatting.h"
#include "apps/maxwell/services/context/context_engine.fidl.h"
#include "apps/maxwell/src/integration/context_engine_test_base.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace maxwell {
namespace {

ComponentScopePtr CreateGlobalScope() {
  auto scope = ComponentScope::New();
  scope->set_global_scope(GlobalScope::New());
  return scope;
}

class TestListener : public ContextListener {
 public:
  TestListener() : binding_(this) {}

  void OnUpdate(ContextUpdatePtr update) override {
    FTL_LOG(INFO) << "OnUpdate(" << update << ")";
    last_update_ = std::move(update);
  }

  void WaitForUpdate() {
    WaitUntil([this] { return last_update_ ? true : false; });
  }

  ContextUpdate* PeekLast() { return last_update_.get(); }
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
    context_engine()->GetProvider(CreateGlobalScope(), provider_.NewRequest());
    context_engine()->GetPublisher(CreateGlobalScope(),
                                   publisher_.NewRequest());
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

TEST_F(ContextEngineTest, PublishAndSubscribe) {
  // Show that we can publish to a topic and that we can subscribe to that
  // topic.
  publisher_->Publish("topic", "foobar");
  publisher_->Publish("a_different_topic", "baz");

  TestListener listener;
  provider_->Subscribe(CreateQuery("topic"), listener.GetHandle());
  listener.WaitForUpdate();

  ContextUpdatePtr update;
  ASSERT_TRUE((update = listener.PopLast()));
  EXPECT_EQ(1ul, update->values.size());
  EXPECT_EQ(update->values["topic"], "foobar");
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

TEST_F(ContextEngineTest, CloseListener) {
  // Ensure that listeners can be closed individually.
  TestListener listener2;
  {
    TestListener listener1;
    provider_->Subscribe(CreateQuery("topic"), listener1.GetHandle());
    provider_->Subscribe(CreateQuery("topic"), listener2.GetHandle());
  }

  publisher_->Publish("topic", "don't crash");
  WAIT_UNTIL(listener2.PopLast());
}

TEST_F(ContextEngineTest, CloseProvider) {
  // After a provider is closed, its listeners should no longer recieve updates.
  TestListener listener1;
  provider_->Subscribe(CreateQuery("topic"), listener1.GetHandle());

  // Close the provider and open a new one to ensure we're still running.
  provider_.reset();
  context_engine()->GetProvider(CreateGlobalScope(), provider_.NewRequest());

  publisher_->Publish("topic", "please don't crash");
  TestListener listener2;
  provider_->Subscribe(CreateQuery("topic"), listener2.GetHandle());

  WAIT_UNTIL(listener2.PopLast());
  ASYNC_CHECK(!listener1.PeekLast());
}

}  // namespace maxwell
