// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_PUPPET_MASTER_IMPL_H_
#define PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_PUPPET_MASTER_IMPL_H_

#include <modular/cpp/fidl.h>

#include "lib/fidl/cpp/binding_set.h"

namespace modular {

class PuppetMasterImpl : public PuppetMaster {
 public:
  PuppetMasterImpl();
  ~PuppetMasterImpl() override;

  void Connect(fidl::InterfaceRequest<PuppetMaster> request);

 private:
  // |PuppetMaster|
  void ControlStory(fidl::StringPtr story_id,
                    fidl::InterfaceRequest<StoryPuppetMaster> request) override;

  // |PuppetMaster|
  void WatchSession(WatchSessionParams params,
                    fidl::InterfaceHandle<SessionWatcher> session_watcher,
                    WatchSessionCallback done) override;

  fidl::BindingSet<PuppetMaster> bindings_;
};

}  // namespace modular
#endif  // PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_PUPPET_MASTER_IMPL_H_
