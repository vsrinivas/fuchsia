// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/network/upload_element_reader.h"

#include <sstream>

#include "garnet/public/lib/fsl/vmo/strings.h"
#include "lib/fsl/vmo/sized_vmo.h"

#include <gtest/gtest.h>

namespace network {

namespace {

TEST(VmoUploadElementReader, SmallData) {
  const std::string data = "abc";
  fsl::SizedVmo sized_vmo;
  ASSERT_TRUE(fsl::VmoFromString(data, &sized_vmo));

  VmoUploadElementReader reader(std::move(sized_vmo.vmo()), sized_vmo.size());
  std::stringstream output;

  while (reader.ReadAvailable(&output)) {
    // Do nothing.
  }

  EXPECT_EQ(data, output.str());
}

TEST(VmoUploadElementReader, BiggerData) {
  // 1MB of data should be enough to require more than one call to
  // ReadAvailable.
  const std::string data(1'000'000, 'a');
  fsl::SizedVmo sized_vmo;
  ASSERT_TRUE(fsl::VmoFromString(data, &sized_vmo));

  VmoUploadElementReader reader(std::move(sized_vmo.vmo()), sized_vmo.size());
  std::stringstream output;

  int read_count = 0;
  while (reader.ReadAvailable(&output)) {
    read_count++;
  }

  // Ensure that the test indeed requires multiple reads.
  EXPECT_GT(read_count, 0);

  EXPECT_EQ(data, output.str());
}

}  // namespace

}  // namespace network
