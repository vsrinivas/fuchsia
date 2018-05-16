// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_PUPPET_MASTER_IMPL_H_
#define PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_PUPPET_MASTER_IMPL_H_

#include <modular/cpp/fidl.h>
#include <memory>

#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace modular {

class StoryCommandExecutor;
class StoryPuppetMasterImpl;

// An implementation of PuppetMaster which owns and connect clients to
// instances of StoryPuppetMasterImpl for story control.
class PuppetMasterImpl : public PuppetMaster {
 public:
  // Does not take ownership of |executor|.
  explicit PuppetMasterImpl(StoryCommandExecutor* executor);
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

  StoryCommandExecutor* const executor_;  // Not owned.

  fidl::BindingSet<PuppetMaster> bindings_;
  // There is a one-impl-per-connection relationship between StoryPuppetMaster
  // and its bindings.
  fidl::BindingSet<StoryPuppetMaster, std::unique_ptr<StoryPuppetMasterImpl>>
      story_puppet_masters_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PuppetMasterImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_PUPPET_MASTER_IMPL_H_
