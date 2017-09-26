// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/context_engine/context_repository.h"
#include "gtest/gtest.h"
#include "lib/context/cpp/context_metadata_builder.h"
#include "lib/context/cpp/formatting.h"
#include "lib/context/fidl/context_engine.fidl.h"

namespace maxwell {
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

class TestListener : public ContextListener {
 public:
  ContextUpdatePtr last_update;

  void OnContextUpdate(ContextUpdatePtr update) override {
    last_update = std::move(update);
  }

  void reset() { last_update.reset(); }
};

ContextValuePtr CreateValue(ContextValueType type,
                            const std::string& content,
                            ContextMetadataPtr metadata) {
  ContextValuePtr value = ContextValue::New();
  value->type = type;
  value->content = content;
  value->meta = std::move(metadata);
  return value;
}

}  // namespace

TEST_F(ContextRepositoryTest, GetAddUpdateRemove) {
  // This test ensures that we can do basic, synchronous add/update/remove/get
  // operations.

  // Show that when we set values, we can get them back.
  auto id1 = repository_.Add(
      CreateValue(ContextValueType::ENTITY, "content", nullptr));
  auto value1 = repository_.Get(id1);
  ASSERT_TRUE(value1);
  ASSERT_FALSE(value1->meta);
  EXPECT_EQ(ContextValueType::ENTITY, value1->type);
  EXPECT_EQ("content", value1->content);

  // Setting another value doesn't affect the original value.
  auto id2 = repository_.Add(
      CreateValue(ContextValueType::ENTITY, "content2", nullptr));
  auto value2 = repository_.Get(id2);
  ASSERT_TRUE(value2);
  ASSERT_FALSE(value2->meta);
  EXPECT_EQ("content2", value2->content);
  value1 = repository_.Get(id1);
  ASSERT_TRUE(value1);
  EXPECT_EQ(ContextValueType::ENTITY, value1->type);
  EXPECT_EQ("content", value1->content);

  // Let's create metadata.
  auto id3 = repository_.Add(
      CreateValue(ContextValueType::ENTITY, "content3",
                  ContextMetadataBuilder().SetStoryId("id3story").Build()));
  auto value3 = repository_.Get(id3);
  ASSERT_TRUE(value3);
  EXPECT_EQ("content3", value3->content);
  ASSERT_TRUE(value3->meta);
  EXPECT_EQ("id3story", value3->meta->story->id);

  // Update one of the previous values.
  repository_.Update(
      id2,
      CreateValue(ContextValueType::ENTITY, "new content2",
                  ContextMetadataBuilder().SetStoryId("id2story").Build()));
  value2 = repository_.Get(id2);
  ASSERT_TRUE(value2);
  ASSERT_TRUE(value2->meta);
  EXPECT_EQ("id2story", value2->meta->story->id);
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
  ContextMetadataPtr meta1 = ContextMetadataBuilder().SetStoryId("id").Build();
  auto id1 = repository_.Add(
      CreateValue(ContextValueType::STORY, "s", std::move(meta1)));

  ContextMetadataPtr meta2 =
      ContextMetadataBuilder().SetModuleUrl("url").Build();
  auto id2 = repository_.Add(
      id1, CreateValue(ContextValueType::MODULE, "m", std::move(meta2)));

  auto value1 = repository_.GetMerged(id1);
  ASSERT_TRUE(value1);
  // value1's metadata shouldn't have changed.
  ASSERT_TRUE(value1->meta);
  ASSERT_TRUE(value1->meta->story);
  EXPECT_EQ("id", value1->meta->story->id);
  ASSERT_FALSE(value1->meta->mod);

  auto value2 = repository_.GetMerged(id2);
  ASSERT_TRUE(value2);
  // value2's metadata should combine both value1's and value2's.
  ASSERT_TRUE(value2->meta);
  ASSERT_TRUE(value2->meta->story);
  EXPECT_EQ("id", value2->meta->story->id);
  ASSERT_TRUE(value2->meta->mod);
  ASSERT_TRUE(value2->meta->mod->url);
  EXPECT_EQ("url", value2->meta->mod->url);

  // Changing the parent's metadata value should update the child's also.
  meta1 = ContextMetadata::New();
  meta1->story = StoryMetadata::New();
  meta1->story->id = "newid";
  repository_.Update(
      id1, CreateValue(ContextValueType::STORY, "s", std::move(meta1)));
  value2 = repository_.GetMerged(id2);
  ASSERT_TRUE(value2);
  ASSERT_TRUE(value2->meta);
  ASSERT_TRUE(value2->meta->story);
  EXPECT_EQ("newid", value2->meta->story->id);
  ASSERT_TRUE(value2->meta->mod);
  ASSERT_TRUE(value2->meta->mod->url);
  EXPECT_EQ("url", value2->meta->mod->url);

  // If a parent contains metadata that the child also contains (they both have
  // 'mod' metadata), the parent's takes precendence.
  meta1 =
      ContextMetadataBuilder(meta1.Clone()).SetModuleUrl("override").Build();
  repository_.Update(
      id1, CreateValue(ContextValueType::STORY, "s", std::move(meta1)));
  value2 = repository_.GetMerged(id2);
  ASSERT_TRUE(value2);
  ASSERT_TRUE(value2->meta);
  ASSERT_FALSE(value2->meta->story);
  ASSERT_TRUE(value2->meta->mod);
  ASSERT_EQ("override", value2->meta->mod->url);
}

TEST_F(ContextRepositoryTest, ListenersGetUpdates) {
  // We want to test these subscription behaviors.
  // 1) A value is added but doesn't match our subscription.
  //    a) It's the wrong type (ie, STORY vs ENTITY)
  //    b) Its metadata doesn't match.
  // 2) A value is added that matches our existing subscription.
  // 3) A value is updated that newly matches our subscription.

  // (1)
  auto query = ContextQuery::New();
  auto selector = ContextSelector::New();
  selector->type = ContextValueType::ENTITY;
  selector->meta = ContextMetadataBuilder().SetEntityTopic("topic").Build();
  query->selector["a"] = std::move(selector);

  TestListener listener;
  repository_.AddSubscription(std::move(query), &listener,
                              SubscriptionDebugInfoPtr());
  EXPECT_EQ(0lu, listener.last_update->values["a"].size());
  listener.reset();

  // (a)
  auto value = ContextValue::New();
  value->type = ContextValueType::STORY;
  value->content = "no match";
  value->meta = ContextMetadataBuilder().SetEntityTopic("topic").Build();
  repository_.Add(std::move(value));
  // No new update because nothing changed for our subscription.
  EXPECT_FALSE(listener.last_update);
  listener.reset();

  // (b)
  value = ContextValue::New();
  value->type = ContextValueType::ENTITY;
  value->content = "no match yet";
  value->meta =
      ContextMetadataBuilder().SetEntityTopic("not the topic").Build();
  auto id = repository_.Add(std::move(value));  // Save id for later.
  // No new update because nothing changed for our subscription.
  EXPECT_FALSE(listener.last_update);
  listener.reset();

  // (2)
  value = ContextValue::New();
  value->type = ContextValueType::ENTITY;
  value->content = "match";
  value->meta = ContextMetadataBuilder().SetEntityTopic("topic").Build();
  repository_.Add(std::move(value));
  EXPECT_EQ(1lu, listener.last_update->values["a"].size());
  EXPECT_EQ("match", listener.last_update->values["a"][0]->content);
  listener.reset();

  // (3)
  value = ContextValue::New();
  value->type = ContextValueType::ENTITY;
  value->content = "now it matches";
  // Add more metadata than the query is looking for. It shouldn't affect
  // the query, because it doesn't express any constraint on 'type'.
  value->meta = ContextMetadataBuilder()
                    .SetEntityTopic("topic")
                    .AddEntityType("type1")
                    .AddEntityType("type2")
                    .Build();
  repository_.Update(id, std::move(value));
  ASSERT_TRUE(listener.last_update);
  EXPECT_EQ(2lu, listener.last_update->values["a"].size());
  EXPECT_EQ("now it matches", listener.last_update->values["a"][0]->content);
  EXPECT_EQ("match", listener.last_update->values["a"][1]->content);
  listener.reset();
}

TEST_F(ContextRepositoryTest, ListenersGetUpdates_WhenParentsUpdated) {
  // We should see updates to listeners when an update to a node's
  // parent causes that node to be matched by a query.
  auto query = ContextQuery::New();
  auto selector = ContextSelector::New();
  selector->type = ContextValueType::ENTITY;
  selector->meta = ContextMetadataBuilder().SetStoryId("match").Build();
  query->selector["a"] = std::move(selector);

  TestListener listener;
  repository_.AddSubscription(std::move(query), &listener,
                              SubscriptionDebugInfoPtr());
  ASSERT_TRUE(listener.last_update);
  EXPECT_EQ(0lu, listener.last_update->values["a"].size());
  listener.reset();

  // Add a Story value.
  auto story_value = ContextValue::New();
  story_value->type = ContextValueType::STORY;
  story_value->meta = ContextMetadataBuilder().SetStoryId("no match").Build();
  auto first_story_value = story_value.Clone();  // Save for later.
  auto story_value_id = repository_.Add(std::move(story_value));

  // Expect no update.
  EXPECT_FALSE(listener.last_update);

  // Add an Entity node, but it still shouldn't match.
  auto entity_value = ContextValue::New();
  entity_value->type = ContextValueType::ENTITY;
  entity_value->content = "content";
  repository_.Add(story_value_id, std::move(entity_value));

  // Still expect no update.
  EXPECT_FALSE(listener.last_update);

  // Update the story value so its metadata matches the query, and we should
  // see the entity value returned in our update.
  story_value = ContextValue::New();
  story_value->type = ContextValueType::STORY;
  story_value->meta = ContextMetadataBuilder().SetStoryId("match").Build();
  auto matching_story_value = story_value.Clone();  // Save for later.
  repository_.Update(story_value_id, std::move(story_value));

  ASSERT_TRUE(listener.last_update);
  EXPECT_EQ(1lu, listener.last_update->values["a"].size());
  EXPECT_EQ("content", listener.last_update->values["a"][0]->content);
  // Make sure we adopted the parent metadata from the story node.
  ASSERT_TRUE(listener.last_update->values["a"][0]->meta->story);
  EXPECT_EQ("match", listener.last_update->values["a"][0]->meta->story->id);
  listener.reset();

  // Set the value back to something that doesn't match, and we should get an
  // empty update.
  repository_.Update(story_value_id, std::move(first_story_value));
  ASSERT_TRUE(listener.last_update);
  EXPECT_EQ(0lu, listener.last_update->values["a"].size());
  listener.reset();

  // Set it back to something that matched, and this time remove the value
  // entirely. We should observe it go away.
  repository_.Update(story_value_id, std::move(matching_story_value));
  ASSERT_TRUE(listener.last_update);
  EXPECT_EQ(1lu, listener.last_update->values["a"].size());
  listener.reset();

  repository_.Remove(story_value_id);
  ASSERT_TRUE(listener.last_update);
  EXPECT_EQ(0lu, listener.last_update->values["a"].size());
  listener.reset();
}

}  // namespace maxwell

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
