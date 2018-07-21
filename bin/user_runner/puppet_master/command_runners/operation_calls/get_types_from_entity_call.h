// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_GET_TYPES_FROM_ENTITY_CALL_H_
#define PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_GET_TYPES_FROM_ENTITY_CALL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async/cpp/operation.h>

namespace modular {

class GetTypesFromEntityCall : public Operation<std::vector<std::string>> {
 public:
  GetTypesFromEntityCall(
      fuchsia::modular::EntityResolver* const entity_resolver,
      const fidl::StringPtr& entity_reference, ResultCall result);

  void Run() override;

 private:
  fuchsia::modular::EntityResolver* const entity_resolver_;
  fidl::StringPtr const entity_reference_;
  fuchsia::modular::EntityPtr entity_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GetTypesFromEntityCall);
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_PUPPET_MASTER_COMMAND_RUNNERS_OPERATION_CALLS_GET_TYPES_FROM_ENTITY_CALL_H_
