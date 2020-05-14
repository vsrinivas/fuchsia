// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/testing/test_with_session_storage.h"

namespace modular_testing {

TestWithSessionStorage::TestWithSessionStorage() = default;
TestWithSessionStorage::~TestWithSessionStorage() = default;

std::unique_ptr<modular::SessionStorage> TestWithSessionStorage::MakeSessionStorage() {
  return std::make_unique<modular::SessionStorage>();
}

std::shared_ptr<modular::StoryStorage> TestWithSessionStorage::GetStoryStorage(
    modular::SessionStorage* const storage, std::string story_id) {
  auto story_storage = storage->GetStoryStorage(story_id);
  FX_DCHECK(!!story_storage) << story_id;
  return story_storage;
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
