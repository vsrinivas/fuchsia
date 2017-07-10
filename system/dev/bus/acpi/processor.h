// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

// Check if *buf* is a valid PNP or ACPI id.  *len* does not include a null byte
bool is_pnp_acpi_id(char* buf, unsigned int len);

mx_status_t begin_processing(mx_handle_t acpi_root);
