// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/fxl/log_settings.h"
#include "lib/fxl/logging.h"

#define TS_LOG(LEVEL) FXL_LOG(LEVEL) << "network_time: "
