// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

mx_status_t devhost_launch_acpisvc(mx_handle_t job_handle);
mx_status_t devhost_init_pcie(void);

void devhost_acpi_reboot(void);
void devhost_acpi_poweroff(void);
void devhost_acpi_ps0(char* arg);
mx_handle_t devhost_acpi_clone(void);
