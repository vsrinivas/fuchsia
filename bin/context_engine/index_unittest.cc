// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/context_engine/index.h"
#include "apps/maxwell/lib/context/formatting.h"
#include "apps/maxwell/services/context/context_engine.fidl.h"
#include "gtest/gtest.h"

namespace maxwell {
namespace {

TEST(IndexTest, Encode_Basic) {
  // Basic encoding correctness:
  //  * null case(s)
  //  * values are indexed along with their key
  auto type = ContextValueType::ENTITY;
  EXPECT_EQ(1lu, internal::EncodeMetadataAndType(type, nullptr).size());
  EXPECT_EQ(
      1lu,
      internal::EncodeMetadataAndType(type, ContextMetadata::New()).size());

  auto meta = ContextMetadata::New();
  meta->story = StoryMetadata::New();
  EXPECT_EQ(1lu, internal::EncodeMetadataAndType(type, meta).size());

  meta->story->id = "value";
  auto out = internal::EncodeMetadataAndType(type, meta);
  EXPECT_EQ(2lu, out.size());

  meta->mod = ModuleMetadata::New();
  meta->mod->url = "value";
  out = internal::EncodeMetadataAndType(type, meta);
  // Even though we use "value" as the value for both fields above, we should
  // see them encoded differently since they are for different fields.
  EXPECT_EQ(3lu, out.size());
}

TEST(IndexTest, Encode_Differences) {
  auto kEntity = ContextValueType::ENTITY;
  auto kStory = ContextValueType::STORY;
  // Encoding two entirely different ContextMetadata structs should produce
  // two non-intersecting sets of encodings.
  auto meta1 = ContextMetadata::New();
  meta1->story = StoryMetadata::New();
  meta1->story->id = "story1";
  meta1->story->focused = FocusedState::New();
  meta1->story->focused->state = FocusedState::State::FOCUSED;
  meta1->mod = ModuleMetadata::New();
  meta1->mod->url = "url1";
  meta1->mod->path = fidl::Array<fidl::String>::New(0);
  meta1->mod->path.push_back("1");
  meta1->mod->path.push_back("2");
  meta1->entity = EntityMetadata::New();
  meta1->entity->topic = "topic1";
  meta1->entity->type = fidl::Array<fidl::String>::New(0);
  meta1->entity->type.push_back("type1");
  meta1->entity->type.push_back("type2");

  auto meta2 = ContextMetadata::New();
  meta2->story = StoryMetadata::New();
  meta2->story->id = "story2";
  meta2->story->focused = FocusedState::New();
  meta2->story->focused->state = FocusedState::State::NOT_FOCUSED;
  meta2->mod = ModuleMetadata::New();
  meta2->mod->url = "url2";
  meta2->mod->path = fidl::Array<fidl::String>::New(0);
  meta2->mod->path.push_back("2");
  meta2->entity = EntityMetadata::New();
  meta2->entity->topic = "topic2";
  meta2->entity->type = fidl::Array<fidl::String>::New(0);
  meta2->entity->type.push_back("type3");
  meta2->entity->type.push_back("type4");
  meta2->entity->type.push_back("type5");

  auto encoded1 = internal::EncodeMetadataAndType(kEntity, meta1);
  auto encoded2 = internal::EncodeMetadataAndType(kStory, meta2);

  // Every field we set has a value here. entity->type fields each get their
  // own.
  EXPECT_EQ(8lu, encoded1.size());
  EXPECT_EQ(9lu, encoded2.size());

  std::set<std::string> intersection;
  std::set_intersection(encoded1.begin(), encoded1.end(), encoded2.begin(),
                        encoded2.end(),
                        std::inserter(intersection, intersection.begin()));
  EXPECT_TRUE(intersection.empty());

  // If we start changing some values to be equal, we should see encoded values
  // included.
  meta2->story->focused->state = FocusedState::State::FOCUSED;
  meta2->entity->type[1] = "type2";

  encoded1 = internal::EncodeMetadataAndType(kEntity, meta1);
  encoded2 = internal::EncodeMetadataAndType(kEntity, meta2);
  intersection.clear();
  std::set_intersection(encoded1.begin(), encoded1.end(), encoded2.begin(),
                        encoded2.end(),
                        std::inserter(intersection, intersection.begin()));
  EXPECT_EQ(3lu, intersection.size());
}

TEST(IndexTest, AddRemoveQuery) {
  auto kEntity = ContextValueType::ENTITY;
  auto kStory = ContextValueType::STORY;
  // We do not need to test that querying works for every single field in
  // ContextMetadata: between the Encode tests above, and the knowledge that
  // Encode is used internally by ContextIndex, we can test here for correct
  // query results for a subset of fields, and infer that the same behavior
  // would happen for other fields.
  ContextIndex index;
  auto meta1 = ContextMetadata::New();
  meta1->story = StoryMetadata::New();
  meta1->story->id = "story1";
  meta1->entity = EntityMetadata::New();
  meta1->entity->topic = "topic1";
  meta1->entity->type = fidl::Array<fidl::String>::New(0);
  meta1->entity->type.push_back("type1");
  meta1->entity->type.push_back("type2");

  index.Add("e1", kEntity, meta1);

  // This query won't match because story->id != "s".
  auto query1 = ContextMetadata::New();
  query1->story = StoryMetadata::New();
  query1->story->id = "s";  // Won't match.
  std::set<std::string> res;
  index.Query(kEntity, query1, &res);
  EXPECT_TRUE(res.empty());

  // This one still won't because kStory != kEntity.
  query1->story->id = "story1";
  res.clear();
  index.Query(kStory, query1, &res);
  EXPECT_TRUE(res.empty());

  // This one will.
  query1->story->id = "story1";
  res.clear();
  index.Query(kEntity, query1, &res);
  EXPECT_EQ(1ul, res.size());
  EXPECT_TRUE(res.find("e1") != res.end());

  // Add more to the query that we know will match.
  query1->entity = EntityMetadata::New();
  query1->entity->type.push_back("type1");
  res.clear();
  index.Query(kEntity, query1, &res);
  EXPECT_EQ(1ul, res.size());
  EXPECT_TRUE(res.find("e1") != res.end());

  // Add a new entity.
  auto meta2 = meta1->Clone();
  meta2->entity->type.push_back("type3");
  index.Add("e2", kEntity, meta2);

  res.clear();
  index.Query(kEntity, query1, &res);
  EXPECT_EQ(2ul, res.size());
  EXPECT_TRUE(res.find("e1") != res.end());
  EXPECT_TRUE(res.find("e2") != res.end());

  // Changing the query's type param to "type3" should only return "e2".
  query1->entity->type[0] = "type3";
  res.clear();
  index.Query(kEntity, query1, &res);
  EXPECT_EQ(1ul, res.size());
  EXPECT_TRUE(res.find("e2") != res.end());

  // And removing "e2" from the index makes it no longer appear in
  // query results.
  index.Remove("e2", kEntity, meta2);
  res.clear();
  index.Query(kEntity, query1, &res);
  EXPECT_TRUE(res.empty());

  // But "e1" is still there.
  query1->entity->type[0] = "type2";
  res.clear();
  index.Query(kEntity, query1, &res);
  EXPECT_EQ(1ul, res.size());
  EXPECT_TRUE(res.find("e1") != res.end());
}

}  // namespace
}  // namespace maxwell

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
