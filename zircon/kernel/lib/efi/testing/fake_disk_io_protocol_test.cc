// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/efi/testing/fake_disk_io_protocol.h>

#include <gtest/gtest.h>

namespace efi {

namespace {

TEST(FakeDiskIoProtocol, Read) {
  FakeDiskIoProtocol fake;
  const std::vector<uint8_t> expected{0, 1, 2, 3, 4, 5};
  fake.contents(0) = expected;

  std::vector<uint8_t> actual(6);
  ASSERT_EQ(EFI_SUCCESS, fake.protocol()->ReadDisk(fake.protocol(), 0, 0, 6, actual.data()));
  ASSERT_EQ(expected, actual);
}

TEST(FakeDiskIoProtocol, Write) {
  FakeDiskIoProtocol fake;
  fake.contents(0).resize(6);

  const std::vector<uint8_t> expected{0, 1, 2, 3, 4, 5};
  ASSERT_EQ(EFI_SUCCESS, fake.protocol()->WriteDisk(fake.protocol(), 0, 0, 6, expected.data()));
  ASSERT_EQ(expected, fake.contents(0));
}

TEST(FakeDiskIoProtocol, ReadOffset) {
  FakeDiskIoProtocol fake;
  fake.contents(0) = {0, 1, 2, 3, 4, 5};

  uint8_t byte = 0;
  ASSERT_EQ(EFI_SUCCESS, fake.protocol()->ReadDisk(fake.protocol(), 0, 3, 1, &byte));
  ASSERT_EQ(3, byte);
}

TEST(FakeDiskIoProtocol, WriteOffset) {
  FakeDiskIoProtocol fake;
  fake.contents(0).resize(6);

  uint8_t byte = 4;
  ASSERT_EQ(EFI_SUCCESS, fake.protocol()->WriteDisk(fake.protocol(), 0, 2, 1, &byte));
  ASSERT_EQ(4, fake.contents(0)[2]);
}

// Trying to read/write a MediaId before it's been created in the fake should error.
TEST(FakeDiskIoProtocol, BadMediaId) {
  FakeDiskIoProtocol fake;
  uint8_t byte;
  ASSERT_EQ(EFI_NO_MEDIA, fake.protocol()->ReadDisk(fake.protocol(), 0, 0, 1, &byte));
  ASSERT_EQ(EFI_NO_MEDIA, fake.protocol()->WriteDisk(fake.protocol(), 0, 0, 1, &byte));
}

// Trying to read/write past the end of the created disk should be an error.
TEST(FakeDiskIoProtocol, DiskOverflow) {
  FakeDiskIoProtocol fake;
  fake.contents(0).resize(1);

  uint8_t bytes[2];
  ASSERT_EQ(EFI_END_OF_MEDIA, fake.protocol()->ReadDisk(fake.protocol(), 0, 0, 2, bytes));
  ASSERT_EQ(EFI_END_OF_MEDIA, fake.protocol()->WriteDisk(fake.protocol(), 0, 0, 2, bytes));
  ASSERT_EQ(EFI_END_OF_MEDIA, fake.protocol()->ReadDisk(fake.protocol(), 0, 1, 1, bytes));
  ASSERT_EQ(EFI_END_OF_MEDIA, fake.protocol()->WriteDisk(fake.protocol(), 0, 1, 1, bytes));
}

}  // namespace

}  // namespace efi
