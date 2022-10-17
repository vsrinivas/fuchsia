// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "nhlt.h"

#include <zircon/assert.h>
#include <zircon/errors.h>

#include <cstdint>
#include <vector>

#include <intel-hda/utils/nhlt.h>
#include <zxtest/zxtest.h>

namespace audio::intel_hda {
namespace {

// Push the raw bytes of the given object into the given std::vector<uint8_t>.
template <typename T>
void PushBytes(std::vector<uint8_t>* buffer, const T* object) {
  const auto* object_bytes = reinterpret_cast<const uint8_t*>(object);
  std::copy(object_bytes, object_bytes + sizeof(T), std::back_inserter(*buffer));
}

std::vector<uint8_t> SampleEndpoint() {
  std::vector<uint8_t> data;
  const specific_config_t no_capabilities = {
      /*capabilities_size=*/0,
  };

  // Write out an endpoint.
  uint32_t length =
      (sizeof(nhlt_descriptor_t) + sizeof(specific_config_t) + sizeof(formats_config_t) +
       sizeof(format_config_t) + sizeof(specific_config_t));
  nhlt_descriptor_t endpoint = {
      /*length=*/length,
      /*link_type=*/NHLT_LINK_TYPE_SSP,
      /*instance_id=*/1,
      /*vendor_id=*/2,
      /*device_id=*/3,
      /*revision_id=*/4,
      /*subsystem_id=*/5,
      /*device_type=*/6,
      /*direction=*/NHLT_DIRECTION_RENDER,
      /*virtual_bus_id=*/7,
  };
  PushBytes(&data, &endpoint);
  PushBytes(&data, &no_capabilities);

  formats_config_t formats = {
      /*format_config_count=*/1,
  };
  PushBytes(&data, &formats);

  format_config_t format = {
      /*format_tag=*/1,
      /*n_channels=*/2,
      /*n_samples_per_sec=*/3,
      /*n_avg_bytes_per_sec=*/4,
      /*n_block_align=*/5,
      /*bits_per_sample=*/6,
      /*cb_size=*/7,
      /*valid_bits_per_sample=*/8,
      /*channel_mask=*/9,
      /*subformat_guid=*/{},
  };
  PushBytes(&data, &format);
  PushBytes(&data, &no_capabilities);

  // Ensure we got our length calculations correct.
  ZX_ASSERT(data.size() == length);

  return data;
}

std::vector<uint8_t> SampleNHLT() {
  std::vector<uint8_t> data;

  // Create endpoint.
  std::vector<uint8_t> endpoint = SampleEndpoint();

  // Write out header.
  nhlt_table_t nhlt = {
      /*header=*/{
          /*signature=*/{'N', 'H', 'L', 'T'},
          /*length=*/static_cast<uint32_t>(sizeof(nhlt_table_t) + endpoint.size()),
          /*revision=*/5,
          /*checksum=*/0,  // Invalid, but we don't check.
          /*oem_id=*/{'O', 'E', 'M', 'M', 'Y'},
          /*oem_table_id=*/{'O', 'E', 'M', 'T', 'A', 'B', 'L', 'E'},
          /*oem_revision=*/0,
          /*asl_compiler_id=*/{'C', 'O', 'M', 'P'},
          /*asl_compiler_revision=*/0,
      },
      /*endpoint_desc_count=*/1,
  };
  PushBytes(&data, &nhlt);

  // Append endpoint.
  std::copy(endpoint.begin(), endpoint.end(), std::back_inserter(data));

  return data;
}

std::vector<uint8_t> SampleNHLT2() {
  std::vector<uint8_t> data;

  // Create endpoint.
  std::vector<uint8_t> endpoint = SampleEndpoint();

  // Write out header.
  nhlt_table_t nhlt = {
      /*header=*/{
          /*signature=*/{'N', 'H', 'L', 'T'},
          /*length=*/static_cast<uint32_t>(sizeof(nhlt_table_t) + endpoint.size()),
          /*revision=*/5,
          /*checksum=*/0,  // Invalid, but we don't check.
          /*oem_id=*/{'G', 'O', 'O', 'G', 'L', 'E'},
          /*oem_table_id=*/{'E', 'V', 'E', 'M', 'A', 'X'},
          /*oem_revision=*/0,
          /*asl_compiler_id=*/{'C', 'O', 'M', 'P'},
          /*asl_compiler_revision=*/0,
      },
      /*endpoint_desc_count=*/1,
  };
  PushBytes(&data, &nhlt);

  // Append endpoint.
  std::copy(endpoint.begin(), endpoint.end(), std::back_inserter(data));

  return data;
}

TEST(Nhlt, DefaultInitializer) {
  Nhlt x{};
  EXPECT_TRUE(x.configs().is_empty());
}

TEST(Nhlt, ParseEmpty) {
  cpp20::span<uint8_t> empty{};
  EXPECT_FALSE(Nhlt::FromBuffer(empty).is_ok());
}

TEST(Nhlt, ParseSimple) {
  std::vector<uint8_t> data = SampleNHLT();
  zx::result<std::unique_ptr<Nhlt>> nhlt = Nhlt::FromBuffer(data);
  ASSERT_OK(nhlt.status_value());

  // Ensure the data looks reasonable.
  std::unique_ptr<Nhlt> nhlt_testvalue = std::move(nhlt.value());
  ASSERT_EQ(nhlt_testvalue->configs().size(), 1);
  ASSERT_EQ(nhlt_testvalue->configs()[0].formats.size(), 1);
  ASSERT_EQ(nhlt_testvalue->configs()[0].formats[0].config.format_tag, 1);
}

TEST(Nhlt, ParseTruncated) {
  std::vector<uint8_t> data = SampleNHLT();

  // Remove a byte, and ensure we still successfully notice that the data size is all wrong.
  do {
    data.resize(data.size() - 1);
    zx::result<std::unique_ptr<Nhlt>> nhlt = Nhlt::FromBuffer(data);
    ASSERT_FALSE(nhlt.is_ok());
  } while (!data.empty());
}

TEST(Nhlt, ParseOemStrings) {
  std::vector<uint8_t> data = SampleNHLT();
  zx::result<std::unique_ptr<Nhlt>> nhlt = Nhlt::FromBuffer(data);
  ASSERT_OK(nhlt.status_value());

  std::unique_ptr<Nhlt> nhlt_testvalue = std::move(nhlt.value());
  // Ensure the data looks reasonable.
  ASSERT_FALSE(nhlt_testvalue->IsOemMatch("OEMMY", "TABLESSSS"));
  ASSERT_TRUE(nhlt_testvalue->IsOemMatch("OEMMY", "OEMTABLE"));
  ASSERT_FALSE(nhlt_testvalue->IsOemMatch("OMM", "OEMTABLE"));
}

TEST(Nhlt, ParseOemStrings2) {
  std::vector<uint8_t> data = SampleNHLT2();
  zx::result<std::unique_ptr<Nhlt>> nhlt = Nhlt::FromBuffer(data);
  ASSERT_OK(nhlt.status_value());

  std::unique_ptr<Nhlt> nhlt_testvalue = std::move(nhlt.value());
  // Ensure the data looks reasonable.
  ASSERT_FALSE(nhlt_testvalue->IsOemMatch("GOOGLW", "EVEMAX"));
  ASSERT_FALSE(nhlt_testvalue->IsOemMatch("GOOGLE", "EVEMAXXX"));
  ASSERT_TRUE(nhlt_testvalue->IsOemMatch("GOOGLE", "EVEMAX"));
}

}  // namespace
}  // namespace audio::intel_hda
