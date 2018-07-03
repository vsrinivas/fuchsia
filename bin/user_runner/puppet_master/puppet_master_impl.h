// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_PUPPET_MASTER_IMPL_H_
#define PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_PUPPET_MASTER_IMPL_H_

#include <memory>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fxl/memory/weak_ptr.h>

namespace modular {

class StoryCommandExecutor;
class StoryPuppetMasterImpl;

// An implementation of fuchsia::modular::PuppetMaster which owns and connect
// clients to instances of StoryPuppetMasterImpl for story control.
class PuppetMasterImpl : public fuchsia::modular::PuppetMaster {
 public:
  // Does not take ownership of |executor|.
  explicit PuppetMasterImpl(StoryCommandExecutor* executor);
  ~PuppetMasterImpl() override;

  void Connect(fidl::InterfaceRequest<fuchsia::modular::PuppetMaster> request);

 private:
  // |fuchsia::modular::PuppetMaster|
  void ControlStory(fidl::StringPtr story_id,
                    fidl::InterfaceRequest<fuchsia::modular::StoryPuppetMaster>
                        request) override;

  // |fuchsia::modular::PuppetMaster|
  void WatchSession(
      fuchsia::modular::WatchSessionParams params,
      fidl::InterfaceHandle<fuchsia::modular::SessionWatcher> session_watcher,
      WatchSessionCallback done) override;

  StoryCommandExecutor* const executor_;  // Not owned.

  fidl::BindingSet<fuchsia::modular::PuppetMaster> bindings_;
  // There is a one-impl-per-connection relationship between
  // fuchsia::modular::StoryPuppetMaster and its bindings.
  fidl::BindingSet<fuchsia::modular::StoryPuppetMaster,
                   std::unique_ptr<StoryPuppetMasterImpl>>
      story_puppet_masters_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PuppetMasterImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_PUPPET_MASTER_IMPL_H_
