// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_FOCUS_H_
#define PERIDOT_BIN_USER_RUNNER_FOCUS_H_

#include <string>
#include <vector>

#include <fuchsia/ledger/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>

#include "lib/async/cpp/operation.h"
#include "lib/fidl/cpp/array.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/string.h"
#include "peridot/lib/ledger_client/ledger_client.h"
#include "peridot/lib/ledger_client/page_client.h"
#include "peridot/lib/ledger_client/types.h"

// See services/user/focus.fidl for details.

namespace modular {

class FocusHandler : fuchsia::modular::FocusProvider,
                     fuchsia::modular::FocusController,
                     PageClient {
 public:
  FocusHandler(fidl::StringPtr device_id, LedgerClient* ledger_client,
               LedgerPageId page_id);
  ~FocusHandler() override;

  void AddProviderBinding(
      fidl::InterfaceRequest<fuchsia::modular::FocusProvider> request);
  void AddControllerBinding(
      fidl::InterfaceRequest<fuchsia::modular::FocusController> request);

 private:
  // |fuchsia::modular::FocusProvider|
  void Query(QueryCallback callback) override;
  void Watch(
      fidl::InterfaceHandle<fuchsia::modular::FocusWatcher> watcher) override;
  void Request(fidl::StringPtr story_id) override;
  void Duplicate(
      fidl::InterfaceRequest<fuchsia::modular::FocusProvider> request) override;

  // |fuchsia::modular::FocusController|
  void Set(fidl::StringPtr story_id) override;
  void WatchRequest(fidl::InterfaceHandle<fuchsia::modular::FocusRequestWatcher>
                        watcher) override;

  // |PageClient|
  void OnPageChange(const std::string& key, const std::string& value) override;

  const fidl::StringPtr device_id_;

  fidl::BindingSet<fuchsia::modular::FocusProvider> provider_bindings_;
  fidl::BindingSet<fuchsia::modular::FocusController> controller_bindings_;

  std::vector<fuchsia::modular::FocusWatcherPtr> change_watchers_;
  std::vector<fuchsia::modular::FocusRequestWatcherPtr> request_watchers_;

  // Operations on an instance of this clas are sequenced in this operation
  // queue. TODO(mesch): They currently do not need to be, but it's easier to
  // reason this way.
  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FocusHandler);
};

class VisibleStoriesHandler : fuchsia::modular::VisibleStoriesProvider,
                              fuchsia::modular::VisibleStoriesController {
 public:
  VisibleStoriesHandler();
  ~VisibleStoriesHandler() override;

  void AddProviderBinding(
      fidl::InterfaceRequest<fuchsia::modular::VisibleStoriesProvider> request);
  void AddControllerBinding(
      fidl::InterfaceRequest<fuchsia::modular::VisibleStoriesController>
          request);

 private:
  // |fuchsia::modular::VisibleStoriesProvider|
  void Query(QueryCallback callback) override;
  void Watch(fidl::InterfaceHandle<fuchsia::modular::VisibleStoriesWatcher>
                 watcher) override;
  void Duplicate(
      fidl::InterfaceRequest<fuchsia::modular::VisibleStoriesProvider> request)
      override;

  // |fuchsia::modular::VisibleStoriesController|
  void Set(fidl::VectorPtr<fidl::StringPtr> story_ids) override;

  fidl::BindingSet<fuchsia::modular::VisibleStoriesProvider> provider_bindings_;
  fidl::BindingSet<fuchsia::modular::VisibleStoriesController>
      controller_bindings_;

  std::vector<fuchsia::modular::VisibleStoriesWatcherPtr> change_watchers_;

  fidl::VectorPtr<fidl::StringPtr> visible_stories_;

  FXL_DISALLOW_COPY_AND_ASSIGN(VisibleStoriesHandler);
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_FOCUS_H_
