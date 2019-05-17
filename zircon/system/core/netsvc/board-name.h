// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unistd.h>

#include <lib/zx/channel.h>

bool CheckBoardName(const zx::channel& sysinfo, const char* name, size_t length);
