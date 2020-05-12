// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_WEAVE_DEVICE_PLATFORM_ERROR_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_WEAVE_DEVICE_PLATFORM_ERROR_H_

#define WEAVE_DEVICE_PLATFORM_ERROR_MIN 12000000
#define WEAVE_DEVICE_PLATFORM_ERROR_MAX 12000999
#define _WEAVE_DEVICE_PLATFORM_ERROR(e) (WEAVE_DEVICE_PLATFORM_ERROR_MIN + (e))

// The requested configuration value did not match the expected type.
#define WEAVE_DEVICE_PLATFORM_ERROR_CONFIG_TYPE_MISMATCH _WEAVE_DEVICE_PLATFORM_ERROR(1)

// The configuration file is invalid or has an unsupported format.
#define WEAVE_DEVICE_PLATFORM_ERROR_CONFIG_INVALID _WEAVE_DEVICE_PLATFORM_ERROR(2)

// An error occurred on a request made to a FIDL connection.
#define WEAVE_DEVICE_PLATFORM_ERROR_FIDL_ERROR _WEAVE_DEVICE_PLATFORM_ERROR(3)

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_WEAVE_DEVICE_PLATFORM_ERROR_H_
