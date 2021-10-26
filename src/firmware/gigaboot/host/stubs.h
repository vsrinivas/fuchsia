// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file defines stubs for device-only functionality that we don't
// need for unittests, but have to define for compilation to succeed.

#ifndef SRC_FIRMWARE_GIGABOOT_HOST_STUBS_H_
#define SRC_FIRMWARE_GIGABOOT_HOST_STUBS_H_

int puts16(char16_t *str);

#endif  // SRC_FIRMWARE_GIGABOOT_HOST_STUBS_H_
