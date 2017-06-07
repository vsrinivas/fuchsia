// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

mx_status_t setup_magenta(const uintptr_t addr, const uintptr_t acpi_off,
                          const int fd, const char* bootdata_path, uintptr_t* guest_ip,
                          uintptr_t* bootdata_offset);
