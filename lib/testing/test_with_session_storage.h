// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_TESTING_TEST_WITH_SESSION_STORAGE_H_
#define PERIDOT_LIB_TESTING_TEST_WITH_SESSION_STORAGE_H_

#include <fuchsia/modular/cpp/fidl.h>

#include "peridot/bin/user_runner/storage/session_storage.h"
#include "peridot/bin/user_runner/storage/story_storage.h"
#include "peridot/lib/ledger_client/page_id.h"
#include "peridot/lib/testing/test_with_ledger.h"

namespace modular {
namespace testing {

class TestWithSessionStorage : public testing::TestWithLedger {
 public:
  TestWithSessionStorage();
  ~TestWithSessionStorage() override;

 protected:
  std::unique_ptr<SessionStorage> MakeSessionStorage(std::string ledger_page);

  std::unique_ptr<StoryStorage> GetStoryStorage(SessionStorage* const storage,
                                                std::string story_id);

  fidl::StringPtr CreateStory(SessionStorage* const storage);

  void SetLinkValue(StoryStorage* const story_storage,
                    const std::string& link_name,
                    const std::string& link_value);

  void SetLinkValue(StoryStorage* const story_storage,
                    const fuchsia::modular::LinkPath& link_path,
                    const std::string& link_value);

  void WriteModuleData(StoryStorage* const story_storage,
                       fuchsia::modular::ModuleData module_data);

  std::string GetLinkValue(StoryStorage* const story_storage,
                           const fuchsia::modular::LinkPath& path);

  std::string GetLinkValue(StoryStorage* const story_storage,
                           const std::string& link_name);

  fuchsia::modular::LinkPath MakeLinkPath(const std::string& name);
};

}  // namespace testing
}  // namespace modular

#endif  // PERIDOT_LIB_TESTING_TEST_WITH_SESSION_STORAGE_H_
