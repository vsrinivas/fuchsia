// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_STUBS_UI_STATE_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_STUBS_UI_STATE_PROVIDER_H_

#include <fuchsia/ui/activity/cpp/fidl.h>
#include <fuchsia/ui/activity/cpp/fidl_test_base.h>

#include "src/developer/forensics/testing/stubs/fidl_server.h"

namespace forensics::stubs {

using UIStateProviderBase = SINGLE_BINDING_STUB_FIDL_SERVER(fuchsia::ui::activity, Provider);

class UIStateProvider : public UIStateProviderBase {
 public:
  UIStateProvider(async_dispatcher_t* dispatcher, fuchsia::ui::activity::State state,
                  zx::time time);

  void WatchState(::fidl::InterfaceHandle<::fuchsia::ui::activity::Listener> listener) override;
  void SetState(fuchsia::ui::activity::State state, zx::time time);
  void UnbindListener();

 private:
  void OnStateChanged();

  async_dispatcher_t* dispatcher_;
  fuchsia::ui::activity::State state_;
  zx::time time_;
  fuchsia::ui::activity::ListenerPtr listener_;
};

}  // namespace forensics::stubs

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_STUBS_UI_STATE_PROVIDER_H_
