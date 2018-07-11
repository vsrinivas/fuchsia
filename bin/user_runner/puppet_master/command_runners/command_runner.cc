// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "peridot/bin/user_runner/puppet_master/command_runners/command_runner.h"

namespace modular {

CommandRunner::CommandRunner(SessionStorage* const session_storage)
    : session_storage_(session_storage) {}

CommandRunner::~CommandRunner() = default;

}  // namespace modular
