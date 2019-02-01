// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_TESTS_TEST_SUPPORT_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_TESTS_TEST_SUPPORT_H_

#include <ddk/device.h>

#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include <zx/vmo.h>

class TestSupport {
 public:
  struct FirmwareFile {
    ~FirmwareFile();
    zx::vmo vmo;
    uint8_t* ptr = {};
    size_t size = {};
  };

  static zx_device_t* parent_device();

  static void set_parent_device(zx_device_t* handle);

  static void RunAllTests();

  static std::unique_ptr<FirmwareFile> LoadFirmwareFile(const char* name);
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_TESTS_TEST_SUPPORT_H_
