// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/driver.h>
#include <zx/vmar.h>

#include "gtest/gtest.h"

#include "test_support.h"

static zx_device_t* g_parent_device;

TestSupport::FirmwareFile::~FirmwareFile() {
  if (ptr)
    zx::vmar::root_self()->unmap((uintptr_t)ptr, size);
}

zx_device_t* TestSupport::parent_device() { return g_parent_device; }

void TestSupport::set_parent_device(zx_device_t* handle) {
  g_parent_device = handle;
}

void TestSupport::RunAllTests() {
  const int kArgc = 1;
  const char* argv[kArgc] = {"test_support"};
  testing::InitGoogleTest(const_cast<int*>(&kArgc), const_cast<char**>(argv));
  (void)RUN_ALL_TESTS();
}

std::unique_ptr<TestSupport::FirmwareFile> TestSupport::LoadFirmwareFile(
    const char* name) {
  auto firmware_file = std::make_unique<FirmwareFile>();
  zx::vmo test_file;
  size_t test_file_size;
  zx_status_t status =
      load_firmware(TestSupport::parent_device(), name,
                    test_file.reset_and_get_address(), &test_file_size);
  if (status != ZX_OK)
    return nullptr;
  uint64_t ptr;
  status = zx::vmar::root_self()->map(0, test_file, 0, test_file_size,
                                      ZX_VM_FLAG_PERM_READ, &ptr);
  if (status != ZX_OK)
    return nullptr;
  firmware_file->vmo = std::move(test_file);
  firmware_file->size = test_file_size;
  firmware_file->ptr = (uint8_t*)ptr;
  return firmware_file;
}
