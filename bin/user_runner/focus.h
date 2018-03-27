// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_FOCUS_H_
#define PERIDOT_BIN_USER_RUNNER_FOCUS_H_

#include <string>
#include <vector>

#include "lib/async/cpp/operation.h"
#include "lib/fidl/cpp/array.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/string.h"
#include <fuchsia/cpp/ledger.h>
#include "lib/user/fidl/focus.fidl.h"
#include "peridot/lib/ledger_client/ledger_client.h"
#include "peridot/lib/ledger_client/page_client.h"
#include "peridot/lib/ledger_client/types.h"

// See services/user/focus.fidl for details.

namespace modular {

class FocusHandler : FocusProvider, FocusController, PageClient {
 public:
  FocusHandler(const f1dl::StringPtr& device_id,
               LedgerClient* ledger_client,
               LedgerPageId page_id);
  ~FocusHandler() override;

  void AddProviderBinding(f1dl::InterfaceRequest<FocusProvider> request);
  void AddControllerBinding(f1dl::InterfaceRequest<FocusController> request);

 private:
  // |FocusProvider|
  void Query(const QueryCallback& callback) override;
  void Watch(f1dl::InterfaceHandle<FocusWatcher> watcher) override;
  void Request(const f1dl::StringPtr& story_id) override;
  void Duplicate(f1dl::InterfaceRequest<FocusProvider> request) override;

  // |FocusController|
  void Set(const f1dl::StringPtr& story_id) override;
  void WatchRequest(
      f1dl::InterfaceHandle<FocusRequestWatcher> watcher) override;

  // |PageClient|
  void OnPageChange(const std::string& key, const std::string& value) override;

  const f1dl::StringPtr device_id_;

  f1dl::BindingSet<FocusProvider> provider_bindings_;
  f1dl::BindingSet<FocusController> controller_bindings_;

  std::vector<FocusWatcherPtr> change_watchers_;
  std::vector<FocusRequestWatcherPtr> request_watchers_;

  // Operations on an instance of this clas are sequenced in this operation
  // queue. TODO(mesch): They currently do not need to be, but it's easier to
  // reason this way.
  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FocusHandler);
};

class VisibleStoriesHandler : VisibleStoriesProvider, VisibleStoriesController {
 public:
  VisibleStoriesHandler();
  ~VisibleStoriesHandler() override;

  void AddProviderBinding(
      f1dl::InterfaceRequest<VisibleStoriesProvider> request);
  void AddControllerBinding(
      f1dl::InterfaceRequest<VisibleStoriesController> request);

 private:
  // |VisibleStoriesProvider|
  void Query(const QueryCallback& callback) override;
  void Watch(f1dl::InterfaceHandle<VisibleStoriesWatcher> watcher) override;
  void Duplicate(
      f1dl::InterfaceRequest<VisibleStoriesProvider> request) override;

  // |VisibleStoriesController|
  void Set(f1dl::VectorPtr<f1dl::StringPtr> story_ids) override;

  f1dl::BindingSet<VisibleStoriesProvider> provider_bindings_;
  f1dl::BindingSet<VisibleStoriesController> controller_bindings_;

  std::vector<VisibleStoriesWatcherPtr> change_watchers_;

  f1dl::VectorPtr<f1dl::StringPtr> visible_stories_;

  FXL_DISALLOW_COPY_AND_ASSIGN(VisibleStoriesHandler);
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_FOCUS_H_
