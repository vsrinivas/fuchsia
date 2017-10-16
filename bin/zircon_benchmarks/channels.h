// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

constexpr uint32_t HELPER_HANDLE_ID = 0x23011985;
constexpr const char HELPER_PATH[] = "/system/bin/zircon-benchmarks";

int channel_read(uint32_t num_bytes);
int channel_write(uint32_t num_bytes);
