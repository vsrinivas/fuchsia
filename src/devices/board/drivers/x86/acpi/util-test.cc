// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <zxtest/zxtest.h>

// These tests exercise custom types and methods added to make using ACPI easier.
namespace {

TEST(AcpiBuffer, IteratorEmpty) {
  acpi::AcpiBuffer<ACPI_RESOURCE> buf;
  for (auto& e : buf) {
    (void)e;
    FAIL();
  }
}

TEST(AcpiBuffer, IteratorValid) {
  // Manually build an AcpiBuffer backed by a list of resources since we cannot
  // get a real set of data for the test.
  const ACPI_SIZE cnt = 16;
  auto* backing_buffer = static_cast<ACPI_RESOURCE*>(AcpiOsAllocate(sizeof(ACPI_RESOURCE) * cnt));
  for (unsigned int i = 0; i < cnt; i++) {
    backing_buffer[i].Length = sizeof(ACPI_RESOURCE);
    backing_buffer[i].Type = i;
  }
  acpi::AcpiBuffer<ACPI_RESOURCE> buf_to_iterate(cnt * sizeof(ACPI_RESOURCE), backing_buffer);

  // Walk through the iterator and ensure we find the right elements and none of
  // our state is mutated unexpectedly.
  ACPI_SIZE iter_cnt = 0;
  for (auto& r : buf_to_iterate) {
    ASSERT_EQ(&r, &backing_buffer[iter_cnt]);
    ASSERT_EQ(r.Length, sizeof(ACPI_RESOURCE));
    ASSERT_EQ(r.Type, iter_cnt);
    iter_cnt++;
  }
  ASSERT_EQ(iter_cnt, cnt);
}

}  // namespace
