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
#include "apps/modular/services/application/application_environment.fidl.h"
#include "apps/modular/services/user/story_data.fidl.h"
#include "apps/modular/services/user/story_provider.fidl.h"
#include "apps/modular/src/user_runner/story_storage_impl.h"
#include "apps/modular/src/user_runner/transaction.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/ftl/macros.h"

namespace modular {
class ApplicationContext;
class StoryControllerImpl;

class StoryProviderImpl : public StoryProvider, ledger::PageWatcher {
 public:
  StoryProviderImpl(
      ApplicationEnvironmentPtr environment,
      fidl::InterfaceHandle<ledger::Ledger> ledger,
      fidl::InterfaceRequest<StoryProvider> story_provider_request);

  ~StoryProviderImpl() override = default;

  // Adds a non-lifecycle-governing binding to this |StoryProvider|. (The
  // principal binding established in the constructor governs the lifespan of
  // this instance.)
  void AddAuxiliaryBinding(fidl::InterfaceRequest<StoryProvider> request) {
    aux_bindings_.AddBinding(this, std::move(request));
  }

  // Obtains the StoryData for an existing story from the ledger.
  void GetStoryData(const fidl::String& story_id,
                    const std::function<void(StoryDataPtr)>& result);

  // Used by CreateStory() to write story meta-data to the ledger.
  void WriteStoryData(StoryDataPtr story_data, std::function<void()> done);

  // Used by StoryControllerImpl.
  using Storage = StoryStorageImpl::Storage;
  std::shared_ptr<Storage> storage() { return storage_; }
  ledger::PagePtr GetStoryPage(const fidl::Array<uint8_t>& story_page_id);

 private:
  // |StoryProvider|
  // Obtains the StoryInfo for an existing story from the ledger.
  void GetStoryInfo(const fidl::String& story_id,
                    const GetStoryInfoCallback& story_info_callback) override;

  // |StoryProvider|
  void CreateStory(const fidl::String& url,
                   fidl::InterfaceRequest<StoryController>
                       story_controller_request) override;

  // |StoryProvider|
  void DeleteStory(const fidl::String& story_id,
                   const DeleteStoryCallback& callback) override;

  // |StoryProvider|
  void ResumeStory(const fidl::String& story_id,
                   fidl::InterfaceRequest<StoryController>
                       story_controller_request) override;

  // |StoryProvider|
  void PreviousStories(const PreviousStoriesCallback& callback) override;

  // |StoryProvider|
  void Watch(fidl::InterfaceHandle<StoryProviderWatcher> watcher) override;

  // |PageWatcher|
  void OnInitialState(fidl::InterfaceHandle<ledger::PageSnapshot> page,
                      const OnInitialStateCallback& cb) override;
  void OnChange(ledger::PageChangePtr page,
                const OnChangeCallback& cb) override;

  ApplicationEnvironmentPtr environment_;
  StrongBinding<StoryProvider> binding_;
  fidl::BindingSet<StoryProvider> aux_bindings_;
  ledger::LedgerPtr ledger_;

  std::unordered_set<std::string> story_ids_;
  TransactionContainer transaction_container_;
  std::shared_ptr<Storage> storage_;
  fidl::Binding<ledger::PageWatcher> page_watcher_binding_;

  fidl::InterfacePtrSet<StoryProviderWatcher> watchers_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryProviderImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_USER_RUNNER_STORY_PROVIDER_IMPL_H_
