// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CPUPERF_PRINT_TALLIES_H_
#define GARNET_BIN_CPUPERF_PRINT_TALLIES_H_

#include <stdio.h>

#include "garnet/lib/perfmon/controller.h"
#include "garnet/lib/perfmon/events.h"

#include "session_spec.h"
#include "session_result_spec.h"

void PrintTallyResults(FILE* f, const cpuperf::SessionSpec& spec,
                       const cpuperf::SessionResultSpec& result_spec,
                       const perfmon::ModelEventManager* model_event_manager,
                       perfmon::Controller* controller);

#endif  // GARNET_BIN_CPUPERF_PRINT_TALLIES_H_
