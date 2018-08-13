// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_ADD_MOD_CALL_H_
#define PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_ADD_MOD_CALL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async/cpp/operation.h>
#include "peridot/bin/user_runner/storage/story_storage.h"

namespace modular {

class AddModCall : public Operation<fuchsia::modular::ExecuteResult,
                                    fuchsia::modular::ModuleData> {
 public:
  AddModCall(StoryStorage* story_storage,
             fuchsia::modular::ModuleResolver* module_resolver,
             fuchsia::modular::EntityResolver* entity_resolver,
             fidl::VectorPtr<fidl::StringPtr> mod_name,
             fuchsia::modular::Intent intent,
             fuchsia::modular::SurfaceRelationPtr surface_relation,
             fidl::VectorPtr<fidl::StringPtr> surface_parent_mod_name,
             fuchsia::modular::ModuleSource module_source, ResultCall done);

 private:
  // Start by finding the module through module resolver
  void Run() override;

  // Create module parameters info and create links.
  void CreateLinks(FlowToken flow);

  // Write module data
  void WriteModuleData(FlowToken flow,
                       fuchsia::modular::ModuleParameterMapPtr map);

  FuturePtr<> CreateModuleParameterMapInfo(FlowToken flow);

  StoryStorage* const story_storage_;  // Not owned.
  fuchsia::modular::ModuleResolver* const module_resolver_;  // Not owned.
  fuchsia::modular::EntityResolver* const entity_resolver_;  // Not owned.
  fidl::VectorPtr<fidl::StringPtr> mod_name_;
  fuchsia::modular::Intent intent_;
  fuchsia::modular::SurfaceRelationPtr surface_relation_;
  fidl::VectorPtr<fidl::StringPtr> surface_parent_mod_name_;
  fuchsia::modular::ModuleSource module_source_;
  fuchsia::modular::FindModulesResponse resolver_response_;
  fuchsia::modular::CreateModuleParameterMapInfoPtr parameter_info_;
  fuchsia::modular::ModuleData module_data_;
  fuchsia::modular::ExecuteResult result_;
  // Used when creating the map info to execute an operation as soon as it
  // arrives.
  OperationCollection operations_;
  // Used to enqueue sub-operations that should be executed sequentially.
  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AddModCall);
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_ADD_MOD_CALL_H_
