// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_USER_RUNNER_FOCUS_H_
#define APPS_MODULAR_SRC_USER_RUNNER_FOCUS_H_

#include <unordered_map>

#include "apps/ledger/services/internal/internal.fidl.h"
#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/modular/lib/fidl/operation.h"
#include "apps/modular/services/user/focus.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/binding_set.h"

// See services/user/focus.fidl for details.

namespace modular {

class FocusHandler : FocusProvider, FocusController, ledger::PageWatcher {
 public:
  FocusHandler(const fidl::String& device_name,
               ledger::LedgerRepository* ledger_repository);

  FocusProviderPtr GetProvider() {
    FocusProviderPtr ptr;
    provider_bindings_.AddBinding(this, ptr.NewRequest());
    return ptr;
  }

  void GetProvider(fidl::InterfaceRequest<FocusProvider> request) {
    provider_bindings_.AddBinding(this, std::move(request));
  }

  void GetController(fidl::InterfaceRequest<FocusController> request) {
    controller_bindings_.AddBinding(this, std::move(request));
  }

 private:
  // |FocusProvider|
  void Query(const QueryCallback& callback) override;

  // |FocusProvider|
  void Watch(fidl::InterfaceHandle<FocusWatcher> watcher) override {
    change_watchers_.push_back(FocusWatcherPtr::Create(std::move(watcher)));
  }

  // |FocusProvider|
  void Request(const fidl::String& story_id) override {
    for (const auto& watcher : request_watchers_) {
      watcher->OnRequest(story_id);
    }
  }

  // |FocusProvider|
  void Duplicate(fidl::InterfaceRequest<FocusProvider> request) override {
    provider_bindings_.AddBinding(this, std::move(request));
  }

  // |FocusController|
  void Set(const fidl::String& story_id) override;

  // |FocusController|
  void WatchRequest(
      fidl::InterfaceHandle<FocusRequestWatcher> watcher) override {
    request_watchers_.push_back(
        FocusRequestWatcherPtr::Create(std::move(watcher)));
  }

  // |ledger::PageWatcher|
  void OnChange(ledger::PageChangePtr page,
                ledger::ResultState result_state,
                const OnChangeCallback& callback) override;

  OperationQueue operation_queue_;
  fidl::Binding<ledger::PageWatcher> page_watcher_binding_;
  ledger::LedgerPtr ledger_;
  ledger::PagePtr page_;

  const std::string device_name_;

  fidl::BindingSet<FocusProvider> provider_bindings_;
  fidl::BindingSet<FocusController> controller_bindings_;

  std::vector<FocusWatcherPtr> change_watchers_;
  std::vector<FocusRequestWatcherPtr> request_watchers_;

  // An in-memory map of what is in the ledger. This is first constructed from
  // a snapshot and then updated via a watcher
  std::unordered_map<std::string, std::string> ledger_map_;

  FTL_DISALLOW_COPY_AND_ASSIGN(FocusHandler);
};

class VisibleStoriesHandler : VisibleStoriesProvider, VisibleStoriesController {
 public:
  VisibleStoriesHandler()
      : visible_stories_(fidl::Array<fidl::String>::New(0)) {}

  VisibleStoriesProviderPtr GetProvider() {
    VisibleStoriesProviderPtr ptr;
    provider_bindings_.AddBinding(this, ptr.NewRequest());
    return ptr;
  }

  void GetController(fidl::InterfaceRequest<VisibleStoriesController> request) {
    controller_bindings_.AddBinding(this, std::move(request));
  }

 private:
  // |VisibleStoriesProvider|
  void Query(const QueryCallback& callback) override {
    callback(visible_stories_.Clone());
  }

  // |VisibleStoriesProvider|
  void Watch(fidl::InterfaceHandle<VisibleStoriesWatcher> watcher) override {
    change_watchers_.push_back(
        VisibleStoriesWatcherPtr::Create(std::move(watcher)));
  }

  // |VisibleStoriesProvider|
  void Duplicate(
      fidl::InterfaceRequest<VisibleStoriesProvider> request) override {
    provider_bindings_.AddBinding(this, std::move(request));
  }

  // |VisibleStoriesController|
  void Set(fidl::Array<fidl::String> story_ids) override {
    visible_stories_ = std::move(story_ids);
    for (const auto& watcher : change_watchers_) {
      watcher->OnVisibleStoriesChange(visible_stories_.Clone());
    }
  }

  fidl::BindingSet<VisibleStoriesProvider> provider_bindings_;
  fidl::BindingSet<VisibleStoriesController> controller_bindings_;

  std::vector<VisibleStoriesWatcherPtr> change_watchers_;

  fidl::Array<fidl::String> visible_stories_;

  FTL_DISALLOW_COPY_AND_ASSIGN(VisibleStoriesHandler);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_USER_RUNNER_FOCUS_H_
