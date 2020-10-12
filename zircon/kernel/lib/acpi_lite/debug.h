// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_ACPI_LITE_DEBUG_H_
#define ZIRCON_KERNEL_LIB_ACPI_LITE_DEBUG_H_

// Logging.
//
// Both the kernel and muslc want to define |dprintf|, but with different parameters.
// Define our own names to avoid conflicting.
#if defined(_KERNEL)
#include <debug.h>
#define LOG_INFO(x...) dprintf(INFO, x)
#define LOG_DEBUG(x...) dprintf(SPEW, x)
#define LOG_ERROR(x...) dprintf(CRITICAL, x)
#else
#include <stdio.h>
#define LOG_INFO(x...) printf(x)
#define LOG_DEBUG(x...) printf(x)
#define LOG_ERROR(x...) printf(x)
#endif

#endif  // ZIRCON_KERNEL_LIB_ACPI_LITE_DEBUG_H_
