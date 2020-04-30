// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_FOCUS_H_
#define SRC_MODULAR_BIN_SESSIONMGR_FOCUS_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/string.h>

#include <array>
#include <string>
#include <vector>

#include "src/modular/lib/async/cpp/operation.h"

// See services/user/focus.fidl for details.

namespace modular {

class FocusHandler : fuchsia::modular::FocusProvider,
                     fuchsia::modular::FocusController {
 public:
  FocusHandler(fidl::StringPtr device_id);
  ~FocusHandler() override;

  void AddProviderBinding(fidl::InterfaceRequest<fuchsia::modular::FocusProvider> request);
  void AddControllerBinding(fidl::InterfaceRequest<fuchsia::modular::FocusController> request);

 private:
  // |fuchsia::modular::FocusProvider|
  void Query(QueryCallback callback) override;
  void Watch(fidl::InterfaceHandle<fuchsia::modular::FocusWatcher> watcher) override;
  void Request(fidl::StringPtr story_id) override;

  // |fuchsia::modular::FocusController|
  void Set(fidl::StringPtr story_id) override;
  void WatchRequest(fidl::InterfaceHandle<fuchsia::modular::FocusRequestWatcher> watcher) override;

  // Convenience method to return a populated copy of the current focus data
  fuchsia::modular::FocusInfoPtr CurrentData();

  const fidl::StringPtr device_id_;

  // Canonical record of the focused story and the last time that changed.
  // This was previously stored in the ledger, but now is just stored in-memory
  // locally.
  fidl::StringPtr focused_story_id_;
  uint64_t last_focus_change_timestamp_;

  fidl::BindingSet<fuchsia::modular::FocusProvider> provider_bindings_;
  fidl::BindingSet<fuchsia::modular::FocusController> controller_bindings_;

  std::vector<fuchsia::modular::FocusWatcherPtr> change_watchers_;
  std::vector<fuchsia::modular::FocusRequestWatcherPtr> request_watchers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FocusHandler);
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONMGR_FOCUS_H_
