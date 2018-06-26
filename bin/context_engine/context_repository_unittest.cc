// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/context_engine/context_repository.h"

#include <fuchsia/modular/cpp/fidl.h>

#include "gtest/gtest.h"
#include "lib/context/cpp/context_helper.h"
#include "lib/context/cpp/context_metadata_builder.h"
#include "lib/context/cpp/formatting.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fidl/cpp/optional.h"

using maxwell::ContextMetadataBuilder;

namespace modular {
namespace {

TEST(ContextGraph, GetChildrenRecursive_GetAncestors) {
  ContextGraph graph;

  graph.AddEdge("a", "b");
  graph.AddEdge("b", "c");
  graph.AddEdge("b", "d");

  auto children = graph.GetChildrenRecursive("b");
  EXPECT_EQ(2lu, children.size());
  EXPECT_TRUE(children.find("c") != children.end());
  EXPECT_TRUE(children.find("d") != children.end());

  children = graph.GetChildrenRecursive("a");
  EXPECT_EQ(3lu, children.size());
  EXPECT_TRUE(children.find("b") != children.end());
  EXPECT_TRUE(children.find("c") != children.end());
  EXPECT_TRUE(children.find("d") != children.end());

  auto ancestors = graph.GetAncestors("c");
  EXPECT_EQ(2lu, ancestors.size());
  EXPECT_EQ("a", ancestors[0]);
  EXPECT_EQ("b", ancestors[1]);
}

class ContextRepositoryTest : public ::testing::Test {
 public:
  ContextRepositoryTest() {}

 protected:
  ContextRepository repository_;
};

class TestListener : public fuchsia::modular::ContextListener {
 public:
  fuchsia::modular::ContextUpdatePtr last_update;

  void OnContextUpdate(fuchsia::modular::ContextUpdate update) override {
    last_update = fidl::MakeOptional(std::move(update));
  }

  void reset() { last_update.reset(); }
};

fuchsia::modular::ContextValue CreateValue(
    fuchsia::modular::ContextValueType type, const std::string& content) {
  fuchsia::modular::ContextValue value;
  value.type = type;
  value.content = content;
  return value;
}

fuchsia::modular::ContextValue CreateValue(
    fuchsia::modular::ContextValueType type, const std::string& content,
    fuchsia::modular::ContextMetadata meta) {
  fuchsia::modular::ContextValue value;
  value.type = type;
  value.content = content;
  value.meta = std::move(meta);
  return value;
}

}  // namespace

TEST_F(ContextRepositoryTest, GetAddUpdateRemove) {
  // This test ensures that we can do basic, synchronous add/update/remove/get
  // operations.

  // Show that when we set values, we can get them back.
  auto id1 = repository_.Add(
      CreateValue(fuchsia::modular::ContextValueType::ENTITY, "content"));
  auto value1 = repository_.Get(id1);
  ASSERT_TRUE(value1);
  EXPECT_EQ(fuchsia::modular::ContextValueType::ENTITY, value1->type);
  EXPECT_EQ("content", value1->content);

  // Setting another value doesn't affect the original value.
  auto id2 = repository_.Add(
      CreateValue(fuchsia::modular::ContextValueType::ENTITY, "content2"));
  auto value2 = repository_.Get(id2);
  ASSERT_TRUE(value2);
  EXPECT_EQ("content2", value2->content);
  value1 = repository_.Get(id1);
  ASSERT_TRUE(value1);
  EXPECT_EQ(fuchsia::modular::ContextValueType::ENTITY, value1->type);
  EXPECT_EQ("content", value1->content);

  // Let's create metadata.
  auto id3 = repository_.Add(
      CreateValue(fuchsia::modular::ContextValueType::ENTITY, "content3",
                  ContextMetadataBuilder().SetStoryId("id3story").Build()));
  auto value3 = repository_.Get(id3);
  ASSERT_TRUE(value3);
  EXPECT_EQ("content3", value3->content);
  ASSERT_TRUE(value3->meta.story);
  EXPECT_EQ("id3story", value3->meta.story->id);

  // Update one of the previous values.
  repository_.Update(
      id2,
      CreateValue(fuchsia::modular::ContextValueType::ENTITY, "new content2",
                  ContextMetadataBuilder().SetStoryId("id2story").Build()));
  value2 = repository_.Get(id2);
  ASSERT_TRUE(value2);
  ASSERT_TRUE(value2->meta.story);
  EXPECT_EQ("id2story", value2->meta.story->id);
  EXPECT_EQ("new content2", value2->content);

  // Now remove id3.
  repository_.Remove(id3);
  EXPECT_FALSE(repository_.Get(id3));
  EXPECT_TRUE(repository_.Get(id1));
  EXPECT_TRUE(repository_.Get(id2));

  // And the others.
  repository_.Remove(id1);
  repository_.Remove(id2);
  EXPECT_FALSE(repository_.Get(id1));
  EXPECT_FALSE(repository_.Get(id2));
}

TEST_F(ContextRepositoryTest, ValuesInheritMetadata) {
  // When a value is added as a child of another value, the child inherits the
  // metadata of its parent.
  auto meta1 = ContextMetadataBuilder().SetStoryId("id").Build();
  auto id1 = repository_.Add(CreateValue(
      fuchsia::modular::ContextValueType::STORY, "s", std::move(meta1)));

  auto meta2 = ContextMetadataBuilder().SetModuleUrl("url").Build();
  auto id2 = repository_.Add(
      id1, CreateValue(fuchsia::modular::ContextValueType::MODULE, "m",
                       std::move(meta2)));

  auto value1 = repository_.GetMerged(id1);
  ASSERT_TRUE(value1);
  // value1's metadata shouldn't have changed.
  ASSERT_TRUE(value1->meta.story);
  EXPECT_EQ("id", value1->meta.story->id);
  ASSERT_FALSE(value1->meta.mod);

  auto value2 = repository_.GetMerged(id2);
  ASSERT_TRUE(value2);
  // value2's metadata should combine both value1's and value2's.
  ASSERT_TRUE(value2->meta.story);
  EXPECT_EQ("id", value2->meta.story->id);
  ASSERT_TRUE(value2->meta.mod);
  ASSERT_TRUE(value2->meta.mod->url);
  EXPECT_EQ("url", value2->meta.mod->url);

  // Changing the parent's metadata value should update the child's also.
  meta1 = fuchsia::modular::ContextMetadata();
  meta1.story = fuchsia::modular::StoryMetadata::New();
  meta1.story->id = "newid";
  repository_.Update(id1, CreateValue(fuchsia::modular::ContextValueType::STORY,
                                      "s", std::move(meta1)));
  value2 = repository_.GetMerged(id2);
  ASSERT_TRUE(value2);
  ASSERT_TRUE(value2->meta.story);
  EXPECT_EQ("newid", value2->meta.story->id);
  ASSERT_TRUE(value2->meta.mod);
  ASSERT_TRUE(value2->meta.mod->url);
  EXPECT_EQ("url", value2->meta.mod->url);

  // If a parent contains metadata that the child also contains (they both have
  // 'mod' metadata), the parent's takes precendence.
  meta1 =
      ContextMetadataBuilder(std::move(meta1)).SetModuleUrl("override").Build();
  repository_.Update(id1, CreateValue(fuchsia::modular::ContextValueType::STORY,
                                      "s", std::move(meta1)));
  value2 = repository_.GetMerged(id2);
  ASSERT_TRUE(value2);
  ASSERT_FALSE(value2->meta.story);
  ASSERT_TRUE(value2->meta.mod);
  ASSERT_EQ("override", value2->meta.mod->url);
}

TEST_F(ContextRepositoryTest, ListenersGetUpdates) {
  // We want to test these subscription behaviors.
  // 1) A value is added but doesn't match our subscription.
  //    a) It's the wrong type (ie, STORY vs ENTITY)
  //    b) Its metadata doesn't match.
  // 2) A value is added that matches our existing subscription.
  // 3) A value is updated that newly matches our subscription.
  // 4) When a value is removed, it is no longer returned.

  // (1)
  fuchsia::modular::ContextQuery query;
  fuchsia::modular::ContextSelector selector;
  selector.type = fuchsia::modular::ContextValueType::ENTITY;
  selector.meta = ContextMetadataBuilder().SetEntityTopic("topic").BuildPtr();
  AddToContextQuery(&query, "a", std::move(selector));

  TestListener listener;
  repository_.AddSubscription(std::move(query), &listener,
                              fuchsia::modular::SubscriptionDebugInfo());
  EXPECT_EQ(0lu,
            TakeContextValue(listener.last_update.get(), "a").second->size());
  listener.reset();

  // (a)
  fuchsia::modular::ContextValue value;
  value.type = fuchsia::modular::ContextValueType::STORY;
  value.content = "no match";
  value.meta = ContextMetadataBuilder().SetEntityTopic("topic").Build();
  repository_.Add(std::move(value));
  // No new update because nothing changed for our subscription.
  EXPECT_FALSE(listener.last_update);
  listener.reset();

  // (b)
  value = fuchsia::modular::ContextValue();
  value.type = fuchsia::modular::ContextValueType::ENTITY;
  value.content = "no match yet";
  value.meta = ContextMetadataBuilder().SetEntityTopic("not the topic").Build();
  auto id = repository_.Add(std::move(value));  // Save id for later.
  // No new update because nothing changed for our subscription.
  EXPECT_FALSE(listener.last_update);
  listener.reset();

  // (2)
  value = fuchsia::modular::ContextValue();
  value.type = fuchsia::modular::ContextValueType::ENTITY;
  value.content = "match";
  value.meta = ContextMetadataBuilder().SetEntityTopic("topic").Build();
  repository_.Add(std::move(value));
  auto result = TakeContextValue(listener.last_update.get(), "a").second;
  EXPECT_EQ(1lu, result->size());
  EXPECT_EQ("match", result->at(0).content);
  listener.reset();

  // (3)
  value = fuchsia::modular::ContextValue();
  value.type = fuchsia::modular::ContextValueType::ENTITY;
  value.content = "now it matches";
  // Add more metadata than the query is looking for. It shouldn't affect
  // the query, because it doesn't express any constraint on 'type'.
  value.meta = ContextMetadataBuilder()
                   .SetEntityTopic("topic")
                   .AddEntityType("type1")
                   .AddEntityType("type2")
                   .Build();
  repository_.Update(id, std::move(value));
  ASSERT_TRUE(listener.last_update);
  result = TakeContextValue(listener.last_update.get(), "a").second;
  EXPECT_EQ(2lu, result->size());
  EXPECT_EQ("now it matches", result->at(0).content);
  EXPECT_EQ("match", result->at(1).content);
  listener.reset();

  // (4)
  repository_.Remove(id);
  ASSERT_TRUE(listener.last_update);
  result = TakeContextValue(listener.last_update.get(), "a").second;
  EXPECT_EQ(1lu, result->size());
  EXPECT_EQ("match", result->at(0).content);
  listener.reset();
}

TEST_F(ContextRepositoryTest, ListenersGetUpdates_WhenParentsUpdated) {
  // We should see updates to listeners when an update to a node's
  // parent causes that node to be matched by a query.
  fuchsia::modular::ContextQuery query;
  fuchsia::modular::ContextSelector selector;
  selector.type = fuchsia::modular::ContextValueType::ENTITY;
  selector.meta = ContextMetadataBuilder().SetStoryId("match").BuildPtr();
  AddToContextQuery(&query, "a", std::move(selector));

  TestListener listener;
  repository_.AddSubscription(std::move(query), &listener,
                              fuchsia::modular::SubscriptionDebugInfo());
  ASSERT_TRUE(listener.last_update);
  auto result = TakeContextValue(listener.last_update.get(), "a").second;
  EXPECT_EQ(0lu, result->size());
  listener.reset();

  // Add a Story value.
  fuchsia::modular::ContextValue story_value;
  story_value.type = fuchsia::modular::ContextValueType::STORY;
  story_value.meta = ContextMetadataBuilder().SetStoryId("no match").Build();
  fuchsia::modular::ContextValue first_story_value;
  fidl::Clone(story_value, &first_story_value);  // Save for later.
  auto story_value_id = repository_.Add(std::move(story_value));

  // Expect no update.
  EXPECT_FALSE(listener.last_update);

  // Add an fuchsia::modular::Entity node, but it still shouldn't match.
  fuchsia::modular::ContextValue entity_value;
  entity_value.type = fuchsia::modular::ContextValueType::ENTITY;
  entity_value.content = "content";
  repository_.Add(story_value_id, std::move(entity_value));

  // Still expect no update.
  EXPECT_FALSE(listener.last_update);

  // Update the story value so its metadata matches the query, and we should
  // see the entity value returned in our update.
  story_value = fuchsia::modular::ContextValue();
  story_value.type = fuchsia::modular::ContextValueType::STORY;
  story_value.meta = ContextMetadataBuilder().SetStoryId("match").Build();
  fuchsia::modular::ContextValue matching_story_value;
  fidl::Clone(story_value, &matching_story_value);  // Save for later.
  repository_.Update(story_value_id, std::move(story_value));

  ASSERT_TRUE(listener.last_update);
  result = TakeContextValue(listener.last_update.get(), "a").second;
  EXPECT_EQ(1lu, result->size());
  EXPECT_EQ("content", result->at(0).content);
  // Make sure we adopted the parent metadata from the story node.
  ASSERT_TRUE(result->at(0).meta.story);
  EXPECT_EQ("match", result->at(0).meta.story->id);
  listener.reset();

  // Set the value back to something that doesn't match, and we should get an
  // empty update.
  repository_.Update(story_value_id, std::move(first_story_value));
  ASSERT_TRUE(listener.last_update);
  result = TakeContextValue(listener.last_update.get(), "a").second;
  EXPECT_EQ(0lu, result->size());
  listener.reset();

  // Set it back to something that matched, and this time remove the value
  // entirely. We should observe it go away.
  repository_.Update(story_value_id, std::move(matching_story_value));
  ASSERT_TRUE(listener.last_update);
  result = TakeContextValue(listener.last_update.get(), "a").second;
  EXPECT_EQ(1lu, result->size());
  listener.reset();

  repository_.Remove(story_value_id);
  ASSERT_TRUE(listener.last_update);
  result = TakeContextValue(listener.last_update.get(), "a").second;
  EXPECT_EQ(0lu, result->size());
  listener.reset();
}

}  // namespace modular
