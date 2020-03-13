// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/puppet_master/command_runners/add_mod_command_runner.h"

#include "src/lib/syslog/cpp/logger.h"
#include "src/modular/bin/sessionmgr/puppet_master/command_runners/operation_calls/add_mod_call.h"

namespace modular {

AddModCommandRunner::AddModCommandRunner() = default;
AddModCommandRunner::~AddModCommandRunner() = default;

void AddModCommandRunner::Execute(fidl::StringPtr story_id, StoryStorage* const story_storage,
                                  fuchsia::modular::StoryCommand command,
                                  fit::function<void(fuchsia::modular::ExecuteResult)> done) {
  FX_CHECK(command.is_add_mod());

  auto& add_mod = command.add_mod();
  if (add_mod.mod_name.size() == 0 && !add_mod.mod_name_transitional.has_value()) {
    fuchsia::modular::ExecuteResult result;
    result.status = fuchsia::modular::ExecuteStatus::INVALID_COMMAND;
    result.error_message = "A Module name must be specified";
    done(result);
    return;
  }

  AddModParams params;
  if (add_mod.surface_parent_mod_name.has_value()) {
    params.parent_mod_path = std::move(add_mod.surface_parent_mod_name.value());
  }
  if (add_mod.mod_name_transitional.has_value()) {
    params.mod_name = add_mod.mod_name_transitional.value();
  } else if (add_mod.mod_name.size() == 1) {
    params.mod_name = add_mod.mod_name[0];
  } else {
    params.mod_name = add_mod.mod_name.back();

    add_mod.mod_name.pop_back();
    params.parent_mod_path = add_mod.mod_name;
  }
  params.is_embedded = false;
  params.intent = std::move(add_mod.intent);
  params.surface_relation =
      std::make_unique<fuchsia::modular::SurfaceRelation>(std::move(add_mod.surface_relation));
  params.module_source = fuchsia::modular::ModuleSource::EXTERNAL;

  AddAddModOperation(&operation_queue_, story_storage, std::move(params),
                     [done = std::move(done)](fuchsia::modular::ExecuteResult result,
                                              fuchsia::modular::ModuleData module_data) {
                       done(std::move(result));
                     });
}

}  // namespace modular
