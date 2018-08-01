// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_INITIALIZE_CHAIN_CALL_H_
#define PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_INITIALIZE_CHAIN_CALL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async/cpp/operation.h>
#include "peridot/bin/user_runner/storage/story_storage.h"

namespace modular {

// Populates a fuchsia::modular::ModuleParameterMap struct from a
// fuchsia::modular::CreateModuleParameterMapInfo struct. May create new Links
// for any fuchsia::modular::CreateModuleParameterMapInfo.property_info if
// property_info[i].is_create_link_info().
class InitializeChainCall
    : public Operation<fuchsia::modular::ExecuteResult,
                       fuchsia::modular::ModuleParameterMapPtr> {
 public:
  InitializeChainCall(StoryStorage* const story_storage,
                      fidl::VectorPtr<fidl::StringPtr> module_path,
                      fuchsia::modular::CreateModuleParameterMapInfoPtr
                          create_parameter_map_info,
                      ResultCall result_call);

 private:
  void Run() override;

  StoryStorage* const story_storage_;
  const fidl::VectorPtr<fidl::StringPtr> module_path_;
  const fuchsia::modular::CreateModuleParameterMapInfoPtr
      create_parameter_map_info_;
  fuchsia::modular::ModuleParameterMapPtr parameter_map_;
  fuchsia::modular::ExecuteResult result_;
  OperationCollection operations_;

  FXL_DISALLOW_COPY_AND_ASSIGN(InitializeChainCall);
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_INITIALIZE_CHAIN_CALL_H_
