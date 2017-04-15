// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_SRC_TRACE_COMMANDS_LIST_PROVIDERS_H_
#define APPS_TRACING_SRC_TRACE_COMMANDS_LIST_PROVIDERS_H_

#include "../command.h"

namespace tracing {

class ListProviders : public CommandWithTraceController {
 public:
  static Info Describe();

  explicit ListProviders(faux::ApplicationContext* context);
  void Run(const ftl::CommandLine&) override;
};

}  // namespace tracing

#endif  // APPS_TRACING_SRC_TRACE_COMMANDS_LIST_PROVIDERS_H_
