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
#include "apps/modular/src/user_runner/user_ledger_repository_factory.h"
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
      fidl::InterfaceRequest<StoryProvider> story_provider_request,
      UserLedgerRepositoryFactory* ledger_repository_factory);

  ~StoryProviderImpl() override = default;

  // Adds a non-lifecycle-governing binding to this |StoryProvider|.
  // The principal binding established in the constructor governs the
  // lifespan of this instance.
  void AddAuxiliaryBinding(fidl::InterfaceRequest<StoryProvider> request) {
    aux_bindings_.AddBinding(this, std::move(request));
  }

  // Used by Create and Resume implementations. Takes ownership of the
  // controller.
  void AddController(const std::string& story_id, StoryControllerImpl* story_controller);

  // Removes story controller impls no longer needed. Also called by
  // the empty binding set handler of StoryControllerImpl.
  void PurgeControllers();

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

  // |PageWatcher|
  void OnChange(ledger::PageChangePtr page,
                const OnChangeCallback& cb) override;

  // Used by CreateStory() and ResumeStory(). Followed eventually by
  // AddController(). See impl for details.
  void PendControllerAdd(
      const std::string& story_id,
      fidl::InterfaceRequest<StoryController> story_controller_request);

  // Used by DeleteStory(). Followed eventually by
  // DisposeController(). See impl for details.
  void PendControllerDelete(
      const std::string& story_id,
      const std::function<void()>& delete_callback);

  // Properly disposes the story controller for the given story by
  // first stopping its story if its running. See impl for details.
  void DisposeController(const fidl::String& story_id);

  ApplicationEnvironmentPtr environment_;
  StrongBinding<StoryProvider> binding_;
  fidl::BindingSet<StoryProvider> aux_bindings_;
  ledger::LedgerPtr ledger_;

  std::unordered_set<std::string> story_ids_;
  TransactionContainer transaction_container_;
  std::shared_ptr<Storage> storage_;
  fidl::Binding<ledger::PageWatcher> page_watcher_binding_;

  fidl::InterfacePtrSet<StoryProviderWatcher> watchers_;

  // Between a request coming in for a controller and the controller
  // being created, more requests may come in. To handle this
  // condition correctly, a request issued for a controller is marked
  // by an instance of this struct, and its completion is marked by
  // setting the controller.
  //
  // Likewise, between a request to delete a controller and it being
  // stopped and ready to delete, more requests can come in, which are
  // queued up in the same way. Requests to delete trump requests to
  // connect, so if a connect request is received while a delete is
  // pending, it won't get connected.
  //
  // Instances of this struct are held in a unique_ptr<> to be sure
  // that we never need to move them.
  struct StoryControllerEntry {
    std::vector<fidl::InterfaceRequest<StoryController>> requests;
    std::unique_ptr<StoryControllerImpl> impl;
    bool deleted{};
    std::vector<DeleteStoryCallback> deleted_callbacks;
  };
  std::unordered_map<std::string,
                     std::unique_ptr<StoryControllerEntry>> story_controllers_;

  // Owned by UserRunner.
  UserLedgerRepositoryFactory* const ledger_repository_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryProviderImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_USER_RUNNER_STORY_PROVIDER_IMPL_H_
