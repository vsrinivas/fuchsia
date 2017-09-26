// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/context/cpp/context_metadata_builder.h"
#include "lib/context/cpp/formatting.h"
#include "lib/context/fidl/context_engine.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "peridot/bin/context_engine/scope_utils.h"
#include "peridot/bin/integration/context_engine_test_base.h"

namespace maxwell {
namespace {

ComponentScopePtr MakeGlobalScope() {
  auto scope = ComponentScope::New();
  scope->set_global_scope(GlobalScope::New());
  return scope;
}

/*
ComponentScopePtr MakeModuleScope(const std::string& path,
                                  const std::string& story_id) {
  auto scope = ComponentScope::New();
  auto module_scope = ModuleScope::New();
  module_scope->url = path;
  module_scope->story_id = story_id;
  module_scope->module_path = fidl::Array<fidl::String>::New(1);
  module_scope->module_path[0] = path;
  scope->set_module_scope(std::move(module_scope));
  return scope;
}
*/

class TestListener : public ContextListener {
 public:
  ContextUpdatePtr last_update;

  TestListener() : binding_(this) {}

  void OnContextUpdate(ContextUpdatePtr update) override {
    FXL_LOG(INFO) << "OnUpdate(" << update << ")";
    last_update = std::move(update);
  }

  fidl::InterfaceHandle<ContextListener> GetHandle() {
    return binding_.NewBinding();
  }

  void Reset() { last_update.reset(); }

 private:
  fidl::Binding<ContextListener> binding_;
};

class ContextEngineTest : public ContextEngineTestBase {
 public:
  ContextEngineTest() : ContextEngineTestBase() { InitAllGlobalScope(); }

 protected:
  void InitAllGlobalScope() {
    InitReader(MakeGlobalScope());
    InitWriter(MakeGlobalScope());
  }

  void InitReader(ComponentScopePtr scope) {
    reader_.reset();
    context_engine()->GetReader(std::move(scope), reader_.NewRequest());
  }

  void InitWriter(ComponentScopePtr client_info) {
    writer_.reset();
    context_engine()->GetWriter(std::move(client_info), writer_.NewRequest());
  }

  ContextReaderPtr reader_;
  ContextWriterPtr writer_;
};

}  // namespace

// Tests to add:
// * Write with parent.
// * Update.
// * Remove.

TEST_F(ContextEngineTest, BasicWriteSubscribe) {
  auto value = ContextValue::New();
  value->type = ContextValueType::ENTITY;
  value->content = R"({ "@type": "someType", "foo": "bar" })";
  value->meta = ContextMetadataBuilder().SetEntityTopic("topic").Build();

  fidl::String value1_id;
  writer_->AddValue(std::move(value),
                    [&value1_id](const fidl::String& id) { value1_id = id; });
  WAIT_UNTIL(value1_id);

  value = ContextValue::New();
  value->type = ContextValueType::ENTITY;
  value->content =
      R"({ "@type": ["someType", "alsoAnotherType"], "baz": "bang" })";
  value->meta = ContextMetadataBuilder().SetEntityTopic("frob").Build();

  fidl::String value2_id;
  writer_->AddValue(std::move(value),
                    [&value2_id](const fidl::String& id) { value2_id = id; });
  WAIT_UNTIL(value2_id);

  // Subscribe to those values.
  auto selector = ContextSelector::New();
  selector->type = ContextValueType::ENTITY;
  selector->meta = ContextMetadataBuilder().AddEntityType("someType").Build();
  auto query = ContextQuery::New();
  query->selector["a"] = std::move(selector);

  TestListener listener;
  reader_->Subscribe(std::move(query), listener.GetHandle());
  WAIT_UNTIL(listener.last_update);

  EXPECT_EQ(2lu, listener.last_update->values["a"].size());
  EXPECT_EQ("topic", listener.last_update->values["a"][0]->meta->entity->topic);
  EXPECT_EQ("frob", listener.last_update->values["a"][1]->meta->entity->topic);
}

TEST_F(ContextEngineTest, CloseListenerAndReader) {
  // Ensure that listeners can be closed individually, and that the reader
  // itself can be closed and listeners are still valid.
  auto selector = ContextSelector::New();
  selector->type = ContextValueType::ENTITY;
  selector->meta = ContextMetadataBuilder().SetEntityTopic("topic").Build();
  auto query = ContextQuery::New();
  query->selector["a"] = std::move(selector);

  TestListener listener2;
  {
    TestListener listener1;
    reader_->Subscribe(query.Clone(), listener1.GetHandle());
    reader_->Subscribe(query.Clone(), listener2.GetHandle());
    InitReader(MakeGlobalScope());
    WAIT_UNTIL(listener2.last_update);
    listener2.Reset();
  }

  auto value = ContextValue::New();
  value->type = ContextValueType::ENTITY;
  value->meta = ContextMetadataBuilder().SetEntityTopic("topic").Build();
  // We don't want to crash. There's no way to assert that here, but it will
  // show up in the logs.
  fidl::String value_id;
  writer_->AddValue(std::move(value),
                    [&value_id](const fidl::String& id) { value_id = id; });
  WAIT_UNTIL(value_id);
  WAIT_UNTIL(listener2.last_update);
}

}  // namespace maxwell
