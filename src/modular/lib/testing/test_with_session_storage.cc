// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/testing/test_with_session_storage.h"

namespace modular_testing {

TestWithSessionStorage::TestWithSessionStorage() = default;
TestWithSessionStorage::~TestWithSessionStorage() = default;

std::unique_ptr<modular::SessionStorage> TestWithSessionStorage::MakeSessionStorage(
    std::string ledger_page) {
  auto page_id = modular::MakePageId(ledger_page);
  return std::make_unique<modular::SessionStorage>(ledger_client(), page_id);
}

std::unique_ptr<modular::StoryStorage> TestWithSessionStorage::GetStoryStorage(
    modular::SessionStorage* const storage, std::string story_id) {
  std::unique_ptr<modular::StoryStorage> story_storage;
  bool done{};
  storage->GetStoryStorage(story_id)->Then([&](std::unique_ptr<modular::StoryStorage> result) {
    FX_DCHECK(!!result);
    story_storage = std::move(result);
    done = true;
  });
  RunLoopUntil([&] { return done; });

  return story_storage;
}

fidl::StringPtr TestWithSessionStorage::CreateStoryImpl(fidl::StringPtr story_id,
                                                        modular::SessionStorage* const storage) {
  auto future_story = storage->CreateStory(story_id, /*annotations=*/{});
  bool done{};
  future_story->Then([&](fidl::StringPtr id, fuchsia::ledger::PageId) {
    done = true;
    story_id = std::move(id);
  });
  RunLoopUntil([&] { return done; });

  return story_id;
}

void TestWithSessionStorage::CreateStory(const std::string& story_id,
                                         modular::SessionStorage* const storage) {
  auto created_story_id = CreateStoryImpl(story_id, storage).value_or("");
  FX_DCHECK(story_id == created_story_id);
}

fidl::StringPtr TestWithSessionStorage::CreateStory(modular::SessionStorage* const storage) {
  return CreateStoryImpl(nullptr, storage);
}

void TestWithSessionStorage::SetLinkValue(modular::StoryStorage* const story_storage,
                                          const std::string& link_name,
                                          const std::string& link_value) {
  SetLinkValue(story_storage, MakeLinkPath(link_name), link_value);
}

void TestWithSessionStorage::SetLinkValue(modular::StoryStorage* const story_storage,
                                          const fuchsia::modular::LinkPath& link_path,
                                          const std::string& link_value) {
  bool done{};
  int context;
  story_storage
      ->UpdateLinkValue(
          link_path, [&](fidl::StringPtr* value) { *value = link_value; }, &context)
      ->Then([&](modular::StoryStorage::Status status) {
        ASSERT_EQ(status, modular::StoryStorage::Status::OK);
        done = true;
      });
  RunLoopUntil([&] { return done; });
}

void TestWithSessionStorage::WriteModuleData(modular::StoryStorage* const story_storage,
                                             fuchsia::modular::ModuleData module_data) {
  bool done{};
  story_storage->WriteModuleData(std::move(module_data))->Then([&] { done = true; });
  RunLoopUntil([&] { return done; });
}

std::string TestWithSessionStorage::GetLinkValue(modular::StoryStorage* const story_storage,
                                                 const std::string& link_name) {
  return GetLinkValue(story_storage, MakeLinkPath(link_name));
}

std::string TestWithSessionStorage::GetLinkValue(modular::StoryStorage* const story_storage,
                                                 const fuchsia::modular::LinkPath& path) {
  bool done{};
  std::string value;
  story_storage->GetLinkValue(path)->Then(
      [&](modular::StoryStorage::Status status, fidl::StringPtr v) {
        value = *v;
        done = true;
      });
  RunLoopUntil([&] { return done; });
  return value;
}

fuchsia::modular::LinkPath TestWithSessionStorage::MakeLinkPath(const std::string& name) {
  fuchsia::modular::LinkPath path;
  path.link_name = name;
  return path;
}

}  // namespace modular_testing
