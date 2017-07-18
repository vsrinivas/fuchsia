// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/lib/context/formatting.h"
#include "apps/maxwell/services/context/context_engine.fidl.h"
#include "apps/maxwell/src/context_engine/scope_utils.h"
#include "apps/maxwell/src/integration/context_engine_test_base.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace maxwell {
namespace {

ComponentScopePtr MakeGlobalScope() {
  auto scope = ComponentScope::New();
  scope->set_global_scope(GlobalScope::New());
  return scope;
}

ComponentScopePtr MakeModuleScope(const std::string& module_url,
                                  const std::string& story_id) {
  auto scope = ComponentScope::New();
  auto module_scope = ModuleScope::New();
  module_scope->url = module_url;
  module_scope->story_id = story_id;
  module_scope->module_path = fidl::Array<fidl::String>::New(1);
  module_scope->module_path[0] = module_url;
  scope->set_module_scope(std::move(module_scope));
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

  void Reset() {
    last_update_.reset();
  }

 private:
  ContextUpdatePtr last_update_;
  fidl::Binding<ContextListener> binding_;
};

class ContextEngineTest : public ContextEngineTestBase {
 public:
  ContextEngineTest() : ContextEngineTestBase() { InitAllGlobalScope(); }

 protected:
  void InitAllGlobalScope() {
    InitProvider(MakeGlobalScope());
    InitPublisher(MakeGlobalScope());
  }

  void InitProvider(ComponentScopePtr scope) {
    provider_.reset();
    context_engine()->GetProvider(std::move(scope), provider_.NewRequest());
  }

  void InitPublisher(ComponentScopePtr scope) {
    publisher_.reset();
    context_engine()->GetPublisher(std::move(scope), publisher_.NewRequest());
  }

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
  // topic. Querying behavior and other Listener dynamics are tested elsewhere.
  InitAllGlobalScope();
  publisher_->Publish("topic", "1");
  publisher_->Publish("a_different_topic", "2");

  TestListener listener;
  provider_->Subscribe(CreateQuery("topic"), listener.GetHandle());
  listener.WaitForUpdate();

  ContextUpdatePtr update;
  ASSERT_TRUE((update = listener.PopLast()));
  EXPECT_EQ(1ul, update->values.size());
  EXPECT_EQ(update->values["topic"], "1");

  // Show that if we try to publish invalid JSON, it doesn't go through.
  publisher_->Publish("topic", "not valid JSON");
  ASYNC_CHECK(!listener.PeekLast());
}

TEST_F(ContextEngineTest, MultipleSubscribers) {
  // When multiple subscriptions are made to the same topic, all listeners
  // should be notified of new values.
  TestListener listener1;
  TestListener listener2;
  provider_->Subscribe(CreateQuery("topic"), listener1.GetHandle());
  provider_->Subscribe(CreateQuery("topic"), listener2.GetHandle());

  publisher_->Publish("topic", "1");
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

  publisher_->Publish("topic", "\"don't crash\"");
  WAIT_UNTIL(listener2.PopLast());
}

TEST_F(ContextEngineTest, CloseProvider) {
  // After a provider is closed, its listeners should no longer recieve updates.
  TestListener listener1;
  provider_->Subscribe(CreateQuery("topic"), listener1.GetHandle());

  // Close the provider and open a new one to ensure we're still running.
  InitProvider(MakeGlobalScope());

  publisher_->Publish("topic", "\"please don't crash\"");
  TestListener listener2;
  provider_->Subscribe(CreateQuery("topic"), listener2.GetHandle());

  WAIT_UNTIL(listener2.PopLast());
  // Since the ContextProvider owns subscriptions, and we closed it
  // (through InitProvider), we should not have seen our listener
  // notified of the published topic.
  //
  // Unfortunately, it's a race between when the subscription is
  // actually removed on the service side, so we can't check it here.
  // EXPECT_FALSE(listener1.PeekLast());

  // However, by now (after the WAIT_UNTIL) we always seem to have
  // caught up, so try changing the value here.
  listener1.Reset();
  publisher_->Publish("topic", "\"still don't crash\"");
  WAIT_UNTIL(listener2.PopLast());
  EXPECT_FALSE(listener1.PeekLast());
}

TEST_F(ContextEngineTest, ModuleScope_BasicReadWrite) {
  // Show that when the ContextPublisher is created with Module scope,
  // that the topic is prefixed with a Module-scope prefix.
  InitPublisher(MakeModuleScope("url", "story_id"));
  InitProvider(MakeGlobalScope());

  publisher_->Publish("/topic", "1");

  TestListener listener;
  // This is the 5-char prefix of the sha1 of url.
  const char kSha1OfUrl[] = "81736";
  const std::string kTopicString =
      MakeModuleScopeTopic("story_id", kSha1OfUrl, "explicit/topic");
  provider_->Subscribe(CreateQuery(kTopicString), listener.GetHandle());
  listener.WaitForUpdate();

  ContextUpdatePtr update;
  ASSERT_TRUE((update = listener.PopLast()));
  EXPECT_EQ("1", update->values[kTopicString]);
}

}  // namespace maxwell
