// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_PROCESS_EXPLORER_PROCESS_EXPLORER_H_
#define SRC_DEVELOPER_PROCESS_EXPLORER_PROCESS_EXPLORER_H_

#include <fuchsia/process/explorer/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include "src/lib/fxl/command_line.h"

namespace process_explorer {

class Explorer : public fuchsia::process::explorer::Query {
 public:
  Explorer(std::unique_ptr<sys::ComponentContext> context);
  ~Explorer() override;

  // Writes processes information to |socket| in JSON, in UTF-8.
  // See /src/developer/process_explorer/writer.h for a description of the format of the JSON.
  void WriteJsonProcessesData(zx::socket socket) override;

 private:
  std::unique_ptr<sys::ComponentContext> component_context_;
  fidl::BindingSet<fuchsia::process::explorer::Query> bindings_;
};

}  // namespace process_explorer

#endif  // SRC_DEVELOPER_PROCESS_EXPLORER_PROCESS_EXPLORER_H_
