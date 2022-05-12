// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_PLAYGROUND_SRC_PLAYGROUND_H_
#define SRC_DEVICES_BIN_DRIVER_PLAYGROUND_SRC_PLAYGROUND_H_

#include <fidl/fuchsia.driver.playground/cpp/wire.h>

class Playground final : public fidl::WireServer<fuchsia_driver_playground::ToolRunner> {
 public:
  void RunTool(RunToolRequestView request, RunToolCompleter::Sync& completer) override;
};

#endif  // SRC_DEVICES_BIN_DRIVER_PLAYGROUND_SRC_PLAYGROUND_H_
