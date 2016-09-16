// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

mx_status_t devmgr_launch_acpisvc(void);
mx_status_t devmgr_init_pcie(void);

void devmgr_reboot(void);
void devmgr_poweroff(void);
void devmgr_acpi_ps0(char* arg);
mx_handle_t devmgr_acpi_clone(void);