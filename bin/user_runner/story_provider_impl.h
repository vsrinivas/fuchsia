// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_USER_RUNNER_STORY_PROVIDER_IMPL_H_
#define APPS_MODULAR_SRC_USER_RUNNER_STORY_PROVIDER_IMPL_H_

#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "apps/ledger/services/ledger.fidl.h"
#include "apps/modular/lib/fidl/strong_binding.h"
#include "apps/modular/services/user/user_runner.fidl.h"
#include "apps/modular/src/user_runner/story_storage_impl.h"
#include "apps/modular/src/user_runner/transaction.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/ftl/macros.h"

namespace modular {
class ApplicationContext;
class StoryControllerImpl;

class StoryProviderImpl : public StoryProvider {
 public:
  StoryProviderImpl(
      std::shared_ptr<ApplicationContext> application_context,
      fidl::InterfaceHandle<ledger::Ledger> ledger,
      fidl::InterfaceRequest<StoryProvider> story_provider_request);

  ~StoryProviderImpl() override = default;

  // Obtains the StoryInfo for an existing story from the ledger.
  void GetStoryInfo(
      const fidl::String& story_id,
      std::function<void(StoryInfoPtr story_info)> story_info_callback);

  // Used by StoryControllerImpl to write story meta-data to the
  // ledger. Used after calling |Stop| or when the |Story| pipe is
  // closed.
  void WriteStoryInfo(StoryInfoPtr story_info);

  // Used by CreateStory() to write story meta-data to the ledger.
  void WriteStoryInfo(StoryInfoPtr story_info, std::function<void()> done);

  // Used by StoryControllerImpl.
  using Storage = StoryStorageImpl::Storage;
  std::shared_ptr<Storage> storage() { return storage_; }
  ledger::PagePtr GetStoryPage(const fidl::Array<uint8_t>& story_page_id);

 private:
  // |StoryProvider|
  void CreateStory(const fidl::String& url,
                   fidl::InterfaceRequest<StoryController>
                       story_controller_request) override;

  // |StoryProvider|
  void ResumeStoryById(const fidl::String& story_id,
                       fidl::InterfaceRequest<StoryController>
                           story_controller_request) override;

  // |StoryProvider|
  void ResumeStoryByInfo(fidl::StructPtr<StoryInfo> story_info,
                         fidl::InterfaceRequest<StoryController>
                             story_controller_request) override;

  // |StoryProvider|
  void PreviousStories(const PreviousStoriesCallback& callback) override;

  std::shared_ptr<ApplicationContext> application_context_;
  StrongBinding<StoryProvider> binding_;
  ledger::LedgerPtr ledger_;

  std::unordered_set<std::string> story_ids_;
  TransactionContainer transaction_container_;
  std::shared_ptr<Storage> storage_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryProviderImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_USER_RUNNER_STORY_PROVIDER_IMPL_H_
