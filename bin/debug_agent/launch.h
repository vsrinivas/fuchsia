// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>
#include <zx/process.h>

zx_status_t Launch(const std::vector<std::string>& argv, zx::process* process);
