// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/context/cpp/context_helper.h>
#include <lib/context/cpp/context_metadata_builder.h>
#include <lib/context/cpp/formatting.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fidl/cpp/optional.h>

#include "peridot/bin/context_engine/scope_utils.h"
#include "peridot/tests/maxwell_integration/context_engine_test_base.h"

namespace maxwell {
namespace {

fuchsia::modular::ComponentScope MakeGlobalScope() {
  fuchsia::modular::ComponentScope scope;
  scope.set_global_scope(fuchsia::modular::GlobalScope());
  return scope;
}

class TestListener : public fuchsia::modular::ContextListener {
 public:
  fuchsia::modular::ContextUpdatePtr last_update;

  TestListener() : binding_(this) {}

  void OnContextUpdate(fuchsia::modular::ContextUpdate update) override {
    FXL_VLOG(1) << "OnUpdate(" << update << ")";
    last_update = fidl::MakeOptional(std::move(update));
  }

  fidl::InterfaceHandle<fuchsia::modular::ContextListener> GetHandle() {
    return binding_.NewBinding();
  }

  void Reset() { last_update.reset(); }

 private:
  fidl::Binding<fuchsia::modular::ContextListener> binding_;
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

  void InitReader(fuchsia::modular::ComponentScope scope) {
    reader_.Unbind();
    context_engine()->GetReader(std::move(scope), reader_.NewRequest());
  }

  void InitWriter(fuchsia::modular::ComponentScope client_info) {
    writer_.Unbind();
    context_engine()->GetWriter(std::move(client_info), writer_.NewRequest());
  }

  fuchsia::modular::ContextReaderPtr reader_;
  fuchsia::modular::ContextWriterPtr writer_;
};

// Result ordering for |fuchsia::modular::ContextValue|s is not specified, and
// ordering ends up depending on the order the
// |fuchsia::modular::ContextValueWriter::Set| calls get handled, which is
// nondeterministic since they are on separate channels.
std::set<std::string> GetTopicSet(
    const std::vector<fuchsia::modular::ContextValue>& values) {
  std::set<std::string> topics;
  for (const auto& value : values) {
    topics.emplace(value.meta.entity->topic);
  }
  return topics;
}

}  // namespace

TEST_F(ContextEngineTest, ContextValueWriter) {
  // Use the fuchsia::modular::ContextValueWriter interface, available by
  // calling fuchsia::modular::ContextWriter.CreateValue().
  fuchsia::modular::ContextValueWriterPtr value1;
  writer_->CreateValue(value1.NewRequest(),
                       fuchsia::modular::ContextValueType::ENTITY);
  value1->Set(R"({ "@type": "someType", "foo": "bar" })",
              fidl::MakeOptional(
                  ContextMetadataBuilder().SetEntityTopic("topic").Build()));

  fuchsia::modular::ContextValueWriterPtr value2;
  writer_->CreateValue(value2.NewRequest(),
                       fuchsia::modular::ContextValueType::ENTITY);
  value2->Set(R"({ "@type": ["someType", "alsoAnotherType"], "baz": "bang" })",
              fidl::MakeOptional(
                  ContextMetadataBuilder().SetEntityTopic("frob").Build()));

  fuchsia::modular::ContextValueWriterPtr value3;
  writer_->CreateValue(value3.NewRequest(),
                       fuchsia::modular::ContextValueType::ENTITY);
  value3->Set(
      entity_resolver().AddEntity({{"someType", ""}, {"evenMoreType", ""}}),
      fidl::MakeOptional(
          ContextMetadataBuilder().SetEntityTopic("borf").Build()));

  // Subscribe to those values.
  fuchsia::modular::ContextSelector selector;
  selector.type = fuchsia::modular::ContextValueType::ENTITY;
  selector.meta = fidl::MakeOptional(
      ContextMetadataBuilder().AddEntityType("someType").Build());
  fuchsia::modular::ContextQuery query;
  modular::AddToContextQuery(&query, "a", std::move(selector));

  TestListener listener;
  std::vector<fuchsia::modular::ContextValue> results;
  reader_->Subscribe(std::move(query), listener.GetHandle());

  WaitUntilIdle();
  ASSERT_TRUE(listener.last_update);
  results =
      modular::TakeContextValue(listener.last_update.get(), "a").second.take();
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
  results =
      modular::TakeContextValue(listener.last_update.get(), "a").second.take();
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ("frob", results[0].meta.entity->topic);

  // Create two new values: A Story value and a child fuchsia::modular::Entity
  // value, where the fuchsia::modular::Entity value matches our query.
  listener.Reset();
  fuchsia::modular::ContextValueWriterPtr story_value;
  writer_->CreateValue(story_value.NewRequest(),
                       fuchsia::modular::ContextValueType::STORY);
  story_value->Set(
      nullptr,
      fidl::MakeOptional(ContextMetadataBuilder().SetStoryId("story").Build()));

  fuchsia::modular::ContextValueWriterPtr value4;
  story_value->CreateChildValue(value4.NewRequest(),
                                fuchsia::modular::ContextValueType::ENTITY);
  value4->Set("1",
              fidl::MakeOptional(
                  ContextMetadataBuilder().AddEntityType("someType").Build()));

  WaitUntilIdle();
  ASSERT_TRUE(listener.last_update);
  results =
      modular::TakeContextValue(listener.last_update.get(), "a").second.take();
  ASSERT_EQ(2u, results.size());

  fuchsia::modular::ContextValue entity_result, story_result;
  if (results[0].type == fuchsia::modular::ContextValueType::ENTITY) {
    entity_result = std::move(results[0]);
    story_result = std::move(results[1]);
  } else {
    story_result = std::move(results[0]);
    entity_result = std::move(results[1]);
  }
  EXPECT_EQ("frob", entity_result.meta.entity->topic);
  EXPECT_EQ("1", story_result.content);
  EXPECT_EQ("story", story_result.meta.story->id);

  // Lastly remove one of the values by resetting the
  // fuchsia::modular::ContextValueWriter proxy.
  listener.Reset();
  value4.Unbind();

  WaitUntilIdle();
  ASSERT_TRUE(listener.last_update);
  results =
      modular::TakeContextValue(listener.last_update.get(), "a").second.take();
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ("frob", results[0].meta.entity->topic);
}

TEST_F(ContextEngineTest, WriteNullEntity) {
  fuchsia::modular::ContextMetadata meta =
      ContextMetadataBuilder().SetEntityTopic("topic").Build();

  fuchsia::modular::ContextSelector selector;
  selector.type = fuchsia::modular::ContextValueType::ENTITY;
  selector.meta = fidl::MakeOptional(fidl::Clone(meta));
  fuchsia::modular::ContextQuery query;
  modular::AddToContextQuery(&query, "a", std::move(selector));

  fuchsia::modular::ContextValueWriterPtr value;
  writer_->CreateValue(value.NewRequest(),
                       fuchsia::modular::ContextValueType::ENTITY);

  const std::string value1 = R"({ "@type": "someType", "foo": "frob" })";
  const std::string value2 = R"({ "@type": "someType", "foo": "borf" })";

  std::vector<fuchsia::modular::ContextValue> result;
  value->Set(value1, fidl::MakeOptional(fidl::Clone(meta)));

  TestListener listener;
  reader_->Subscribe(std::move(query), listener.GetHandle());

  WaitUntilIdle();

  ASSERT_TRUE(listener.last_update);
  result =
      modular::TakeContextValue(listener.last_update.get(), "a").second.take();
  ASSERT_EQ(1u, result.size());
  EXPECT_EQ(value1, result[0].content);

  listener.Reset();

  value->Set(nullptr, nullptr);

  // Ensure that this didn't cause a crash; the fidl further specifies that
  // previous values should be unchanged.

  value->Set(value2, fidl::MakeOptional(fidl::Clone(meta)));

  WaitUntilIdle();
  ASSERT_TRUE(listener.last_update);

  result =
      modular::TakeContextValue(listener.last_update.get(), "a").second.take();
  ASSERT_EQ(1u, result.size());
  EXPECT_EQ(value2, result[0].content);
}

TEST_F(ContextEngineTest, CloseListenerAndReader) {
  // Ensure that listeners can be closed individually, and that the reader
  // itself can be closed and listeners are still valid.
  fuchsia::modular::ContextSelector selector;
  selector.type = fuchsia::modular::ContextValueType::ENTITY;
  selector.meta = fidl::MakeOptional(
      ContextMetadataBuilder().SetEntityTopic("topic").Build());
  fuchsia::modular::ContextQuery query;
  modular::AddToContextQuery(&query, "a", std::move(selector));

  TestListener listener2;
  {
    TestListener listener1;
    reader_->Subscribe(fidl::Clone(query), listener1.GetHandle());
    reader_->Subscribe(fidl::Clone(query), listener2.GetHandle());
    InitReader(MakeGlobalScope());

    WaitUntilIdle();
    EXPECT_TRUE(listener2.last_update);
    listener2.Reset();
  }

  // We don't want to crash. If the test below fails, context engine has
  // probably crashed.
  fuchsia::modular::ContextValueWriterPtr value;
  writer_->CreateValue(value.NewRequest(),
                       fuchsia::modular::ContextValueType::ENTITY);
  value->Set("foo",
             fidl::MakeOptional(
                 ContextMetadataBuilder().SetEntityTopic("topic").Build()));

  WaitUntilIdle();
  EXPECT_TRUE(listener2.last_update);
}

TEST_F(ContextEngineTest, GetContext) {
  // Ensure fuchsia::modular::ContextReader::Get returns values in the context
  // we queried for.
  fuchsia::modular::ContextValueWriterPtr value1;
  writer_->CreateValue(value1.NewRequest(),
                       fuchsia::modular::ContextValueType::ENTITY);
  value1->Set(R"({ "@type": "someType", "foo": "bar" })",
              fidl::MakeOptional(
                  ContextMetadataBuilder().SetEntityTopic("topic").Build()));

  fuchsia::modular::ContextValueWriterPtr value2;
  writer_->CreateValue(value2.NewRequest(),
                       fuchsia::modular::ContextValueType::ENTITY);
  value2->Set(R"({ "@type": ["someType", "alsoAnotherType"], "baz": "bang" })",
              fidl::MakeOptional(
                  ContextMetadataBuilder().SetEntityTopic("frob").Build()));

  fuchsia::modular::ContextValueWriterPtr value3;
  writer_->CreateValue(value3.NewRequest(),
                       fuchsia::modular::ContextValueType::ENTITY);
  value3->Set(R"({ "@type": ["otherType", "alsoAnotherType"], "qux": "quux" })",
              fidl::MakeOptional(
                  ContextMetadataBuilder().SetEntityTopic("borf").Build()));

  // Query those values.
  fuchsia::modular::ContextSelector selector;
  selector.type = fuchsia::modular::ContextValueType::ENTITY;
  selector.meta = fidl::MakeOptional(
      ContextMetadataBuilder().AddEntityType("someType").Build());
  fuchsia::modular::ContextQuery query;
  modular::AddToContextQuery(&query, "a", std::move(selector));

  // Make sure context has been written.
  TestListener listener;
  reader_->Subscribe(fidl::Clone(query), listener.GetHandle());

  WaitUntilIdle();
  EXPECT_TRUE(listener.last_update);

  // Assert Get gives us the expected context.
  bool callback_called = false;
  reader_->Get(std::move(query), [&callback_called](
                                     fuchsia::modular::ContextUpdate update) {
    callback_called = true;

    std::pair<bool, fidl::VectorPtr<fuchsia::modular::ContextValue>> results =
        modular::TakeContextValue(&update, "a");
    EXPECT_TRUE(results.first);
    ASSERT_EQ(2u, results.second->size());
    EXPECT_EQ(std::set<std::string>({"topic", "frob"}),
              GetTopicSet(*results.second));
  });

  WaitUntilIdle();
  EXPECT_TRUE(callback_called);
}

}  // namespace maxwell
