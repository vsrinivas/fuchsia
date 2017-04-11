// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/ftl/log_settings.h"
#include "lib/ftl/logging.h"

#define TS_LOG(LEVEL) FTL_LOG(LEVEL) << "time-service: "
