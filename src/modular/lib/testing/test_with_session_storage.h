// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_TESTING_TEST_WITH_SESSION_STORAGE_H_
#define SRC_MODULAR_LIB_TESTING_TEST_WITH_SESSION_STORAGE_H_

#include <fuchsia/modular/cpp/fidl.h>

#include "src/modular/bin/sessionmgr/storage/session_storage.h"
#include "src/modular/bin/sessionmgr/storage/story_storage.h"
#include "src/modular/lib/ledger_client/page_id.h"
#include "src/modular/lib/testing/test_with_ledger.h"

namespace modular_testing {

class TestWithSessionStorage : public TestWithLedger {
 public:
  TestWithSessionStorage();
  ~TestWithSessionStorage() override;

 protected:
  std::unique_ptr<modular::SessionStorage> MakeSessionStorage(std::string ledger_page);

  std::unique_ptr<modular::StoryStorage> GetStoryStorage(modular::SessionStorage* storage,
                                                         std::string story_id);

  // Create a new story with a specific story_id (name)
  void CreateStory(const std::string& story_id, modular::SessionStorage* storage);

  // Create a new story and return the generated name
  fidl::StringPtr CreateStory(modular::SessionStorage* storage);

  void SetLinkValue(modular::StoryStorage* story_storage, const std::string& link_name,
                    const std::string& link_value);

  void SetLinkValue(modular::StoryStorage* story_storage,
                    const fuchsia::modular::LinkPath& link_path, const std::string& link_value);

  void WriteModuleData(modular::StoryStorage* story_storage,
                       fuchsia::modular::ModuleData module_data);

  std::string GetLinkValue(modular::StoryStorage* story_storage,
                           const fuchsia::modular::LinkPath& path);

  std::string GetLinkValue(modular::StoryStorage* story_storage, const std::string& link_name);

  fuchsia::modular::LinkPath MakeLinkPath(const std::string& name);

 private:
  // Implements CreateStory on behalf of protected variants
  fidl::StringPtr CreateStoryImpl(fidl::StringPtr story_id, modular::SessionStorage* storage);
};

}  // namespace modular_testing

#endif  // SRC_MODULAR_LIB_TESTING_TEST_WITH_SESSION_STORAGE_H_
