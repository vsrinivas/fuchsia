// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_TESTS_TEST_SUPPORT_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_TESTS_TEST_SUPPORT_H_

#include <lib/zx/vmo.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include <memory>

#include <ddk/device.h>

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

  static bool RunAllTests();

  static std::unique_ptr<FirmwareFile> LoadFirmwareFile(const char* name);
};

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_TESTS_TEST_SUPPORT_H_
