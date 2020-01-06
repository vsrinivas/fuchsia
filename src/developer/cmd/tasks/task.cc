// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/tasks/task.h"

namespace cmd {

Task::Task(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

Task::~Task() = default;

void Task::Complete(Autocomplete* autocomplete) { autocomplete->CompleteAsPath(); }

}  // namespace cmd
