// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_FIND_MODULES_CALL_H_
#define PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_FIND_MODULES_CALL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async/cpp/operation.h>
#include "peridot/bin/user_runner/storage/story_storage.h"

namespace modular {

class FindModulesCall
    : public Operation<fuchsia::modular::ExecuteResult,
                       fuchsia::modular::FindModulesResponse> {
 public:
  FindModulesCall(StoryStorage* const story_storage,
                  fuchsia::modular::ModuleResolver* const module_resolver,
                  fuchsia::modular::EntityResolver* const entity_resolver,
                  fuchsia::modular::IntentPtr intent,
                  fidl::VectorPtr<fidl::StringPtr> requesting_module_path,
                  ResultCall result_call);

 private:
  void Run() override;

  // To avoid deadlocks, this function must not depend on anything that executes
  // on the story controller's operation queue.
  FuturePtr<std::vector<std::string>> GetTypesFromIntentParameter(
      fidl::VectorPtr<fidl::StringPtr> module_path,
      const fuchsia::modular::IntentParameterData& input,
      const fidl::StringPtr& param_name);

  std::pair<bool, std::vector<std::string>> GetTypesFromJson(
      const fidl::StringPtr& input);

  void GetTypesFromLink(fuchsia::modular::LinkPathPtr link_path,
                        std::function<void(std::vector<std::string>)> done,
                        const fidl::StringPtr& param_name);

  StoryStorage* const story_storage_;                        // Not owned.
  fuchsia::modular::ModuleResolver* const module_resolver_;  // Not Owned
  fuchsia::modular::EntityResolver* const entity_resolver_;  // Not owned.
  const fuchsia::modular::IntentPtr intent_;
  const fidl::VectorPtr<fidl::StringPtr> requesting_module_path_;

  fuchsia::modular::FindModulesQuery resolver_query_;
  std::vector<FuturePtr<fuchsia::modular::FindModulesParameterConstraint>>
      constraint_futs_;
  fuchsia::modular::LinkPtr link_;  // in case we need itf for
  fuchsia::modular::ExecuteResult result_;
  fuchsia::modular::FindModulesResponse response_;
  OperationCollection operations_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FindModulesCall);
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_FIND_MODULES_CALL_H_
