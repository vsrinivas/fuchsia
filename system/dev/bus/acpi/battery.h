// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define BATTERY_HID_STRING "PNP0C0A"

mx_status_t battery_init(mx_device_t* parent, ACPI_HANDLE acpi_handle);
