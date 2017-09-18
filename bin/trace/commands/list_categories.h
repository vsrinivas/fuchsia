// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_SRC_TRACE_COMMANDS_LIST_CATEGORIES_H_
#define APPS_TRACING_SRC_TRACE_COMMANDS_LIST_CATEGORIES_H_

#include "apps/tracing/src/trace/command.h"

namespace tracing {

class ListCategories : public CommandWithTraceController {
 public:
  static Info Describe();

  explicit ListCategories(app::ApplicationContext* context);
  void Run(const fxl::CommandLine& command_line) override;
};

}  // namespace tracing

#endif  // APPS_TRACING_SRC_TRACE_COMMANDS_LIST_CATEGORIES_H_
