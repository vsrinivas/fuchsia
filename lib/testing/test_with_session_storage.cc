// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/testing/test_with_session_storage.h"

namespace modular {
namespace testing {

TestWithSessionStorage::TestWithSessionStorage() = default;
TestWithSessionStorage::~TestWithSessionStorage() = default;

std::unique_ptr<SessionStorage> TestWithSessionStorage::MakeSessionStorage(
    std::string ledger_page) {
  auto page_id = MakePageId(ledger_page);
  return std::make_unique<SessionStorage>(ledger_client(), page_id);
}

std::unique_ptr<StoryStorage> TestWithSessionStorage::GetStoryStorage(
    SessionStorage* const storage, std::string story_id) {
  std::unique_ptr<StoryStorage> story_storage;
  bool done{};
  storage->GetStoryStorage(story_id)->Then(
      [&](std::unique_ptr<StoryStorage> result) {
        FXL_DCHECK(result);
        story_storage = std::move(result);
        done = true;
      });
  RunLoopUntil([&] { return done; });

  return story_storage;
}

fidl::StringPtr TestWithSessionStorage::CreateStory(
    SessionStorage* const storage) {
  auto future_story = storage->CreateStory(nullptr /* extra */,
                                           false /* is_kind_of_proto_story */);
  bool done{};
  fidl::StringPtr story_id;
  future_story->Then([&](fidl::StringPtr id, fuchsia::ledger::PageId) {
    done = true;
    story_id = std::move(id);
  });
  RunLoopUntil([&] { return done; });

  return story_id;
}

void TestWithSessionStorage::SetLinkValue(StoryStorage* const story_storage,
                                          const std::string& link_path,
                                          const std::string& link_value) {
  bool done{};
  int context;
  story_storage
      ->UpdateLinkValue(MakeLinkPath(link_path),
                        [&](fidl::StringPtr* value) { *value = link_value; },
                        &context)
      ->Then([&](StoryStorage::Status status) {
        ASSERT_EQ(status, StoryStorage::Status::OK);
        done = true;
      });
  RunLoopUntil([&] { return done; });
}

void TestWithSessionStorage::WriteModuleData(
    StoryStorage* const story_storage,
    fuchsia::modular::ModuleData module_data) {
  bool done{};
  story_storage->WriteModuleData(std::move(module_data))->Then([&] {
    done = true;
  });
  RunLoopUntil([&] { return done; });
}

std::string TestWithSessionStorage::GetLinkValue(
    StoryStorage* const story_storage, const std::string& link_name) {
  return GetLinkValue(story_storage, MakeLinkPath(link_name));
}

std::string TestWithSessionStorage::GetLinkValue(
    StoryStorage* const story_storage, const fuchsia::modular::LinkPath& path) {
  bool done{};
  std::string value;
  story_storage->GetLinkValue(path)->Then(
      [&](StoryStorage::Status status, fidl::StringPtr v) {
        value = *v;
        done = true;
      });
  RunLoopUntil([&] { return done; });
  return value;
}

fuchsia::modular::LinkPath TestWithSessionStorage::MakeLinkPath(
    const std::string& name) {
  fuchsia::modular::LinkPath path;
  path.link_name = name;
  return path;
}

}  // namespace testing
}  // namespace modular
