// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/context/cpp/context_helper.h"
#include "lib/context/cpp/context_metadata_builder.h"
#include "lib/context/cpp/formatting.h"
#include "lib/context/fidl/context_engine.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fsl/tasks/message_loop.h"
#include "peridot/bin/context_engine/scope_utils.h"
#include "peridot/tests/maxwell_integration/context_engine_test_base.h"

namespace maxwell {
namespace {

ComponentScopePtr MakeGlobalScope() {
  auto scope = ComponentScope::New();
  scope->set_global_scope(GlobalScope::New());
  return scope;
}

class TestListener : public ContextListener {
 public:
  ContextUpdatePtr last_update;

  TestListener() : binding_(this) {}

  void OnContextUpdate(ContextUpdatePtr update) override {
    FXL_VLOG(1) << "OnUpdate(" << update << ")";
    last_update = std::move(update);
  }

  f1dl::InterfaceHandle<ContextListener> GetHandle() {
    return binding_.NewBinding();
  }

  void Reset() { last_update.reset(); }

 private:
  f1dl::Binding<ContextListener> binding_;
};

class ContextEngineTest : public ContextEngineTestBase {
 public:
  void SetUp() override {
    ContextEngineTestBase::SetUp();
    InitAllGlobalScope();
  }

 protected:
  void InitAllGlobalScope() {
    InitReader(MakeGlobalScope());
    InitWriter(MakeGlobalScope());
  }

  void InitReader(ComponentScopePtr scope) {
    reader_.Unbind();
    context_engine()->GetReader(std::move(scope), reader_.NewRequest());
  }

  void InitWriter(ComponentScopePtr client_info) {
    writer_.Unbind();
    context_engine()->GetWriter(std::move(client_info), writer_.NewRequest());
  }

  ContextReaderPtr reader_;
  ContextWriterPtr writer_;
};

// Result ordering for |ContextValue|s is not specified, and ordering ends up
// depending on the order the |ContextValueWriter::Set| calls get handled,
// which is nondeterministic since they are on separate channels.
std::set<std::string> GetTopicSet(const std::vector<ContextValuePtr>& values) {
  std::set<std::string> topics;
  for (const auto& value : values) {
    topics.emplace(value->meta->entity->topic);
  }
  return topics;
}

}  // namespace

TEST_F(ContextEngineTest, ContextValueWriter) {
  // Use the ContextValueWriter interface, available by calling
  // ContextWriter.CreateValue().
  ContextValueWriterPtr value1;
  writer_->CreateValue(value1.NewRequest(), ContextValueType::ENTITY);
  value1->Set(R"({ "@type": "someType", "foo": "bar" })",
              ContextMetadataBuilder().SetEntityTopic("topic").Build());

  ContextValueWriterPtr value2;
  writer_->CreateValue(value2.NewRequest(), ContextValueType::ENTITY);
  value2->Set(R"({ "@type": ["someType", "alsoAnotherType"], "baz": "bang" })",
              ContextMetadataBuilder().SetEntityTopic("frob").Build());

  ContextValueWriterPtr value3;
  writer_->CreateValue(value3.NewRequest(), ContextValueType::ENTITY);
  value3->Set(
      entity_resolver().AddEntity({{"someType", ""}, {"evenMoreType", ""}}),
      ContextMetadataBuilder().SetEntityTopic("borf").Build());

  // Subscribe to those values.
  auto selector = ContextSelector::New();
  selector->type = ContextValueType::ENTITY;
  selector->meta = ContextMetadataBuilder().AddEntityType("someType").Build();
  auto query = ContextQuery::New();
  AddToContextQuery(query.get(), "a", std::move(selector));

  TestListener listener;
  std::vector<ContextValuePtr> results;
  reader_->Subscribe(std::move(query), listener.GetHandle());

  WaitUntilIdle();
  ASSERT_TRUE(listener.last_update);
  results = TakeContextValue(listener.last_update.get(), "a").second.take();
  ASSERT_EQ(3u, results.size());
  EXPECT_EQ(std::set<std::string>({"topic", "frob", "borf"}),
            GetTopicSet(results));

  // Update value1 and value3 so they're no longer matches for the 'someType'
  // query.
  listener.Reset();
  value1->Set(R"({ "@type": "notSomeType", "foo": "bar" })", nullptr);
  value3.Unbind();

  WaitUntilIdle();
  ASSERT_TRUE(listener.last_update);
  results = TakeContextValue(listener.last_update.get(), "a").second.take();
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ("frob", results[0]->meta->entity->topic);

  // Create two new values: A Story value and a child Entity value, where the
  // Entity value matches our query.
  listener.Reset();
  ContextValueWriterPtr story_value;
  writer_->CreateValue(story_value.NewRequest(), ContextValueType::STORY);
  story_value->Set(nullptr,
                   ContextMetadataBuilder().SetStoryId("story").Build());

  ContextValueWriterPtr value4;
  story_value->CreateChildValue(value4.NewRequest(), ContextValueType::ENTITY);
  value4->Set("1", ContextMetadataBuilder().AddEntityType("someType").Build());

  WaitUntilIdle();
  ASSERT_TRUE(listener.last_update);
  results = TakeContextValue(listener.last_update.get(), "a").second.take();
  ASSERT_EQ(2u, results.size());

  maxwell::ContextValuePtr entity_result, story_result;
  if (results[0]->type == maxwell::ContextValueType::ENTITY) {
    entity_result = std::move(results[0]);
    story_result = std::move(results[1]);
  } else {
    story_result = std::move(results[0]);
    entity_result = std::move(results[1]);
  }
  EXPECT_EQ("frob", entity_result->meta->entity->topic);
  EXPECT_EQ("1", story_result->content);
  EXPECT_EQ("story", story_result->meta->story->id);

  // Lastly remove one of the values by resetting the ContextValueWriter proxy.
  listener.Reset();
  value4.Unbind();

  WaitUntilIdle();
  ASSERT_TRUE(listener.last_update);
  results = TakeContextValue(listener.last_update.get(), "a").second.take();
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ("frob", results[0]->meta->entity->topic);
}

TEST_F(ContextEngineTest, WriteNullEntity) {
  ContextMetadataPtr meta =
      ContextMetadataBuilder().SetEntityTopic("topic").Build();

  auto selector = ContextSelector::New();
  selector->type = ContextValueType::ENTITY;
  selector->meta = meta->Clone();
  auto query = ContextQuery::New();
  AddToContextQuery(query.get(), "a", std::move(selector));

  ContextValueWriterPtr value;
  writer_->CreateValue(value.NewRequest(), ContextValueType::ENTITY);

  const std::string value1 = R"({ "@type": "someType", "foo": "frob" })";
  const std::string value2 = R"({ "@type": "someType", "foo": "borf" })";

  std::vector<ContextValuePtr> result;
  value->Set(value1, meta->Clone());

  TestListener listener;
  reader_->Subscribe(std::move(query), listener.GetHandle());

  WaitUntilIdle();

  ASSERT_TRUE(listener.last_update);
  result = TakeContextValue(listener.last_update.get(), "a").second.take();
  ASSERT_EQ(1u, result.size());
  EXPECT_EQ(value1, result[0]->content);

  listener.Reset();

  value->Set(nullptr, nullptr);

  // Ensure that this didn't cause a crash; the fidl further specifies that
  // previous values should be unchanged.

  value->Set(value2, meta->Clone());

  WaitUntilIdle();
  ASSERT_TRUE(listener.last_update);

  result = TakeContextValue(listener.last_update.get(), "a").second.take();
  ASSERT_EQ(1u, result.size());
  EXPECT_EQ(value2, result[0]->content);
}

TEST_F(ContextEngineTest, CloseListenerAndReader) {
  // Ensure that listeners can be closed individually, and that the reader
  // itself can be closed and listeners are still valid.
  auto selector = ContextSelector::New();
  selector->type = ContextValueType::ENTITY;
  selector->meta = ContextMetadataBuilder().SetEntityTopic("topic").Build();
  auto query = ContextQuery::New();
  AddToContextQuery(query.get(), "a", std::move(selector));

  TestListener listener2;
  {
    TestListener listener1;
    reader_->Subscribe(query.Clone(), listener1.GetHandle());
    reader_->Subscribe(query.Clone(), listener2.GetHandle());
    InitReader(MakeGlobalScope());

    WaitUntilIdle();
    EXPECT_TRUE(listener2.last_update);
    listener2.Reset();
  }

  // We don't want to crash. If the test below fails, context engine has
  // probably crashed.
  ContextValueWriterPtr value;
  writer_->CreateValue(value.NewRequest(), ContextValueType::ENTITY);
  value->Set("foo", ContextMetadataBuilder().SetEntityTopic("topic").Build());

  WaitUntilIdle();
  EXPECT_TRUE(listener2.last_update);
}

TEST_F(ContextEngineTest, GetContext) {
  // Ensure ContextReader::Get returns values in the context we queried for.
  ContextValueWriterPtr value1;
  writer_->CreateValue(value1.NewRequest(), ContextValueType::ENTITY);
  value1->Set(R"({ "@type": "someType", "foo": "bar" })",
              ContextMetadataBuilder().SetEntityTopic("topic").Build());

  ContextValueWriterPtr value2;
  writer_->CreateValue(value2.NewRequest(), ContextValueType::ENTITY);
  value2->Set(R"({ "@type": ["someType", "alsoAnotherType"], "baz": "bang" })",
              ContextMetadataBuilder().SetEntityTopic("frob").Build());

  ContextValueWriterPtr value3;
  writer_->CreateValue(value3.NewRequest(), ContextValueType::ENTITY);
  value3->Set(R"({ "@type": ["otherType", "alsoAnotherType"], "qux": "quux" })",
              ContextMetadataBuilder().SetEntityTopic("borf").Build());

  // Query those values.
  auto selector = ContextSelector::New();
  selector->type = ContextValueType::ENTITY;
  selector->meta = ContextMetadataBuilder().AddEntityType("someType").Build();
  auto query = ContextQuery::New();
  AddToContextQuery(query.get(), "a", std::move(selector));

  // Make sure context has been written.
  TestListener listener;
  reader_->Subscribe(query.Clone(), listener.GetHandle());

  WaitUntilIdle();
  EXPECT_TRUE(listener.last_update);

  // Assert Get gives us the expected context.
  bool callback_called = false;
  reader_->Get(std::move(query),
               [&callback_called](const ContextUpdatePtr& update) {
                 callback_called = true;

                 std::pair<bool, f1dl::VectorPtr<ContextValuePtr>> results =
                     TakeContextValue(update.get(), "a");
                 EXPECT_TRUE(results.first);
                 ASSERT_EQ(2u, results.second->size());
                 EXPECT_EQ(std::set<std::string>({"topic", "frob"}),
                           GetTopicSet(*results.second));
               });

  WaitUntilIdle();
  EXPECT_TRUE(callback_called);
}

}  // namespace maxwell
