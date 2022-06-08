// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_TRACING_BIN_TRACE_COMMANDS_LIST_CATEGORIES_H_
#define SRC_DEVELOPER_TRACING_BIN_TRACE_COMMANDS_LIST_CATEGORIES_H_

#include "src/developer/tracing/bin/trace/command.h"

namespace tracing {

class ListCategoriesCommand : public CommandWithController {
 public:
  static Info Describe();

  explicit ListCategoriesCommand(sys::ComponentContext* context);

 protected:
  void Start(const fxl::CommandLine& command_line) override;
};

}  // namespace tracing

#endif  // SRC_DEVELOPER_TRACING_BIN_TRACE_COMMANDS_LIST_CATEGORIES_H_
